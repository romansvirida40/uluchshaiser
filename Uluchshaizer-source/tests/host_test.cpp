// Loads the BUILT Uluchshaizer.vst3 like a DAW would, processes a WAV through it and checks:
// parameters, latency reporting, Low Latency / Input Type latency changes, state round-trip.
// Usage: UluchshaizerHostTest <path/to/Uluchshaizer.vst3> <input.wav> <output.wav>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>
#include <thread>
#include <atomic>

using namespace juce;
static int failures = 0;
static void check (bool ok, const String& what) { std::printf ("  %s  %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8()); if (! ok) ++failures; }

static void setParam (AudioPluginInstance& p, const String& name, float denorm)
{
    for (auto* prm : p.getParameters())
        if (auto* r = dynamic_cast<AudioProcessorParameterWithID*> (prm))
            if (r->paramID == name || prm->getName (64) == name)
            {
                if (auto* ranged = dynamic_cast<RangedAudioParameter*> (prm)) { prm->setValueNotifyingHost (ranged->convertTo0to1 (denorm)); return; }
                // hosted VST3 parameters are not RangedAudioParameter: use text conversion
                prm->setValueNotifyingHost (prm->getValueForText (String (denorm)));
                return;
            }
    for (auto* prm : p.getParameters())
        if (prm->getName (64) == name) { prm->setValueNotifyingHost (prm->getValueForText (String (denorm))); return; }
    std::printf ("  (parameter %s not found)\n", name.toRawUTF8());
}

static AudioProcessorParameter* findParam (AudioPluginInstance& p, const String& name)
{
    for (auto* prm : p.getParameters()) if (prm->getName (64) == name) return prm;
    return nullptr;
}

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI init;
    if (argc < 4) { std::printf ("usage: host_test plugin.vst3 in.wav out.wav\n"); return 2; }

    AudioPluginFormatManager fm;
    fm.addFormat (std::make_unique<VST3PluginFormat>());
    OwnedArray<PluginDescription> found;
    VST3PluginFormat vst3;
    vst3.findAllTypesForFile (found, argv[1]);
    check (found.size() == 1, "VST3 scanned: " + (found.isEmpty() ? String ("none") : found[0]->name + " / " + found[0]->category));
    if (found.isEmpty()) return 1;

    String err;
    const double sr = 48000.0; const int block = 512;
    auto plugin = fm.createPluginInstance (*found[0], sr, block, err);
    check (plugin != nullptr, "instantiated " + err);
    if (plugin == nullptr) return 1;

    std::printf ("  parameters: %d\n", plugin->getParameters().size());
    for (auto* prm : plugin->getParameters()) std::printf ("    - %-16s = %s\n", prm->getName (64).toRawUTF8(), prm->getCurrentValueAsText().toRawUTF8());
    check (plugin->getParameters().size() >= 25, "all parameters exposed to the host");

    // read input
    AudioFormatManager afm; afm.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> reader (afm.createReaderFor (File (argv[2])));
    check (reader != nullptr, "read input wav");
    AudioBuffer<float> in (2, (int) reader->lengthInSamples);
    reader->read (&in, 0, (int) reader->lengthInSamples, 0, true, true);

    // hard tune, C major (default key), Alto/Tenor
    setParam (*plugin, "Retune Speed", 100.0f);
    setParam (*plugin, "Amount", 100.0f);
    plugin->setPlayConfigDetails (2, 2, sr, block);
    plugin->prepareToPlay (sr, block);
    const int lat = plugin->getLatencySamples();
    check (lat > 0, "latency reported after prepareToPlay: " + String (lat) + " samples (" + String (1000.0 * lat / sr, 1) + " ms)");

    AudioBuffer<float> out (2, in.getNumSamples());
    MidiBuffer midi;
    AudioBuffer<float> buf (2, block);
    for (int pos = 0; pos < in.getNumSamples(); pos += block)
    {
        const int n = jmin (block, in.getNumSamples() - pos);
        buf.setSize (2, n, false, false, true);
        for (int ch = 0; ch < 2; ++ch) buf.copyFrom (ch, 0, in, ch, pos, n);
        plugin->processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, pos, buf, ch, 0, n);
    }
    float peak = out.getMagnitude (0, out.getNumSamples());
    check (peak > 0.05f && peak <= 1.0f, "output has signal and stays below 0 dBFS (peak " + String (peak, 3) + ")");

    WavAudioFormat wav;
    File outFile (argv[3]); outFile.deleteFile();
    if (auto os = outFile.createOutputStream())
    {
        std::unique_ptr<AudioFormatWriter> w (wav.createWriterFor (os.get(), sr, 2, 24, {}, 0));
        if (w) { os.release(); w->writeFromAudioSampleBuffer (out, 0, out.getNumSamples()); }
    }

    // Low Latency must change the reported latency (async update on the message thread)
    setParam (*plugin, "Low Latency", 1.0f);
    if (auto* ll = findParam (*plugin, "Low Latency")) ll->setValueNotifyingHost (1.0f);
    for (int i = 0; i < 4; ++i) { buf.setSize (2, block); buf.clear(); plugin->processBlock (buf, midi); }
    MessageManager::getInstance()->runDispatchLoopUntil (300);
    const int latLL = plugin->getLatencySamples();
    check (latLL > 0 && latLL < lat, "Low Latency reduces reported latency: " + String (lat) + " -> " + String (latLL));

    if (auto* it = findParam (*plugin, "Input Type")) it->setValueNotifyingHost (1.0f);   // last choice = Bass Instrument
    for (int i = 0; i < 4; ++i) { buf.setSize (2, block); buf.clear(); plugin->processBlock (buf, midi); }
    MessageManager::getInstance()->runDispatchLoopUntil (300);
    check (plugin->getLatencySamples() > latLL, "Input Type = Bass Instrument raises latency: " + String (plugin->getLatencySamples()));

    // state round-trip
    if (auto* f = findParam (*plugin, "Flex Tune")) f->setValueNotifyingHost (0.42f);
    MemoryBlock state; plugin->getStateInformation (state);
    if (auto* f = findParam (*plugin, "Flex Tune")) f->setValueNotifyingHost (0.0f);
    plugin->setStateInformation (state.getData(), (int) state.getSize());
    MessageManager::getInstance()->runDispatchLoopUntil (200);
    const auto* f = findParam (*plugin, "Flex Tune");
    check (f != nullptr && std::abs (f->getValue() - 0.42f) < 0.01f, "state restore (Flex Tune " + (f ? f->getCurrentValueAsText() : String ("?")) + ")");

    // key/scale change rewrites the note toggles (E minor -> F# on, F off)
    if (auto* k = findParam (*plugin, "Key")) k->setValueNotifyingHost (4.0f / 11.0f);
    if (auto* s = findParam (*plugin, "Scale")) s->setValueNotifyingHost (0.5f);
    MessageManager::getInstance()->runDispatchLoopUntil (300);
    auto* nf = findParam (*plugin, "Note F"); auto* nfs = findParam (*plugin, "Note F#");
    check (nf && nfs && nf->getValue() < 0.5f && nfs->getValue() > 0.5f, "E minor: Note F off, Note F# on");

    // editor opens
    if (auto* ed = plugin->createEditorAndMakeActive())
    {
        check (ed->getWidth() >= 800 && ed->getHeight() >= 450, "editor created " + String (ed->getWidth()) + "x" + String (ed->getHeight()));
        delete ed;
    }

    plugin->releaseResources();
    plugin.reset();
    std::printf ("\n%s\n", failures == 0 ? "HOST TEST PASSED" : "HOST TEST FAILED");
    return failures == 0 ? 0 : 1;
}
