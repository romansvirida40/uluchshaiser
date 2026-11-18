#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "PresetManager.h"
#include "dsp/VocalProcessor.h"

class UluchshaizerProcessor : public juce::AudioProcessor,
                              private juce::AudioProcessorValueTreeState::Listener,
                              private juce::AsyncUpdater
{
public:
    UluchshaizerProcessor();
    ~UluchshaizerProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Uluchshaizer"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    PresetManager presets { apvts };

    ulz::VisualState& getVisualState() noexcept { return dsp.getVisualState(); }
    uint16_t getNoteMask() const noexcept;
    int getRoot() const noexcept { return (int) keyParam->load(); }
    ulz::ScaleType getScaleType() const noexcept { return (ulz::ScaleType) (int) scaleParam->load(); }

    /** Writes the 12 note toggles from the current key + scale (called when key/scale change). */
    void applyScaleToNotes();

private:
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;

    ulz::VocalProcessor dsp;
    std::atomic<float>* p[9] {};
    std::atomic<float>* keyParam = nullptr;
    std::atomic<float>* scaleParam = nullptr;
    std::atomic<float>* inputTypeParam = nullptr;
    std::atomic<float>* lowLatencyParam = nullptr;
    std::atomic<float>* noteParams[12] {};

    std::atomic<bool> scaleApplyPending { false };
    std::atomic<bool> latencyUpdatePending { false };
    int currentProgram = 0;

    juce::AudioBuffer<float> scratch;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UluchshaizerProcessor)
};
