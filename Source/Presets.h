#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace sumi
{
// Фабричные пресеты: встроенная картинка + настройки (остальные параметры — по умолчанию)
struct FactoryPreset
{
    const char* name;
    const char* picture;   // id встроенной картинки или «text:…»
    std::vector<std::pair<const char*, float>> values;
};

const std::vector<FactoryPreset>& factoryPresets();

// Пресеты: фабричные и пользовательские (~/Music/Sumi/Presets/*.sumi). Пользовательский хранит и картинку.
class PresetManager
{
public:
    PresetManager (juce::AudioProcessorValueTreeState& state, std::function<void (const juce::String&)> setPictureSource);

    int getNumFactory() const { return (int) factoryPresets().size(); }
    void loadFactory (int index);
    void step (int dir);
    juce::String getCurrentName() const;
    void setCurrentName (const juce::String& n);

    static juce::File userFolder();
    juce::Array<juce::File> userPresets() const;
    bool loadUser (const juce::File& f);
    bool save (const juce::String& name);

private:
    juce::AudioProcessorValueTreeState& apvts;
    std::function<void (const juce::String&)> setPictureSource;
};
} // namespace sumi
