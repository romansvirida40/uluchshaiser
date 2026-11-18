// Renders the real editor while the real processor runs on a synthetic voice in real time,
// so the waveform / detected note / piano highlighting are driven by actual DSP output.
// Usage: UluchshaizerUiSnapshot <voice.wav> <outDir>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <thread>
#include <atomic>
using namespace juce;

class SnapshotApp : public JUCEApplication, private Timer
{
public:
    const String getApplicationName() override { return "UluchshaizerUiSnapshot"; }
    const String getApplicationVersion() override { return "1.0"; }

    void initialise (const String& cmd) override
    {
        auto args = StringArray::fromTokens (cmd, true);
        args.removeEmptyStrings();
        if (args.size() < 2) { setApplicationReturnValue (2); quit(); return; }
        outDir = args[1].unquoted();
        AudioFormatManager afm; afm.registerBasicFormats();
        std::unique_ptr<AudioFormatReader> reader (afm.createReaderFor (File (args[0].unquoted())));
        in.setSize (2, (int) reader->lengthInSamples);
        reader->read (&in, 0, (int) reader->lengthInSamples, 0, true, true);

        proc = std::make_unique<UluchshaizerProcessor>();
        proc->setPlayConfigDetails (2, 2, 48000.0, 512);
        proc->prepareToPlay (48000.0, 512);
        auto set = [&] (const char* id, float v) { auto* p = proc->apvts.getParameter (id); p->setValueNotifyingHost (p->convertTo0to1 (v)); };
        set ("retuneSpeed", 32.7f); set ("sustain", 72.0f); set ("amount", 58.0f); set ("flexTune", 50.0f);
        set ("doubleMix", 49.0f); set ("doublesWidth", 48.0f);

        editor.reset (proc->createEditor());
        window = std::make_unique<DocumentWindow> ("Uluchshaizer", Colours::black, 0, true);
        window->setUsingNativeTitleBar (true);
        window->setContentNonOwned (editor.get(), true);
        window->setVisible (true);

        feeder = std::thread ([this]
        {
            AudioBuffer<float> b (2, 512); MidiBuffer m;
            const int start = (int) (2.4 * 48000);
            int pos = start;
            while (! stop)
            {
                if (pos + 512 >= in.getNumSamples()) pos = start;
                for (int ch = 0; ch < 2; ++ch) b.copyFrom (ch, 0, in, ch, pos, 512);
                proc->processBlock (b, m);
                pos += 512;
                Thread::sleep (10);
            }
        });
        startTimer (100);
    }

    void shutdown() override
    {
        stop = true;
        if (feeder.joinable()) feeder.join();
        if (window) window->clearContentComponent();
        editor.reset(); window.reset(); proc.reset();
    }
    void systemRequestedQuit() override { quit(); }

private:
    void snap (const String& name)
    {
        auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        File f (outDir + "/" + name); f.deleteFile();
        PNGImageFormat png;
        if (auto os = f.createOutputStream()) png.writeImageToStream (img, *os);
    }
    void timerCallback() override
    {
        ++n;
        if (n == 18) snap ("live_compact.png");
        if (n == 19) if (auto* e = dynamic_cast<UluchshaizerEditor*> (editor.get())) e->setPianoOpen (true);
        if (n == (getenv ("ULZ_LONG") ? 120 : 30)) { snap ("live_piano.png"); stopTimer(); quit(); }
    }

    AudioBuffer<float> in;
    std::unique_ptr<UluchshaizerProcessor> proc;
    std::unique_ptr<AudioProcessorEditor> editor;
    std::unique_ptr<DocumentWindow> window;
    std::thread feeder;
    std::atomic<bool> stop { false };
    String outDir;
    int n = 0;
};

START_JUCE_APPLICATION (SnapshotApp)
