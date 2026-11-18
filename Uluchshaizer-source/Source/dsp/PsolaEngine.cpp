#include "PsolaEngine.h"
#include <algorithm>
#include <cmath>

namespace ulz
{
static int nextPow2 (int n) { int p = 1; while (p < n) p <<= 1; return p; }
static constexpr double kPi = 3.14159265358979323846;

void PsolaEngine::prepare (double sampleRate)
{
    sr = sampleRate;
    const int maxT = (int) std::ceil (sr / 25.0);
    const int ringSize = nextPow2 (maxT * 8 + 8192);
    for (auto& b : in) b.assign ((size_t) ringSize, 0.0f);
    mono.assign ((size_t) ringSize, 0.0f);
    inMask = ringSize - 1;

    for (auto& v : voices)
    {
        for (auto& a : v.acc) a.assign ((size_t) ringSize, 0.0f);
        v.wsum.assign ((size_t) ringSize, 0.0f);
    }
    outMask = ringSize - 1;

    marks.assign (4096, Mark { 0.0, 1.0f, false });
    markMask = (int) marks.size() - 1;
    ctrl.assign (1024, Control { 0, 0.0f, false, 1.0f });
    ctrlMask = (int) ctrl.size() - 1;

    const int dSize = nextPow2 ((int) (sr * 0.08) + 4);
    for (auto& d : dline) d.assign ((size_t) dSize, 0.0f);
    dMask = dSize - 1;

    // doubles character: opposite detune, slightly different formants, independent slow drift
    voices[1].detuneCents = +9.0f;  voices[1].formantOffset = std::pow (2.0f, +0.25f / 12.0f);
    voices[1].lfoRate = 0.17; voices[1].lfoRate2 = 0.071; voices[1].lfoPhase = 0.3;
    voices[2].detuneCents = -11.0f; voices[2].formantOffset = std::pow (2.0f, -0.30f / 12.0f);
    voices[2].lfoRate = 0.13; voices[2].lfoRate2 = 0.053; voices[2].lfoPhase = 1.9;

    configure ((float) (sr / 95.0), false);
    reset();
}

void PsolaEngine::configure (float maxPeriodSamples, bool ll)
{
    maxPeriod = std::max (16.0f, maxPeriodSamples);
    lowLatency = ll;
    if (ll) { loopAhead = 0.6 * maxPeriod; latency = (int) std::ceil (1.5 * maxPeriod); }
    else    { loopAhead = 1.25 * maxPeriod; latency = (int) std::ceil (3.0 * maxPeriod); }
}

void PsolaEngine::reset()
{
    for (auto& b : in) std::fill (b.begin(), b.end(), 0.0f);
    std::fill (mono.begin(), mono.end(), 0.0f);
    for (auto& v : voices)
    {
        for (auto& a : v.acc) std::fill (a.begin(), a.end(), 0.0f);
        std::fill (v.wsum.begin(), v.wsum.end(), 0.0f);
        v.nextSynth = 0.0; v.markHint = 0; v.ctrlHint = 0;
    }
    for (auto& d : dline) std::fill (d.begin(), d.end(), 0.0f);
    dPos = 0;
    inPos = 0;
    markWrite = 0;
    lastMarkPos = 0.0;
    ctrlWrite = 0;
    pushControl (0, 0.0f, false, 1.0f);
}

void PsolaEngine::pushControl (int64_t time, float period, bool voiced, float leadRatio)
{
    // keep the track monotonic in time
    if (ctrlWrite > 0)
    {
        const auto& last = ctrl[(size_t) ((ctrlWrite - 1) & ctrlMask)];
        if (time < last.time) time = last.time;
    }
    ctrl[(size_t) (ctrlWrite & ctrlMask)] = Control { time, period, voiced, leadRatio };
    ++ctrlWrite;
}

const PsolaEngine::Control& PsolaEngine::controlAt (double t, int& hintRel) const
{
    // hintRel is stored as an absolute index truncated to int; ctrlWrite never gets near INT_MAX
    // in practice (one entry per >= 32 samples => ~ 12 days at 48 kHz).
    int64_t oldest = std::max<int64_t> (0, ctrlWrite - (int64_t) ctrl.size() + 1);
    int64_t h = std::max<int64_t> (hintRel, oldest);
    while (h + 1 < ctrlWrite && (double) ctrl[(size_t) ((h + 1) & ctrlMask)].time <= t) ++h;
    hintRel = (int) h;
    return ctrl[(size_t) (h & ctrlMask)];
}

void PsolaEngine::placeMarks()
{
    const auto& c = ctrl[(size_t) ((ctrlWrite - 1) & ctrlMask)];
    const float unvoicedSpacing = (float) (sr * 0.005);
    const double newest = (double) (inPos - 1);

    for (;;)
    {
        float spacing = (c.voiced && c.period > 4.0f) ? c.period : unvoicedSpacing;
        spacing = std::max (8.0f, spacing);
        const double next = lastMarkPos + spacing;
        if (next > newest) break;
        marks[(size_t) (markWrite & markMask)] = Mark { next, spacing, c.voiced && c.period > 4.0f };
        ++markWrite;
        lastMarkPos = next;
    }
}

int PsolaEngine::findMark (double s, int& hintRel, double maxAvailablePos, float formantSpan) const
{
    if (markWrite == 0) return -1;
    const int64_t oldest = std::max<int64_t> (0, markWrite - (int64_t) marks.size() + 1);
    int64_t h = std::max<int64_t> (hintRel, oldest);
    while (h + 1 < markWrite && marks[(size_t) ((h + 1) & markMask)].pos <= s) ++h;
    hintRel = (int) h;

    int64_t cand = h;
    if (h + 1 < markWrite)
    {
        const double d0 = std::fabs (marks[(size_t) (h & markMask)].pos - s);
        const double d1 = std::fabs (marks[(size_t) ((h + 1) & markMask)].pos - s);
        if (d1 < d0) cand = h + 1;
    }
    // the grain must be fully inside the input that has already arrived
    while (cand > oldest)
    {
        const auto& m = marks[(size_t) (cand & markMask)];
        if (m.pos + m.period * formantSpan <= maxAvailablePos) break;
        --cand;
    }
    const auto& m = marks[(size_t) (cand & markMask)];
    if (m.pos + m.period * formantSpan > maxAvailablePos) return -1;
    return (int) cand;
}

float PsolaEngine::readInterp (const std::vector<float>& ring, double pos) const
{
    const double fl = std::floor (pos);
    const int64_t i = (int64_t) fl;
    const float t = (float) (pos - fl);
    const float y0 = ring[(size_t) ((i - 1) & inMask)];
    const float y1 = ring[(size_t) (i & inMask)];
    const float y2 = ring[(size_t) ((i + 1) & inMask)];
    const float y3 = ring[(size_t) ((i + 2) & inMask)];
    // 4-point, 3rd-order Hermite
    const float c0 = y1;
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + c0;
}

void PsolaEngine::synthesise (Voice& v, int voiceIndex, int64_t outTime)
{
    const double maxAvail = (double) (inPos - 1) - 3.0;
    const bool isLead = voiceIndex == 0;

    if (v.nextSynth < (double) outTime - maxPeriod * 3.0)   // fell behind (e.g. after reset)
        v.nextSynth = (double) outTime;

    while (v.nextSynth <= (double) outTime + loopAhead)
    {
        const double s = v.nextSynth;
        const Control& c = controlAt (s, v.ctrlHint);

        float f = formant * v.formantOffset;
        const int mi = findMark (s, v.markHint, maxAvail, std::max (1.0f, f));
        if (mi < 0) { v.nextSynth += sr * 0.005; continue; }
        const Mark& m = marks[(size_t) (mi & markMask)];

        double ratio = 1.0;
        if (m.voiced)
        {
            ratio = c.ratio;
            if (! isLead)
            {
                const double drift = 3.0 * std::sin (v.lfoPhase) + 2.0 * std::sin (v.lfoPhase2);
                ratio *= std::pow (2.0, (v.detuneCents + drift) / 1200.0);
            }
        }
        const double Ta = m.period;
        const double Ps = std::clamp (Ta / ratio, Ta * 0.2, Ta * 3.0);

        double L = std::max (Ta, Ps);
        const double availRight = (maxAvail - m.pos) / f;
        const double availLeft = (m.pos - (double) (inPos - (int64_t) in[0].size() + 8)) / f;
        L = std::min ({ L, availRight, availLeft });
        if (L < 4.0) { v.nextSynth += Ps; continue; }

        const int64_t j0 = std::max<int64_t> (outTime, (int64_t) std::ceil (s - L));
        const int64_t j1 = (int64_t) std::floor (s + L);
        const double invL = kPi / L;

        for (int64_t j = j0; j <= j1; ++j)
        {
            const double x = (double) j - s;
            const float w = (float) (0.5 + 0.5 * std::cos (x * invL));
            const double pos = m.pos + x * f;
            const size_t o = (size_t) (j & outMask);
            if (isLead)
            {
                for (int ch = 0; ch < numCh; ++ch)
                    v.acc[ch][o] += w * readInterp (in[ch], pos);
            }
            else
            {
                v.acc[0][o] += w * readInterp (mono, pos);
            }
            v.wsum[o] += w;
        }

        v.nextSynth += Ps;
        const double dt = Ps / sr;
        v.lfoPhase += 2.0 * kPi * v.lfoRate * dt;
        v.lfoPhase2 += 2.0 * kPi * v.lfoRate2 * dt;
        if (v.lfoPhase > 2.0 * kPi) v.lfoPhase -= 2.0 * kPi;
        if (v.lfoPhase2 > 2.0 * kPi) v.lfoPhase2 -= 2.0 * kPi;
    }
}

void PsolaEngine::process (const float* input, int numChannels, float* lead, float* dbl)
{
    numCh = std::clamp (numChannels, 1, maxChannels);
    const size_t wi = (size_t) (inPos & inMask);
    float m = 0.0f;
    for (int ch = 0; ch < numCh; ++ch) { in[ch][wi] = input[ch]; m += input[ch]; }
    mono[wi] = m / (float) numCh;
    ++inPos;

    placeMarks();

    const int64_t t = inPos - 1 - latency;
    if (t < 0)
    {
        for (int ch = 0; ch < numCh; ++ch) lead[ch] = 0.0f;
        dbl[0] = dbl[1] = 0.0f;
        return;
    }

    synthesise (voices[0], 0, t);
    const size_t o = (size_t) (t & outMask);
    {
        auto& v = voices[0];
        const float norm = 1.0f / std::max (v.wsum[o], 0.25f);
        for (int ch = 0; ch < numCh; ++ch) { lead[ch] = v.acc[ch][o] * norm; v.acc[ch][o] = 0.0f; }
        v.wsum[o] = 0.0f;
    }

    float dRaw[2] = { 0.0f, 0.0f };
    for (int k = 0; k < 2; ++k)
    {
        auto& v = voices[k + 1];
        if (doublesActive)
        {
            synthesise (v, k + 1, t);
            dRaw[k] = v.acc[0][o] / std::max (v.wsum[o], 0.25f);
        }
        else
        {
            v.nextSynth = std::max (v.nextSynth, (double) t + 1.0);
        }
        v.acc[0][o] = 0.0f; v.wsum[o] = 0.0f;
    }

    // per-double timing offset: 11 / 19 ms base +- ~2 ms slow wander (natural double-tracking feel)
    const size_t dw = (size_t) (dPos & dMask);
    dline[0][dw] = dRaw[0];
    dline[1][dw] = dRaw[1];
    for (int k = 0; k < 2; ++k)
    {
        const auto& v = voices[k + 1];
        const double baseMs = k == 0 ? 11.0 : 19.0;
        const double delay = (baseMs + 2.0 * std::sin (v.lfoPhase2 * 1.7 + k)) * 0.001 * sr;
        const double rp = (double) dPos - delay;
        const double fl = std::floor (rp);
        const float fr = (float) (rp - fl);
        const int64_t i = (int64_t) fl;
        const float a = dline[k][(size_t) (i & dMask)];
        const float b = dline[k][(size_t) ((i + 1) & dMask)];
        dbl[k] = a + (b - a) * fr;
    }
    ++dPos;
}
} // namespace ulz
