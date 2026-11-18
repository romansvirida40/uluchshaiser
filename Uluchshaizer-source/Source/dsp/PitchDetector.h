#pragma once
// Real-time monophonic fundamental-frequency detector.
//
// Algorithm: YIN (de Cheveigné & Kawahara, 2002) run on a decimated, band-limited copy of the
// signal for speed, then refined at the full sample rate around the coarse lag with parabolic
// interpolation for sub-sample accuracy. Octave errors are reduced by (1) YIN's first-dip rule,
// (2) an octave-continuity check against the previous frame, and (3) a short median filter.
// Unvoiced / silent frames are flagged so the corrector can leave them alone.

#include "Biquad.h"
#include "InputType.h"
#include <vector>
#include <cstdint>

namespace ulz
{
class PitchDetector
{
public:
    struct Frame
    {
        int64_t centerTime = 0;     // input sample index the estimate is centred on
        float   period = 0.0f;      // fundamental period in samples (full rate); 0 if unvoiced
        float   midi = 0.0f;        // smoothed MIDI pitch (A4 = 69); valid only if voiced
        float   aperiodicity = 1.0f;// YIN d'(tau) at the chosen lag (0 = perfectly periodic)
        float   rms = 0.0f;         // RMS of the analysis window
        bool    voiced = false;
    };

    /** Allocates for the worst case (lowest possible frequency). Call from prepareToPlay. */
    void prepare (double sampleRate);

    /** Re-targets the detector to an input type. Does not allocate; safe on the audio thread. */
    void configure (const InputTypeConfig& cfg, bool lowLatency);

    void reset();

    /** Feed one mono sample. Returns true when a new analysis frame was produced. */
    bool push (float x, Frame& out);

    int getHop() const noexcept { return hop; }
    float getA4() const noexcept { return a4; }
    void setA4 (float hz) noexcept { a4 = hz; }

private:
    void analyse (Frame& out);
    float refineFullRate (float coarseFullLag, int fullWindow);

    double sr = 48000.0;
    InputTypeConfig cfg {};
    bool lowLatency = false;
    float a4 = 440.0f;

    // conditioning
    Biquad hp1, hp2;      // rumble removal
    Biquad aa1, aa2;      // anti-alias before decimation

    std::vector<float> full;   // full-rate conditioned ring
    std::vector<float> dec;    // decimated ring
    int fullMask = 0, decMask = 0;
    int64_t fullPos = 0, decPos = 0;
    int decimation = 1, decPhase = 0;

    int tauMin = 2, tauMax = 64, window = 64; // decimated domain
    int hop = 256, hopCounter = 0;

    std::vector<float> diff, cmnd;      // sized for worst case

    // tracking state
    float prevPeriod = 0.0f;
    bool  prevVoiced = false;
    float medianBuf[7] {};
    int   medianCount = 0, medianIdx = 0;
    int   unvoicedRun = 0;
};
} // namespace ulz
