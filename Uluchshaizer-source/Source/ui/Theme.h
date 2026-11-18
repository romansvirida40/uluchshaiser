#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "BinaryData.h"

namespace ulz::ui
{
namespace colours
{
    inline const juce::Colour background   { 0xff060509 };
    inline const juce::Colour panelTop     { 0xff14111b };
    inline const juce::Colour panelBottom  { 0xff0a090e };
    inline const juce::Colour panelBorder  { 0x14ffffff };
    inline const juce::Colour control      { 0xff15121b };
    inline const juce::Colour controlLight { 0xff1f1b27 };
    inline const juce::Colour accent       { 0xff8a55e6 };   // muted purple
    inline const juce::Colour accentDeep   { 0xff6a36c9 };
    inline const juce::Colour violet       { 0xffa979f5 };
    inline const juce::Colour lavender     { 0xffd3bfff };
    inline const juce::Colour cyan         { 0xff46d2e3 };
    inline const juce::Colour text         { 0xffe8e3f1 };
    inline const juce::Colour textDim      { 0xff9a93a8 };
    inline const juce::Colour textFaint    { 0xff5d5669 };
}

/** Embedded Inter (SIL OFL 1.1) so the plugin looks identical on every OS. */
struct Fonts
{
    static juce::Typeface::Ptr regularTf()  { static auto t = juce::Typeface::createSystemTypefaceFor (BinaryData::InterRegular_ttf,  BinaryData::InterRegular_ttfSize);  return t; }
    static juce::Typeface::Ptr mediumTf()   { static auto t = juce::Typeface::createSystemTypefaceFor (BinaryData::InterMedium_ttf,   BinaryData::InterMedium_ttfSize);   return t; }
    static juce::Typeface::Ptr semiboldTf() { static auto t = juce::Typeface::createSystemTypefaceFor (BinaryData::InterSemiBold_ttf, BinaryData::InterSemiBold_ttfSize); return t; }

    static juce::Font regular (float h)  { return juce::Font (juce::FontOptions (regularTf()).withHeight (h)); }
    static juce::Font medium (float h)   { return juce::Font (juce::FontOptions (mediumTf()).withHeight (h)); }
    static juce::Font semibold (float h) { return juce::Font (juce::FontOptions (semiboldTf()).withHeight (h)); }
};

// ---------------------------------------------------------------- animation helpers
inline float easeInOutCubic (float t) { t = juce::jlimit (0.0f, 1.0f, t); return t < 0.5f ? 4 * t * t * t : 1 - std::pow (-2 * t + 2, 3.0f) / 2; }
inline float easeOutCubic (float t)   { t = juce::jlimit (0.0f, 1.0f, t); return 1 - std::pow (1 - t, 3.0f); }

/** Global switch, set from the OS "reduce motion / animations off" setting where available. */
inline bool& reducedMotion() { static bool r = false; return r; }

/** Value that moves toward a target with a fixed-duration eased transition. Driven by tick(dt). */
struct Tween
{
    float value = 0.0f, from = 0.0f, to = 0.0f, t = 1.0f, duration = 0.3f;
    bool cubicInOut = true;

    void set (float target, bool animate = true)
    {
        if (juce::approximatelyEqual (target, to) && t >= 1.0f) return;
        if (! animate || reducedMotion() || duration <= 0.0f) { value = from = to = target; t = 1.0f; return; }
        from = value; to = target; t = 0.0f;
    }
    bool tick (float dt)   // returns true while animating
    {
        if (t >= 1.0f) return false;
        t = juce::jmin (1.0f, t + dt / duration);
        const float e = cubicInOut ? easeInOutCubic (t) : easeOutCubic (t);
        value = from + (to - from) * e;
        return true;
    }
    bool isAnimating() const { return t < 1.0f; }
};

/** Exponential follower for continuous signals (glows, hover). */
struct Follower
{
    float value = 0.0f, target = 0.0f, timeConstant = 0.08f;
    bool tick (float dt)
    {
        if (std::abs (target - value) < 1e-4f) { value = target; return false; }
        if (reducedMotion()) { value = target; return true; }
        value += (target - value) * (1.0f - std::exp (-dt / timeConstant));
        return true;
    }
};

/** Components that animate implement this; the editor drives them from a single vblank callback. */
struct Animated
{
    virtual ~Animated() = default;
    virtual void tick (float dt) = 0;
};

// ---------------------------------------------------------------- drawing helpers
inline void drawGlassPanel (juce::Graphics& g, juce::Rectangle<float> r, float radius = 18.0f, float alpha = 1.0f)
{
    using namespace juce;
    // soft drop shadow
    for (int i = 4; i > 0; --i)
    {
        g.setColour (Colours::black.withAlpha (0.06f * alpha));
        g.fillRoundedRectangle (r.expanded ((float) i * 2.5f).translated (0, (float) i * 1.5f), radius + (float) i * 2.5f);
    }
    ColourGradient grad (colours::panelTop.withAlpha (0.62f * alpha), r.getX(), r.getY(),
                         colours::panelBottom.withAlpha (0.74f * alpha), r.getX(), r.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (r, radius);
    // faint inner top highlight
    ColourGradient hl (Colours::white.withAlpha (0.045f * alpha), r.getX(), r.getY(),
                       Colours::white.withAlpha (0.0f), r.getX(), r.getY() + 60.0f, false);
    g.setGradientFill (hl);
    g.fillRoundedRectangle (r, radius);
    // glass edge: brighter at the top, fading down
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.11f * alpha), r.getX(), r.getY(), Colours::white.withAlpha (0.035f * alpha), r.getX(), r.getBottom(), false));
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
}

/** Caches a glass panel as an image so repaints of animated children only cost a blit. */
struct GlassCache
{
    juce::Image img;
    juce::Rectangle<int> lastSize;
    float lastScale = 0.0f, lastAlpha = -1.0f;
    void draw (juce::Graphics& g, juce::Rectangle<float> r, float radius, float alpha = 1.0f)
    {
        const float scale = juce::jlimit (1.0f, 3.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
        const auto size = r.getSmallestIntegerContainer();
        if (! img.isValid() || size != lastSize || ! juce::approximatelyEqual (scale, lastScale) || ! juce::approximatelyEqual (alpha, lastAlpha))
        {
            const int pad = 14;
            img = juce::Image (juce::Image::ARGB, juce::roundToInt ((float) (size.getWidth() + 2 * pad) * scale),
                               juce::roundToInt ((float) (size.getHeight() + 2 * pad) * scale), true);
            juce::Graphics ig (img);
            ig.addTransform (juce::AffineTransform::scale (scale).translated (0, 0));
            ig.addTransform (juce::AffineTransform::translation ((float) pad - (float) size.getX(), (float) pad - (float) size.getY()).scaled (1.0f));
            drawGlassPanel (ig, r, radius, alpha);
            lastSize = size; lastScale = scale; lastAlpha = alpha;
        }
        const int pad = 14;
        g.drawImage (img, juce::Rectangle<float> ((float) (size.getX() - pad), (float) (size.getY() - pad),
                                                  (float) (size.getWidth() + 2 * pad), (float) (size.getHeight() + 2 * pad)));
    }
};

inline void drawControlBox (juce::Graphics& g, juce::Rectangle<float> r, float radius, float hover = 0.0f)
{
    using namespace juce;
    g.setGradientFill (ColourGradient (colours::controlLight.interpolatedWith (Colour (0xff2a2434), hover * 0.6f), r.getX(), r.getY(),
                                       colours::control, r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, radius);
    g.setColour (Colours::white.withAlpha (0.06f + 0.05f * hover));
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
}

} // namespace ulz::ui
