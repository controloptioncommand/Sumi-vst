#pragma once
#include "PluginProcessor.h"
#include "UI/LookAndFeel.h"
#include "UI/Widgets.h"
#include "UI/Views.h"
#include "UI/About.h"

namespace sumi
{
class SumiEditor : public juce::AudioProcessorEditor, private juce::Timer, private juce::DarkModeSettingListener
{
public:
    explicit SumiEditor (SumiProcessor&);
    ~SumiEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kBaseW = 1120, kBaseH = 760;

    // для тестов
    void setThemeForTest (ThemeId id) { setThemeMode (id == ThemeId::Rubber ? ThemeMode::Rubber : ThemeMode::Oxygen); }
    AboutPage& getAboutForTest() { return about; }
    void showAboutForTest (bool show, bool game) { showAbout (show); if (show) about.showGame (game); }
    void setViewForTest (int m) { view.setMode ((CanvasView::Mode) m); }
    void pumpForTest() { timerCallback(); }
    CanvasView& getViewForTest() { return view; }

private:
    void timerCallback() override;
    void darkModeSettingChanged() override;
    void showAbout (bool show);
    void showPresetMenu();
    void showSettingsMenu();
    void showPictureMenu();
    void openImageChooser();
    void askText();
    void savePreset();
    void setThemeMode (ThemeMode m);
    void applyThemeMode();
    void updateModeControls();

    SumiProcessor& proc;
    SumiLookAndFeel lnf;
    ThemeMode themeMode = ThemeMode::System;
    juce::Component content;   // базовый размер, масштабируется трансформом

    // тулбар
    LogoButton logo;
    IconButton prevPreset { IconButton::Prev, "Previous preset" }, nextPreset { IconButton::Next, "Next preset" },
               settingsButton { IconButton::Settings, "Settings" };
    TextLinkButton presetName;
    Segmented modeSwitch;
    OutMeter outMeter;
    juce::Rectangle<int> toolbarArea;

    CanvasView view;

    // панель
    juce::Rectangle<int> panelArea, leftHead, rightHead, footArea;
    int dividerX = 0;
    MenuPill picturePill { "Picture" };
    Segmented scaleSwitch, blendSwitch;
    ParamKnob low { "Low" }, high { "High" }, contrast { "Contrast" }, range { "Range" }, start { "Start" };
    PillChoice resolution, length;
    ParamKnob level { "Level" }, tone { "Tone" }, width { "Width" }, mix { "Mix" }, flatten { "Flatten" };
    ParamToggle invert { "Invert" }, loop { "Loop" };
    juce::Label status;
    juce::Rectangle<int> knobRow;
    int shownMode = -1, shownBlend = -1;
    std::unique_ptr<juce::ParameterAttachment> modeAtt, scaleAtt, blendAtt;

    AboutPage about;
    std::unique_ptr<juce::AlertWindow> dialog;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SumiEditor)
};
} // namespace sumi
