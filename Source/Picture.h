#pragma once
#include <juce_graphics/juce_graphics.h>
#include <memory>
#include <vector>

namespace sumi
{
// Картинка в виде яркости 0…1 (строка 0 — верх = высокие частоты) и таблицы сумм для быстрого
// среднего по любому прямоугольнику с дробными краями. Неизменяема: аудиопоток получает её готовой.
struct Picture
{
    static constexpr int kMaxW = 1024, kMaxH = 768;

    int w = 0, h = 0;
    std::vector<float> lum;     // h × w
    std::vector<double> sat;    // (h + 1) × (w + 1)
    float mean = 0.0f;
    juce::String name;

    bool empty() const { return w <= 0 || h <= 0; }
    float at (int x, int y) const { return lum[(size_t) (y * w + x)]; }
    // среднее яркости по [x0, x1] × [y0, y1] в пикселях (края дробные, за пределами картинки — обрезается)
    float boxMean (double x0, double x1, double y0, double y1) const;

    static std::shared_ptr<Picture> fromImage (const juce::Image& img, const juce::String& name);

private:
    double integral (double x, double y) const;
};

// Как яркость становится громкостью — общее для звука и экрана
struct Look
{
    float gamma = 1.0f, rangeDb = 60.0f;
    bool invert = false;
    static constexpr float kBlack = 0.03f;   // ниже — тишина (шум JPEG в чёрном не звучит)

    // 0…1 после Invert и Contrast; < 0 — тишина
    float level (float v) const
    {
        if (invert) v = 1.0f - v;
        if (v <= kBlack) return -1.0f;
        return std::pow (juce::jlimit (0.0f, 1.0f, (v - kBlack) / (1.0f - kBlack)), gamma);
    }
    float amp (float v) const
    {
        const float l = level (v);
        return l < 0.0f ? 0.0f : std::pow (10.0f, (l - 1.0f) * rangeDb / 20.0f);
    }
};

namespace Pictures
{
    // Встроенные картинки рисуются кодом (одинаково на любой машине), в состоянии хранится только их id
    juce::StringArray factoryIds();
    juce::String factoryName (const juce::String& id);
    juce::Image makeFactory (const juce::String& id);
    juce::Image makeText (const juce::String& text);

    juce::Image fit (const juce::Image& img);              // не больше kMaxW × kMaxH, RGB
    juce::String encode (const juce::Image& img);         // PNG → base64
    juce::Image decode (const juce::String& base64);
    juce::Image loadFile (const juce::File& f);
    bool isImageFile (const juce::String& path);
}
} // namespace sumi
