#include "PluginEditor.h"

namespace sumi
{
using LnF = SumiLookAndFeel;

namespace
{
std::unique_ptr<juce::PropertiesFile> openPrefs()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "Sumi";
    o.filenameSuffix = "settings";
    o.folderName = "Sumi";
    o.osxLibrarySubFolder = "Application Support";
    return std::make_unique<juce::PropertiesFile> (o);
}

const char* modeDescription (int mode, int blend)
{
    if (mode == 0) return "Builds sound from the picture alone, the input is not used. Tone 0% is noise, 100% steady tones.";
    if (blend == 0) return "Adds the picture on top of the incoming sound. Level sets how loud the picture is.";
    return "Carves the incoming sound with the picture: it plays only where the picture is bright. Flatten evens the input out.";
}
} // namespace

SumiEditor::SumiEditor (SumiProcessor& p)
    : AudioProcessorEditor (&p), proc (p), outMeter (p.apvts), view (p),
      resolution (p.apvts, ParamId::resolution, "FFT"), length (p.apvts, ParamId::length, "Length")
{
    Accessibility::refresh();
    auto prefs = openPrefs();
    themeMode = (ThemeMode) juce::jlimit (0, 2, prefs->getIntValue ("themeMode", 0));
    applyThemeMode();
    setLookAndFeel (&lnf);
    juce::Desktop::getInstance().addDarkModeSettingListener (this);
    setTitle ("Sumi");
    addAndMakeVisible (content);

    // тулбар
    for (juce::Component* c : std::initializer_list<juce::Component*> { &logo, &prevPreset, &presetName, &nextPreset, &modeSwitch, &outMeter, &settingsButton })
        content.addAndMakeVisible (c);
    logo.onClick = [this] { showAbout (true); };
    presetName.setTitle ("Preset");
    presetName.onClick = [this] { showPresetMenu(); };
    prevPreset.onClick = [this] { proc.presets.step (-1); };
    nextPreset.onClick = [this] { proc.presets.step (1); };
    settingsButton.onClick = [this] { showSettingsMenu(); };

    modeSwitch.setTitle ("Mode");
    modeSwitch.setItems ({ { "Paint", 0 }, { "Imprint", 1 } });
    modeAtt = std::make_unique<juce::ParameterAttachment> (*p.apvts.getParameter (ParamId::mode),
        [this] (float v) { modeSwitch.setSelectedValue ((int) v); }, nullptr);
    modeSwitch.onChange = [this] (int v) { modeAtt->setValueAsCompleteGesture ((float) v); };
    modeAtt->sendInitialUpdate();

    content.addAndMakeVisible (view);
    view.onEmptyClick = [this] { openImageChooser(); };
    view.onFileDropped = [this] (const juce::File& f)
    {
        if (! proc.loadPictureFile (f))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Sumi", "Could not read this image.");
    };

    // панель: картинка
    picturePill.onOpen = [this] { showPictureMenu(); };
    scaleSwitch.setTitle ("Frequency scale");
    scaleSwitch.setItems ({ { "Lin", 0 }, { "Log", 1 }, { "Mel", 2 } });
    scaleAtt = std::make_unique<juce::ParameterAttachment> (*p.apvts.getParameter (ParamId::scale),
        [this] (float v) { scaleSwitch.setSelectedValue ((int) v); }, nullptr);
    scaleSwitch.onChange = [this] (int v) { scaleAtt->setValueAsCompleteGesture ((float) v); };
    scaleAtt->sendInitialUpdate();

    blendSwitch.setTitle ("Imprint blend");
    blendSwitch.setItems ({ { "Add", 0 }, { "Mask", 1 } });
    blendAtt = std::make_unique<juce::ParameterAttachment> (*p.apvts.getParameter (ParamId::blend),
        [this] (float v) { blendSwitch.setSelectedValue ((int) v); }, nullptr);
    blendSwitch.onChange = [this] (int v) { blendAtt->setValueAsCompleteGesture ((float) v); };
    blendAtt->sendInitialUpdate();

    low.attach (p.apvts, ParamId::low);
    high.attach (p.apvts, ParamId::high);
    contrast.attach (p.apvts, ParamId::contrast);
    range.attach (p.apvts, ParamId::range);
    start.attach (p.apvts, ParamId::start);
    level.attach (p.apvts, ParamId::level);
    tone.attach (p.apvts, ParamId::tone);
    width.attach (p.apvts, ParamId::width);
    mix.attach (p.apvts, ParamId::mix);
    flatten.attach (p.apvts, ParamId::flatten);
    invert.attach (p.apvts, ParamId::invert);
    loop.attach (p.apvts, ParamId::loop);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &picturePill, &scaleSwitch, &low, &high, &contrast, &range, &start,
                                                                        &blendSwitch, &resolution, &level, &tone, &width, &mix, &flatten,
                                                                        &invert, &loop, &length, &status })
        content.addAndMakeVisible (c);
    status.setFont (LnF::mono (LnF::Type::footnote));
    status.getProperties().set ("dim", true);
    status.setJustificationType (juce::Justification::centredRight);

    content.addChildComponent (about);
    about.onClose = [this] { showAbout (false); };
    disableClickFocus (*this);

    setResizable (true, true);
    setResizeLimits (kBaseW / 2, kBaseH / 2, kBaseW * 2, kBaseH * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) kBaseW / kBaseH);
    const int scale = juce::jlimit (50, 200, prefs->getIntValue ("scale", 100));
    setSize (kBaseW * scale / 100, kBaseH * scale / 100);
    updateModeControls();
    timerCallback();
    startTimerHz (30);
}

SumiEditor::~SumiEditor()
{
    stopTimer();
    juce::Desktop::getInstance().removeDarkModeSettingListener (this);
    setLookAndFeel (nullptr);
}

// ---------------------------------------------------------------------------
void SumiEditor::applyThemeMode()
{
    ThemeId id = ThemeId::Oxygen;
    if (themeMode == ThemeMode::Rubber) id = ThemeId::Rubber;
    else if (themeMode == ThemeMode::System) id = juce::Desktop::getInstance().isDarkModeActive() ? ThemeId::Rubber : ThemeId::Oxygen;
    applyTheme (id);
    lnf.refreshColours();
    sendLookAndFeelChange();
    repaintTree (*this);
}

void SumiEditor::setThemeMode (ThemeMode m)
{
    themeMode = m;
    if (auto prefs = openPrefs()) { prefs->setValue ("themeMode", (int) m); prefs->saveIfNeeded(); }
    applyThemeMode();
}

void SumiEditor::darkModeSettingChanged()
{
    if (themeMode == ThemeMode::System) applyThemeMode();
}

void SumiEditor::showAbout (bool show)
{
    if (show == about.isVisible()) return;
    if (show)
    {
        about.setBounds (0, 0, kBaseW, kBaseH);
        about.setBackLogoBounds (logo.getBounds());
        about.setVisible (true);
        about.toFront (true);
        about.grabKeyboardFocus();
    }
    else
    {
        about.showGame (false);
        about.setVisible (false);
    }
}

// ---------------------------------------------------------------------------
void SumiEditor::showPresetMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("Factory");
    const auto& fp = factoryPresets();
    const auto cur = proc.presets.getCurrentName();
    for (int i = 0; i < (int) fp.size(); ++i) m.addItem (1 + i, fp[(size_t) i].name, true, cur == fp[(size_t) i].name);
    const auto users = proc.presets.userPresets();
    if (! users.isEmpty())
    {
        m.addSeparator();
        m.addSectionHeader ("User");
        for (int i = 0; i < users.size(); ++i)
            m.addItem (1000 + i, users[i].getFileNameWithoutExtension(), true, cur == users[i].getFileNameWithoutExtension());
    }
    m.addSeparator();
    m.addItem (2000, "Save preset...");
    m.addItem (2001, "Show presets folder");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetName), [this, users] (int r)
    {
        if (r >= 1 && r < 1000) proc.presets.loadFactory (r - 1);
        else if (r >= 1000 && r < 2000 && r - 1000 < users.size()) proc.presets.loadUser (users[r - 1000]);
        else if (r == 2000) savePreset();
        else if (r == 2001)
        {
            auto dir = PresetManager::userFolder();
            dir.createDirectory();
            dir.startAsProcess();
        }
    });
}

void SumiEditor::savePreset()
{
    dialog = std::make_unique<juce::AlertWindow> ("Save preset", "Settings and the picture are saved together.", juce::MessageBoxIconType::NoIcon);
    dialog->addTextEditor ("name", proc.presets.getCurrentName(), "Name");
    dialog->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    dialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    dialog->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int r)
    {
        if (r == 1 && dialog != nullptr)
            if (! proc.presets.save (dialog->getTextEditorContents ("name")))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Sumi", "Could not save preset.");
        dialog.reset();
    }), false);
}

void SumiEditor::openImageChooser()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a picture", juce::File::getSpecialLocation (juce::File::userPicturesDirectory), "*.png;*.jpg;*.jpeg;*.gif");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f.existsAsFile() && ! proc.loadPictureFile (f))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Sumi", "Could not read this image.");
    });
}

void SumiEditor::askText()
{
    const auto src = proc.getPictureSource();
    dialog = std::make_unique<juce::AlertWindow> ("Text picture", "The text is drawn white on black and becomes the picture.", juce::MessageBoxIconType::NoIcon);
    dialog->addTextEditor ("text", src.startsWith ("text:") ? src.fromFirstOccurrenceOf (":", false, false) : juce::String ("SUMI"), "Text");
    dialog->addButton ("Use", 1, juce::KeyPress (juce::KeyPress::returnKey));
    dialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    dialog->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int r)
    {
        if (r == 1 && dialog != nullptr)
        {
            const auto t = dialog->getTextEditorContents ("text").trim().substring (0, 60);
            if (t.isNotEmpty()) proc.setPictureSource ("text:" + t);
        }
        dialog.reset();
    }), false);
}

void SumiEditor::showPictureMenu()
{
    juce::PopupMenu m;
    m.addItem (1, "Load image...");
    m.addItem (2, "Type text...");
    m.addSeparator();
    m.addSectionHeader ("Built-in");
    const auto ids = Pictures::factoryIds();
    const auto cur = proc.getPictureSource();
    for (int i = 0; i < ids.size(); ++i) m.addItem (100 + i, Pictures::factoryName (ids[i]), true, cur == "factory:" + ids[i]);
    m.addSeparator();
    m.addItem (3, "Clear", cur.isNotEmpty());
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (picturePill), [this, ids] (int r)
    {
        if (r == 1) openImageChooser();
        else if (r == 2) askText();
        else if (r == 3) proc.clearPicture();
        else if (r >= 100 && r - 100 < ids.size()) proc.setPictureSource ("factory:" + ids[r - 100]);
    });
}

void SumiEditor::showSettingsMenu()
{
    juce::PopupMenu m, appearance, size;
    appearance.addItem (201, "Match system", true, themeMode == ThemeMode::System);
    appearance.addSeparator();
    appearance.addItem (202, "Oxygen (light)", true, themeMode == ThemeMode::Oxygen);
    appearance.addItem (203, "Rubber (dark)", true, themeMode == ThemeMode::Rubber);
    m.addSubMenu ("Appearance", appearance);
    for (int pct : { 75, 100, 125, 150 })
        size.addItem (100 + pct, juce::String (pct) + "%", true, std::abs (getWidth() - kBaseW * pct / 100) < 4);
    m.addSubMenu ("Size", size);
    m.addSeparator();
    m.addItem (3000, "Load image...");
    m.addItem (2000, "Save preset...");
    m.addSeparator();
    m.addItem (5, "About Sumi");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (settingsButton), [this] (int r)
    {
        if (r == 5) showAbout (true);
        else if (r == 2000) savePreset();
        else if (r == 3000) openImageChooser();
        else if (r == 201) setThemeMode (ThemeMode::System);
        else if (r == 202) setThemeMode (ThemeMode::Oxygen);
        else if (r == 203) setThemeMode (ThemeMode::Rubber);
        else if (r > 100 && r < 300)
        {
            const int pct = r - 100;
            if (auto prefs = openPrefs()) { prefs->setValue ("scale", pct); prefs->saveIfNeeded(); }
            setSize (kBaseW * pct / 100, kBaseH * pct / 100);
        }
    });
}

// ---------------------------------------------------------------------------
void SumiEditor::updateModeControls()
{
    const int m = (int) proc.apvts.getRawParameterValue (ParamId::mode)->load();
    const int b = (int) proc.apvts.getRawParameterValue (ParamId::blend)->load();
    if (m == shownMode && b == shownBlend) return;
    shownMode = m;
    shownBlend = b;
    const bool imprint = m == 1, add = b == 0;
    blendSwitch.setVisible (imprint);
    mix.setVisible (imprint);
    tone.setVisible (! imprint || add);
    width.setVisible (! imprint || add);
    flatten.setVisible (imprint && ! add);
    level.slider.setDescription (imprint && ! add ? "Level of the carved sound" : "Level of the picture");
    if (! imprint) layoutRow (knobRow, { &level, &tone, &width });
    else if (add) layoutRow (knobRow, { &mix, &level, &tone, &width });
    else layoutRow (knobRow, { &mix, &level, &flatten });
    repaint();
}

void SumiEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::window);
    g.addTransform (juce::AffineTransform::scale ((float) getWidth() / (float) kBaseW));

    LnF::drawPlate (g, toolbarArea.toFloat(), 12.0f);
    g.setColour (Colours::outline);
    g.drawVerticalLine (logo.getRight() + 8, (float) toolbarArea.getY() + 15, (float) toolbarArea.getBottom() - 15);

    g.setColour (Colours::surface);
    g.fillRoundedRectangle (panelArea.toFloat(), 12.0f);
    g.setColour (Colours::outline);
    g.drawRoundedRectangle (panelArea.toFloat().reduced (0.5f), 12.0f, 1.0f);
    g.drawHorizontalLine (footArea.getY(), (float) footArea.getX(), (float) footArea.getRight());
    g.drawVerticalLine (dividerX, (float) leftHead.getY(), (float) footArea.getY() - 14.0f);

    // подсказка про картинку
    g.setColour (Colours::ink2);
    g.setFont (LnF::font (LnF::Type::callout));
    g.drawFittedText ("Brightness is loudness: black is silent, white is loud. The bottom edge sits at Low, the top at High.",
                      leftHead.translated (0, 38).withHeight (32), juce::Justification::topLeft, 2, 1.0f);

    // заголовок режима
    auto ta = rightHead;
    g.setColour (Colours::ink);
    g.setFont (LnF::font (LnF::Type::title3, true));
    g.drawText (shownMode == 0 ? juce::String ("Paint") + midDot() + "Spectral synth" : juce::String ("Imprint") + midDot() + "Into your sound",
                ta.removeFromTop (20), juce::Justification::centredLeft);
    g.setColour (Colours::ink2);
    g.setFont (LnF::font (LnF::Type::callout));
    g.drawFittedText (modeDescription (shownMode, shownBlend), ta.withTrimmedTop (4), juce::Justification::topLeft, 2, 1.0f);
}

void SumiEditor::resized()
{
    content.setBounds (0, 0, kBaseW, kBaseH);
    content.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) kBaseW));
    about.setBounds (0, 0, kBaseW, kBaseH);

    auto r = juce::Rectangle<int> (0, 0, kBaseW, kBaseH).reduced (14);
    toolbarArea = r.removeFromTop (52);
    {
        auto h = toolbarArea.reduced (10, 10);
        const auto logoSize = LnF::pixelTextSize ("Sumi", 1);
        logo.setBounds (h.removeFromLeft (juce::jmax (64, logoSize.x + 8)));
        h.removeFromLeft (18);
        prevPreset.setBounds (h.removeFromLeft (32));
        presetName.setBounds (h.removeFromLeft (150));
        nextPreset.setBounds (h.removeFromLeft (32));
        settingsButton.setBounds (h.removeFromRight (32));
        h.removeFromRight (10);
        outMeter.setBounds (h.removeFromRight (140));
        modeSwitch.setBounds (toolbarArea.withSizeKeepingCentre (200, 32));
    }
    r.removeFromTop (10);
    view.setBounds (r.removeFromTop (386));
    r.removeFromTop (10);
    panelArea = r;

    auto inner = panelArea.reduced (18, 14);
    footArea = inner.removeFromBottom (44);
    auto left = inner.removeFromLeft ((int) ((float) inner.getWidth() * 0.5f));
    dividerX = left.getRight() + 18;
    inner.removeFromLeft (36);
    auto right = inner;

    leftHead = left.removeFromTop (30);
    picturePill.setBounds (leftHead.withWidth (250).withSizeKeepingCentre (250, 28));
    scaleSwitch.setBounds (leftHead.removeFromRight (168));
    left.removeFromTop (44);
    layoutRow (left, { &low, &high, &contrast, &range, &start });

    rightHead = right.removeFromTop (44);
    blendSwitch.setBounds (rightHead.getRight() - 140, rightHead.getY() - 4, 140, 28);
    right.removeFromTop (30);
    {
        auto row = right;
        auto pill = row.removeFromLeft (150);
        resolution.setBounds (pill.withSizeKeepingCentre (146, 28));
        row.removeFromLeft (8);
        knobRow = row;
        shownMode = -1;
        updateModeControls();
    }

    auto b = footArea.withTrimmedTop (10);
    invert.setBounds (b.removeFromLeft (92).withSizeKeepingCentre (92, 28));
    b.removeFromLeft (4);
    loop.setBounds (b.removeFromLeft (84).withSizeKeepingCentre (84, 28));
    b.removeFromLeft (10);
    length.setBounds (b.removeFromLeft (156).withSizeKeepingCentre (156, 28));
    status.setBounds (b.removeFromRight (300));
}

void SumiEditor::timerCallback()
{
    const int ready = proc.scopeFifo.getNumReady();
    if (ready > 0)
    {
        const auto rd = proc.scopeFifo.read (ready);
        if (! about.isVisible())
            for (auto [st, size] : { std::pair<int, int> { rd.startIndex1, rd.blockSize1 }, std::pair<int, int> { rd.startIndex2, rd.blockSize2 } })
                if (size > 0) view.push (proc.scopeBuf.data() + st, size);
    }
    if (about.isVisible()) return;

    updateModeControls();
    view.tick();
    picturePill.setText (proc.getPictureName());
    outMeter.setLevel (proc.outPeak.load());
    presetName.setButtonText (proc.presets.getCurrentName());
    presetName.setDescription ("Preset " + proc.presets.getCurrentName());
    const int n = 1 << proc.desiredOrder();
    const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
    status.setText ("FFT " + juce::String (n) + midDot() + juce::String (juce::roundToInt (n / sr * 1000.0)) + " ms latency"
                    + "   CPU " + juce::String (juce::roundToInt (proc.cpuLoad.load() * 100.0f)) + "%", juce::dontSendNotification);
}
} // namespace sumi
