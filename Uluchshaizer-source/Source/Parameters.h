#pragma once
// Single source of truth for every Uluchshaizer parameter: IDs, ranges, defaults, display text.
// The GUI, the DSP, host automation, presets and state restore all go through these parameters.

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/Scales.h"

namespace ulz::params
{
namespace id
{
    inline constexpr const char* retuneSpeed  = "retuneSpeed";
    inline constexpr const char* sustain      = "sustain";
    inline constexpr const char* amount       = "amount";
    inline constexpr const char* flexTune     = "flexTune";
    inline constexpr const char* volume       = "volume";
    inline constexpr const char* pitch        = "pitch";
    inline constexpr const char* formant      = "formant";
    inline constexpr const char* doubleMix    = "doubleMix";
    inline constexpr const char* doublesWidth = "doublesWidth";
    inline constexpr const char* key          = "key";
    inline constexpr const char* scale        = "scale";
    inline constexpr const char* inputType    = "inputType";
    inline constexpr const char* lowLatency   = "lowLatency";
    inline juce::String note (int pc) { return "note" + juce::String (pc); }
}

inline const juce::StringArray& keyChoices()
{
    static const juce::StringArray k { "C", "C#/Db", "D", "D#/Eb", "E", "F", "F#/Gb", "G", "G#/Ab", "A", "A#/Bb", "B" };
    return k;
}
inline const juce::StringArray& scaleChoices()     { static const juce::StringArray s { "Major", "Minor", "Chromatic" }; return s; }
inline const juce::StringArray& inputTypeChoices() { static const juce::StringArray s { "Soprano", "Alto / Tenor", "Low Male", "Instrument", "Bass Instrument" }; return s; }

inline constexpr float retuneDefault = 32.7f;

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    const int v = 1;

    auto pct = [] (float val, int) { return String (juce::roundToInt (val)) + "%"; };
    auto pctParse = [] (const String& t) { return t.retainCharacters ("-0123456789.").getFloatValue(); };

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id::retuneSpeed, v }, "Retune Speed",
        NormalisableRange<float> (-3.0f, 100.0f, 0.1f), retuneDefault,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float val, int) { return String (val, 1); })
                                       .withValueFromStringFunction ([] (const String& t) { return t.getFloatValue(); })));

    auto addPct = [&] (const char* pid, const char* name, float def)
    {
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pid, v }, name,
            NormalisableRange<float> (0.0f, 100.0f, 0.1f), def,
            AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (pct).withValueFromStringFunction (pctParse)));
    };
    addPct (id::sustain,      "Sustain",         30.0f);
    addPct (id::amount,       "Amount",         100.0f);
    addPct (id::flexTune,     "Flex Tune",        0.0f);
    addPct (id::volume,       "Volume",          80.0f);   // 80 % = unity gain
    addPct (id::doubleMix,    "Double Dry/Wet",   0.0f);
    addPct (id::doublesWidth, "Doubles Width",   50.0f);

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id::pitch, v }, "Pitch",
        NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("st")
            .withStringFromValueFunction ([] (float val, int) { return String (val, 1) + " st"; })
            .withValueFromStringFunction (pctParse)));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id::formant, v }, "Formant",
        NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("%")
            .withStringFromValueFunction ([] (float val, int) { return String (val, 1) + "%"; })
            .withValueFromStringFunction (pctParse)));

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { id::key, v }, "Key", keyChoices(), 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { id::scale, v }, "Scale", scaleChoices(), 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { id::inputType, v }, "Input Type", inputTypeChoices(), 1));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { id::lowLatency, v }, "Low Latency", false));

    const auto cMajor = scaleMask (0, ScaleType::Major);
    for (int pc = 0; pc < 12; ++pc)
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { id::note (pc), v },
            String ("Note ") + pitchClassName (pc, false), ((cMajor >> pc) & 1) != 0));

    return layout;
}

// "Sound" parameters stored in presets. Key, scale and note selection are deliberately NOT part of
// presets, so browsing presets never changes the song's key (same idea as "Keep Key & Scale").
inline const juce::StringArray& soundParameterIDs()
{
    static const juce::StringArray ids { id::retuneSpeed, id::sustain, id::amount, id::flexTune, id::volume,
                                         id::pitch, id::formant, id::doubleMix, id::doublesWidth };
    return ids;
}

struct FactoryPreset
{
    const char* name;
    float retune, sustain, amount, flex, volume, pitch, formant, dblMix, dblWidth;
};

inline const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> p {
        //  name                   speed  sus  amt  flex  vol  pitch  frm  dbl  width
        { "Default",               32.7f, 30, 100,   0,  80,   0,     0,   0,   50 },
        { "Hard Tune",            100.0f,  0, 100,   0,  80,   0,     0,   0,   50 },
        { "Modern Pop",            70.0f, 40, 100,  15,  80,   0,     0,  20,   60 },
        { "Trap Lead",             95.0f, 10, 100,   0,  80,   0,     0,  35,   80 },
        { "Natural Correction",    15.0f, 70,  85,  60,  80,   0,     0,   0,   50 },
        { "Subtle Polish",          5.0f, 80,  60,  80,  80,   0,     0,   0,   50 },
        { "Wide Doubles",          50.0f, 40, 100,  10,  78,   0,     0,  55,  100 },
        { "Deep Voice",            40.0f, 30, 100,   0,  80,  -5,   -40,   0,   50 },
        { "Bright Character",      60.0f, 20, 100,   0,  80,   7,    60,   0,   50 },
    };
    return p;
}

} // namespace ulz::params
