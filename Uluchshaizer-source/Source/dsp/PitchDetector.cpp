#include "PitchDetector.h"
#include "Scales.h"
#include <algorithm>
#include <cmath>

namespace ulz
{
static int nextPow2 (int n) { int p = 1; while (p < n) p <<= 1; return p; }

void PitchDetector::prepare (double sampleRate)
{
    sr = sampleRate;
    // Worst case: 30 Hz lowest note, decimated rate >= 8 kHz, window 1.5 periods.
    const int maxDecTau = (int) std::ceil (sr / 25.0) + 8;   // as if no decimation (safe upper bound)
    const int maxFullSpan = maxDecTau * 3 + 64;
    full.assign ((size_t) nextPow2 (maxFullSpan * 2), 0.0f);
    dec.assign ((size_t) nextPow2 (maxFullSpan * 2), 0.0f);
    fullMask = (int) full.size() - 1;
    decMask = (int) dec.size() - 1;
    diff.assign ((size_t) maxDecTau + 4, 0.0f);
    cmnd.assign ((size_t) maxDecTau + 4, 0.0f);
    configure (getInputTypeConfig (InputType::AltoTenor), false);
    reset();
}

void PitchDetector::configure (const InputTypeConfig& c, bool ll)
{
    cfg = c;
    lowLatency = ll;

    const double targetDecRate = std::max (8000.0, 5.0 * cfg.maxHz);
    decimation = std::max (1, (int) std::floor (sr / targetDecRate));
    const double decRate = sr / decimation;

    tauMin = std::max (2, (int) std::floor (decRate / cfg.maxHz));
    tauMax = std::min ((int) diff.size() - 3, (int) std::ceil (decRate / cfg.minHz) + 1);
    window = lowLatency ? tauMax : (tauMax * 3) / 2;

    hop = std::max (32, (int) std::round (sr * (lowLatency ? 0.0025 : 0.005)));

    hp1.setHighPass (sr, cfg.minHz * 0.6, 0.5412);
    hp2.setHighPass (sr, cfg.minHz * 0.6, 1.3066);
    aa1.setLowPass (sr, decRate * 0.42, 0.5412);
    aa2.setLowPass (sr, decRate * 0.42, 1.3066);
}

void PitchDetector::reset()
{
    std::fill (full.begin(), full.end(), 0.0f);
    std::fill (dec.begin(), dec.end(), 0.0f);
    fullPos = decPos = 0;
    decPhase = 0;
    hopCounter = 0;
    hp1.reset(); hp2.reset(); aa1.reset(); aa2.reset();
    prevPeriod = 0.0f; prevVoiced = false;
    medianCount = medianIdx = 0;
    unvoicedRun = 0;
}

bool PitchDetector::push (float x, Frame& out)
{
    const float c = hp2.process (hp1.process (x));
    full[(size_t) (fullPos & fullMask)] = c;
    ++fullPos;

    const float a = aa2.process (aa1.process (c));
    if (++decPhase >= decimation)
    {
        decPhase = 0;
        dec[(size_t) (decPos & decMask)] = a;
        ++decPos;
    }

    if (++hopCounter >= hop)
    {
        hopCounter = 0;
        if (decPos > (int64_t) (window + tauMax + 2))
        {
            analyse (out);
            return true;
        }
    }
    return false;
}

float PitchDetector::refineFullRate (float coarseFullLag, int fullWindow)
{
    // Difference function at full rate for lags around the coarse estimate.
    const int lo = std::max (2, (int) std::floor (coarseFullLag) - decimation - 1);
    const int hi = (int) std::ceil (coarseFullLag) + decimation + 1;
    const int64_t start = fullPos - (int64_t) fullWindow - hi - 1;

    float best = 1e30f; int bestLag = lo;
    float vals[64]; const int n = std::min (64, hi - lo + 1);
    for (int i = 0; i < n; ++i)
    {
        const int lag = lo + i;
        float s = 0.0f;
        for (int j = 0; j < fullWindow; ++j)
        {
            const float d = full[(size_t) ((start + j) & fullMask)] - full[(size_t) ((start + j + lag) & fullMask)];
            s += d * d;
        }
        vals[i] = s;
        if (s < best) { best = s; bestLag = lag; }
    }
    const int bi = bestLag - lo;
    float refined = (float) bestLag;
    if (bi > 0 && bi < n - 1)
    {
        const float y0 = vals[bi - 1], y1 = vals[bi], y2 = vals[bi + 1];
        const float den = y0 - 2.0f * y1 + y2;
        if (den > 1e-12f) refined += 0.5f * (y0 - y2) / den;
    }
    return refined;
}

void PitchDetector::analyse (Frame& out)
{
    const int span = window + tauMax;
    const int64_t start = decPos - span;

    // --- YIN difference function (decimated domain) ---
    diff[0] = 0.0f;
    float energy = 0.0f;
    for (int j = 0; j < window; ++j)
    {
        const float v = dec[(size_t) ((start + j) & decMask)];
        energy += v * v;
    }
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        float s = 0.0f;
        for (int j = 0; j < window; ++j)
        {
            const float d = dec[(size_t) ((start + j) & decMask)] - dec[(size_t) ((start + j + tau) & decMask)];
            s += d * d;
        }
        diff[(size_t) tau] = s;
    }
    // cumulative mean normalised difference
    cmnd[0] = 1.0f;
    float running = 0.0f;
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        running += diff[(size_t) tau];
        cmnd[(size_t) tau] = running > 0.0f ? diff[(size_t) tau] * (float) tau / running : 1.0f;
    }

    const float rms = std::sqrt (energy / (float) std::max (1, window));
    out.rms = rms;
    out.centerTime = fullPos - (int64_t) (span * decimation) / 2;

    // --- first dip below the absolute threshold, then walk to its local minimum ---
    int tauEst = -1;
    for (int tau = tauMin; tau <= tauMax; ++tau)
    {
        if (cmnd[(size_t) tau] < cfg.yinThreshold)
        {
            while (tau + 1 <= tauMax && cmnd[(size_t) tau + 1] < cmnd[(size_t) tau]) ++tau;
            tauEst = tau;
            break;
        }
    }
    if (tauEst < 0)
    {   // no dip under threshold: take the global minimum (low confidence)
        float m = 2.0f;
        for (int tau = tauMin; tau <= tauMax; ++tau)
            if (cmnd[(size_t) tau] < m) { m = cmnd[(size_t) tau]; tauEst = tau; }
    }

    // --- octave continuity: prefer the previous octave if it is almost as periodic ---
    if (prevVoiced && prevPeriod > 0.0f && tauEst > 0)
    {
        const float prevDecTau = prevPeriod / (float) decimation;
        const float ratio = (float) tauEst / prevDecTau;
        auto localMinNear = [&] (float t) -> int
        {
            int c = (int) std::lround (t);
            int lo = std::max (tauMin, c - 2), hi = std::min (tauMax, c + 2);
            int best = -1; float bv = 2.0f;
            for (int k = lo; k <= hi; ++k) if (cmnd[(size_t) k] < bv) { bv = cmnd[(size_t) k]; best = k; }
            return best;
        };
        if (std::fabs (ratio - 2.0f) < 0.15f || std::fabs (ratio - 0.5f) < 0.04f)
        {
            const int alt = localMinNear (prevDecTau);
            if (alt > 0 && cmnd[(size_t) alt] < cmnd[(size_t) tauEst] + 0.12f)
                tauEst = alt;
        }
    }

    const float aper = tauEst > 0 ? cmnd[(size_t) tauEst] : 1.0f;
    out.aperiodicity = aper;

    const float gate = 0.0018f; // about -55 dBFS RMS
    const bool voiced = tauEst > 0 && aper < cfg.voicedThreshold && rms > gate;

    if (! voiced)
    {
        out.voiced = false;
        out.period = 0.0f;
        if (++unvoicedRun > 3) { prevVoiced = false; medianCount = 0; }
        out.midi = prevVoiced ? out.midi : 0.0f;
        return;
    }
    unvoicedRun = 0;

    // --- sub-sample refinement at full rate ---
    float coarse = (float) tauEst;
    if (tauEst > tauMin && tauEst < tauMax)
    {
        const float y0 = cmnd[(size_t) tauEst - 1], y1 = cmnd[(size_t) tauEst], y2 = cmnd[(size_t) tauEst + 1];
        const float den = y0 - 2.0f * y1 + y2;
        if (den > 1e-9f) coarse += 0.5f * (y0 - y2) / den;
    }
    float period = coarse * (float) decimation;
    if (decimation > 1)
        period = refineFullRate (period, window * decimation);

    const float hz = (float) sr / period;
    if (hz < cfg.minHz * 0.9f || hz > cfg.maxHz * 1.1f)
    {
        out.voiced = false; out.period = 0.0f;
        return;
    }

    // --- median smoothing of the pitch track (in semitones) ---
    const float midiRaw = hzToMidi (hz, a4);
    const int len = std::max (1, std::min (7, cfg.medianLength));
    medianBuf[medianIdx] = midiRaw;
    medianIdx = (medianIdx + 1) % len;
    medianCount = std::min (medianCount + 1, len);
    float tmp[7];
    for (int i = 0; i < medianCount; ++i) tmp[i] = medianBuf[i];
    std::nth_element (tmp, tmp + medianCount / 2, tmp + medianCount);
    const float med = tmp[medianCount / 2];
    // use the median only to reject outliers; keep the raw value when it agrees (preserves vibrato detail)
    const float midi = std::fabs (midiRaw - med) > 0.7f ? med : midiRaw;

    out.voiced = true;
    out.midi = midi;
    out.period = (float) sr / midiToHz (midi, a4);
    prevPeriod = out.period;
    prevVoiced = true;
}
} // namespace ulz
