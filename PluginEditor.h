#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

//==============================================================================
class SteamLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SteamLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h,
                           float pos, float startAngle, float endAngle, juce::Slider&) override;
};

//==============================================================================
class ULUCHSHAISEREditor : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit ULUCHSHAISEREditor (ULUCHSHAISERProcessor&);
    ~ULUCHSHAISEREditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    ULUCHSHAISERProcessor& proc;
    SteamLookAndFeel lnf;

    juce::ComboBox keyBox, scaleBox;
    juce::Slider speed, amount, stab, dMix, dDetune, dWidth, outGain;
    std::array<juce::Slider*, 7> sliders { &speed, &amount, &stab, &dMix, &dDetune, &dWidth, &outGain };

    std::vector<std::unique_ptr<SliderAtt>> sliderAtts;
    std::unique_ptr<ComboAtt> keyAtt, scaleAtt;

    float needle = 0.0f, gearAngle = 0.0f, targetMidi = 60.0f;
    bool voiced = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ULUCHSHAISEREditor)
};
