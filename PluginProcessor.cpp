#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    struct ScaleDef { const char* name; int mask; };

    const ScaleDef scaleTable[] = {
        { "Chromatic",        0xFFF },
        { "Major",            0xAB5 },
        { "Minor",            0x5AD },
        { "Harmonic Minor",   0x9AD },
        { "Dorian",           0x6AD },
        { "Mixolydian",       0x6B5 },
        { "Pentatonic Major", 0x295 },
        { "Pentatonic Minor", 0x4A9 },
        { "Blues",            0x4E9 },
    };
    constexpr int numScales = (int) (sizeof (scaleTable) / sizeof (scaleTable[0]));
}

int ULUCHSHAISERProcessor::getScaleMask (int index)
{
    return scaleTable[juce::jlimit (0, numScales - 1, index)].mask;
}

juce::AudioProcessorValueTreeState::ParameterLayout ULUCHSHAISERProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "key", 1 }, "Key",
                StringArray { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 0));

    StringArray scaleNames;
    for (auto& s : scaleTable) scaleNames.add (s.name);
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "scale", 1 }, "Scale", scaleNames, 1));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "speed", 1 }, "Retune Speed",
                NormalisableRange<float> (0.0f, 400.0f, 0.1f, 0.4f), 25.0f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "amount", 1 }, "Amount",
                NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "stab", 1 }, "Stabilizer",
                NormalisableRange<float> (0.0f, 100.0f, 1.0f), 30.0f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "dmix", 1 }, "Doubler Mix",
                NormalisableRange<float> (0.0f, 100.0f, 1.0f), 0.0f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "ddetune", 1 }, "Doubler Detune",
                NormalisableRange<float> (0.0f, 50.0f, 0.1f), 12.0f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "dwidth", 1 }, "Doubler Width",
                NormalisableRange<float> (0.0f, 100.0f, 1.0f), 80.0f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "out", 1 }, "Output",
                NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f));
    return layout;
}

//==============================================================================
ULUCHSHAISERProcessor::ULUCHSHAISERProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    pKey    = apvts.getRawParameterValue ("key");
    pScale  = apvts.getRawParameterValue ("scale");
    pSpeed  = apvts.getRawParameterValue ("speed");
    pAmount = apvts.getRawParameterValue ("amount");
    pStab   = apvts.getRawParameterValue ("stab");
    pMix    = apvts.getRawParameterValue ("dmix");
    pDetune = apvts.getRawParameterValue ("ddetune");
    pWidth  = apvts.getRawParameterValue ("dwidth");
    pOut    = apvts.getRawParameterValue ("out");

    for (auto& h : heat) h.store (0.0f);
    ring.assign ((size_t) ringSize, 0.0f);
    yinX.assign (4096, 0.0f);
    yinCm.assign (2048, 0.0f);
}

bool ULUCHSHAISERProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto& in  = l.getMainInputChannelSet();
    const auto& out = l.getMainOutputChannelSet();
    return in == out && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
}

void ULUCHSHAISERProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    for (auto& s : shifter) s.prepare (sr);
    shifterA.prepare (sr);
    shifterB.prepare (sr);
    delayA.prepare ((int) (0.06 * sr));
    delayB.prepare ((int) (0.06 * sr));
    std::fill (ring.begin(), ring.end(), 0.0f);
    ringPos = hopCount = unvoiced = 0;
    curTarget = -1;
    corr = targetCorr = 0.0f;
    setLatencySamples (shifter[0].latency());
}

//==============================================================================
bool ULUCHSHAISERProcessor::inScale (int m) const
{
    const int rel = ((m - keyVal) % 12 + 12) % 12;
    return (scaleMask & (1 << rel)) != 0;
}

int ULUCHSHAISERProcessor::nearestInScale (float midi) const
{
    const int base = (int) std::floor (midi);
    int best = base;
    float bd = 1.0e9f;
    for (int m = base - 6; m <= base + 7; ++m)
    {
        if (! inScale (m)) continue;
        const float d = std::abs ((float) m - midi);
        if (d < bd) { bd = d; best = m; }
    }
    return best;
}

// YIN pitch detector (de Cheveigne & Kawahara, 2002), with early exit.
float ULUCHSHAISERProcessor::detectPitch()
{
    const int tauMax = juce::jmin ((int) (sr / 70.0), 1400);
    const int N = yinW + tauMax;

    for (int i = 0; i < N; ++i)
        yinX[(size_t) i] = ring[(size_t) ((ringPos - N + i) & ringMask)];

    float energy = 0.0f;
    for (int j = 0; j < yinW; ++j) energy += yinX[(size_t) (tauMax + j)] * yinX[(size_t) (tauMax + j)];
    if (std::sqrt (energy / (float) yinW) < 0.004f) return 0.0f;   // too quiet

    yinCm[0] = 1.0f;
    float runSum = 0.0f;
    int best = -1;

    for (int tau = 1; tau <= tauMax - 1; ++tau)
    {
        float d = 0.0f;
        for (int j = 0; j < yinW; ++j)
        {
            const float diff = yinX[(size_t) j] - yinX[(size_t) (j + tau)];
            d += diff * diff;
        }
        runSum += d;
        yinCm[(size_t) tau] = runSum > 0.0f ? d * (float) tau / runSum : 1.0f;

        if (tau >= 3)
        {
            const float c1 = yinCm[(size_t) (tau - 1)];
            if (c1 < 0.15f && c1 <= yinCm[(size_t) (tau - 2)] && c1 <= yinCm[(size_t) tau])
            {
                best = tau - 1;
                break;
            }
        }
    }
    if (best < 2) return 0.0f;

    const float a = yinCm[(size_t) (best - 1)], b = yinCm[(size_t) best], c = yinCm[(size_t) (best + 1)];
    const float denom = a - 2.0f * b + c;
    const float shift = denom != 0.0f ? 0.5f * (a - c) / denom : 0.0f;
    return (float) sr / ((float) best + shift);
}

void ULUCHSHAISERProcessor::analyse()
{
    const float hz = detectPitch();

    for (auto& h : heat) h.store (h.load() * 0.97f);

    if (hz > 60.0f && hz < 1400.0f)
    {
        unvoiced = 0;
        const float midi = 69.0f + 12.0f * std::log2 (hz / 440.0f);
        const int nearest = nearestInScale (midi);

        if (curTarget < 0)
            curTarget = nearest;
        else if (nearest != curTarget)
        {
            const float hold = 0.5f + stab * 0.9f;   // stabilizer: how far a note may drift before we switch
            if (! (std::abs (midi - (float) curTarget) < hold && inScale (curTarget)))
                curTarget = nearest;
        }

        targetCorr = ((float) curTarget - midi) * amount;

        const int pc = ((int) std::lround (midi) % 12 + 12) % 12;
        heat[(size_t) pc].store (heat[(size_t) pc].load() + 0.03f);

        uiCents.store ((midi - (float) curTarget) * 100.0f);
        uiTarget.store ((float) curTarget);
        uiVoiced.store (true);
    }
    else if (++unvoiced > 6)
    {
        curTarget = -1;
        targetCorr = 0.0f;
        uiVoiced.store (false);
    }
}

//==============================================================================
void ULUCHSHAISERProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int nCh = juce::jmin (2, buffer.getNumChannels());
    const int n   = buffer.getNumSamples();
    if (nCh == 0) return;

    keyVal    = (int) pKey->load();
    scaleMask = getScaleMask ((int) pScale->load());
    amount    = pAmount->load() * 0.01f;
    stab      = pStab->load() * 0.01f;

    const float speedMs = juce::jmax (4.0f, pSpeed->load());
    const float coef    = 1.0f - std::exp (-1.0f / (speedMs * 0.001f * (float) sr));
    const float mix     = pMix->load() * 0.01f;
    const float width   = pWidth->load() * 0.01f;
    const float ratioA  = std::pow (2.0f, pDetune->load() / 1200.0f);
    const float ratioB  = 1.0f / ratioA;
    const float outGain = juce::Decibels::decibelsToGain (pOut->load());
    const int   dlyA    = (int) (0.019 * sr), dlyB = (int) (0.027 * sr);

    float* ch[2] = { buffer.getWritePointer (0), nCh > 1 ? buffer.getWritePointer (1) : nullptr };

    for (int s = 0; s < n; ++s)
    {
        const float in0 = ch[0][s];
        const float in1 = ch[1] != nullptr ? ch[1][s] : in0;

        ring[(size_t) ringPos] = 0.5f * (in0 + in1);
        ringPos = (ringPos + 1) & ringMask;
        if (++hopCount >= hopSize) { hopCount = 0; analyse(); }

        corr += coef * (targetCorr - corr);
        const float ratio = std::pow (2.0f, corr / 12.0f);

        float o0 = shifter[0].process (in0, ratio);
        float o1 = ch[1] != nullptr ? shifter[1].process (in1, ratio) : o0;

        if (mix > 0.001f)
        {
            const float m = 0.5f * (o0 + o1);
            const float a = shifterA.process (delayA.process (m, dlyA), ratioA);
            const float b = shifterB.process (delayB.process (m, dlyB), ratioB);
            const float side = 1.0f - width;
            o0 += mix * 0.7f * (a + b * side);
            o1 += mix * 0.7f * (b + a * side);
        }

        ch[0][s] = o0 * outGain;
        if (ch[1] != nullptr) ch[1][s] = o1 * outGain;
    }
}

//==============================================================================
juce::AudioProcessorEditor* ULUCHSHAISERProcessor::createEditor() { return new ULUCHSHAISEREditor (*this); }

void ULUCHSHAISERProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void ULUCHSHAISERProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ULUCHSHAISERProcessor(); }
