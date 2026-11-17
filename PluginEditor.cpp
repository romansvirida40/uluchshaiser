#include "PluginEditor.h"

namespace pal
{
    const juce::Colour iron      (0xff1c1510);
    const juce::Colour ironLight (0xff33261a);
    const juce::Colour brassHi   (0xffe6c068);
    const juce::Colour brass     (0xffb5832f);
    const juce::Colour brassLo   (0xff6e4a17);
    const juce::Colour copper    (0xffb8642f);
    const juce::Colour amber     (0xffffb347);
    const juce::Colour verdi     (0xff5fb8a0);
    const juce::Colour parchment (0xfff1e2b5);
    const juce::Colour ink       (0xff2a1d14);
}

static const char* noteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

static juce::Font serif (float h, bool bold = true)
{
    return juce::Font (juce::FontOptions ("Georgia", h, bold ? juce::Font::bold : juce::Font::plain));
}

//==============================================================================
static void drawRivet (juce::Graphics& g, juce::Point<float> c, float r = 4.0f)
{
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillEllipse (c.x - r + 1, c.y - r + 1.5f, 2 * r, 2 * r);
    g.setGradientFill (juce::ColourGradient (pal::brassHi, c.x - r * 0.5f, c.y - r * 0.6f,
                                             pal::brassLo, c.x + r, c.y + r, false));
    g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.fillEllipse (c.x - r * 0.45f, c.y - r * 0.55f, r * 0.5f, r * 0.4f);
}

static void drawGear (juce::Graphics& g, juce::Point<float> c, float r, int teeth, float rot, juce::Colour col)
{
    juce::Path p;
    const int n = teeth * 4;
    for (int i = 0; i < n; ++i)
    {
        const float a  = rot + juce::MathConstants<float>::twoPi * (float) i / (float) n;
        const float rr = (i % 4 < 2) ? r : r * 0.82f;
        const auto pt = c.getPointOnCircumference (rr, a);
        if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
    }
    p.closeSubPath();

    g.setGradientFill (juce::ColourGradient (col.brighter (0.3f), c.x - r, c.y - r,
                                             col.darker (0.5f),  c.x + r, c.y + r, false));
    g.fillPath (p);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.strokePath (p, juce::PathStrokeType (1.5f));

    // spokes + hub
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    for (int i = 0; i < 6; ++i)
    {
        const float a = rot + juce::MathConstants<float>::twoPi * (float) i / 6.0f;
        g.drawLine (juce::Line<float> (c.getPointOnCircumference (r * 0.25f, a),
                                       c.getPointOnCircumference (r * 0.7f, a)), r * 0.12f);
    }
    g.drawEllipse (c.x - r * 0.72f, c.y - r * 0.72f, r * 1.44f, r * 1.44f, r * 0.05f);
    g.fillEllipse (c.x - r * 0.18f, c.y - r * 0.18f, r * 0.36f, r * 0.36f);
}

//==============================================================================
SteamLookAndFeel::SteamLookAndFeel()
{
    setColour (juce::ComboBox::backgroundColourId, pal::ink);
    setColour (juce::ComboBox::textColourId, pal::amber);
    setColour (juce::ComboBox::outlineColourId, pal::brass);
    setColour (juce::ComboBox::arrowColourId, pal::brassHi);
    setColour (juce::PopupMenu::backgroundColourId, pal::ink);
    setColour (juce::PopupMenu::textColourId, pal::amber);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, pal::copper);
    setColour (juce::PopupMenu::highlightedTextColourId, pal::parchment);
    setColour (juce::Slider::textBoxTextColourId, pal::amber);
    setColour (juce::Slider::textBoxBackgroundColourId, pal::ink);
    setColour (juce::Slider::textBoxOutlineColourId, pal::brassLo);
}

void SteamLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                                         float pos, float a0, float a1, juce::Slider&)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
    const auto c = bounds.getCentre();
    const float R = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;

    // scale ticks
    for (int i = 0; i <= 10; ++i)
    {
        const float a = a0 + (a1 - a0) * (float) i / 10.0f;
        g.setColour (pal::brassHi.withAlpha (0.85f));
        g.drawLine (juce::Line<float> (c.getPointOnCircumference (R * 0.88f, a),
                                       c.getPointOnCircumference (R * (i % 5 == 0 ? 1.0f : 0.95f), a)),
                    i % 5 == 0 ? 2.0f : 1.2f);
    }

    // glowing value arc
    juce::Path arc;
    arc.addCentredArc (c.x, c.y, R * 0.92f, R * 0.92f, 0.0f, a0, a0 + (a1 - a0) * pos, true);
    g.setColour (pal::amber.withAlpha (0.85f));
    g.strokePath (arc, juce::PathStrokeType (2.5f));

    // knob body
    const float k = R * 0.74f;
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillEllipse (c.x - k + 2, c.y - k + 4, 2 * k, 2 * k);
    g.setGradientFill (juce::ColourGradient (pal::brassHi, c.x - k * 0.6f, c.y - k * 0.8f,
                                             pal::brassLo, c.x + k * 0.7f, c.y + k * 0.9f, false));
    g.fillEllipse (c.x - k, c.y - k, 2 * k, 2 * k);

    g.setColour (pal::ink.withAlpha (0.6f));
    for (int i = 0; i < 36; ++i)
    {
        const float a = juce::MathConstants<float>::twoPi * (float) i / 36.0f;
        g.drawLine (juce::Line<float> (c.getPointOnCircumference (k * 0.92f, a),
                                       c.getPointOnCircumference (k, a)), 1.0f);
    }

    // inner dish
    const float ki = k * 0.68f;
    g.setGradientFill (juce::ColourGradient (pal::brassLo, c.x - ki * 0.5f, c.y - ki * 0.6f,
                                             pal::brassHi.brighter (0.2f), c.x + ki * 0.6f, c.y + ki * 0.7f, false));
    g.fillEllipse (c.x - ki, c.y - ki, 2 * ki, 2 * ki);
    g.setColour (pal::ink);
    g.drawEllipse (c.x - ki, c.y - ki, 2 * ki, 2 * ki, 1.5f);

    // pointer
    const float ang = a0 + (a1 - a0) * pos;
    g.setColour (pal::ink);
    g.drawLine (juce::Line<float> (c.getPointOnCircumference (ki * 0.2f, ang),
                                   c.getPointOnCircumference (ki * 0.95f, ang)), 3.5f);
    g.setColour (pal::amber);
    const auto tip = c.getPointOnCircumference (ki * 0.8f, ang);
    g.fillEllipse (tip.x - 2.5f, tip.y - 2.5f, 5.0f, 5.0f);
}

//==============================================================================
ULUCHSHAISEREditor::ULUCHSHAISEREditor (ULUCHSHAISERProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);

    keyBox.addItemList (proc.apvts.getParameter ("key")->getAllValueStrings(), 1);
    scaleBox.addItemList (proc.apvts.getParameter ("scale")->getAllValueStrings(), 1);
    addAndMakeVisible (keyBox);
    addAndMakeVisible (scaleBox);
    keyAtt   = std::make_unique<ComboAtt> (proc.apvts, "key", keyBox);
    scaleAtt = std::make_unique<ComboAtt> (proc.apvts, "scale", scaleBox);

    const char* ids[7]      = { "speed", "amount", "stab", "dmix", "ddetune", "dwidth", "out" };
    const char* suffixes[7] = { " ms", " %", " %", " %", " ct", " %", " dB" };
    const int   decimals[7] = { 0, 0, 0, 0, 1, 0, 1 };

    for (int i = 0; i < 7; ++i)
    {
        auto& s = *sliders[(size_t) i];
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 18);
        s.setTextValueSuffix (suffixes[i]);
        s.setNumDecimalPlacesToDisplay (decimals[i]);
        addAndMakeVisible (s);
        sliderAtts.push_back (std::make_unique<SliderAtt> (proc.apvts, ids[i], s));
    }

    setSize (860, 550);
    startTimerHz (30);
}

ULUCHSHAISEREditor::~ULUCHSHAISEREditor()
{
    setLookAndFeel (nullptr);
}

void ULUCHSHAISEREditor::resized()
{
    keyBox.setBounds (300, 105, 140, 30);
    scaleBox.setBounds (460, 105, 210, 30);
    for (int i = 0; i < 7; ++i)
        sliders[(size_t) i]->setBounds (40 + i * 111 + 5, 428, 100, 112);
}

void ULUCHSHAISEREditor::timerCallback()
{
    voiced     = proc.uiVoiced.load();
    targetMidi = proc.uiTarget.load();
    const float want = voiced ? juce::jlimit (-50.0f, 50.0f, proc.uiCents.load()) : 0.0f;
    needle += 0.3f * (want - needle);
    gearAngle += voiced ? 0.025f : 0.003f;
    repaint();
}

//==============================================================================
void ULUCHSHAISEREditor::paint (juce::Graphics& g)
{
    const float W = (float) getWidth(), H = (float) getHeight();

    // iron plate
    g.setGradientFill (juce::ColourGradient (pal::ironLight, 0, 0, pal::iron, 0, H, false));
    g.fillAll();

    // background gears (decor; they spin while a voice is detected)
    drawGear (g, { 50.0f, 70.0f },   80.0f, 14,  gearAngle,        pal::copper.withAlpha (0.22f));
    drawGear (g, { 160.0f, 10.0f },  42.0f, 8,  -gearAngle * 1.75f, pal::brass.withAlpha (0.20f));
    drawGear (g, { 815.0f, 100.0f }, 90.0f, 17, -gearAngle * 0.8f, pal::copper.withAlpha (0.22f));
    drawGear (g, { 700.0f, 20.0f },  46.0f, 9,   gearAngle * 1.6f, pal::brass.withAlpha (0.20f));

    // brass frame + rivets
    g.setGradientFill (juce::ColourGradient (pal::brassHi, 0, 0, pal::brassLo, W, H, false));
    g.drawRoundedRectangle (juce::Rectangle<float> (0, 0, W, H).reduced (5.0f), 12.0f, 9.0f);
    g.setColour (pal::ink);
    g.drawRoundedRectangle (juce::Rectangle<float> (0, 0, W, H).reduced (12.0f), 8.0f, 1.5f);
    for (float px = 30.0f; px < W - 20.0f; px += 70.0f) { drawRivet (g, { px, 9.0f }); drawRivet (g, { px, H - 9.0f }); }
    for (float py = 30.0f; py < H - 20.0f; py += 70.0f) { drawRivet (g, { 9.0f, py }); drawRivet (g, { W - 9.0f, py }); }

    // title plaque
    const juce::Rectangle<float> plaque (215.0f, 18.0f, 430.0f, 60.0f);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (plaque.translated (2, 4), 10.0f);
    g.setGradientFill (juce::ColourGradient (pal::brassHi, plaque.getX(), plaque.getY(),
                                             pal::brassLo, plaque.getRight(), plaque.getBottom(), false));
    g.fillRoundedRectangle (plaque, 10.0f);
    g.setColour (pal::ink);
    g.drawRoundedRectangle (plaque.reduced (3.0f), 8.0f, 1.2f);
    drawRivet (g, { plaque.getX() + 14, plaque.getCentreY() }, 3.5f);
    drawRivet (g, { plaque.getRight() - 14, plaque.getCentreY() }, 3.5f);

    g.setFont (serif (31.0f));
    g.setColour (pal::parchment.withAlpha (0.6f));
    g.drawText ("ULUCHSHAISER", plaque.translated (1, 1).withHeight (42.0f), juce::Justification::centred);
    g.setColour (pal::ink);
    g.drawText ("ULUCHSHAISER", plaque.withHeight (42.0f), juce::Justification::centred);
    g.setFont (serif (11.5f, false));
    g.drawText ("PNEUMATIC PITCH RECTIFICATION ENGINE", plaque.withTrimmedTop (40.0f), juce::Justification::centredTop);

    // ---- cents gauge -------------------------------------------------------
    const juce::Point<float> gc (150.0f, 205.0f);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillEllipse (gc.x - 106, gc.y - 102, 212, 212);
    g.setGradientFill (juce::ColourGradient (pal::brassHi, gc.x - 100, gc.y - 100, pal::brassLo, gc.x + 100, gc.y + 100, false));
    g.fillEllipse (gc.x - 106, gc.y - 106, 212, 212);
    g.setColour (pal::ink);
    g.fillEllipse (gc.x - 97, gc.y - 97, 194, 194);
    g.setGradientFill (juce::ColourGradient (pal::parchment.brighter (0.2f), gc.x, gc.y - 30, pal::parchment.darker (0.35f), gc.x, gc.y + 95, true));
    g.fillEllipse (gc.x - 93, gc.y - 93, 186, 186);

    const float range = 1.05f;
    for (int c = -50; c <= 50; c += 10)
    {
        const float a = (float) c / 50.0f * range;
        const bool major = (c % 50 == 0) || c == 0;
        g.setColour (c == 0 ? pal::copper : pal::ink);
        g.drawLine (juce::Line<float> (gc.getPointOnCircumference (major ? 62.0f : 68.0f, a),
                                       gc.getPointOnCircumference (80.0f, a)), major ? 3.0f : 1.5f);
    }
    g.setColour (pal::ink);
    g.setFont (serif (11.0f));
    g.drawText ("-50", juce::Rectangle<float> (gc.x - 82, gc.y - 6, 34, 14), juce::Justification::centred);
    g.drawText ("+50", juce::Rectangle<float> (gc.x + 48, gc.y - 6, 34, 14), juce::Justification::centred);
    g.drawText ("CENTS", juce::Rectangle<float> (gc.x - 40, gc.y + 24, 80, 14), juce::Justification::centred);
    g.setFont (serif (10.0f, false));
    g.drawText ("FLAT",  juce::Rectangle<float> (gc.x - 80, gc.y + 55, 50, 12), juce::Justification::centred);
    g.drawText ("SHARP", juce::Rectangle<float> (gc.x + 30, gc.y + 55, 50, 12), juce::Justification::centred);

    const float na = needle / 50.0f * range;
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawLine (juce::Line<float> (gc.translated (2, 3), gc.getPointOnCircumference (80.0f, na).translated (2, 3)), 3.0f);
    g.setColour (voiced ? pal::copper.darker (0.3f) : pal::ink.withAlpha (0.6f));
    g.drawLine (juce::Line<float> (gc.getPointOnCircumference (-18.0f, na), gc.getPointOnCircumference (82.0f, na)), 3.0f);
    g.setGradientFill (juce::ColourGradient (pal::brassHi, gc.x - 8, gc.y - 8, pal::brassLo, gc.x + 8, gc.y + 8, false));
    g.fillEllipse (gc.x - 9, gc.y - 9, 18, 18);
    g.setColour (pal::ink);
    g.fillEllipse (gc.x - 3, gc.y - 3, 6, 6);
    g.setColour (juce::Colours::white.withAlpha (0.12f));   // glass glint
    g.fillEllipse (gc.x - 70, gc.y - 88, 140, 60);

    // nixie-style note readout
    const juce::Rectangle<float> nixie (70.0f, 335.0f, 160.0f, 50.0f);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (nixie.translated (2, 3), 8.0f);
    g.setColour (pal::ink);
    g.fillRoundedRectangle (nixie, 8.0f);
    g.setColour (pal::brass);
    g.drawRoundedRectangle (nixie, 8.0f, 2.5f);
    juce::String noteText = "--";
    if (voiced)
    {
        const int m = (int) std::lround (targetMidi);
        noteText = juce::String (noteNames[((m % 12) + 12) % 12]) + juce::String (m / 12 - 1);
    }
    g.setFont (juce::Font (juce::FontOptions ("Courier New", 32.0f, juce::Font::bold)));
    g.setColour (pal::amber.withAlpha (0.25f));
    for (int dx = -1; dx <= 1; ++dx) for (int dy = -1; dy <= 1; ++dy)
        g.drawText (noteText, nixie.translated ((float) dx * 1.5f, (float) dy * 1.5f), juce::Justification::centred);
    g.setColour (pal::amber);
    g.drawText (noteText, nixie, juce::Justification::centred);

    // ---- key / scale captions ---------------------------------------------
    g.setColour (pal::brassHi);
    g.setFont (serif (12.0f));
    g.drawText ("KEY",   juce::Rectangle<float> (300, 86, 140, 16), juce::Justification::centredLeft);
    g.drawText ("SCALE", juce::Rectangle<float> (460, 86, 210, 16), juce::Justification::centredLeft);

    // ---- heat glasses ------------------------------------------------------
    const int keyIdx  = juce::jlimit (0, 11, (int) proc.apvts.getRawParameterValue ("key")->load());
    const int scaleMk = ULUCHSHAISERProcessor::getScaleMask ((int) proc.apvts.getRawParameterValue ("scale")->load());
    const int targetPc = voiced ? (((int) std::lround (targetMidi) % 12) + 12) % 12 : -1;

    g.setColour (pal::brassHi);
    g.setFont (serif (11.0f));
    g.drawText ("NOTE HEAT GLASSES", juce::Rectangle<float> (300, 146, 300, 14), juce::Justification::centredLeft);

    const float tubeW = 36.0f, gap = 8.0f, tubeTop = 170.0f, tubeH = 110.0f;
    for (int i = 0; i < 12; ++i)
    {
        const float tx = 300.0f + (float) i * (tubeW + gap);
        const juce::Rectangle<float> tube (tx, tubeTop, tubeW, tubeH);
        const bool black = (i == 1 || i == 3 || i == 6 || i == 8 || i == 10);
        const bool inKey = (scaleMk & (1 << (((i - keyIdx) % 12 + 12) % 12))) != 0;

        g.setColour (black ? pal::iron.darker (0.4f) : pal::ink.brighter (0.15f));
        g.fillRoundedRectangle (tube, 8.0f);

        const float level = juce::jlimit (0.0f, 1.0f, proc.heat[(size_t) i].load());
        if (level > 0.01f)
        {
            const float fh = level * (tubeH - 10.0f);
            const juce::Rectangle<float> liquid (tx + 4, tubeTop + tubeH - 5 - fh, tubeW - 8, fh);
            g.setGradientFill (juce::ColourGradient (pal::amber, 0, liquid.getY(), pal::copper, 0, liquid.getBottom(), false));
            g.fillRoundedRectangle (liquid, 4.0f);
        }
        g.setColour (juce::Colours::white.withAlpha (0.14f));
        g.fillRoundedRectangle (tx + 6, tubeTop + 6, 4.0f, tubeH - 12, 2.0f);

        g.setColour (i == targetPc ? pal::verdi : pal::brass);
        g.drawRoundedRectangle (tube, 8.0f, i == targetPc ? 3.0f : 2.0f);

        // scale lamp
        g.setColour (inKey ? pal::verdi : pal::ink);
        g.fillEllipse (tx + tubeW * 0.5f - 3.5f, tubeTop - 12.0f, 7.0f, 7.0f);
        g.setColour (pal::brassLo);
        g.drawEllipse (tx + tubeW * 0.5f - 3.5f, tubeTop - 12.0f, 7.0f, 7.0f, 1.0f);

        g.setColour (pal::parchment);
        g.setFont (serif (12.0f));
        g.drawText (noteNames[i], juce::Rectangle<float> (tx, tubeTop + tubeH + 2, tubeW, 14), juce::Justification::centred);
    }

    // ---- knob group plaques -----------------------------------------------
    g.setColour (pal::brassHi);
    g.setFont (serif (12.0f));
    auto header = [&] (const juce::String& t, int fromKnob, int toKnob)
    {
        const float x0 = 40.0f + (float) fromKnob * 111.0f + 6.0f;
        const float x1 = 40.0f + (float) (toKnob + 1) * 111.0f - 6.0f;
        g.setColour (pal::brass);
        g.drawLine (x0, 400.0f, x1, 400.0f, 1.5f);
        g.setColour (pal::brassHi);
        g.drawText (t, juce::Rectangle<float> (x0, 384.0f, x1 - x0, 14.0f), juce::Justification::centred);
    };
    header ("TUNING", 0, 2);
    header ("DOUBLER", 3, 5);
    header ("OUTPUT", 6, 6);

    const char* names[7] = { "RETUNE SPEED", "AMOUNT", "STABILIZER", "MIX", "DETUNE", "WIDTH", "LEVEL" };
    g.setFont (serif (11.0f));
    g.setColour (pal::parchment);
    for (int i = 0; i < 7; ++i)
        g.drawText (names[i], juce::Rectangle<float> (40.0f + (float) i * 111.0f, 406.0f, 111.0f, 16.0f), juce::Justification::centred);
}
