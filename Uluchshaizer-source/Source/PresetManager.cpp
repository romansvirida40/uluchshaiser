#include "PresetManager.h"
#include "Parameters.h"

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& s) : apvts (s) {}

juce::File PresetManager::getUserPresetFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
              .getChildFile ("Uluchshaizer").getChildFile ("Presets");
}

juce::StringArray PresetManager::getFactoryNames() const
{
    juce::StringArray names;
    for (const auto& p : ulz::params::factoryPresets()) names.add (p.name);
    return names;
}

juce::Array<juce::File> PresetManager::getUserPresets() const
{
    auto files = getUserPresetFolder().findChildFiles (juce::File::findFiles, false, "*.ulzpreset");
    files.sort();
    return files;
}

void PresetManager::setParam (const juce::String& id, float value)
{
    if (auto* prm = apvts.getParameter (id))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (value));
        prm->endChangeGesture();
    }
}

void PresetManager::loadFactory (int index)
{
    const auto& list = ulz::params::factoryPresets();
    if (! juce::isPositiveAndBelow (index, (int) list.size())) return;
    const auto& f = list[(size_t) index];
    using namespace ulz::params;
    setParam (id::retuneSpeed, f.retune);
    setParam (id::sustain, f.sustain);
    setParam (id::amount, f.amount);
    setParam (id::flexTune, f.flex);
    setParam (id::volume, f.volume);
    setParam (id::pitch, f.pitch);
    setParam (id::formant, f.formant);
    setParam (id::doubleMix, f.dblMix);
    setParam (id::doublesWidth, f.dblWidth);
    currentName = f.name;
    if (onPresetLoaded) onPresetLoaded();
}

bool PresetManager::loadUser (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName ("UluchshaizerPreset")) return false;
    for (const auto& pid : ulz::params::soundParameterIDs())
        if (xml->hasAttribute (pid))
            setParam (pid, (float) xml->getDoubleAttribute (pid));
    currentName = file.getFileNameWithoutExtension();
    if (onPresetLoaded) onPresetLoaded();
    return true;
}

bool PresetManager::saveUser (const juce::String& rawName)
{
    const auto name = juce::File::createLegalFileName (rawName.trim());
    if (name.isEmpty()) return false;
    auto folder = getUserPresetFolder();
    if (! folder.createDirectory()) return false;

    juce::XmlElement xml ("UluchshaizerPreset");
    xml.setAttribute ("version", 1);
    for (const auto& pid : ulz::params::soundParameterIDs())
        if (auto* prm = apvts.getParameter (pid))
            xml.setAttribute (pid, prm->convertFrom0to1 (prm->getValue()));

    if (! xml.writeTo (folder.getChildFile (name + ".ulzpreset"))) return false;
    currentName = name;
    return true;
}

void PresetManager::step (int delta)
{
    const auto factory = getFactoryNames();
    const auto user = getUserPresets();
    const int total = factory.size() + user.size();
    if (total == 0) return;

    int cur = factory.indexOf (currentName);
    if (cur < 0)
        for (int i = 0; i < user.size(); ++i)
            if (user[i].getFileNameWithoutExtension() == currentName) cur = factory.size() + i;
    if (cur < 0) cur = 0;

    const int next = ((cur + delta) % total + total) % total;
    if (next < factory.size()) loadFactory (next);
    else loadUser (user[next - factory.size()]);
}
