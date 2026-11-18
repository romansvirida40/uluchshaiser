#pragma once
// Complete Uluchshaizer signal chain, framework independent:
//
//   input -> conditioning -> YIN pitch detection & tracking -> note / scale lookup
//         -> target pitch (Retune Speed, Sustain, Flex Tune, Amount) -> + manual Pitch
//         -> PSOLA resynthesis with independent Formant -> Doubles (2 detuned voices, timing
//         offsets, stereo Width) -> Double Dry/Wet -> Volume -> safety soft clip -> output

#include "PitchDetector.h"
#include "PitchCorrector.h"
#include "PsolaEngine.h"
#include "InputType.h"
#include <atomic>
#include <array>
#include <cmath>

namespace ulz
{
struct VocalSettings
{
    float retuneSpeed = 32.7f;   // -3..100
    float sustain     = 0.30f;   // 0..1
    float amount      = 1.00f;   // 0..1
    float flexTune    = 0.00f;   // 0..1
    float volume      = 0.80f;   // 0..1  (0.8 = unity)
    float pitchSt     = 0.0f;    // -12..12 semitones
    float formantPct  = 0.0f;    // -100..100  (+-100 % = +-5 semitones of formant shift)
    float doubleMix   = 0.0f;    // 0..1
    float doublesWidth= 0.5f;    // 0..1
    uint16_t noteMask = 0x0AB5;
    InputType inputType = InputType::AltoTenor;
    bool lowLatency = false;
};

/** Lock-free snapshot of what the processor is doing, for the GUI (written on the audio thread). */
struct VisualState
{
    static constexpr int historySize = 256;
    std::array<std::atomic<float>, historySize> levels {};   // peak level per analysis hop
    std::atomic<uint32_t> writeCount { 0 };
    std::atomic<float> detectedMidi { 0.0f };
    std::atomic<float> correctedMidi { 0.0f };
    std::atomic<int>   targetNote { -1 };
    std::atomic<bool>  voiced { false };
    std::atomic<float> inputLevel { 0.0f };
};

class VocalProcessor
{
public:
    void prepare (double sampleRate);
    void reset();

    /** Processes in place. Returns true if the plugin latency changed (Input Type / Low Latency). */
    bool process (float* const* channels, int numChannels, int numSamples, const VocalSettings& s);

    int getLatency() const noexcept { return engine.getLatency(); }
    static int latencyFor (double sampleRate, InputType t, bool lowLatency);

    VisualState& getVisualState() noexcept { return vis; }

    static float volumeToGain (float v01)
    {
        const float r = v01 / 0.8f;
        return r * r;                          // 0 -> mute, 0.8 -> 0 dB, 1.0 -> +3.9 dB
    }
    static float formantPctToFactor (float pct) { return std::pow (2.0f, (pct * 0.05f) / 12.0f); }

private:
    void applyConfig (InputType t, bool ll);

    double sr = 48000.0;
    PitchDetector detector;
    PitchCorrector corrector;
    PsolaEngine engine;

    InputType curType = InputType::AltoTenor;
    bool curLowLatency = false;
    bool configured = false;
    float hysteresis = 0.12f;

    // smoothed parameters
    float smPitch = 0.0f, smFormant = 0.0f, smGain = 1.0f, smMix = 0.0f, smWidth = 0.5f;
    float smCoef = 0.001f;
    float fadeIn = 0.0f, fadeStep = 0.001f;

    float peakAcc = 0.0f; int peakCount = 0;
    float lastMidi = 0.0f;
    bool lastVoiced = false;

    VisualState vis;
};
} // namespace ulz
