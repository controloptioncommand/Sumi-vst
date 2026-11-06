#include "Picture.h"
#include "UI/LookAndFeel.h"

namespace sumi
{
// ===========================================================================
double Picture::integral (double x, double y) const
{
    // сумма яркости по [0, x] × [0, y]: внутри клетки — билинейно (это точный интеграл кусочно-постоянной картинки)
    x = juce::jlimit (0.0, (double) w, x);
    y = juce::jlimit (0.0, (double) h, y);
    const int xi = juce::jmin ((int) x, w - 1), yi = juce::jmin ((int) y, h - 1);
    const double fx = x - xi, fy = y - yi;
    const size_t W = (size_t) w + 1;
    const double s00 = sat[(size_t) yi * W + (size_t) xi], s10 = sat[(size_t) yi * W + (size_t) xi + 1];
    const double s01 = sat[((size_t) yi + 1) * W + (size_t) xi], s11 = sat[((size_t) yi + 1) * W + (size_t) xi + 1];
    return s00 * (1 - fx) * (1 - fy) + s10 * fx * (1 - fy) + s01 * (1 - fx) * fy + s11 * fx * fy;
}

float Picture::boxMean (double x0, double x1, double y0, double y1) const
{
    if (empty()) return 0.0f;
    x0 = juce::jlimit (0.0, (double) w, x0); x1 = juce::jlimit (0.0, (double) w, x1);
    y0 = juce::jlimit (0.0, (double) h, y0); y1 = juce::jlimit (0.0, (double) h, y1);
    // слишком узкое окно (у края) — расширяем до четверти пикселя внутрь картинки
    constexpr double minSpan = 0.25;
    if (x1 - x0 < minSpan) { if (x0 + minSpan <= w) x1 = x0 + minSpan; else x0 = x1 - minSpan; }
    if (y1 - y0 < minSpan) { if (y0 + minSpan <= h) y1 = y0 + minSpan; else y0 = y1 - minSpan; }
    const double s = integral (x1, y1) - integral (x0, y1) - integral (x1, y0) + integral (x0, y0);
    return (float) juce::jlimit (0.0, 1.0, s / ((x1 - x0) * (y1 - y0)));
}

std::shared_ptr<Picture> Picture::fromImage (const juce::Image& src, const juce::String& nm)
{
    auto p = std::make_shared<Picture>();
    p->name = nm;
    if (! src.isValid()) return p;
    const auto img = Pictures::fit (src);
    p->w = img.getWidth();
    p->h = img.getHeight();
    p->lum.resize ((size_t) (p->w * p->h));
    {
        const juce::Image::BitmapData bd (img, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < p->h; ++y)
            for (int x = 0; x < p->w; ++x)
            {
                const auto c = bd.getPixelColour (x, y);
                // прозрачное — как чёрное
                const float l = (0.2126f * c.getFloatRed() + 0.7152f * c.getFloatGreen() + 0.0722f * c.getFloatBlue()) * c.getFloatAlpha();
                p->lum[(size_t) (y * p->w + x)] = juce::jlimit (0.0f, 1.0f, l);
            }
    }
    const size_t W = (size_t) p->w + 1;
    p->sat.assign (W * ((size_t) p->h + 1), 0.0);
    double total = 0;
    for (int y = 0; y < p->h; ++y)
    {
        double row = 0;
        for (int x = 0; x < p->w; ++x)
        {
            row += p->lum[(size_t) (y * p->w + x)];
            p->sat[((size_t) y + 1) * W + (size_t) x + 1] = p->sat[(size_t) y * W + (size_t) x + 1] + row;
        }
        total += row;
    }
    p->mean = (float) (total / (double) (p->w * p->h));
    return p;
}

// ===========================================================================
namespace Pictures
{
namespace
{
constexpr int kW = 1024, kH = 512;

juce::Image blank (int w = kW, int h = kH)
{
    juce::Image img (juce::Image::RGB, w, h, true);
    juce::Graphics g (img);
    g.fillAll (juce::Colours::black);
    return img;
}

juce::Colour grey (float v) { return juce::Colour::greyLevel (juce::jlimit (0.0f, 1.0f, v)); }

// Энсо: круг одним мазком кисти. Ворсинки кончаются в разное время — сухой хвост.
juce::Image enso()
{
    auto img = blank();
    juce::Graphics g (img);
    juce::Random rng (53);
    const juce::Point<float> c (512.0f, 256.0f);
    const float R = 196.0f;
    const float a0 = juce::degreesToRadians (-110.0f), sweep = juce::degreesToRadians (322.0f);
    constexpr int bristles = 44, steps = 1600;
    for (int b = 0; b < bristles; ++b)
    {
        const float o = (float) b / (bristles - 1) - 0.5f;
        const float end = 0.80f + 0.2f * rng.nextFloat() - 0.12f * std::abs (o);
        const float ink = 0.72f + 0.28f * rng.nextFloat();
        float skip = 0.0f;
        for (int s = 0; s < steps; ++s)
        {
            const float t = (float) s / steps;
            if (t > end) break;
            if (skip > 0.0f) { skip -= 1.0f / steps; continue; }
            if (t > 0.55f && rng.nextFloat() < 0.004f * (t - 0.5f) * 10.0f) { skip = 0.01f + 0.03f * rng.nextFloat(); continue; }
            // толщина: тонкое начало, тяжёлая середина, сухой конец
            const float thick = 10.0f + 46.0f * std::pow (std::sin (juce::MathConstants<float>::pi * juce::jmin (1.0f, t * 1.15f + 0.05f)), 0.6f);
            const float a = a0 + sweep * t;
            const float r = R + o * thick;
            const float fade = juce::jlimit (0.0f, 1.0f, (end - t) * 12.0f);
            g.setColour (grey (ink * (0.55f + 0.45f * fade)));
            const float d = 2.2f;
            g.fillEllipse (c.x + r * std::cos (a) - d * 0.5f, c.y + r * std::sin (a) - d * 0.5f, d, d);
        }
    }
    return img;
}

juce::Image wordmark()
{
    auto img = blank();
    juce::Graphics g (img);
    const auto sz = SumiLookAndFeel::pixelTextSize ("Sumi", 1);
    const int cell = juce::jmax (1, juce::jmin (880 / juce::jmax (1, sz.x), 400 / juce::jmax (1, sz.y)));
    SumiLookAndFeel::drawPixelText (g, "Sumi", { 0, 0, kW, kH }, cell, juce::Colours::white);
    return img;
}

// Горы в тумане: дальние хребты бледнее, туман снизу каждого хребта
juce::Image mountains()
{
    juce::Image img (juce::Image::RGB, kW, kH, true);
    juce::Random rng (7);
    constexpr int layers = 4;
    float ridge[layers][kW];
    for (int l = 0; l < layers; ++l)
    {
        float ph[5];
        for (auto& p : ph) p = rng.nextFloat() * juce::MathConstants<float>::twoPi;
        const float base = (float) kH * (0.30f + 0.15f * (float) l), amp = (float) kH * (0.13f - 0.015f * (float) l);
        for (int x = 0; x < kW; ++x)
        {
            const float u = (float) x / kW * juce::MathConstants<float>::twoPi;
            const float n = std::sin (1.3f * u + ph[0]) + 0.5f * std::sin (2.7f * u + ph[1]) + 0.25f * std::sin (5.3f * u + ph[2])
                          + 0.12f * std::sin (11.0f * u + ph[3]) + 0.06f * std::sin (23.0f * u + ph[4]);
            ridge[l][x] = base - amp * n;
        }
    }
    juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
    for (int x = 0; x < kW; ++x)
        for (int y = 0; y < kH; ++y)
        {
            float v = 0.0f;
            for (int l = 0; l < layers; ++l)
            {
                const float d = (float) y - ridge[l][x];
                if (d < 0.0f) continue;
                const float b = 0.4f + 0.6f * (float) l / (layers - 1);
                v = juce::jmax (v, b * std::exp (-d / (38.0f + 14.0f * (float) l)));
                if (d < 2.5f) v = juce::jmax (v, b);   // чёткая линия хребта
            }
            // солнце
            const float sx = (float) x - 790.0f, sy = (float) y - 92.0f;
            if (sx * sx + sy * sy < 34.0f * 34.0f) v = juce::jmax (v, 0.9f);
            bd.setPixelColour (x, y, grey (v));
        }
    return img;
}

juce::Image rain()
{
    auto img = blank();
    juce::Graphics g (img);
    juce::Random rng (11);
    for (int i = 0; i < 260; ++i)
    {
        const float x = rng.nextFloat() * kW, y = rng.nextFloat() * kH;
        const float len = 18.0f + 46.0f * rng.nextFloat();
        g.setColour (grey (0.45f + 0.55f * rng.nextFloat()));
        g.drawLine (x, y, x + len * 0.28f, y + len, 2.0f + rng.nextFloat() * 1.5f);
    }
    return img;
}

juce::Image waves()
{
    auto img = blank();
    juce::Graphics g (img);
    for (int i = 0; i < 5; ++i)
    {
        juce::Path p;
        const float y0 = (float) kH * (0.16f + 0.17f * (float) i), amp = 22.0f + 6.0f * (float) i;
        for (int x = 0; x <= kW; x += 4)
        {
            const float y = y0 + amp * std::sin ((float) x / kW * juce::MathConstants<float>::twoPi * (1.5f + 0.75f * (float) i) + (float) i);
            if (x == 0) p.startNewSubPath (0.0f, y); else p.lineTo ((float) x, y);
        }
        g.setColour (juce::Colours::white);
        g.strokePath (p, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    return img;
}

juce::Image testCard()
{
    auto img = blank();
    juce::Graphics g (img);
    g.setColour (juce::Colours::white);
    g.drawRect (0, 0, kW, kH, 8);
    for (int i = 1; i < 8; ++i) g.fillRect (i * kW / 8 - 2, 0, 4, kH);
    for (int i = 1; i < 4; ++i) g.fillRect (0, i * kH / 4 - 2, kW, 4);
    g.drawLine (0.0f, 0.0f, (float) kW, (float) kH, 6.0f);
    g.drawLine (0.0f, (float) kH, (float) kW, 0.0f, 6.0f);
    g.drawEllipse (juce::Rectangle<float> (360.0f, 360.0f).withCentre ({ kW * 0.5f, kH * 0.5f }), 8.0f);
    // шкала серого снизу слева: от тихого к громкому
    for (int i = 0; i < 8; ++i)
    {
        g.setColour (grey ((float) (i + 1) / 8.0f));
        g.fillRect (16 + i * 40, kH - 70, 36, 50);
    }
    return img;
}

juce::Image bamboo()
{
    auto img = blank();
    juce::Graphics g (img);
    juce::Random rng (3);
    const float xs[] = { 250.0f, 520.0f, 770.0f }, ws[] = { 24.0f, 32.0f, 20.0f };
    for (int s = 0; s < 3; ++s)
    {
        float y = (float) kH;
        const float tilt = (s - 1) * 0.04f;
        while (y > -20.0f)
        {
            const float seg = 84.0f + 40.0f * rng.nextFloat();
            const float x = xs[s] + tilt * ((float) kH - y);
            g.setColour (grey (0.8f));
            g.fillRect (x - ws[s] * 0.5f, y - seg + 5.0f, ws[s], seg - 10.0f);
            g.setColour (grey (1.0f));   // узел
            g.fillRect (x - ws[s] * 0.62f, y - 4.0f, ws[s] * 1.24f, 3.0f);
            // листья у некоторых узлов
            if (rng.nextFloat() < 0.45f)
            {
                const float dir = rng.nextBool() ? 1.0f : -1.0f;
                for (int l = 0; l < 3; ++l)
                {
                    const float ang = dir * (0.35f + 0.35f * (float) l + 0.2f * rng.nextFloat());
                    const float len = 90.0f + 50.0f * rng.nextFloat(), wid = 13.0f + 5.0f * rng.nextFloat();
                    juce::Path leaf;
                    leaf.startNewSubPath (0.0f, 0.0f);
                    leaf.quadraticTo (len * 0.45f, -wid, len, 0.0f);
                    leaf.quadraticTo (len * 0.45f, wid * 0.4f, 0.0f, 0.0f);
                    leaf.closeSubPath();
                    leaf.applyTransform (juce::AffineTransform::rotation (dir > 0 ? -ang * 0.6f : juce::MathConstants<float>::pi + ang * 0.6f)
                                             .translated (x + dir * ws[s] * 0.5f, y - 6.0f));
                    g.setColour (grey (0.95f));
                    g.fillPath (leaf);
                }
            }
            y -= seg;
        }
    }
    return img;
}
} // namespace

juce::StringArray factoryIds() { return { "enso", "wordmark", "mountains", "bamboo", "rain", "waves", "testcard" }; }

juce::String factoryName (const juce::String& id)
{
    if (id == "enso") return juce::String::fromUTF8 ("Ens\xc5\x8d");
    if (id == "wordmark") return "Sumi";
    if (id == "mountains") return "Mountains";
    if (id == "bamboo") return "Bamboo";
    if (id == "rain") return "Rain";
    if (id == "waves") return "Waves";
    if (id == "testcard") return "Test Card";
    return id;
}

juce::Image makeFactory (const juce::String& id)
{
    if (id == "enso") return enso();
    if (id == "wordmark") return wordmark();
    if (id == "mountains") return mountains();
    if (id == "bamboo") return bamboo();
    if (id == "rain") return rain();
    if (id == "waves") return waves();
    if (id == "testcard") return testCard();
    return {};
}

juce::Image makeText (const juce::String& text)
{
    const auto t = text.trim().substring (0, 60);
    if (t.isEmpty()) return {};
    float size = 190.0f;
    auto f = SumiLookAndFeel::font (size, true);
    float tw = juce::GlyphArrangement::getStringWidth (f, t);
    if (tw > 940.0f) { size *= 940.0f / tw; f = SumiLookAndFeel::font (size, true); tw = juce::GlyphArrangement::getStringWidth (f, t); }
    const int w = juce::jlimit (256, 1024, (int) std::ceil (tw) + 80);
    auto img = blank (w, 256);
    juce::Graphics g (img);
    g.setColour (juce::Colours::white);
    g.setFont (f);
    g.drawText (t, juce::Rectangle<int> (0, 0, w, 256), juce::Justification::centred, false);
    return img;
}

juce::Image fit (const juce::Image& src)
{
    if (! src.isValid()) return {};
    int w = src.getWidth(), h = src.getHeight();
    const double k = juce::jmin (1.0, (double) Picture::kMaxW / w, (double) Picture::kMaxH / h);
    w = juce::jmax (1, (int) std::round (w * k));
    h = juce::jmax (1, (int) std::round (h * k));
    juce::Image out (juce::Image::RGB, w, h, true);
    juce::Graphics g (out);
    g.fillAll (juce::Colours::black);   // прозрачное — чёрное
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (src, 0, 0, w, h, 0, 0, src.getWidth(), src.getHeight());
    return out;
}

juce::String encode (const juce::Image& img)
{
    if (! img.isValid()) return {};
    juce::MemoryOutputStream mo;
    juce::PNGImageFormat png;
    if (! png.writeImageToStream (img, mo)) return {};
    return juce::Base64::toBase64 (mo.getData(), mo.getDataSize());
}

juce::Image decode (const juce::String& b64)
{
    if (b64.isEmpty()) return {};
    juce::MemoryOutputStream mo;
    if (! juce::Base64::convertFromBase64 (mo, b64)) return {};
    return juce::ImageFileFormat::loadFrom (mo.getData(), mo.getDataSize());
}

juce::Image loadFile (const juce::File& f)
{
    return juce::ImageFileFormat::loadFrom (f);
}

bool isImageFile (const juce::String& path)
{
    const auto ext = juce::File (path).getFileExtension().toLowerCase();
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".gif";
}
} // namespace Pictures
} // namespace sumi
