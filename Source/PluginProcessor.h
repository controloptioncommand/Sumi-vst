#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Params.h"
#include "Presets.h"
#include "Picture.h"
#include "DSP/SpectralEngine.h"

namespace sumi
{
class SumiProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    SumiProcessor();
    ~SumiProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sumi"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Картинка (хранится в проекте). Источник: «factory:enso», «text:SUMI», «image» (PNG внутри состояния).
    void setPictureSource (const juce::String& source);
    void setPictureImage (const juce::Image& img, const juce::String& name);
    bool loadPictureFile (const juce::File& f);
    void clearPicture();
    juce::String getPictureSource() const { return apvts.state.getProperty ("pictureSource").toString(); }
    juce::String getPictureName() const;
    std::shared_ptr<const Picture> getLatestPicture() const { return latest; }   // поток сообщений
    void rebuildPicture();

    EngineParams snapshotParams() const;
    int desiredOrder() const;
    double beatsPerBar() const { return bar.load(); }

    juce::AudioProcessorValueTreeState apvts;
    PresetManager presets;
    SpectralEngine engine;

    // для интерфейса: выход (моно) в кольцо, позиция картинки, пик, нагрузка
    static constexpr int kScopeSize = 1 << 15;
    juce::AbstractFifo scopeFifo { kScopeSize };
    std::vector<float> scopeBuf = std::vector<float> (kScopeSize, 0.0f);
    std::atomic<float> uiPhase { -1.0f }, outPeak { 0.0f }, cpuLoad { 0.0f };
    std::atomic<bool> hostTransport { false };
    std::atomic<double> bar { 4.0 };

    // для тестов: позиция свободного хода (без транспорта хоста)
    double freePpq = 0.0;

private:
    void timerCallback() override;
    juce::String pictureKey() const;

    std::shared_ptr<const Picture> latest;
    std::vector<std::shared_ptr<const Picture>> keepAlive;   // аудиопоток не освобождает память
    std::shared_ptr<const Picture> pending;
    juce::SpinLock pendingLock;
    juce::String builtKey;
    juce::SmoothedValue<float> outGain;
    double sr = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SumiProcessor)
};
} // namespace sumi
