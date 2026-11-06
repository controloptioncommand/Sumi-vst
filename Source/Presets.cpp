#include "Presets.h"
#include "Params.h"

namespace sumi
{
const std::vector<FactoryPreset>& factoryPresets()
{
    using namespace ParamId;
    // mode: 0 Paint, 1 Imprint; blend: 0 Add, 1 Mask; scale: 0 Lin, 1 Log, 2 Mel; length: 1 — 1 bar, 2 — 2, 3 — 4, 4 — 8
    static const std::vector<FactoryPreset> list = {
        { "Enso", "enso", { { mode, 0 }, { scale, 1 }, { low, 200 }, { high, 16000 }, { length, 3 } } },
        { "Ink Wordmark", "wordmark", { { mode, 0 }, { scale, 0 }, { low, 1000 }, { high, 18000 }, { length, 2 }, { range, 48 } } },
        { "Misty Mountains", "mountains", { { mode, 0 }, { scale, 2 }, { low, 60 }, { high, 12000 }, { length, 4 }, { tone, 35 } } },
        { "Bamboo Organ", "bamboo", { { mode, 0 }, { scale, 1 }, { low, 80 }, { high, 8000 }, { length, 3 }, { tone, 100 }, { width, 40 } } },
        { "Rain on Glass", "rain", { { mode, 0 }, { scale, 1 }, { low, 1500 }, { high, 18000 }, { length, 2 }, { level, 6 } } },
        { "Sliding Tones", "waves", { { mode, 0 }, { scale, 1 }, { low, 100 }, { high, 4000 }, { length, 3 }, { tone, 100 }, { resolution, 3 } } },
        { "Test Card", "testcard", { { mode, 0 }, { scale, 0 }, { low, 20 }, { high, 20000 }, { length, 1 } } },
        { "Carve Mountains", "mountains", { { mode, 1 }, { blend, 1 }, { scale, 1 }, { low, 60 }, { high, 16000 }, { length, 3 }, { flatten, 70 }, { level, 6 } } },
        { "Enso Stencil", "enso", { { mode, 1 }, { blend, 1 }, { scale, 1 }, { low, 100 }, { high, 16000 }, { length, 2 }, { flatten, 85 }, { range, 36 }, { level, 6 } } },
        { "Hidden Signature", "text:SUMI", { { mode, 1 }, { blend, 0 }, { scale, 0 }, { low, 15000 }, { high, 19500 }, { length, 2 }, { level, -18 }, { loop, 0 } } },
        { "Rain Layer", "rain", { { mode, 1 }, { blend, 0 }, { scale, 1 }, { low, 2000 }, { high, 18000 }, { length, 2 }, { level, -12 } } },
        { "Half Mask", "bamboo", { { mode, 1 }, { blend, 1 }, { scale, 1 }, { low, 80 }, { high, 12000 }, { length, 3 }, { mix, 60 }, { flatten, 40 } } },
    };
    return list;
}

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& s, std::function<void (const juce::String&)> sp)
    : apvts (s), setPictureSource (std::move (sp)) {}

juce::String PresetManager::getCurrentName() const { return apvts.state.getProperty ("presetName", "Init").toString(); }
void PresetManager::setCurrentName (const juce::String& n) { apvts.state.setProperty ("presetName", n, nullptr); }

void PresetManager::loadFactory (int index)
{
    const auto& list = factoryPresets();
    if (index < 0 || index >= (int) list.size()) return;
    const auto& pr = list[(size_t) index];
    for (auto* p : apvts.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            if (rp->paramID == ParamId::output) continue;   // громкость выхода пресет не трогает
            float v = rp->getDefaultValue();
            for (auto& kv : pr.values)
                if (rp->paramID == kv.first) v = rp->convertTo0to1 (kv.second);
            rp->setValueNotifyingHost (v);
        }
    setPictureSource (pr.picture);
    setCurrentName (pr.name);
}

juce::File PresetManager::userFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Sumi").getChildFile ("Presets");
}

juce::Array<juce::File> PresetManager::userPresets() const
{
    auto files = userFolder().findChildFiles (juce::File::findFiles, false, "*.sumi");
    files.sort();
    return files;
}

bool PresetManager::loadUser (const juce::File& f)
{
    auto xml = juce::XmlDocument::parse (f);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return false;
    apvts.replaceState (juce::ValueTree::fromXml (*xml));
    setCurrentName (f.getFileNameWithoutExtension());
    return true;
}

bool PresetManager::save (const juce::String& name)
{
    const auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty()) return false;
    auto dir = userFolder();
    if (! dir.createDirectory()) return false;
    auto state = apvts.copyState();
    state.setProperty ("presetName", clean, nullptr);
    auto xml = state.createXml();
    if (xml == nullptr || ! xml->writeTo (dir.getChildFile (clean + ".sumi"))) return false;
    setCurrentName (clean);
    return true;
}

void PresetManager::step (int dir)
{
    const auto users = userPresets();
    const int nf = getNumFactory(), total = nf + users.size();
    if (total == 0) return;
    const auto cur = getCurrentName();
    int idx = -1;
    for (int i = 0; i < nf; ++i) if (cur == factoryPresets()[(size_t) i].name) idx = i;
    for (int i = 0; i < users.size(); ++i) if (cur == users[i].getFileNameWithoutExtension()) idx = nf + i;
    idx = idx < 0 ? (dir > 0 ? 0 : total - 1) : (idx + dir + total) % total;
    if (idx < nf) loadFactory (idx);
    else loadUser (users[idx - nf]);
}
} // namespace sumi
