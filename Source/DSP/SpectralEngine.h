#pragma once
#include <juce_dsp/juce_dsp.h>
#include "../Picture.h"
#include "FreqMap.h"
#include <atomic>

namespace sumi
{
struct EngineParams
{
    int mode = 0;       // 0 Paint, 1 Imprint
    int blend = 1;      // 0 Add, 1 Mask
    FreqMap map;
    Look look;
    float level = 1.0f; // множитель
    float tone = 0.0f, width = 1.0f, mix = 1.0f, flatten = 0.5f;
};

// Где картинка на временной шкале DAW
struct Timing
{
    bool running = false;          // транспорт идёт (или свободный ход)
    double ppq = 0.0;              // позиция начала блока, в четвертях
    double beatsPerSample = 0.0;
    double startBeat = 0.0, lengthBeats = 16.0;
    bool loop = true;

    // доля картинки 0…1 для момента t (в сэмплах от начала блока); < 0 — картинки нет
    double phaseAt (double t) const
    {
        if (! running || lengthBeats <= 0.0) return -1.0;
        const double rel = (ppq + t * beatsPerSample - startBeat) / lengthBeats;
        if (rel < 0.0) return -1.0;
        if (loop) return rel - std::floor (rel);
        return rel < 1.0 ? rel : -1.0;
    }
};

// STFT-движок: Hann-окна, перекрытие 75%, задержка = размер FFT.
// Paint — спектр каждого кадра строится из столбца картинки (громкость по яркости, фаза — шум или ровные тона).
// Imprint — спектр входа: Add — картинка прибавляется, Mask — вход умножается на картинку.
class SpectralEngine
{
public:
    static constexpr int kMinOrder = 10, kMaxOrder = 13, kMaxSize = 1 << kMaxOrder, kMaxChannels = 2;

    SpectralEngine();
    void prepare (double sampleRate);
    void setOrder (int order);   // сбрасывает буферы; без выделений памяти
    int getOrder() const { return order; }
    int getSize() const { return N; }
    int getLatency() const { return N; }
    void reset();

    void setPicture (std::shared_ptr<const Picture> p) { picture = std::move (p); }
    const Picture* getPicture() const { return picture.get(); }

    void process (float* const* io, int numChannels, int numSamples, const EngineParams& p, const Timing& t);

    // для тестов: сколько кадров с картинкой обработано
    int64_t framesWithPicture = 0;

private:
    void doFrame (int t, int numChannels, const EngineParams& p, const Timing& tm);
    void updateBins (const FreqMap& m);
    void addPicture (float* spec, int channel, float gain, const EngineParams& p) const;
    uint32_t rnd() { state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state; }
    float rndPhase() { return (float) (rnd() >> 8) * (juce::MathConstants<float>::twoPi / 16777216.0f) - juce::MathConstants<float>::pi; }

    double sr = 48000.0;
    int order = 12, N = 4096, hop = 1024, inPos = 0, outPos = 0, hopCount = 0;
    std::unique_ptr<juce::dsp::FFT> ffts[kMaxOrder + 1];
    std::vector<float> window;
    std::vector<float> inRing[kMaxChannels], outAcc[kMaxChannels];
    std::vector<float> buf;                 // 2N для FFT
    std::vector<float> phase, stereoOffset; // по бинам
    std::vector<float> amp, binY0, binY1;   // громкость бина и его полоса в картинке (доли высоты, 0 — верх)
    std::vector<float> power, prefix;       // для Flatten
    int kLo = 1, kHi = 0;
    FreqMap binMap { -1, 0, 1 };
    int binsForN = 0;
    double binsForRate = 0;
    std::shared_ptr<const Picture> picture;
    uint32_t state = 0x9E3779B9u;
};
} // namespace sumi
