#pragma once
// Input Type presets: each one changes the pitch detector's search range and tracking behaviour.

namespace ulz
{
enum class InputType : int { Soprano = 0, AltoTenor = 1, LowMale = 2, Instrument = 3, BassInstrument = 4 };

struct InputTypeConfig
{
    float minHz;            // lowest fundamental searched by the detector
    float maxHz;            // highest fundamental searched by the detector
    float yinThreshold;     // YIN absolute threshold (lower = stricter periodicity test)
    float voicedThreshold;  // max YIN aperiodicity accepted as "voiced"
    float hysteresisSt;     // extra distance (semitones) required before switching target note
    int   medianLength;     // pitch-track median filter length (frames), odd
};

inline InputTypeConfig getInputTypeConfig (InputType t)
{
    switch (t)
    {
        case InputType::Soprano:        return { 150.0f, 1200.0f, 0.15f, 0.30f, 0.12f, 3 };
        case InputType::AltoTenor:      return {  95.0f,  850.0f, 0.15f, 0.30f, 0.12f, 3 };
        case InputType::LowMale:        return {  65.0f,  520.0f, 0.15f, 0.30f, 0.12f, 5 };
        case InputType::Instrument:     return {  60.0f, 1600.0f, 0.12f, 0.25f, 0.06f, 3 };
        case InputType::BassInstrument: return {  30.0f,  420.0f, 0.12f, 0.25f, 0.06f, 5 };
    }
    return { 95.0f, 850.0f, 0.15f, 0.30f, 0.12f, 3 };
}

} // namespace ulz
