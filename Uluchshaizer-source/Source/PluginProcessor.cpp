#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace ulz;

UluchshaizerProcessor::UluchshaizerProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "UluchshaizerState", params::createLayout())
{
    using namespace params;
    const char* ids[9] = { id::retuneSpeed, id::sustain, id::amount, id::flexTune, id::volume,
                           id::pitch, id::formant, id::doubleMix, id::doublesWidth };
    for (int i = 0; i < 9; ++i) p[i] = apvts.getRawParameterValue (ids[i]);
    keyParam = apvts.getRawParameterValue (id::key);
    scaleParam = apvts.getRawParameterValue (id::scale);
    inputTypeParam = apvts.getRawParameterValue (id::inputType);
    lowLatencyParam = apvts.getRawParameterValue (id::lowLatency);
    for (int pc = 0; pc < 12; ++pc) noteParams[pc] = apvts.getRawParameterValue (id::note (pc));

    apvts.addParameterListener (id::key, this);
    apvts.addParameterListener (id::scale, this);

    // report a sensible latency even before prepareToPlay (updated there for the real rate)
    setLatencySamples (VocalProcessor::latencyFor (48000.0, (InputType) (int) inputTypeParam->load(), false));
}

UluchshaizerProcessor::~UluchshaizerProcessor()
{
    apvts.removeParameterListener (params::id::key, this);
    apvts.removeParameterListener (params::id::scale, this);
    cancelPendingUpdate();
}

bool UluchshaizerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo()) return false;
    return in.size() <= out.size();   // mono->mono, stereo->stereo, mono->stereo (for wide doubles)
}

void UluchshaizerProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    dsp.prepare (sampleRate);
    scratch.setSize (2, std::max (1, samplesPerBlock));
    setLatencySamples (VocalProcessor::latencyFor (sampleRate, (InputType) (int) inputTypeParam->load(),
                                                   lowLatencyParam->load() > 0.5f));
}

uint16_t UluchshaizerProcessor::getNoteMask() const noexcept
{
    uint16_t m = 0;
    for (int pc = 0; pc < 12; ++pc)
        if (noteParams[pc]->load() > 0.5f) m |= (uint16_t) (1u << pc);
    return m;
}

void UluchshaizerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels();
    const int n = buffer.getNumSamples();
    if (n == 0) return;
    for (int ch = numIn; ch < numOut; ++ch)
        buffer.copyFrom (ch, 0, buffer, 0, 0, n);   // mono in -> duplicate before stereo processing

    VocalSettings s;
    s.retuneSpeed  = p[0]->load();
    s.sustain      = p[1]->load() * 0.01f;
    s.amount       = p[2]->load() * 0.01f;
    s.flexTune     = p[3]->load() * 0.01f;
    s.volume       = p[4]->load() * 0.01f;
    s.pitchSt      = p[5]->load();
    s.formantPct   = p[6]->load();
    s.doubleMix    = p[7]->load() * 0.01f;
    s.doublesWidth = p[8]->load() * 0.01f;
    s.noteMask     = getNoteMask();
    s.inputType    = (InputType) juce::jlimit (0, 4, (int) inputTypeParam->load());
    s.lowLatency   = lowLatencyParam->load() > 0.5f;

    const bool latencyChanged = dsp.process (buffer.getArrayOfWritePointers(), juce::jmin (2, numOut), n, s);
    if (latencyChanged)
    {
        latencyUpdatePending = true;
        triggerAsyncUpdate();
    }
}

void UluchshaizerProcessor::parameterChanged (const juce::String& parameterID, float)
{
    if (parameterID == params::id::key || parameterID == params::id::scale)
    {
        scaleApplyPending = true;
        triggerAsyncUpdate();
    }
}

void UluchshaizerProcessor::handleAsyncUpdate()
{
    if (scaleApplyPending.exchange (false))
        applyScaleToNotes();
    if (latencyUpdatePending.exchange (false))
        setLatencySamples (dsp.getLatency());
}

void UluchshaizerProcessor::applyScaleToNotes()
{
    const auto mask = scaleMask (getRoot(), getScaleType());
    for (int pc = 0; pc < 12; ++pc)
    {
        const bool want = ((mask >> pc) & 1) != 0;
        if (auto* prm = apvts.getParameter (params::id::note (pc)))
            if ((prm->getValue() > 0.5f) != want)
            {
                prm->beginChangeGesture();
                prm->setValueNotifyingHost (want ? 1.0f : 0.0f);
                prm->endChangeGesture();
            }
    }
}

// ---------------------------------------------------------------- programs (factory presets)
int UluchshaizerProcessor::getNumPrograms() { return (int) params::factoryPresets().size(); }

void UluchshaizerProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumPrograms())) return;
    currentProgram = index;
    presets.loadFactory (index);
}

const juce::String UluchshaizerProcessor::getProgramName (int index)
{
    const auto& f = params::factoryPresets();
    return juce::isPositiveAndBelow (index, (int) f.size()) ? juce::String (f[(size_t) index].name) : juce::String();
}

// ---------------------------------------------------------------- state
void UluchshaizerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("presetName", presets.getCurrentName(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void UluchshaizerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            apvts.replaceState (tree);
            presets.setCurrentName (tree.getProperty ("presetName", "Default").toString());
            // the restored note selection (possibly custom) wins over the key/scale listener
            scaleApplyPending = false;
        }
}

juce::AudioProcessorEditor* UluchshaizerProcessor::createEditor() { return new UluchshaizerEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new UluchshaizerProcessor(); }
