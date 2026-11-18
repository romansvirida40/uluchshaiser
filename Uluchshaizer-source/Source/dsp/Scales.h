#pragma once
// Uluchshaizer — musical scale definitions and context-sensitive note spelling.
// Framework-independent (no JUCE) so it can be unit-tested on its own.

#include <array>
#include <cstdint>
#include <cmath>
#include <string>

namespace ulz
{
// Index order must match the "scale" choice parameter.
enum class ScaleType : int { Major = 0, Minor = 1, Chromatic = 2 };

struct ScaleDef
{
    const char* name;
    int numIntervals;
    std::array<int, 12> intervals;
};

// Extend this table to add scale types (harmonic minor, pentatonic, modes...).
// The DSP only ever consumes the resulting 12-bit pitch-class mask.
inline const std::array<ScaleDef, 3>& scaleTable()
{
    static const std::array<ScaleDef, 3> table {{
        { "Major",     7,  { 0, 2, 4, 5, 7, 9, 11 } },
        { "Minor",     7,  { 0, 2, 3, 5, 7, 8, 10 } },   // natural minor (Aeolian)
        { "Chromatic", 12, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } },
    }};
    return table;
}

/** Bit n set => pitch class n (0 = C) is an allowed correction target. */
inline uint16_t scaleMask (int root, ScaleType type)
{
    const auto& def = scaleTable()[(size_t) type];
    uint16_t mask = 0;
    for (int i = 0; i < def.numIntervals; ++i)
        mask |= (uint16_t) (1u << ((root + def.intervals[(size_t) i]) % 12));
    return mask;
}

/** Whether a key is conventionally written with flats.
    Major:  F Bb Eb Ab Db use flats; Gb/F# is written F#.
    Minor:  D G C F Bb Eb use flats (Eb minor, not D# minor); G# / C# / F# minor use sharps. */
inline bool keyUsesFlats (int root, ScaleType type)
{
    root = ((root % 12) + 12) % 12;
    if (type == ScaleType::Minor)
    {
        switch (root) { case 2: case 7: case 0: case 5: case 10: case 3: return true; default: return false; }
    }
    switch (root) { case 5: case 10: case 3: case 8: case 1: return true; default: return false; }
}

inline const char* pitchClassName (int pc, bool flats)
{
    static const char* sharps[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    static const char* flatsN[12] = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
    pc = ((pc % 12) + 12) % 12;
    return flats ? flatsN[pc] : sharps[pc];
}

/** Name of the key itself, e.g. "Eb Minor", "F# Minor", "Ab Major". */
inline std::string keyName (int root, ScaleType type)
{
    std::string s = pitchClassName (root, keyUsesFlats (root, type));
    s += " ";
    s += scaleTable()[(size_t) type].name;
    return s;
}

inline float midiToHz (float midi, float a4 = 440.0f)  { return a4 * std::pow (2.0f, (midi - 69.0f) / 12.0f); }
inline float hzToMidi (float hz, float a4 = 440.0f)    { return 69.0f + 12.0f * std::log2 (hz / a4); }

} // namespace ulz
