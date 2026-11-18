#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "ui/Controls.h"

namespace ulz::ui
{
class UluchshaizerLookAndFeel : public juce::LookAndFeel_V4
{
public:
    UluchshaizerLookAndFeel();
    juce::Font getPopupMenuFont() override { return Fonts::regular (15.0f); }
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;
    juce::Font getAlertWindowTitleFont() override   { return Fonts::semibold (17.0f); }
    juce::Font getAlertWindowMessageFont() override { return Fonts::regular (14.0f); }
    juce::Font getAlertWindowFont() override        { return Fonts::regular (14.0f); }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return Fonts::medium (14.0f); }
    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override { return Fonts::regularTf(); }
};

/** Draws a line or two of static text (titles, labels). */
class TextLabel : public juce::Component
{
public:
    std::function<void (juce::Graphics&, juce::Rectangle<float>)> painter;
    TextLabel() { setInterceptsMouseClicks (false, false); }
    void paint (juce::Graphics& g) override { if (painter) painter (g, getLocalBounds().toFloat()); }
};

bool systemPrefersReducedMotion();
} // namespace ulz::ui

class UluchshaizerEditor : public juce::AudioProcessorEditor
{
public:
    explicit UluchshaizerEditor (UluchshaizerProcessor&);
    ~UluchshaizerEditor() override;

    void paint (juce::Graphics&) override {}
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void setPianoOpen (bool open);

private:
    void onFrame();
    void layoutForPiano (float amount);
    void showPresetMenu();
    juce::RangedAudioParameter& prm (const juce::String& id) { return *processor.apvts.getParameter (id); }
    std::array<juce::RangedAudioParameter*, 12> noteParams();

    UluchshaizerProcessor& processor;
    ulz::ui::UluchshaizerLookAndFeel laf;

    juce::Component canvas;   // fixed 1280 x 720 design space, scaled to the window
    ulz::ui::Background background;
    ulz::ui::Panel leftPanel, rightPanel, dwPanel, widthPanel, pianoPanel;

    // left
    ulz::ui::TextLabel title, retuneLabel;
    ulz::ui::LowLatencyToggle lowLatency;
    ulz::ui::SphereControl sphere;
    ulz::ui::SpeedValueLabel speedValue;
    ulz::ui::ParamKnob sustain, amount, flexTune, volume;

    // right
    ulz::ui::PresetBar presetBar;
    ulz::ui::IconButton pianoButton { ulz::ui::IconButton::Icon::Piano };
    ulz::ui::IconButton minimiseButton { ulz::ui::IconButton::Icon::Minimise };
    ulz::ui::IconButton closeButton { ulz::ui::IconButton::Icon::Close };
    ulz::ui::ScaleSelector scaleSelector;
    ulz::ui::NoteRow noteRow;
    ulz::ui::InputTypeBox inputType;
    ulz::ui::InputTypeList inputTypeList;
    ulz::ui::VocalView vocal;
    ulz::ui::ParamKnob pitch, formant;

    // bottom
    ulz::ui::TextLabel dwLabel, dwValue;
    ulz::ui::ParamKnob doubleMix;
    ulz::ui::WidthMeter widthMeter;
    ulz::ui::PianoKeyboard piano;
    ulz::ui::IconButton chevron { ulz::ui::IconButton::Icon::Chevron };

    std::vector<ulz::ui::Animated*> animated;
    ulz::ui::Tween pianoAnim;
    bool pianoOpen = false;
    double lastFrameMs = 0.0;
    float lastLayoutAmount = -1.0f;
    juce::TooltipWindow tooltips { this, 700 };
    std::unique_ptr<juce::VBlankAttachment> vblank;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UluchshaizerEditor)
};
