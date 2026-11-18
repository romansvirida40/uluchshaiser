#pragma once
// Pitch-synchronous overlap-add (TD-PSOLA) pitch shifter with independent formant control.
//
// Why PSOLA: for monophonic voice it is low-latency, cheap, and *preserves formants by
// construction* (each grain is one or two glottal periods of the original voice; moving grains
// closer together raises the pitch without stretching the spectral envelope). Formant shifting is
// done independently by resampling each grain (time-compressing a grain by factor f moves its
// spectral envelope up by f) while the grain *spacing* alone sets the pitch.
//
// Three synthesis voices share one analysis: voice 0 is the lead (corrected) voice for every input
// channel, voices 1/2 are mono "doubles" with their own slow detune, formant offset and timing.

#include <vector>
#include <cstdint>
#include <cmath>

namespace ulz
{
class PsolaEngine
{
public:
    static constexpr int maxChannels = 2;
    static constexpr int numVoices = 3;

    void prepare (double sampleRate);             // allocates
    /** maxPeriodSamples = sampleRate / lowest detectable Hz. Does not allocate. */
    void configure (float maxPeriodSamples, bool lowLatency);
    void reset();

    int getLatency() const noexcept { return latency; }

    /** Latest analysis result (period in samples, 0/false if unvoiced) stamped at an input time,
        plus the pitch ratio the lead voice must apply at that time (correction * manual pitch). */
    void pushControl (int64_t time, float period, bool voiced, float leadRatio);

    void setFormantFactor (float f) noexcept   { formant = f; }
    void setDoublesActive (bool b) noexcept    { doublesActive = b; }

    /** Push one input frame, then render one output frame (delayed by getLatency()).
        lead[ch] receives the corrected voice per channel, dbl[0..1] the two mono doubles. */
    void process (const float* in, int numChannels, float* lead, float* dbl);

private:
    struct Mark { double pos; float period; bool voiced; };
    struct Control { int64_t time; float period; bool voiced; float ratio; };

    struct Voice
    {
        double nextSynth = 0.0;
        int markHint = 0;
        int ctrlHint = 0;
        std::vector<float> acc[maxChannels];
        std::vector<float> wsum;
        float detuneCents = 0.0f;
        float formantOffset = 1.0f;
        double lfoPhase = 0.0, lfoRate = 0.1;
        double lfoPhase2 = 0.0, lfoRate2 = 0.05;
    };

    void placeMarks();
    const Control& controlAt (double t, int& hint) const;
    int findMark (double s, int& hint, double maxAvailablePos, float formantSpan) const;
    void synthesise (Voice& v, int voiceIndex, int64_t outTime);
    float readInterp (const std::vector<float>& ring, double pos) const;

    double sr = 48000.0;
    int latency = 0;
    double loopAhead = 0.0;     // how far ahead of the output time grains are generated
    float maxPeriod = 800.0f;
    bool lowLatency = false;

    std::vector<float> in[maxChannels];
    std::vector<float> mono;
    int inMask = 0;
    int64_t inPos = 0;          // number of samples written
    int numCh = 1;

    std::vector<Mark> marks; int markMask = 0;
    int64_t markWrite = 0;      // total marks written
    double lastMarkPos = 0.0;

    std::vector<Control> ctrl; int ctrlMask = 0;
    int64_t ctrlWrite = 0;

    Voice voices[numVoices];
    int outMask = 0;

    float formant = 1.0f;
    bool doublesActive = true;

    // double timing delay lines
    std::vector<float> dline[2];
    int dMask = 0; int64_t dPos = 0;
};
} // namespace ulz
