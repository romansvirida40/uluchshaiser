#include "PluginEditor.h"

using namespace juce;
using namespace ulz;
using namespace ulz::ui;
namespace pid = ulz::params::id;

static constexpr int designW = 1280, designH = 720;

// ============================================================================ look and feel
UluchshaizerLookAndFeel::UluchshaizerLookAndFeel()
{
    setColour (PopupMenu::backgroundColourId, Colour (0xf5141119));
    setColour (PopupMenu::textColourId, colours::text);
    setColour (PopupMenu::highlightedBackgroundColourId, colours::accent.withAlpha (0.55f));
    setColour (PopupMenu::highlightedTextColourId, Colours::white);
    setColour (PopupMenu::headerTextColourId, colours::violet);
    setColour (AlertWindow::backgroundColourId, Colour (0xff141119));
    setColour (AlertWindow::textColourId, colours::text);
    setColour (AlertWindow::outlineColourId, colours::accent.withAlpha (0.4f));
    setColour (TextEditor::backgroundColourId, Colour (0xff0d0b11));
    setColour (TextEditor::textColourId, colours::text);
    setColour (TextEditor::outlineColourId, Colours::white.withAlpha (0.1f));
    setColour (TextEditor::focusedOutlineColourId, colours::accent);
    setColour (TextButton::buttonColourId, Colour (0xff221c2c));
    setColour (TextButton::textColourOffId, colours::text);
    setColour (TooltipWindow::textColourId, colours::text);
}

void UluchshaizerLookAndFeel::drawPopupMenuBackground (Graphics& g, int w, int h)
{
    g.fillAll (findColour (PopupMenu::backgroundColourId));
    g.setColour (Colours::white.withAlpha (0.08f));
    g.drawRect (0, 0, w, h);
}

Rectangle<int> UluchshaizerLookAndFeel::getTooltipBounds (const String& tipText, Point<int> screenPos, Rectangle<int> parentArea)
{
    const auto f = Fonts::regular (13.0f);
    const int w = jmin (320, (int) GlyphArrangement::getStringWidth (f, tipText) + 24);
    const int lines = 1 + (int) (GlyphArrangement::getStringWidth (f, tipText) / 296.0f);
    const int h = 10 + lines * 17;
    return Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                           screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6, w, h)
             .constrainedWithin (parentArea);
}

void UluchshaizerLookAndFeel::drawTooltip (Graphics& g, const String& text, int w, int h)
{
    auto b = Rectangle<float> (0, 0, (float) w, (float) h);
    g.setColour (Colour (0xf2161219));
    g.fillRoundedRectangle (b, 6.0f);
    g.setColour (colours::accent.withAlpha (0.45f));
    g.drawRoundedRectangle (b.reduced (0.5f), 6.0f, 1.0f);
    g.setColour (colours::text);
    g.setFont (Fonts::regular (13.0f));
    g.drawFittedText (text, b.reduced (12, 5).toNearestInt(), Justification::centredLeft, 6);
}

// ============================================================================ editor
std::array<RangedAudioParameter*, 12> UluchshaizerEditor::noteParams()
{
    std::array<RangedAudioParameter*, 12> a {};
    for (int i = 0; i < 12; ++i) a[(size_t) i] = processor.apvts.getParameter (pid::note (i));
    return a;
}

static String pctText (float v) { return String (roundToInt (v)) + "%"; }

UluchshaizerEditor::UluchshaizerEditor (UluchshaizerProcessor& p)
    : AudioProcessorEditor (p), processor (p),
      lowLatency (prm (pid::lowLatency)),
      sphere (prm (pid::retuneSpeed)),
      speedValue (prm (pid::retuneSpeed)),
      sustain  (prm (pid::sustain),  "Sustain",   ParamKnob::Style::LabelBelow, false, pctText),
      amount   (prm (pid::amount),   "Amount",    ParamKnob::Style::LabelBelow, false, pctText),
      flexTune (prm (pid::flexTune), "Flex Tune", ParamKnob::Style::LabelBelow, false, pctText),
      volume   (prm (pid::volume),   "Volume",    ParamKnob::Style::LabelBelow, false, pctText),
      scaleSelector (prm (pid::scale)),
      noteRow (prm (pid::key), noteParams()),
      inputType (prm (pid::inputType)),
      inputTypeList (inputType),
      vocal (p.getVisualState()),
      pitch   (prm (pid::pitch),   "Pitch",   ParamKnob::Style::LabelAbove, false, [] (float v) { return String (v, 1) + " st"; }),
      formant (prm (pid::formant), "Formant", ParamKnob::Style::LabelAbove, false, [] (float v) { return String (v, 1) + "%"; }),
      doubleMix (prm (pid::doubleMix), "Double Dry/Wet", ParamKnob::Style::Bare, false, pctText),
      widthMeter (prm (pid::doublesWidth)),
      piano (noteParams())
{
    const int savedW = jlimit (800, 1920, (int) processor.apvts.state.getProperty ("uiWidth", designW));
    reducedMotion() = systemPrefersReducedMotion();
    setLookAndFeel (&laf);

    addAndMakeVisible (canvas);
    canvas.addAndMakeVisible (background);
    for (auto* panel : { &leftPanel, &rightPanel, &dwPanel, &widthPanel, &pianoPanel })
        canvas.addAndMakeVisible (panel);
    canvas.addAndMakeVisible (chevron);

    // ---------------- left panel
    title.painter = [] (Graphics& g, Rectangle<float> r)
    {
        GlyphArrangement ga;
        ga.addLineOfText (Fonts::semibold (25.0f), "Uluchshaizer", r.getX(), r.getCentreY() + 9.0f);
        Path path; ga.createPath (path);
        g.setGradientFill (ColourGradient (Colour (0xffc08cff), r.getX(), r.getY(), Colour (0xff9a5cf0), r.getX() + 170, r.getBottom(), false));
        g.fillPath (path);
    };
    retuneLabel.painter = [] (Graphics& g, Rectangle<float> r)
    {
        g.setColour (colours::text); g.setFont (Fonts::regular (16.0f));
        g.drawText ("Retune Speed", r, Justification::centredLeft);
    };
    for (Component* c : std::initializer_list<Component*> { &title, &lowLatency, &retuneLabel, &sphere, &speedValue, &sustain, &amount, &flexTune, &volume })
        leftPanel.addAndMakeVisible (c);

    // ---------------- right panel
    for (Component* c : std::initializer_list<Component*> { &presetBar, &pianoButton, &minimiseButton, &closeButton, &scaleSelector, &noteRow,
                                                             &inputType, &vocal, &pitch, &formant, &inputTypeList })
        rightPanel.addAndMakeVisible (c);
    inputTypeList.setVisible (false);

    // ---------------- bottom
    dwLabel.painter = [] (Graphics& g, Rectangle<float> r)
    {
        g.setColour (colours::text); g.setFont (Fonts::regular (16.0f));
        g.drawText ("Double", r.withTrimmedBottom (r.getHeight() * 0.5f), Justification::bottomLeft);
        g.drawText ("Dry/Wet", r.withTrimmedTop (r.getHeight() * 0.5f), Justification::topLeft);
    };
    dwValue.painter = [this] (Graphics& g, Rectangle<float> r)
    {
        g.setColour (colours::text); g.setFont (Fonts::regular (20.0f));
        g.drawText (doubleMix.getValueText(), r, Justification::centredLeft);
    };
    doubleMix.onValueChanged = [this] (float) { dwValue.repaint(); };
    for (Component* c : std::initializer_list<Component*> { &dwLabel, &doubleMix, &dwValue }) dwPanel.addAndMakeVisible (c);
    widthPanel.addAndMakeVisible (widthMeter);
    pianoPanel.addAndMakeVisible (piano);

    // ---------------- tooltips (describe the real DSP behaviour)
    sphere.setTooltip ("Retune Speed: how fast pitch correction is applied. Drag up/down (Shift = fine), double-click to reset.");
    sustain.setTooltip ("Sustain: slows correction on held notes so long notes keep natural movement; short notes stay tight.");
    amount.setTooltip ("Amount: how much of the pitch correction is applied.");
    flexTune.setTooltip ("Flex Tune: higher values only correct notes the singer is already close to; bends and ornaments pass through.");
    volume.setTooltip ("Volume: output gain (80% = 0 dB). A soft safety limiter prevents clipping.");
    pitch.setTooltip ("Pitch: manual pitch shift in semitones (formants preserved).");
    formant.setTooltip ("Formant: moves the vocal timbre up or down without changing pitch.");
    doubleMix.setTooltip ("Double Dry/Wet: balance between the lead and the generated doubles (50% = both at full level).");
    lowLatency.setTooltip ("Low Latency: halves the processing delay (shorter analysis). Slightly more artifacts on very low notes.");
    pianoButton.setTooltip ("Piano Mode");
    chevron.setTooltip ("Piano Mode");

    pianoButton.onClick = [this] { setPianoOpen (! pianoOpen); };
    chevron.onClick = [this] { setPianoOpen (! pianoOpen); };
    minimiseButton.onClick = [this]
    {
        if (JUCEApplicationBase::isStandaloneApp()) { if (auto* peer = getPeer()) peer->setMinimised (true); }
        else { setPianoOpen (false); inputType.setOpen (false); }   // inside a DAW: collapse to the compact view
    };
    closeButton.onClick = [this]
    {
        if (JUCEApplicationBase::isStandaloneApp()) { if (auto* app = JUCEApplicationBase::getInstance()) app->systemRequestedQuit(); }
        else { setPianoOpen (false); inputType.setOpen (false); }
    };
    minimiseButton.setTooltip (JUCEApplicationBase::isStandaloneApp() ? "Minimise" : "Collapse panels");
    closeButton.setTooltip (JUCEApplicationBase::isStandaloneApp() ? "Quit" : "Close expanded panels");

    presetBar.onStep = [this] (int d) { processor.presets.step (d); };
    presetBar.onMenu = [this] { showPresetMenu(); };

    animated = { &background, &lowLatency, &sphere, &sustain, &amount, &flexTune, &volume, &pianoButton, &minimiseButton, &closeButton,
                 &scaleSelector, &noteRow, &inputType, &inputTypeList, &vocal, &pitch, &formant, &doubleMix, &widthMeter, &piano, &chevron };

    pianoAnim.duration = 0.45f;
    pianoAnim.set (0.0f, false);   // startup state: compact, piano closed, dropdown closed
    canvas.addMouseListener (this, true);

    // window size: resizable with fixed aspect ratio, remembered per instance
    setResizable (true, true);
    setResizeLimits (800, 450, 1920, 1080);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) designW / designH);
    setSize (savedW, roundToInt (savedW * (double) designH / designW));

    layoutForPiano (0.0f);
    lastFrameMs = Time::getMillisecondCounterHiRes();
    vblank = std::make_unique<VBlankAttachment> (this, [this] { onFrame(); });
}

UluchshaizerEditor::~UluchshaizerEditor()
{
    vblank.reset();
    canvas.removeMouseListener (this);
    setLookAndFeel (nullptr);
}

void UluchshaizerEditor::resized()
{
    const float s = (float) getWidth() / (float) designW;
    canvas.setBounds (0, 0, designW, designH);
    canvas.setTransform (AffineTransform::scale (s));
    processor.apvts.state.setProperty ("uiWidth", getWidth(), nullptr);
}

void UluchshaizerEditor::setPianoOpen (bool open)
{
    pianoOpen = open;
    pianoAnim.set (open ? 1.0f : 0.0f);
    pianoButton.setActive (open);
    chevron.setActive (open);
    if (open) inputType.setOpen (false);
}

static Rectangle<int> lerpRect (Rectangle<int> a, Rectangle<int> b, float t)
{
    auto l = [t] (int x, int y) { return roundToInt ((float) x + ((float) y - (float) x) * t); };
    return { l (a.getX(), b.getX()), l (a.getY(), b.getY()), l (a.getWidth(), b.getWidth()), l (a.getHeight(), b.getHeight()) };
}

void UluchshaizerEditor::layoutForPiano (float t)
{
    // coordinated transition: panels slide up together, Width fades out, keyboard rises in
    background.setBounds (0, 0, designW, designH);
    // piano-mode rectangles follow the reference artwork; Doubles Width stays visible
    leftPanel.setBounds   (lerpRect ({ 22, 72, 496, 440 },  { 10, 8, 506, 445 }, t));
    rightPanel.setBounds  (lerpRect ({ 522, 72, 736, 440 }, { 522, 8, 748, 445 }, t));
    dwPanel.setBounds     (lerpRect ({ 24, 531, 494, 77 },  { 18, 465, 434, 76 }, t));
    widthPanel.setBounds  (lerpRect ({ 532, 531, 726, 77 }, { 466, 465, 796, 76 }, t));
    pianoPanel.setBounds  (lerpRect ({ 14, 610, 1252, 150 }, { 14, 556, 1252, 156 }, t));
    chevron.setBounds     (lerpRect ({ 19, 619, 26, 26 },   { 19, 660, 26, 26 }, t));
    widthMeter.setBounds (widthPanel.getLocalBounds());
    piano.setBounds (pianoPanel.getLocalBounds());

    pianoPanel.setAlpha (t);
    pianoPanel.setVisible (t > 0.001f);
    chevron.setAlpha (1.0f - t);
    chevron.setVisible (t < 0.999f);

    if (lastLayoutAmount >= 0.0f) return;   // children only need laying out once
    lastLayoutAmount = t;

    // left panel children (panel-relative)
    title.setBounds (12, 14, 250, 36);
    lowLatency.setBounds (326, 16, 156, 36);
    retuneLabel.setBounds (12, 58, 200, 24);
    sphere.setBounds (170, 60, 196, 196);
    speedValue.setBounds (208, 262, 120, 30);
    const int knobX[4] = { 58, 186, 313, 435 };
    ParamKnob* knobs[4] = { &sustain, &amount, &flexTune, &volume };
    for (int i = 0; i < 4; ++i) knobs[i]->setBounds (knobX[i] - 55, 290, 110, 136);

    // right panel children
    presetBar.setBounds (330, 13, 230, 34);
    pianoButton.setBounds (573, 10, 58, 44);
    minimiseButton.setBounds (656, 18, 30, 30);
    closeButton.setBounds (698, 18, 30, 30);
    scaleSelector.setBounds (19, 53, 360, 32);
    noteRow.setBounds (21, 94, 554, 44);
    inputType.setBounds (19, 153, 290, 34);
    inputTypeList.setBounds (19 + 116, 153 + 36, 290 - 116, InputTypeList::fullHeight() + 12);
    vocal.setBounds (10, 233, 590, 194);
    pitch.setBounds (627, 85, 100, 160);
    formant.setBounds (627, 229, 100, 160);

    // bottom panels
    dwLabel.setBounds (21, 10, 110, 57);
    doubleMix.setBounds (182 - 40, 38 - 40, 80, 80);
    dwValue.setBounds (240, 20, 90, 38);
}

void UluchshaizerEditor::mouseDown (const MouseEvent& e)
{
    // click anywhere outside the open dropdown closes it
    if (inputType.isOpen() && e.eventComponent != &inputTypeList && e.eventComponent != &inputType)
        inputType.setOpen (false);
}

void UluchshaizerEditor::onFrame()
{
    const double now = Time::getMillisecondCounterHiRes();
    const float dt = (float) jlimit (0.0, 0.1, (now - lastFrameMs) * 0.001);
    lastFrameMs = now;

    if (pianoAnim.tick (dt) || ! approximatelyEqual (pianoAnim.value, pianoOpen ? 1.0f : 0.0f) || lastLayoutAmount < 0.0f)
        layoutForPiano (easeInOutCubic (pianoAnim.value));

    // mirror processor state into the views
    const auto mask = processor.getNoteMask();
    const int root = processor.getRoot();
    const auto scale = processor.getScaleType();
    const bool flats = keyUsesFlats (root, scale);
    auto& vs = processor.getVisualState();
    const bool voiced = vs.voiced.load();
    const int detectedNote = voiced ? (int) std::lround (vs.detectedMidi.load()) : -1;
    noteRow.setState (mask, flats);
    vocal.setSpelling (flats);
    piano.setState (mask, root, detectedNote, voiced);
    presetBar.setName (processor.presets.getCurrentName());

    for (auto* a : animated) a->tick (dt);
}

void UluchshaizerEditor::showPresetMenu()
{
    PopupMenu m;
    m.setLookAndFeel (&laf);
    m.addSectionHeader ("Factory");
    const auto factory = processor.presets.getFactoryNames();
    for (int i = 0; i < factory.size(); ++i)
        m.addItem (1 + i, factory[i], true, factory[i] == processor.presets.getCurrentName());
    const auto user = processor.presets.getUserPresets();
    if (! user.isEmpty())
    {
        m.addSectionHeader ("User");
        for (int i = 0; i < user.size(); ++i)
            m.addItem (1000 + i, user[i].getFileNameWithoutExtension(), true, user[i].getFileNameWithoutExtension() == processor.presets.getCurrentName());
    }
    m.addSeparator();
    m.addItem (5000, "Save preset...");
    m.addItem (5001, "Open preset folder");

    SafePointer<UluchshaizerEditor> safe (this);
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&presetBar), [safe, user] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& pm = safe->processor.presets;
        if (r >= 1 && r < 1000) pm.loadFactory (r - 1);
        else if (r >= 1000 && r < 5000) pm.loadUser (user[r - 1000]);
        else if (r == 5001) { PresetManager::getUserPresetFolder().createDirectory(); PresetManager::getUserPresetFolder().startAsProcess(); }
        else if (r == 5000)
        {
            auto* w = new AlertWindow ("Save preset", "Name of the new preset:", MessageBoxIconType::NoIcon, safe.getComponent());
            w->setLookAndFeel (&safe->laf);
            w->addTextEditor ("name", pm.getCurrentName());
            w->addButton ("Save", 1, KeyPress (KeyPress::returnKey));
            w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
            w->enterModalState (true, ModalCallbackFunction::create ([safe, w] (int result)
            {
                if (safe != nullptr && result == 1)
                    safe->processor.presets.saveUser (w->getTextEditorContents ("name"));
            }), true);
        }
    });
}
