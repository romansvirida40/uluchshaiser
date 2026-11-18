#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

/** Factory presets (built in) + user presets (.ulzpreset XML files in Documents/Uluchshaizer/Presets).
    Presets only contain sound parameters; key, scale and note selection are kept when loading. */
class PresetManager
{
public:
    explicit PresetManager (juce::AudioProcessorValueTreeState& state);

    static juce::File getUserPresetFolder();

    juce::StringArray getFactoryNames() const;
    juce::Array<juce::File> getUserPresets() const;

    void loadFactory (int index);
    bool loadUser (const juce::File& file);
    bool saveUser (const juce::String& name);

    /** Steps through factory + user presets (wraps). */
    void step (int delta);

    juce::String getCurrentName() const { return currentName; }
    void setCurrentName (const juce::String& n) { currentName = n; }

    std::function<void()> onPresetLoaded;

private:
    void setParam (const juce::String& id, float value);

    juce::AudioProcessorValueTreeState& apvts;
    juce::String currentName { "Default" };
};
