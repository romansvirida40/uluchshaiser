#pragma once
// Turns a detected pitch track into a corrected pitch track (all in MIDI semitones).
//
//   target note  : nearest *enabled* pitch class (12-bit mask), with hysteresis so the
//                  target does not flicker between two notes when the singer sits in the middle.
//   Retune Speed : sets the time constant of a one-pole smoother applied to the correction
//                  offset (target - input). Fast = offset follows instantly (hard tune);
//                  slow = only slow drift is removed, vibrato and scoops pass through.
//   Sustain      : note-length dependent speed ("humanize"). Once a note has been held for a
//                  while, the correction slows down by an amount set by Sustain, so long notes
//                  keep their natural movement while short notes stay tight.
//   Flex Tune    : correction weight that falls off with distance from the target. 0 = classic
//                  (always correct); higher = only correct when the singer is already close to
//                  the note, deliberate bends/ornaments pass through.
//   Amount       : final wet/dry of the *correction* itself (0 = no correction, 100 = full).

#include <cmath>
#include <cstdint>
#include <algorithm>

namespace ulz
{
struct CorrectorSettings
{
    float retuneSpeed = 32.7f;   // -3 .. 100   (display units)
    float sustain     = 0.3f;    // 0 .. 1
    float amount      = 1.0f;    // 0 .. 1
    float flexTune    = 0.0f;    // 0 .. 1
    uint16_t noteMask = 0x0AB5;  // C major
    float hysteresis  = 0.12f;   // semitones (from Input Type)
};

/** Retune Speed (display value, -3..100) -> correction time constant in milliseconds.
    Exponential mapping:  tau = 600 ms * (1.5/600)^u,  u = (v+3)/103.
      v = -3   -> 600 ms (very gentle, natural)
      v = 32.7 -> ~75 ms (medium pop)
      v = 80   -> ~4.5 ms (fast)
      v >= 99.9 -> 0 ms (instant, the classic hard-tune effect)
    Larger value = faster correction. */
inline float retuneSpeedToMs (float v)
{
    if (v >= 99.9f) return 0.0f;
    const float u = std::clamp ((v + 3.0f) / 103.0f, 0.0f, 1.0f);
    return 600.0f * std::pow (1.5f / 600.0f, u);
}

class PitchCorrector
{
public:
    struct Result
    {
        float correctedMidi = 0.0f; // output pitch
        float shiftSemitones = 0.0f;// corrected - detected
        int   targetNote = -1;      // MIDI note of the current target (-1 none)
    };

    void reset()
    {
        target = -1; offsetLp = 0.0f; noteAge = 0.0f; wasVoiced = false;
    }

    Result process (bool voiced, float midi, float dtSeconds, const CorrectorSettings& s)
    {
        Result r;
        if (! voiced || midi <= 0.0f || s.noteMask == 0)
        {
            wasVoiced = false;
            noteAge = 0.0f;
            offsetLp *= 0.5f;   // decay so the next onset starts naturally
            r.correctedMidi = midi;
            r.targetNote = voiced ? target : -1;
            return r;
        }

        // ---- choose target with hysteresis ----
        const int nearest = nearestAllowed (midi, s.noteMask);
        if (target < 0 || ! isAllowed (target, s.noteMask) || ! wasVoiced)
        {
            setTarget (nearest);
        }
        else if (nearest != target)
        {
            const float dCur = std::fabs (midi - (float) target);
            const float dNew = std::fabs (midi - (float) nearest);
            if (dNew + s.hysteresis < dCur) setTarget (nearest);
        }

        if (! wasVoiced) offsetLp = 0.0f;  // onset: start from the natural pitch (keeps the scoop)
        wasVoiced = true;
        noteAge += dtSeconds;

        // ---- Retune Speed + Sustain (note-length dependent speed) ----
        const float tauBase = retuneSpeedToMs (s.retuneSpeed) * 0.001f;
        const float ramp = smoothstep (0.08f, 0.40f, noteAge);
        const float tau = tauBase + s.sustain * ramp * (3.0f * tauBase + 0.12f);
        const float desiredOffset = (float) target - midi;
        const float alpha = tau <= 1e-5f ? 1.0f : 1.0f - std::exp (-dtSeconds / tau);
        offsetLp += alpha * (desiredOffset - offsetLp);

        // ---- Flex Tune: weight by distance to the target ----
        float w = 1.0f;
        if (s.flexTune > 0.001f)
        {
            const float outer = 1.0f - 0.8f * s.flexTune;   // semitones: 1.0 -> 0.2
            const float inner = outer * 0.35f;
            w = 1.0f - smoothstep (inner, outer, std::fabs (desiredOffset));
        }

        const float shift = s.amount * w * offsetLp;
        r.correctedMidi = midi + shift;
        r.shiftSemitones = shift;
        r.targetNote = target;
        return r;
    }

    static bool isAllowed (int note, uint16_t mask) { return (mask >> (((note % 12) + 12) % 12)) & 1u; }

    static int nearestAllowed (float midi, uint16_t mask)
    {
        const int c = (int) std::lround (midi);
        int best = c; float bestD = 1e9f;
        for (int k = -6; k <= 6; ++k)
        {
            const int n = c + k;
            if (! isAllowed (n, mask)) continue;
            const float d = std::fabs (midi - (float) n);
            if (d < bestD) { bestD = d; best = n; }
        }
        return best;
    }

private:
    static float smoothstep (float a, float b, float x)
    {
        if (b <= a) return x >= b ? 1.0f : 0.0f;
        const float t = std::clamp ((x - a) / (b - a), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    void setTarget (int n)
    {
        if (n != target) noteAge = 0.0f;
        target = n;
    }

    int target = -1;
    float offsetLp = 0.0f;
    float noteAge = 0.0f;
    bool wasVoiced = false;
};
} // namespace ulz
