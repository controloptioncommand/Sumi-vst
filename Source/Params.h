#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace sumi
{
namespace ParamId
{
    inline constexpr const char* mode = "mode";             // Paint / Imprint
    inline constexpr const char* blend = "blend";           // Imprint: Add / Mask
    // картинка → спектр
    inline constexpr const char* scale = "scale";           // Lin / Log / Mel
    inline constexpr const char* low = "low";               // нижний край картинки, Гц
    inline constexpr const char* high = "high";             // верхний край картинки, Гц
    inline constexpr const char* contrast = "contrast";     // гамма яркости
    inline constexpr const char* range = "range";           // яркость 0…1 → −Range…0 дБ
    inline constexpr const char* invert = "invert";
    // время
    inline constexpr const char* start = "start";           // такт начала
    inline constexpr const char* length = "length";         // длина картинки в тактах
    inline constexpr const char* loop = "loop";
    // звук
    inline constexpr const char* level = "level";           // громкость картинки (в Mask — громкость обработанного звука)
    inline constexpr const char* tone = "tone";             // фаза: 0 — шум, 100 — ровные тона
    inline constexpr const char* width = "width";           // стерео
    inline constexpr const char* mix = "mix";               // Imprint: сухой / обработанный
    inline constexpr const char* flatten = "flatten";       // Mask: выравнивание спектра входа
    inline constexpr const char* resolution = "resolution"; // размер FFT
    inline constexpr const char* output = "output";
}

enum class Mode { Paint = 0, Imprint = 1 };
enum class Blend { Add = 0, Mask = 1 };
enum class Scale { Lin = 0, Log = 1, Mel = 2 };

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
juce::StringArray lengthNames();
float lengthBars (int index);              // 1/2, 1, 2 … 64
int fftOrderFor (int resolutionIndex);     // 10…13
juce::String formatHz (float hz);          // «200 Hz», «16 kHz»
} // namespace sumi
