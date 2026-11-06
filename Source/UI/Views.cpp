#include "Views.h"
#include "../PluginProcessor.h"

namespace sumi
{
using LnF = SumiLookAndFeel;

namespace
{
const char* kTabNames[] = { "Canvas", "Live" };

juce::String shortHz (double f)
{
    if (f >= 1000.0) { const double k = f / 1000.0; return (std::abs (k - std::round (k)) < 0.01 ? juce::String ((int) std::round (k)) : juce::String (k, 1)) + "k"; }
    return juce::String ((int) std::round (f));
}
} // namespace

// ===========================================================================
MenuPill::MenuPill (const juce::String& cap) : caption (cap)
{
    setWantsKeyboardFocus (true);
    setTitle (cap);
}

void MenuPill::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    const float rad = juce::jmin (r.getHeight() * 0.5f, 8.0f);
    g.setColour (Colours::well);
    g.fillRoundedRectangle (r, rad);
    g.setColour (Colours::outline);
    g.drawRoundedRectangle (r, rad, 1.0f);
    auto t = getLocalBounds().reduced (12, 0);
    g.setFont (LnF::font (LnF::Type::callout));
    g.setColour (Colours::ink2);
    const int cw = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), caption);
    g.drawText (caption, t.removeFromLeft (cw + 6), juce::Justification::centredLeft);
    g.setColour (Colours::ink);
    g.setFont (LnF::font (LnF::Type::callout, true));
    g.drawFittedText (text, t.withTrimmedRight (14), juce::Justification::centredLeft, 1, 0.85f);
    juce::Path arrow;
    const float ax = (float) getWidth() - 16.0f, ay = (float) getHeight() * 0.5f;
    arrow.startNewSubPath (ax - 3, ay - 1.5f); arrow.lineTo (ax, ay + 1.5f); arrow.lineTo (ax + 3, ay - 1.5f);
    g.setColour (Colours::ink2);
    g.strokePath (arrow, juce::PathStrokeType (1.3f));
    if (hasKeyboardFocus (false)) LnF::drawFocusRing (g, r, rad);
}

bool MenuPill::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::returnKey || k == juce::KeyPress::spaceKey) { if (onOpen) onOpen(); return true; }
    return false;
}

// ===========================================================================
CanvasView::CanvasView (SumiProcessor& p) : proc (p)
{
    setTitle ("Picture view");
    setDescription ("The picture as it will appear on a spectrogram. Left and right arrows switch Canvas and Live.");
    setWantsKeyboardFocus (true);
}

juce::Colour CanvasView::background() const { return currentTheme() == ThemeId::Rubber ? Colours::well : Colours::surface; }

CanvasView::State CanvasView::readState() const
{
    State s;
    auto& a = proc.apvts;
    s.pic = proc.getLatestPicture();
    const auto ep = proc.snapshotParams();
    s.map = ep.map;
    s.look = ep.look;
    s.sampleRate = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
    const double nyq = s.sampleRate * 0.5;
    s.view = s.map.scale == 1 ? FreqMap (1, 20.0, nyq) : FreqMap (s.map.scale, 0.0, nyq);
    s.lengthBars = lengthBars ((int) a.getRawParameterValue (ParamId::length)->load());
    s.startBar = a.getRawParameterValue (ParamId::start)->load();
    s.loop = a.getRawParameterValue (ParamId::loop)->load() > 0.5f;
    s.order = proc.desiredOrder();
    return s;
}

juce::String CanvasView::keyOf (const State& s) const
{
    return juce::String ((juce::pointer_sized_int) s.pic.get()) + "|" + juce::String (s.map.scale) + "|" + juce::String (s.map.lo) + "|" + juce::String (s.map.hi)
         + "|" + juce::String (s.view.hi) + "|" + juce::String (s.look.gamma) + "|" + juce::String (s.look.rangeDb) + "|" + juce::String ((int) s.look.invert)
         + "|" + plot.toString() + "|" + juce::String ((int) currentTheme());
}

juce::Rectangle<int> CanvasView::cellArea() const
{
    return plot.withSize (plot.getWidth() / kCell * kCell, plot.getHeight() / kCell * kCell);
}

void CanvasView::resized()
{
    const float h = 28.0f;
    const float tabW = 2 * 72.0f;
    tabsArea = { (float) getWidth() - 12.0f - tabW, (float) getHeight() - 12.0f - h, tabW, h };
    legendArea = { (float) getWidth() - 12.0f - 236.0f, 12.0f, 236.0f, h };
    plot = getLocalBounds().reduced (16, 0).withTrimmedTop (48).withTrimmedBottom (48);
    plot = cellArea();
    canvasKey.clear();
    clearLive();
}

void CanvasView::clearLive()
{
    live = juce::Image (juce::Image::ARGB, juce::jmax (1, plot.getWidth()), juce::jmax (1, plot.getHeight()), true);
    lastCol = -1;
}

// ---------------------------------------------------------------------------
void CanvasView::buildCanvas (const State& s)
{
    canvas = juce::Image (juce::Image::ARGB, juce::jmax (1, plot.getWidth()), juce::jmax (1, plot.getHeight()), true);
    if (s.pic == nullptr || s.pic->empty()) return;
    const auto& pic = *s.pic;
    const int cols = plot.getWidth() / kCell, rows = plot.getHeight() / kCell;
    juce::RectangleList<int> buckets[10];
    for (int r = 0; r < rows; ++r)
    {
        double f0 = s.view.freq ((double) r / rows), f1 = s.view.freq ((double) (r + 1) / rows);
        if (f1 <= s.map.lo || f0 >= s.map.hi) continue;
        f0 = juce::jmax (f0, s.map.lo);
        f1 = juce::jmin (f1, s.map.hi);
        const double y0 = (1.0 - s.map.pos (f1)) * pic.h, y1 = (1.0 - s.map.pos (f0)) * pic.h;
        const int cy = (rows - 1 - r) * kCell;
        for (int c = 0; c < cols; ++c)
        {
            const float v = pic.boxMean ((double) c / cols * pic.w, (double) (c + 1) / cols * pic.w, y0, y1);
            const float l = s.look.level (v);
            if (l < 0.0f) continue;
            buckets[juce::jlimit (0, 9, (int) (l * 10.0f))].addWithoutMerging ({ c * kCell, cy, kCell, kCell });
        }
    }
    juce::Graphics g (canvas);
    for (int b = 0; b < 10; ++b)
    {
        if (buckets[b].isEmpty()) continue;
        g.setColour (rampBucketColour (Colours::carrierRamp, b));
        g.fillRectList (buckets[b]);
    }
}

void CanvasView::push (const float* x, int n)
{
    const int mask = (int) ring.size() - 1;
    for (int i = 0; i < n; ++i) ring[(size_t) ((written + i) & mask)] = x[i];
    written += n;
}

void CanvasView::liveColumn (const State& s, std::vector<int8_t>& out)
{
    const int rows = plot.getHeight() / kCell;
    out.assign ((size_t) rows, (int8_t) -1);
    const int order = juce::jlimit (10, 13, s.order);
    if (fft == nullptr || fftOrder != order)
    {
        fft = std::make_unique<juce::dsp::FFT> (order);
        fftOrder = order;
    }
    const int N = 1 << order, mask = (int) ring.size() - 1;
    work.assign ((size_t) N * 2, 0.0f);
    for (int i = 0; i < N; ++i)
    {
        const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) N);
        work[(size_t) i] = ring[(size_t) ((written - N + i) & mask)] * w;
    }
    fft->performFrequencyOnlyForwardTransform (work.data(), true);
    const double df = s.sampleRate / N;
    auto dbAt = [&] (int k) { return juce::Decibels::gainToDecibels (work[(size_t) juce::jlimit (0, N / 2, k)] / (float) (N / 4), -140.0f); };

    std::vector<float> col ((size_t) rows, -140.0f);
    float colMax = -140.0f;
    for (int r = 0; r < rows; ++r)
    {
        const double b0 = s.view.freq ((double) r / rows) / df, b1 = s.view.freq ((double) (r + 1) / rows) / df;
        float db;
        if (b1 - b0 < 1.0)
        {
            const double bc = 0.5 * (b0 + b1);
            const int k = (int) bc;
            const float fr = (float) (bc - k);
            db = dbAt (k) * (1.0f - fr) + dbAt (k + 1) * fr;
        }
        else
        {
            db = -140.0f;
            for (int k = (int) std::ceil (b0); k <= (int) b1; ++k) db = juce::jmax (db, dbAt (k));
        }
        col[(size_t) r] = db;
        colMax = juce::jmax (colMax, db);
    }
    // опорный уровень: пик последних колонок, спад ~3 дБ/с
    refDb = juce::jmax (colMax, refDb - 0.1f);
    const float range = s.look.rangeDb;
    for (int r = 0; r < rows; ++r)
    {
        const float t = (col[(size_t) r] - (refDb - range)) / range;
        if (t > 0.0f) out[(size_t) r] = (int8_t) juce::jlimit (0, 9, (int) (t * 10.0f));
    }
}

void CanvasView::tick()
{
    const auto s = readState();
    phase = proc.uiPhase.load();
    const auto key = keyOf (s);
    if (key != canvasKey)
    {
        buildCanvas (s);
        canvasKey = key;
    }
    const auto lk = plot.toString() + "|" + juce::String (s.view.scale) + "|" + juce::String (s.view.hi);
    if (lk != liveKey) { clearLive(); liveKey = lk; }

    const int cols = plot.getWidth() / kCell, rows = plot.getHeight() / kCell;
    if (phase >= 0.0f && cols > 0 && rows > 0)
    {
        const int col = juce::jlimit (0, cols - 1, (int) (phase * (float) cols));
        if (col != lastCol)
        {
            std::vector<int8_t> vals;
            liveColumn (s, vals);
            std::vector<int> todo;
            const int dist = lastCol < 0 ? cols : (col - lastCol + cols) % cols;
            if (dist > cols / 2) todo.push_back (col);
            else for (int i = 1; i <= dist; ++i) todo.push_back ((lastCol + i) % cols);
            juce::Graphics g (live);
            for (int c : todo)
            {
                live.clear ({ c * kCell, 0, kCell, rows * kCell });
                for (int r = 0; r < rows; ++r)
                    if (vals[(size_t) r] >= 0)
                    {
                        g.setColour (rampBucketColour (Colours::carrierRamp, vals[(size_t) r]));
                        g.fillRect (c * kCell, (rows - 1 - r) * kCell, kCell, kCell);
                    }
            }
            lastCol = col;
        }
    }
    else lastCol = -1;
    shown = s;
    repaint();
}

void CanvasView::setMode (Mode m)
{
    mode = m;
    repaint();
}

// ---------------------------------------------------------------------------
void CanvasView::paintGrid (juce::Graphics& g, const State& s)
{
    const auto pr = plot.toFloat();
    const float dashes[] = { 3.0f, 4.0f };
    auto yOf = [&] (double f) { return pr.getBottom() - (float) s.view.pos (f) * pr.getHeight(); };

    // частоты
    std::vector<double> marks;
    if (s.view.scale == 1) marks = { 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
    else if (s.view.scale == 2) marks = { 250, 500, 1000, 2000, 4000, 8000, 16000 };
    else marks = { 5000, 10000, 15000, 20000 };
    g.setFont (LnF::mono (LnF::Type::footnote));
    float lastY = 1e9f;
    for (double f : marks)
    {
        if (f >= s.view.hi) continue;
        const float y = yOf (f);
        if (lastY - y < 16.0f) continue;
        lastY = y;
        g.setColour (Colours::ink2.withAlpha (0.3f));
        g.drawDashedLine ({ pr.getX(), y, pr.getRight(), y }, dashes, 2, 1.0f);
        g.setColour (Colours::ink2);
        g.drawText (shortHz (f), juce::Rectangle<float> (pr.getX() + 4.0f, y - 14.0f, 40.0f, 13.0f), juce::Justification::centredLeft);
    }

    // полоса картинки Low–High
    for (double f : { s.map.lo, s.map.hi })
    {
        if (f <= s.view.lo || f >= s.view.hi) continue;
        const float y = yOf (f);
        g.setColour (Colours::ink.withAlpha (0.45f));
        g.fillRect (pr.getX(), y - 0.5f, pr.getWidth(), 1.0f);
        g.setColour (Colours::ink);
        g.drawText (formatHz ((float) f), juce::Rectangle<float> (pr.getRight() - 70.0f, f == s.map.hi ? y - 15.0f : y + 2.0f, 66.0f, 13.0f),
                    juce::Justification::centredRight);
    }

    // такты
    const double bars = s.lengthBars;
    const int ticks = bars < 1.0 ? 2 : (int) bars;   // полтакта — две доли
    const int every = juce::jmax (1, (int) std::ceil (ticks * 30.0 / pr.getWidth()));
    g.setFont (LnF::mono (LnF::Type::footnote));
    for (int i = 0; i <= ticks; ++i)
    {
        const float x = pr.getX() + pr.getWidth() * (float) i / (float) ticks;
        g.setColour (Colours::ink2.withAlpha (0.5f));
        g.fillRect (juce::jmin (x, pr.getRight() - 1.0f), pr.getY(), 1.0f, pr.getHeight() + 4.0f);
        if (i == ticks || i % every != 0) continue;
        const juce::String label = bars < 1.0 ? juce::String ((int) s.startBar) + "." + juce::String (i + 1) : juce::String ((int) s.startBar + i);
        g.setColour (Colours::ink2);
        g.drawText (label, juce::Rectangle<float> (x + 3.0f, pr.getBottom() + 4.0f, 40.0f, 16.0f), juce::Justification::centredLeft);
    }
}

void CanvasView::paintOverlay (juce::Graphics& g, const State& s)
{
    const auto plateFill = currentTheme() == ThemeId::Rubber ? Colours::surface : Colours::well;
    {
        juce::String info;
        if (s.pic != nullptr && ! s.pic->empty())
            info << s.pic->name << midDot() << s.pic->w << juce::String::fromUTF8 (" \xc3\x97 ") << s.pic->h << midDot();
        info << juce::StringArray { "Lin", "Log", "Mel" }[s.map.scale] << " " << formatHz ((float) s.map.lo) << juce::String::fromUTF8 (" \xe2\x80\x93 ") << formatHz ((float) s.map.hi)
             << midDot() << "Bar " << (int) s.startBar << ", " << lengthNames()[juce::jlimit (0, 7, (int) proc.apvts.getRawParameterValue (ParamId::length)->load())]
             << (s.loop ? ", loop" : "");
        g.setFont (LnF::mono (LnF::Type::footnote));
        g.setColour (Colours::ink2);
        g.drawFittedText (info, juce::Rectangle<float> (16.0f, 12.0f, legendArea.getX() - 28.0f, 28.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.9f);
    }

    LnF::drawPlate (g, legendArea, 8.0f, plateFill);
    {
        auto r = legendArea.reduced (12.0f, 0.0f);
        g.setFont (LnF::mono (LnF::Type::footnote));
        g.setColour (Colours::ink);
        g.drawText (minus() + juce::String (juce::roundToInt (s.look.rangeDb)) + " dB", r.removeFromLeft (46.0f), juce::Justification::centredLeft);
        g.drawText ("0 dB", r.removeFromRight (30.0f), juce::Justification::centredRight);
        auto bar = r.reduced (6.0f, 0.0f).withSizeKeepingCentre (r.getWidth() - 12.0f, 6.0f);
        const float seg = bar.getWidth() / 10.0f;
        for (int b = 0; b < 10; ++b)
        {
            g.setColour (rampBucketColour (Colours::carrierRamp, b));
            g.fillRect (juce::Rectangle<float> (bar.getX() + seg * (float) b + 0.5f, bar.getY(), seg - 1.0f, bar.getHeight()));
        }
    }

    LnF::drawPlate (g, tabsArea, tabsArea.getHeight() * 0.5f, plateFill);
    const float w = (tabsArea.getWidth() - 4.0f) / 2.0f;
    for (int i = 0; i < 2; ++i)
    {
        auto cell = juce::Rectangle<float> (tabsArea.getX() + 2.0f + w * (float) i, tabsArea.getY() + 2.0f, w, tabsArea.getHeight() - 4.0f);
        const bool sel = (int) mode == i;
        if (sel)
        {
            g.setColour (currentTheme() == ThemeId::Rubber ? Colours::well : Colours::surface);
            g.fillRoundedRectangle (cell, cell.getHeight() * 0.5f);
            g.setColour (Colours::outline);
            g.drawRoundedRectangle (cell.reduced (0.5f), cell.getHeight() * 0.5f, 1.0f);
        }
        g.setColour (sel ? Colours::ink : Colours::ink2);
        g.setFont (LnF::font (LnF::Type::subheadline, sel));
        g.drawText (kTabNames[i], cell, juce::Justification::centred);
    }
    if (hasKeyboardFocus (false))
        LnF::drawFocusRing (g, tabsArea.expanded (2.0f), tabsArea.getHeight() * 0.5f + 2.0f);
}

void CanvasView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const float radius = 12.0f;
    g.setColour (background());
    g.fillRoundedRectangle (r, radius);
    {
        juce::Graphics::ScopedSaveState ss (g);
        juce::Path clip;
        clip.addRoundedRectangle (r.reduced (1.0f), radius - 1.0f);
        g.reduceClipRegion (clip);

        const bool hasPic = shown.pic != nullptr && ! shown.pic->empty();
        if (mode == Canvas)
            g.drawImageAt (canvas, plot.getX(), plot.getY());
        else
        {
            // под живой спектрограммой — бледный след картинки, чтобы было с чем сравнить
            g.setOpacity (0.14f);
            g.drawImageAt (canvas, plot.getX(), plot.getY());
            g.setOpacity (1.0f);
            g.drawImageAt (live, plot.getX(), plot.getY());
        }
        paintGrid (g, shown);

        if (phase >= 0.0f)
        {
            const float x = (float) plot.getX() + (float) plot.getWidth() * phase;
            g.setColour (Colours::signal);
            g.fillRect (x - 1.0f, (float) plot.getY(), 2.0f, (float) plot.getHeight());
        }

        g.setFont (LnF::font (LnF::Type::body));
        g.setColour (Colours::ink2);
        if (! hasPic)
            g.drawText ("Drop an image here or click to load one", plot, juce::Justification::centred);
        else if (mode == Live && phase < 0.0f && lastCol < 0)
            g.drawText (proc.hostTransport.load() ? "Press play: the picture is drawn under the cursor" : "Waiting for audio", plot, juce::Justification::centred);

        paintOverlay (g, shown);
    }
    g.setColour (dragOver ? Colours::signal : Colours::outline);
    g.drawRoundedRectangle (r.reduced (dragOver ? 1.0f : 0.5f), radius, dragOver ? 2.0f : 1.0f);
}

void CanvasView::mouseDown (const juce::MouseEvent& e)
{
    if (tabsArea.contains (e.position))
    {
        setMode ((Mode) juce::jlimit (0, 1, (int) ((e.position.x - tabsArea.getX()) / tabsArea.getWidth() * 2.0f)));
        return;
    }
    if (plot.contains (e.getPosition()) && (shown.pic == nullptr || shown.pic->empty()) && onEmptyClick) onEmptyClick();
}

bool CanvasView::keyPressed (const juce::KeyPress& k)
{
    if (k.getKeyCode() == juce::KeyPress::leftKey)  { setMode (Canvas); return true; }
    if (k.getKeyCode() == juce::KeyPress::rightKey) { setMode (Live); return true; }
    return false;
}

bool CanvasView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files) if (Pictures::isImageFile (f)) return true;
    return false;
}

void CanvasView::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    for (auto& f : files)
        if (Pictures::isImageFile (f))
        {
            if (onFileDropped) onFileDropped (juce::File (f));
            break;
        }
    repaint();
}
} // namespace sumi
