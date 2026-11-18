#include "VocalProcessor.h"
#include <algorithm>
#include <cmath>

namespace ulz
{
static inline float softClip (float x)
{
    // transparent below -0.9 dBFS, smooth knee into a hard ceiling of 1.0
    const float a = std::fabs (x);
    if (a <= 0.9f) return x;
    const float y = 0.9f + 0.1f * std::tanh ((a - 0.9f) / 0.1f);
    return x < 0.0f ? -y : y;
}

int VocalProcessor::latencyFor (double sampleRate, InputType t, bool lowLatency)
{
    const auto cfg = getInputTypeConfig (t);
    const double maxPeriod = sampleRate / cfg.minHz;
    return (int) std::ceil ((lowLatency ? 1.5 : 3.0) * std::max (16.0, maxPeriod));
}

void VocalProcessor::prepare (double sampleRate)
{
    sr = sampleRate;
    detector.prepare (sr);
    engine.prepare (sr);
    smCoef = 1.0f - std::exp (-1.0f / (float) (0.02 * sr));
    fadeStep = 1.0f / (float) (0.02 * sr);
    configured = false;
    applyConfig (curType, curLowLatency);
    reset();
}

void VocalProcessor::applyConfig (InputType t, bool ll)
{
    curType = t; curLowLatency = ll;
    const auto cfg = getInputTypeConfig (t);
    detector.configure (cfg, ll);
    engine.configure ((float) (sr / cfg.minHz), ll);
    hysteresis = cfg.hysteresisSt;
    configured = true;
}

void VocalProcessor::reset()
{
    detector.reset();
    corrector.reset();
    engine.reset();
    fadeIn = 0.0f;
    peakAcc = 0.0f; peakCount = 0;
    lastVoiced = false;
}

bool VocalProcessor::process (float* const* ch, int numChannels, int numSamples, const VocalSettings& s)
{
    bool latencyChanged = false;
    if (! configured || s.inputType != curType || s.lowLatency != curLowLatency)
    {
        const int before = engine.getLatency();
        applyConfig (s.inputType, s.lowLatency);
        detector.reset(); corrector.reset(); engine.reset();
        fadeIn = 0.0f;
        latencyChanged = before != engine.getLatency();
    }

    const int nCh = std::clamp (numChannels, 1, 2);
    const float tPitch = std::clamp (s.pitchSt, -12.0f, 12.0f);
    const float tFormant = formantPctToFactor (std::clamp (s.formantPct, -100.0f, 100.0f));
    const float tGain = volumeToGain (std::clamp (s.volume, 0.0f, 1.0f));
    const float tMix = std::clamp (s.doubleMix, 0.0f, 1.0f);
    const float tWidth = std::clamp (s.doublesWidth, 0.0f, 1.0f);

    CorrectorSettings cs;
    cs.retuneSpeed = s.retuneSpeed;
    cs.sustain = s.sustain;
    cs.amount = s.amount;
    cs.flexTune = s.flexTune;
    cs.noteMask = s.noteMask;
    cs.hysteresis = hysteresis;

    const float dtFrame = (float) detector.getHop() / (float) sr;
    const int hop = detector.getHop();

    engine.setDoublesActive (tMix > 0.0005f || smMix > 0.0005f);

    PitchDetector::Frame frame;
    float in[2], lead[2], dbl[2];

    for (int i = 0; i < numSamples; ++i)
    {
        smPitch += smCoef * (tPitch - smPitch);
        smFormant += smCoef * (tFormant - smFormant);
        smGain += smCoef * (tGain - smGain);
        smMix += smCoef * (tMix - smMix);
        smWidth += smCoef * (tWidth - smWidth);

        in[0] = ch[0][i];
        in[1] = nCh > 1 ? ch[1][i] : in[0];
        const float monoIn = nCh > 1 ? 0.5f * (in[0] + in[1]) : in[0];

        peakAcc = std::max (peakAcc, std::fabs (monoIn));
        if (++peakCount >= hop)
        {
            const uint32_t w = vis.writeCount.load (std::memory_order_relaxed);
            vis.levels[(size_t) (w % VisualState::historySize)].store (peakAcc, std::memory_order_relaxed);
            vis.writeCount.store (w + 1, std::memory_order_release);
            vis.inputLevel.store (peakAcc, std::memory_order_relaxed);
            peakAcc = 0.0f; peakCount = 0;
        }

        if (detector.push (monoIn, frame))
        {
            const auto r = corrector.process (frame.voiced, frame.midi, dtFrame, cs);
            const float manualRatio = std::pow (2.0f, smPitch / 12.0f);
            const float ratio = frame.voiced ? std::pow (2.0f, r.shiftSemitones / 12.0f) * manualRatio : manualRatio;
            engine.pushControl (frame.centerTime, frame.period, frame.voiced, ratio);

            vis.voiced.store (frame.voiced, std::memory_order_relaxed);
            if (frame.voiced)
            {
                vis.detectedMidi.store (frame.midi, std::memory_order_relaxed);
                vis.correctedMidi.store (r.correctedMidi + smPitch, std::memory_order_relaxed);
                vis.targetNote.store (r.targetNote, std::memory_order_relaxed);
            }
        }

        engine.setFormantFactor (smFormant);
        engine.process (in, nCh, lead, dbl);

        // ---- doubles: dry/wet (50 % = both at full level) and stereo width ----
        const float dryG = smMix <= 0.5f ? 1.0f : 2.0f * (1.0f - smMix);
        const float wetG = (smMix >= 0.5f ? 1.0f : 2.0f * smMix) * 0.6f;
        float outL, outR;
        if (nCh > 1)
        {
            // constant-power pan, double A to the left, double B to the right by Width
            const float pA = 0.5f - 0.5f * smWidth, pB = 0.5f + 0.5f * smWidth;  // 0 = L, 1 = R
            constexpr float hp = 1.5707963f;
            const float aL = std::cos (pA * hp), aR = std::sin (pA * hp);
            const float bL = std::cos (pB * hp), bR = std::sin (pB * hp);
            outL = dryG * lead[0] + wetG * (aL * dbl[0] + bL * dbl[1]);
            outR = dryG * lead[1] + wetG * (aR * dbl[0] + bR * dbl[1]);
        }
        else
        {
            outL = outR = dryG * lead[0] + wetG * 0.7071f * (dbl[0] + dbl[1]);
        }

        float g = smGain;
        if (fadeIn < 1.0f) { fadeIn = std::min (1.0f, fadeIn + fadeStep); g *= fadeIn * fadeIn; }

        ch[0][i] = softClip (outL * g);
        if (nCh > 1) ch[1][i] = softClip (outR * g);
    }
    return latencyChanged;
}
} // namespace ulz
