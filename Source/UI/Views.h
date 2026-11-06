#pragma once
#include "Widgets.h"
#include "../Picture.h"
#include "../DSP/FreqMap.h"
#include <juce_dsp/juce_dsp.h>

namespace sumi
{
class SumiProcessor;

// Чип «Picture Ensō ⌄»: по клику — меню картинок
class MenuPill : public juce::Component
{
public:
    explicit MenuPill (const juce::String& caption);
    void setText (const juce::String& t) { if (t != text) { text = t; setDescription (caption + " " + t); repaint(); } }
    std::function<void()> onOpen;
    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent&) override { if (onOpen) onOpen(); }
    bool keyPressed (const juce::KeyPress& k) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }

private:
    juce::String caption, text;
};

// Главный вид. Canvas — картинка так, как она ляжет на спектрограмму (время × частота в выбранной шкале),
// с тактами, полосой Low–High и курсором воспроизведения. Live — спектрограмма выхода, которая
// пишется под тем же курсором: видно, как картинка проступает в звуке.
class CanvasView : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    enum Mode { Canvas = 0, Live = 1 };
    explicit CanvasView (SumiProcessor& p);

    void push (const float* x, int n);
    void tick();
    void setMode (Mode m);
    Mode getMode() const { return mode; }
    void clearLive();

    std::function<void()> onEmptyClick;
    std::function<void (const juce::File&)> onFileDropped;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    bool keyPressed (const juce::KeyPress& k) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }
    void lookAndFeelChanged() override { canvasKey.clear(); clearLive(); repaint(); }

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;

    static constexpr int kCell = 2;

private:
    struct State
    {
        std::shared_ptr<const Picture> pic;
        FreqMap map, view;
        Look look;
        double lengthBars = 4, startBar = 1;
        bool loop = true;
        int order = 12;
        double sampleRate = 48000.0;
    };
    State readState() const;
    juce::String keyOf (const State& s) const;
    void buildCanvas (const State& s);
    void liveColumn (const State& s, std::vector<int8_t>& out);
    void paintGrid (juce::Graphics& g, const State& s);
    void paintOverlay (juce::Graphics& g, const State& s);
    juce::Colour background() const;
    juce::Rectangle<int> cellArea() const;   // область картинки, кратная клетке

    SumiProcessor& proc;
    Mode mode = Canvas;
    juce::Rectangle<int> plot;
    juce::Rectangle<float> tabsArea, legendArea;
    juce::Image canvas;
    juce::String canvasKey;
    State shown;
    float phase = -1.0f;
    bool dragOver = false;

    // Live
    juce::Image live;
    juce::String liveKey;
    int lastCol = -1;
    float refDb = -60.0f;
    std::vector<float> ring = std::vector<float> (1 << 14, 0.0f);
    int64_t written = 0;
    std::unique_ptr<juce::dsp::FFT> fft;
    int fftOrder = 0;
    std::vector<float> work;
};
} // namespace sumi
