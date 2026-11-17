#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <vector>

//==============================================================================
// Two-tap granular delay-line pitch shifter (one channel).
class Shifter
{
public:
    void prepare (double sr)
    {
        grain = juce::jmax (64, (int) (0.030 * sr));
        size  = juce::nextPowerOfTwo (grain * 4);
        mask  = size - 1;
        buf.assign ((size_t) size, 0.0f);
        pos = 0;
        phase = 0.0f;
    }

    int latency() const { return grain / 2; }

    float process (float x, float ratio)
    {
        buf[(size_t) pos] = x;

        phase += (1.0f - ratio) / (float) grain;
        phase -= std::floor (phase);
        float p2 = phase + 0.5f;
        p2 -= std::floor (p2);

        const float y = tap (phase) * win (phase) + tap (p2) * win (p2);
        pos = (pos + 1) & mask;
        return y;
    }

private:
    static float win (float p) { return 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * p); }

    float tap (float p) const
    {
        const float d  = p * (float) grain + 2.0f;
        const float rp = (float) pos - d;
        const int   i0 = (int) std::floor (rp);
        const float f  = rp - (float) i0;
        const float a  = buf[(size_t) (i0 & mask)];
        const float b  = buf[(size_t) ((i0 + 1) & mask)];
        return a + f * (b - a);
    }

    std::vector<float> buf;
    int   grain = 1323, size = 8192, mask = 8191, pos = 0;
    float phase = 0.0f;
};

struct SimpleDelay
{
    std::vector<float> b;
    int pos = 0, mask = 0;

    void prepare (int maxSamples)
    {
        const int s = juce::nextPowerOfTwo (maxSamples + 1);
        b.assign ((size_t) s, 0.0f);
        mask = s - 1;
        pos = 0;
    }

    float process (float x, int d)
    {
        b[(size_t) pos] = x;
        const float y = b[(size_t) ((pos - d) & mask)];
        pos = (pos + 1) & mask;
        return y;
    }
};

//==============================================================================
class ULUCHSHAISERProcessor : public juce::AudioProcessor
{
public:
    ULUCHSHAISERProcessor();
    ~ULUCHSHAISERProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "ULUCHSHAISER"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static int getScaleMask (int index);

    juce::AudioProcessorValueTreeState apvts;

    // Read by the editor (message thread)
    std::atomic<bool>  uiVoiced { false };
    std::atomic<float> uiCents  { 0.0f };
    std::atomic<float> uiTarget { 60.0f };
    std::array<std::atomic<float>, 12> heat;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    void  analyse();
    float detectPitch();
    int   nearestInScale (float midi) const;
    bool  inScale (int midiNote) const;

    // parameters
    std::atomic<float> *pKey, *pScale, *pSpeed, *pAmount, *pStab, *pMix, *pDetune, *pWidth, *pOut;

    // block-constant state used by analyse()
    int   keyVal = 0, scaleMask = 0xFFF;
    float amount = 1.0f, stab = 0.3f;

    // detection
    static constexpr int ringSize = 4096, ringMask = ringSize - 1, hopSize = 256, yinW = 1024;
    std::vector<float> ring, yinX, yinCm;
    int ringPos = 0, hopCount = 0, unvoiced = 0, curTarget = -1;
    float targetCorr = 0.0f, corr = 0.0f;
    double sr = 44100.0;

    // audio engine
    std::array<Shifter, 2> shifter;
    Shifter shifterA, shifterB;
    SimpleDelay delayA, delayB;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ULUCHSHAISERProcessor)
};
