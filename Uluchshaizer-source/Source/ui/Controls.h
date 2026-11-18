#pragma once
// All custom Uluchshaizer controls. Every control is bound to an AudioProcessorValueTreeState
// parameter through juce::ParameterAttachment: the parameter is the only stored value, the GUI
// just mirrors it (host automation, presets and the DSP always agree with what is drawn).

#include <juce_audio_processors/juce_audio_processors.h>
#include "Theme.h"
#include "../dsp/Scales.h"
#include "../dsp/VocalProcessor.h"
#include "../Parameters.h"
#include <random>
#include <algorithm>

namespace ulz::ui
{
using namespace juce;

// ============================================================================ parameter binding
struct ParamBinding
{
    ParamBinding (RangedAudioParameter& p, std::function<void()> changed)
        : param (p), onChange (std::move (changed)),
          attachment (p, [this] (float v) { value = v; if (onChange) onChange(); }, nullptr)
    {
        attachment.sendInitialUpdate();
    }

    float getNorm() const                { return param.convertTo0to1 (value); }
    float getDefault() const             { return param.convertFrom0to1 (param.getDefaultValue()); }
    void begin()                         { attachment.beginGesture(); }
    void end()                           { attachment.endGesture(); }
    void setInGesture (float denorm)     { value = param.convertFrom0to1 (param.convertTo0to1 (denorm)); attachment.setValueAsPartOfGesture (value); }
    void setNormInGesture (float n)      { setInGesture (param.convertFrom0to1 (jlimit (0.0f, 1.0f, n))); }
    void setComplete (float denorm)      { value = param.convertFrom0to1 (param.convertTo0to1 (denorm)); attachment.setValueAsCompleteGesture (value); }

    RangedAudioParameter& param;
    std::function<void()> onChange;
    float value = 0.0f;
    ParameterAttachment attachment;
};

// ============================================================================ glass panel
class Panel : public Component
{
public:
    Panel() { setInterceptsMouseClicks (false, true); }
    void paint (Graphics& g) override { cache.draw (g, getLocalBounds().toFloat().reduced (1.0f), radius); }
    float radius = 18.0f;
private:
    GlassCache cache;
};

// ============================================================================ rotary knob
class ParamKnob : public Component, public Animated, public SettableTooltipClient
{
public:
    enum class Style { LabelBelow, LabelAbove, Bare };

    ParamKnob (RangedAudioParameter& p, String labelText, Style s, bool isBipolar, std::function<String (float)> formatter)
        : binding (p, [this] { onParamChanged(); }), label (std::move (labelText)), style (s), bipolar (isBipolar), format (std::move (formatter))
    {
        shownNorm.value = shownNorm.target = binding.getNorm();
        shownNorm.timeConstant = 0.045f;
        glow.timeConstant = 0.12f;
        setBufferedToImage (true);
        setRepaintsOnMouseActivity (false);
    }

    std::function<void (float)> onValueChanged;   // optional: lets siblings show the value
    float getValue() const { return binding.value; }
    String getValueText() const { return format (binding.value); }

    void tick (float dt) override
    {
        bool changed = shownNorm.tick (dt);
        glow.target = dragging ? 1.0f : (isMouseOver (true) ? 0.55f : 0.0f);
        changed |= glow.tick (dt);
        if (changed) repaint();
    }

    void paint (Graphics& g) override
    {
        const auto [centre, radius] = geometry();
        drawKnob (g, centre, radius);
        g.setColour (colours::text);
        if (style == Style::LabelBelow)
        {
            g.setFont (Fonts::regular (15.0f));
            g.drawText (label, Rectangle<float> (0, centre.y + radius + 12, (float) getWidth(), 20), Justification::centred);
            g.setFont (Fonts::regular (15.0f));
            g.setColour (colours::text.withAlpha (0.92f));
            g.drawText (format (binding.value), Rectangle<float> (0, centre.y + radius + 32, (float) getWidth(), 20), Justification::centred);
        }
        else if (style == Style::LabelAbove)
        {
            g.setFont (Fonts::regular (15.0f));
            g.drawText (label, Rectangle<float> (0, 0, (float) getWidth(), 22), Justification::centred);
            g.drawText (format (binding.value), Rectangle<float> (0, centre.y + radius + 13, (float) getWidth(), 20), Justification::centred);
        }
    }

    void mouseDown (const MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) return;
        dragging = true; lastPos = e.position;
        dragNorm = binding.getNorm();
        binding.begin();
    }
    void mouseDrag (const MouseEvent& e) override
    {
        if (! dragging) return;
        const auto d = e.position - lastPos; lastPos = e.position;
        const float sens = (e.mods.isShiftDown() ? 0.1f : 1.0f) / 220.0f;   // Shift = fine
        dragNorm = jlimit (0.0f, 1.0f, dragNorm + (-d.y + d.x * 0.35f) * sens);
        binding.setNormInGesture (dragNorm);
        shownNorm.value = shownNorm.target = binding.getNorm();   // no visual lag while dragging
        repaint();
    }
    void mouseUp (const MouseEvent&) override
    {
        if (! dragging) return;
        dragging = false;
        binding.end();
    }
    void mouseDoubleClick (const MouseEvent&) override
    {
        binding.setComplete (binding.getDefault());
    }
    void mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w) override
    {
        const float step = (e.mods.isShiftDown() ? 0.005f : 0.03f) * (w.isReversed ? -1.0f : 1.0f);
        const float delta = std::abs (w.deltaY) > std::abs (w.deltaX) ? w.deltaY : -w.deltaX;
        const float n = jlimit (0.0f, 1.0f, binding.getNorm() + (delta > 0 ? step : -step) * jmin (3.0f, std::abs (delta) * 4.0f + 0.5f));
        binding.setComplete (binding.param.convertFrom0to1 (n));
    }

private:
    void onParamChanged()
    {
        shownNorm.target = binding.getNorm();
        if (dragging) shownNorm.value = shownNorm.target;
        if (onValueChanged) onValueChanged (binding.value);
        repaint();
    }

    std::pair<Point<float>, float> geometry() const
    {
        const float w = (float) getWidth();
        switch (style)
        {
            case Style::LabelBelow: return { { w * 0.5f, 48.0f }, 33.0f };
            case Style::LabelAbove: return { { w * 0.5f, 65.0f }, 34.0f };
            case Style::Bare: default: return { { w * 0.5f, (float) getHeight() * 0.5f }, jmin (w, (float) getHeight()) * 0.5f - 11.0f };
        }
    }

    void drawKnob (Graphics& g, Point<float> c, float r)
    {
        const float startA = -MathConstants<float>::pi * 0.75f, endA = MathConstants<float>::pi * 0.75f;
        const float norm = jlimit (0.0f, 1.0f, shownNorm.value);
        const float valA = startA + (endA - startA) * norm;
        const float g01 = glow.value;
        const float zeroA = bipolar ? 0.0f : startA;
        const float lo = jmin (zeroA, valA), hi = jmax (zeroA, valA);

        // bezel well the knob sits in
        const float rw = r + 9.0f;
        g.setGradientFill (ColourGradient (Colour (0xff17141d), c.x, c.y - rw, Colour (0xff08070b), c.x, c.y + rw, false));
        g.fillEllipse (c.x - rw, c.y - rw, rw * 2, rw * 2);
        g.setColour (Colours::white.withAlpha (0.045f));
        g.drawEllipse (c.x - rw + 0.5f, c.y - rw + 0.5f, rw * 2 - 1, rw * 2 - 1, 1.0f);

        // interaction / value bloom
        const float bloom = 0.10f + 0.10f * norm + 0.22f * g01;
        g.setGradientFill (ColourGradient (Colour (0xff8a4dff).withAlpha (bloom), c.x, c.y, Colour (0xff8a4dff).withAlpha (0.0f), c.x, c.y + rw + 8, true));
        g.fillEllipse (c.x - rw - 8, c.y - rw - 8, (rw + 8) * 2, (rw + 8) * 2);

        // track
        const float thick = jmax (4.0f, r * 0.15f), ra = r - thick * 0.5f;
        {
            Path track; track.addCentredArc (c.x, c.y, ra, ra, 0.0f, startA, endA, true);
            g.setColour (Colour (0xff1d1a24));
            g.strokePath (track, PathStrokeType (thick, PathStrokeType::curved, PathStrokeType::rounded));
        }

        // glowing value arc (continuous, with fine segment ticks)
        if (hi - lo > 0.005f)
        {
            Path arc; arc.addCentredArc (c.x, c.y, ra, ra, 0.0f, lo, hi, true);
            const auto p0 = Point<float> (c.x + ra * std::sin (lo), c.y - ra * std::cos (lo));
            const auto p1 = Point<float> (c.x + ra * std::sin (hi), c.y - ra * std::cos (hi));
            auto grad = [&] (float alpha)
            {
                ColourGradient gr (Colour (0xff5b2bd0).withAlpha (alpha), p0.x, p0.y, Colour (0xffc39bff).withAlpha (alpha), p1.x, p1.y, false);
                gr.addColour (0.55, Colour (0xff9257ff).withAlpha (alpha));
                return gr;
            };
            const float ga = 1.0f + 0.7f * g01;
            g.setGradientFill (grad (0.07f * ga)); g.strokePath (arc, PathStrokeType (thick + 12.0f, PathStrokeType::curved, PathStrokeType::rounded));
            g.setGradientFill (grad (0.16f * ga)); g.strokePath (arc, PathStrokeType (thick + 6.0f, PathStrokeType::curved, PathStrokeType::rounded));
            g.setGradientFill (grad (1.0f));       g.strokePath (arc, PathStrokeType (thick, PathStrokeType::curved, PathStrokeType::rounded));
            // fine segmentation
            g.setColour (Colour (0xff0d0b12).withAlpha (0.55f));
            const float step = (endA - startA) / 36.0f;
            for (float t = startA + step; t < endA - 0.01f; t += step)
                if (t > lo + 0.02f && t < hi - 0.02f)
                    g.drawLine (c.x + (ra - thick * 0.5f) * std::sin (t), c.y - (ra - thick * 0.5f) * std::cos (t),
                                c.x + (ra + thick * 0.5f) * std::sin (t), c.y - (ra + thick * 0.5f) * std::cos (t), 0.9f);
            // hot tip
            g.setColour (Colour (0xffd8c4ff).withAlpha (0.35f + 0.3f * g01));
            g.fillEllipse (p1.x - thick, p1.y - thick, thick * 2, thick * 2);
            g.setColour (Colour (0xfff3ecff));
            g.fillEllipse (p1.x - thick * 0.32f, p1.y - thick * 0.32f, thick * 0.64f, thick * 0.64f);
        }

        // knob body
        const float rc = r - thick - 3.0f;
        g.setGradientFill (ColourGradient (Colours::black.withAlpha (0.8f), c.x, c.y + 3, Colours::transparentBlack, c.x, c.y + rc + 7, true));
        g.fillEllipse (c.x - rc - 5, c.y - rc - 3, (rc + 5) * 2, (rc + 5) * 2);
        g.setGradientFill (ColourGradient (Colour (0xff221e28), c.x - rc * 0.3f, c.y - rc, Colour (0xff050407), c.x + rc * 0.3f, c.y + rc, false));
        g.fillEllipse (c.x - rc, c.y - rc, rc * 2, rc * 2);
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.10f), c.x, c.y - rc, Colours::white.withAlpha (0.0f), c.x, c.y, false));
        g.drawEllipse (c.x - rc + 0.5f, c.y - rc + 0.5f, rc * 2 - 1, rc * 2 - 1, 1.0f);

        // position indicator + centre dot
        const float ri = rc - 5.0f;
        const Point<float> ip (c.x + ri * std::sin (valA), c.y - ri * std::cos (valA));
        g.setColour (Colour (0xffcdb6ff).withAlpha (0.25f + 0.3f * g01));
        g.fillEllipse (ip.x - 3.2f, ip.y - 3.2f, 6.4f, 6.4f);
        g.setColour (Colour (0xffeee6ff).withAlpha (0.9f));
        g.fillEllipse (ip.x - 1.3f, ip.y - 1.3f, 2.6f, 2.6f);
        g.setColour (Colours::white.withAlpha (0.45f));
        g.fillEllipse (c.x - 1.1f, c.y - 1.1f, 2.2f, 2.2f);
    }

    ParamBinding binding;
    String label;
    Style style;
    bool bipolar;
    std::function<String (float)> format;
    Follower shownNorm, glow;
    bool dragging = false;
    Point<float> lastPos;
    float dragNorm = 0.0f;
};

// ============================================================================ Retune Speed sphere
// Vertical drag controller (not a rotary). The orb is ray-cast per pixel: a glass sphere with a
// crystalline (spherical Voronoi) inner structure seen through both the front and back surfaces,
// a liquid light level that rises with Retune Speed, fresnel rim light and floating motes.
// Higher value = more of the inner structure lit; lower value = fewer cells lit but brighter.
class SphereControl : public Component, public Animated, public SettableTooltipClient
{
public:
    explicit SphereControl (RangedAudioParameter& p) : binding (p, [this] { shown.target = binding.getNorm(); needsRender = true; repaint(); })
    {
        shown.value = shown.target = binding.getNorm();
        shown.timeConstant = 0.09f;
        hover.timeConstant = 0.15f;
        std::mt19937 rng (7);
        std::uniform_real_distribution<float> u (-1.0f, 1.0f), u01 (0.0f, 1.0f);
        // well-spread seeds (fibonacci + jitter) -> even, crystal-like cells
        const float golden = MathConstants<float>::pi * (3.0f - std::sqrt (5.0f));
        for (int i = 0; i < numSeeds; ++i)
        {
            const float y = 1.0f - 2.0f * ((float) i + 0.5f) / (float) numSeeds;
            const float r = std::sqrt (1.0f - y * y), th = golden * (float) i;
            V3 v { r * std::cos (th) + 0.18f * u (rng), y + 0.18f * u (rng), r * std::sin (th) + 0.18f * u (rng) };
            seeds[(size_t) i] = v.normalised();
        }
        for (auto& m : motes) { m.radius = 1.08f + 0.32f * u01 (rng); m.angle = MathConstants<float>::twoPi * u01 (rng); m.speed = 0.08f + 0.22f * u01 (rng); m.phase = 6.28f * u01 (rng); m.size = 0.6f + 1.1f * u01 (rng); }
    }

    void tick (float dt) override
    {
        if (! reducedMotion()) time += dt;
        shown.tick (dt);
        hover.target = dragging ? 1.0f : (isMouseOver() ? 0.5f : 0.0f);
        hover.tick (dt);
        renderAcc += dt;
        if (renderAcc >= 1.0f / 30.0f || needsRender)
        {
            renderAcc = 0.0f;
           #if ULZ_PROFILE
            const double t0 = Time::getMillisecondCounterHiRes();
           #endif
            renderOrb(); needsRender = false;
           #if ULZ_PROFILE
            static double sum = 0; static int cnt = 0; sum += Time::getMillisecondCounterHiRes() - t0;
            if (++cnt == 60) { std::fprintf (stderr, "orb render avg %.2f ms (D=%d)\n", sum / cnt, orb.getWidth()); sum = 0; cnt = 0; }
           #endif
            repaint();   // 30 fps: orb, motes and sparks are slow-moving
        }
    }

    bool hitTest (int x, int y) override
    {
        const auto c = getLocalBounds().toFloat().getCentre();
        return c.getDistanceFrom ({ (float) x, (float) y }) <= orbRadius() * 1.42f;
    }

    void mouseDown (const MouseEvent& e) override { dragging = true; lastY = e.position.y; dragNorm = binding.getNorm(); binding.begin(); }
    void mouseDrag (const MouseEvent& e) override
    {
        if (! dragging) return;
        const float dy = lastY - e.position.y; lastY = e.position.y;   // up = increase
        const float sens = (e.mods.isShiftDown() ? 0.1f : 1.0f) / 320.0f;
        dragNorm = jlimit (0.0f, 1.0f, dragNorm + dy * sens);
        binding.setNormInGesture (dragNorm);
    }
    void mouseUp (const MouseEvent&) override { if (dragging) { dragging = false; binding.end(); } }
    void mouseDoubleClick (const MouseEvent&) override { binding.setComplete (binding.getDefault()); }
    void mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w) override
    {
        const float step = e.mods.isShiftDown() ? 0.002f : 0.015f;
        binding.setComplete (binding.param.convertFrom0to1 (jlimit (0.0f, 1.0f, binding.getNorm() + (w.deltaY > 0 ? step : -step))));
    }

    void paint (Graphics& g) override
    {
        const auto c = getLocalBounds().toFloat().getCentre();
        const float R = orbRadius(), h = hover.value;
        const float F = jlimit (0.0f, 1.0f, shown.value);

        // ambient halo
        g.setGradientFill (ColourGradient (Colour (0xff6a3cff).withAlpha (0.30f + 0.12f * h), c.x, c.y,
                                           Colour (0xff6a3cff).withAlpha (0.0f), c.x, c.y + R * 1.75f, true));
        g.fillEllipse (c.x - R * 1.75f, c.y - R * 1.75f, R * 3.5f, R * 3.5f);

        // bezel: dark glass band holding the orb
        const float rb = R * 1.42f;
        g.setGradientFill (ColourGradient (Colour (0xff1a1622), c.x, c.y - rb, Colour (0xff07060a), c.x, c.y + rb, false));
        g.fillEllipse (c.x - rb, c.y - rb, rb * 2, rb * 2);
        g.setColour (Colours::white.withAlpha (0.05f));
        g.drawEllipse (c.x - rb, c.y - rb, rb * 2, rb * 2, 1.0f);

        // glowing ring (blue on the right, violet on the left)
        const float rr = R * 1.25f;
        auto ringGrad = [&] (float a)
        {
            ColourGradient gr (Colour (0xff4f86ff).withAlpha (a), c.x + rr, c.y - rr * 0.6f, Colour (0xff9a4dff).withAlpha (a), c.x - rr, c.y + rr * 0.6f, false);
            gr.addColour (0.5, Colour (0xff7a5cff).withAlpha (a));
            return gr;
        };
        const std::pair<float, float> glows[] { { 16.0f, 0.06f }, { 9.0f, 0.13f }, { 4.5f, 0.35f } };
        for (auto [w, a] : glows)
        {
            g.setGradientFill (ringGrad (a * (1.0f + 0.6f * h)));
            g.drawEllipse (c.x - rr, c.y - rr, rr * 2, rr * 2, w);
        }
        g.setGradientFill (ringGrad (0.95f));
        g.drawEllipse (c.x - rr, c.y - rr, rr * 2, rr * 2, 1.8f);

        // contact shadow between bezel and orb
        g.setGradientFill (ColourGradient (Colours::black.withAlpha (0.0f), c.x, c.y, Colours::black.withAlpha (0.75f), c.x + R * 1.12f, c.y, true));
        g.fillEllipse (c.x - R * 1.12f, c.y - R * 1.12f, R * 2.24f, R * 2.24f);

        // the ray-cast orb
        if (orb.isValid())
            g.drawImage (orb, Rectangle<float> (c.x - R, c.y - R, R * 2, R * 2), RectanglePlacement::stretchToFit);

        // twinkling sparks on lit cell centres (front facing)
        const float intensity = 1.0f + 0.9f * (1.0f - F);
        for (int i = 0; i < numSeeds; i += 3)
        {
            const auto& s = rotSeeds[(size_t) i];
            if (s.z < 0.25f) continue;
            const float hgt = (s.y + 1.0f) * 0.5f;
            if (hgt > F) continue;
            const float tw = 0.5f + 0.5f * std::sin (time * 2.3f + (float) i * 1.7f);
            const float px = c.x + s.x * R * 0.96f, py = c.y - s.y * R * 0.96f;
            const float a = jmin (1.0f, tw * s.z * 0.7f * intensity);
            g.setColour (Colour (0xffd9c8ff).withAlpha (0.22f * a));
            g.fillEllipse (px - 3.5f, py - 3.5f, 7.0f, 7.0f);
            g.setColour (Colours::white.withAlpha (0.9f * a));
            g.fillEllipse (px - 0.9f, py - 0.9f, 1.8f, 1.8f);
        }

        // specular highlight
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.28f), c.x - R * 0.38f, c.y - R * 0.52f, Colours::white.withAlpha (0.0f), c.x - R * 0.38f + R * 0.42f, c.y - R * 0.52f, true));
        g.fillEllipse (c.x - R * 0.82f, c.y - R * 0.86f, R * 0.9f, R * 0.66f);

        // orbiting motes in the gap between orb and ring
        for (const auto& m : motes)
        {
            const float a = m.angle + time * m.speed;
            const float px = c.x + std::cos (a) * R * m.radius, py = c.y + std::sin (a) * R * m.radius * 0.98f;
            const float tw = 0.35f + 0.65f * (0.5f + 0.5f * std::sin (time * 1.7f + m.phase));
            g.setColour (Colour (0xffb79bff).withAlpha (0.18f * tw));
            g.fillEllipse (px - m.size * 3.0f, py - m.size * 3.0f, m.size * 6.0f, m.size * 6.0f);
            g.setColour (Colour (0xffeee6ff).withAlpha (0.85f * tw));
            g.fillEllipse (px - m.size * 0.6f, py - m.size * 0.6f, m.size * 1.2f, m.size * 1.2f);
        }
    }

private:
    struct V3
    {
        float x, y, z;
        float dot (const V3& o) const { return x * o.x + y * o.y + z * o.z; }
        V3 normalised() const { const float l = std::sqrt (x * x + y * y + z * z); return { x / l, y / l, z / l }; }
    };
    struct Mote { float radius, angle, speed, phase, size; };
    static constexpr int numSeeds = 150;

    float orbRadius() const { return (float) jmin (getWidth(), getHeight()) * 0.5f * 0.70f; }

    static inline float sstep (float a, float b, float x)
    {
        const float t = jlimit (0.0f, 1.0f, (x - a) / (b - a));
        return t * t * (3.0f - 2.0f * t);
    }

    /** nearest / second nearest seed by dot product (largest dot = closest on the sphere),
        searching only a tile's candidate list. */
    inline void nearest2 (const int* cand, int nc, float px, float py, float pz, float& d1, float& d2, int& idx) const
    {
        d1 = -2.0f; d2 = -2.0f; idx = 0;
        for (int k = 0; k < nc; ++k)
        {
            const auto& s = rotSeeds[(size_t) cand[k]];
            const float d = s.x * px + s.y * py + s.z * pz;
            if (d > d1) { d2 = d1; d1 = d; idx = cand[k]; }
            else if (d > d2) d2 = d;
        }
    }

    /** the K seeds closest to a direction (candidate list for one 8x8 tile). */
    int candidates (float px, float py, float pz, int K, int* out) const
    {
        float best[32]; int n = 0;
        for (int i = 0; i < numSeeds; ++i)
        {
            const auto& s = rotSeeds[(size_t) i];
            const float d = s.x * px + s.y * py + s.z * pz;
            if (n < K) { int j = n++; while (j > 0 && best[j - 1] < d) { best[j] = best[j - 1]; out[j] = out[j - 1]; --j; } best[j] = d; out[j] = i; }
            else if (d > best[K - 1]) { int j = K - 1; while (j > 0 && best[j - 1] < d) { best[j] = best[j - 1]; out[j] = out[j - 1]; --j; } best[j] = d; out[j] = i; }
        }
        return n;
    }

    void renderOrb()
    {
        const float R = orbRadius();
        if (R < 4.0f) return;
        const float scale = jlimit (1.0f, 1.3f, Component::getApproximateScaleFactorForComponent (this));
        const int D = jlimit (48, 200, roundToInt (2.0f * R * scale));
        if (orb.getWidth() != D) orb = Image (Image::ARGB, D, D, true);

        // rotate seeds into view space
        const float yaw = time * 0.20f, pitch = 0.38f + 0.12f * std::sin (time * 0.17f), roll = 0.08f * std::sin (time * 0.11f);
        const float cy = std::cos (yaw), sy = std::sin (yaw), cp = std::cos (pitch), sp = std::sin (pitch), cr = std::cos (roll), sr = std::sin (roll);
        for (int i = 0; i < numSeeds; ++i)
        {
            const auto& v = seeds[(size_t) i];
            const float x1 = v.x * cy + v.z * sy, z1 = -v.x * sy + v.z * cy;
            const float y2 = v.y * cp - z1 * sp, z2 = v.y * sp + z1 * cp;
            const float x3 = x1 * cr - y2 * sr, y3 = x1 * sr + y2 * cr;
            rotSeeds[(size_t) i] = { x3, y3, z2 };
        }

        const float F = jlimit (0.0f, 1.0f, shown.value);
        const float intensity = 1.0f + 0.9f * (1.0f - F);   // fewer lit cells -> brighter
        const float hv = hover.value;
        const V3 L = V3 { -0.45f, 0.6f, 0.65f }.normalised();
        const float half = (float) D * 0.5f;
        const float inv = 1.0f / half;
        const bool showSurface = F > 0.01f && F < 0.99f;

        // per-column terms (depend on u only)
        waveCol.resize ((size_t) D); nebU.resize ((size_t) D);
        for (int x = 0; x < D; ++x)
        {
            const float u = ((float) x + 0.5f - half) * inv;
            waveCol[(size_t) x] = 0.022f * std::sin (u * 7.0f + time * 1.6f) + 0.012f * std::sin (u * 13.0f - time * 1.1f);
            nebU[(size_t) x] = std::sin (3.1f * u + time * 0.6f);
        }
        Image::BitmapData bd (orb, Image::BitmapData::writeOnly);
        constexpr int T = 8;
        int candF[32], candB[32];
        for (int ty = 0; ty < D; ty += T)
        for (int tx = 0; tx < D; tx += T)
        {
            // tile centre on the sphere (clamped to the rim) -> candidate cells for front and back
            float uc = ((float) tx + T * 0.5f - half) * inv, vc = ((float) ty + T * 0.5f - half) * inv;
            float rc2 = uc * uc + vc * vc;
            if (rc2 > 1.0f + 0.5f) { for (int y = ty; y < jmin (D, ty + T); ++y) for (int x = tx; x < jmin (D, tx + T); ++x) reinterpret_cast<PixelARGB*> (bd.getPixelPointer (x, y))->setARGB (0, 0, 0, 0); continue; }
            if (rc2 > 0.995f) { const float s = std::sqrt (0.995f / rc2); uc *= s; vc *= s; rc2 = 0.995f; }
            const float zc = std::sqrt (1.0f - rc2);
            const int K = rc2 > 0.6f ? 24 : 14;
            const int nF = candidates (uc, -vc, zc, K, candF);
            const int nB = candidates (uc, -vc, -zc, K, candB);

        for (int y = ty; y < jmin (D, ty + T); ++y)
        {
            const float v = ((float) y + 0.5f - half) * inv;     // +down
            const float nebV = std::sin (-2.7f * v - time * 0.45f);
            auto* line = bd.getLinePointer (y);
            for (int x = tx; x < jmin (D, tx + T); ++x)
            {
                auto* px = reinterpret_cast<PixelARGB*> (line + x * bd.pixelStride);
                const float u = ((float) x + 0.5f - half) * inv;
                const float r2 = u * u + v * v;
                if (r2 >= 1.0f + 2.0f * inv) { px->setARGB (0, 0, 0, 0); continue; }
                const float rr = std::sqrt (r2);
                const float aa = jlimit (0.0f, 1.0f, (1.0f - rr) * half + 0.5f);
                const float z = std::sqrt (jmax (0.0f, 1.0f - r2));
                const float Y = -v;

                // front and back surface of the glass shell
                float f1, f2, b1, b2; int fi, bi;
                nearest2 (candF, nF, u, Y, z, f1, f2, fi);
                nearest2 (candB, nB, u, Y, -z, b1, b2, bi);
                const float lf = std::exp (-(f1 - f2) * 260.0f);
                const float gf = std::exp (-(f1 - f2) * 55.0f);
                const float lb = std::exp (-(b1 - b2) * 200.0f);
                const auto& n = rotSeeds[(size_t) fi];
                const float facet = 0.5f + 0.5f * (n.x * L.x + n.y * L.y + n.z * L.z);

                // liquid light level rising with the value
                const float hgt = (1.0f - v) * 0.5f;
                const float wave = waveCol[(size_t) x];
                const float lit = sstep (F + 0.035f, F - 0.035f, hgt - wave);
                const float surf = showSurface ? std::exp (-std::abs (hgt - wave - F) * 70.0f) : 0.0f;

                const float fres = rr * rr * rr;
                // inner nebula
                const float neb = jmax (0.0f, nebU[(size_t) x] * nebV * std::sin (2.3f * z + u + time * 0.3f));

                float R_ = 0.025f + 0.20f * fres + 0.04f * facet;
                float G_ = 0.010f + 0.08f * fres + 0.015f * facet;
                float B_ = 0.075f + 0.45f * fres + 0.09f * facet;

                // lit volume fog + nebula
                const float fog = lit * (0.55f + 0.45f * z);
                R_ += 0.20f * fog; G_ += 0.08f * fog; B_ += 0.48f * fog;
                // energy core glowing from the bottom of the lit volume
                const float core = lit * std::exp (-(u * u * 3.0f + (v - 0.55f) * (v - 0.55f) * 4.0f)) * (0.35f + 0.25f * intensity);
                R_ += 0.55f * core; G_ += 0.30f * core; B_ += 0.95f * core;
                R_ += 0.20f * neb * (0.35f + 0.65f * lit); G_ += 0.10f * neb * (0.35f + 0.65f * lit); B_ += 0.42f * neb * (0.35f + 0.65f * lit);
                // faceted crystal tint
                const float f2_ = facet * facet; const float fc = f2_ * f2_ * (0.10f + 0.22f * lit);
                R_ += 0.6f * fc; G_ += 0.45f * fc; B_ += 1.0f * fc;

                // back-face structure (seen through the glass, bluish, dimmer)
                const float bAmt = lb * (0.10f + 0.26f * lit);
                R_ += 0.40f * bAmt; G_ += 0.45f * bAmt; B_ += 1.0f * bAmt;

                // front-face crystal edges
                const float eLit = lit * (0.55f + 0.45f * intensity);
                const float unl = (1.0f - lit) * (0.35f + 0.65f * z);   // unlit edges fade with depth
                R_ += lf * (0.22f * unl + 0.72f * eLit) + gf * (0.03f * unl + 0.16f * eLit);
                G_ += lf * (0.14f * unl + 0.58f * eLit) + gf * (0.015f * unl + 0.07f * eLit);
                B_ += lf * (0.50f * unl + 0.95f * eLit) + gf * (0.08f * unl + 0.30f * eLit);

                // glowing liquid surface line
                R_ += 0.75f * surf; G_ += 0.62f * surf; B_ += 1.0f * surf;

                // fresnel rim: lavender on the left/top, blue-cyan on the right
                const float r4 = r2 * r2; const float rim = r4 * r4 * (0.95f + 0.35f * hv);
                const float c = jlimit (0.0f, 1.0f, u * 1.1f + 0.1f);
                R_ += rim * (0.80f - 0.55f * c); G_ += rim * (0.70f - 0.05f * c); B_ += rim * 1.0f;
                // wide luminous crescent on the upper-left, like light wrapping through glass
                const float cres = r4 * jmax (0.0f, -0.75f * u + 0.45f * Y) * (0.9f + 0.3f * hv);
                R_ += 0.85f * cres; G_ += 0.72f * cres; B_ += 1.0f * cres;
                // darker core for depth
                const float coreDark = (1.0f - fres) * (1.0f - 0.6f * lit) * 0.35f;
                R_ *= 1.0f - coreDark; G_ *= 1.0f - coreDark; B_ *= 1.0f - coreDark * 0.6f;
                // key light glint on the glass
                const float spec = std::pow (jmax (0.0f, u * L.x + Y * L.y + z * L.z), 24.0f) * 0.5f;
                R_ += spec; G_ += spec; B_ += spec;

                // soft tone map
                R_ = 1.0f - std::exp (-R_ * 1.35f);
                G_ = 1.0f - std::exp (-G_ * 1.35f);
                B_ = 1.0f - std::exp (-B_ * 1.35f);
                const float a = 0.96f * aa;
                px->setARGB ((uint8) (a * 255.0f), (uint8) (R_ * a * 255.0f), (uint8) (G_ * a * 255.0f), (uint8) (B_ * a * 255.0f));
            }
        }
        }
    }

    std::array<V3, (size_t) numSeeds> seeds {}, rotSeeds {};
    std::vector<float> waveCol, nebU;
    std::array<Mote, 12> motes {};
    Image orb;

    ParamBinding binding;
    Follower shown, hover;
    float time = 0.0f, renderAcc = 1.0f;
    bool dragging = false, needsRender = true;
    float lastY = 0.0f, dragNorm = 0.0f;
};

// ============================================================================ Retune value label
class SpeedValueLabel : public Component, public SettableTooltipClient
{
public:
    explicit SpeedValueLabel (RangedAudioParameter& p) : binding (p, [this] { repaint(); })
    {
        setMouseCursor (MouseCursor::PointingHandCursor);
        setTooltip ("Retune Speed presets");
    }
    void paint (Graphics& g) override
    {
        const auto txt = String (binding.value, 1);
        const auto f = Fonts::regular (18.0f);
        const float tw = GlyphArrangement::getStringWidth (f, txt);
        const float total = tw + 18.0f;
        const float x0 = ((float) getWidth() - total) * 0.5f;
        g.setColour (colours::text);
        g.setFont (f);
        g.drawText (txt, Rectangle<float> (x0, 0, tw + 2, (float) getHeight()), Justification::centredLeft);
        Path chev;
        const float cx = x0 + tw + 11.0f, cy = (float) getHeight() * 0.5f;
        chev.startNewSubPath (cx - 4, cy - 2); chev.lineTo (cx, cy + 2); chev.lineTo (cx + 4, cy - 2);
        g.setColour (colours::textDim.withAlpha (isMouseOver() ? 1.0f : 0.8f));
        g.strokePath (chev, PathStrokeType (1.4f, PathStrokeType::curved, PathStrokeType::rounded));
    }
    void mouseEnter (const MouseEvent&) override { repaint(); }
    void mouseExit (const MouseEvent&) override { repaint(); }
    void mouseUp (const MouseEvent& e) override
    {
        if (! e.mouseWasClicked()) return;
        PopupMenu m;
        const std::pair<float, const char*> items[] = {
            { 100.0f, "100  Instant (hard tune)" }, { 85.0f, "85  Very fast" }, { 70.0f, "70  Fast" },
            { 50.0f, "50  Pop" }, { 32.7f, "32.7  Medium (default)" }, { 15.0f, "15  Natural" },
            { 0.0f, "0  Slow" }, { -3.0f, "-3  Slowest" } };
        int idx = 1;
        for (auto& it : items) m.addItem (idx++, it.second, true, std::abs (binding.value - it.first) < 0.05f);
        SafePointer<SpeedValueLabel> safe (this);
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (this), [safe, items] (int r)
        {
            if (safe != nullptr && r > 0) safe->binding.setComplete (items[r - 1].first);
        });
    }
private:
    ParamBinding binding;
};

// ============================================================================ Low Latency toggle
class LowLatencyToggle : public Component, public Animated, public SettableTooltipClient
{
public:
    explicit LowLatencyToggle (RangedAudioParameter& p) : binding (p, [this] { anim.set (binding.value > 0.5f ? 1.0f : 0.0f); repaint(); })
    {
        anim.duration = 0.22f;
        anim.set (binding.value > 0.5f ? 1.0f : 0.0f, false);
        setMouseCursor (MouseCursor::PointingHandCursor);
    }
    void tick (float dt) override { if (anim.tick (dt)) repaint(); }
    void paint (Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (1.0f);
        const float a = anim.value;
        drawControlBox (g, b, b.getHeight() * 0.5f, isMouseOver() ? 0.6f : 0.0f);
        if (a > 0.01f)
        {
            g.setColour (colours::accent.withAlpha (0.10f * a));
            g.fillRoundedRectangle (b, b.getHeight() * 0.5f);
            g.setColour (colours::accent.withAlpha (0.35f * a));
            g.drawRoundedRectangle (b.reduced (0.5f), b.getHeight() * 0.5f, 1.0f);
        }
        g.setColour (colours::text.interpolatedWith (colours::lavender, a * 0.6f));
        g.setFont (Fonts::regular (15.0f));
        g.drawText ("Low Latency", b.withTrimmedLeft (14).withTrimmedRight (56), Justification::centredLeft);

        auto track = Rectangle<float> (b.getRight() - 50, b.getCentreY() - 11, 40, 22);
        const auto offCol = Colour (0xff3a3542), onCol = colours::accent;
        g.setGradientFill (ColourGradient (offCol.interpolatedWith (onCol.brighter (0.15f), a), track.getX(), track.getY(),
                                           offCol.darker (0.2f).interpolatedWith (onCol.darker (0.2f), a), track.getRight(), track.getBottom(), false));
        g.fillRoundedRectangle (track, 11.0f);
        const float kx = track.getX() + 11.0f + (track.getWidth() - 22.0f) * a;
        g.setColour (colours::lavender.withAlpha (0.35f * a));
        g.fillEllipse (kx - 11, track.getCentreY() - 11, 22, 22);
        g.setColour (Colour (0xff9a94a3).interpolatedWith (Colour (0xffeee6ff), a));
        g.fillEllipse (kx - 8, track.getCentreY() - 8, 16, 16);
    }
    void mouseEnter (const MouseEvent&) override { repaint(); }
    void mouseExit (const MouseEvent&) override { repaint(); }
    void mouseUp (const MouseEvent& e) override
    {
        if (e.mouseWasClicked()) binding.setComplete (binding.value > 0.5f ? 0.0f : 1.0f);
    }
private:
    ParamBinding binding;
    Tween anim;
};

// ============================================================================ Major / Minor / Chromatic
class ScaleSelector : public Component, public Animated, public SettableTooltipClient
{
public:
    explicit ScaleSelector (RangedAudioParameter& p) : binding (p, [this] { pill.set ((float) index()); repaint(); })
    {
        pill.duration = 0.28f;
        pill.set ((float) index(), false);
        setMouseCursor (MouseCursor::PointingHandCursor);
    }
    void tick (float dt) override { if (pill.tick (dt)) repaint(); }
    void paint (Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        drawControlBox (g, b, b.getHeight() * 0.5f);
        const float segW = b.getWidth() / 3.0f;
        auto p = Rectangle<float> (b.getX() + segW * pill.value, b.getY(), segW, b.getHeight()).reduced (1.0f);
        g.setColour (colours::accent.withAlpha (0.35f));
        g.fillRoundedRectangle (p.expanded (2), p.getHeight() * 0.5f + 2);
        g.setGradientFill (ColourGradient (colours::accent.brighter (0.15f), p.getX(), p.getY(), colours::accentDeep, p.getX(), p.getBottom(), false));
        g.fillRoundedRectangle (p, p.getHeight() * 0.5f);
        const char* names[] = { "Major", "Minor", "Chromatic" };
        g.setFont (Fonts::regular (15.0f));
        for (int i = 0; i < 3; ++i)
        {
            const float sel = 1.0f - jlimit (0.0f, 1.0f, std::abs (pill.value - (float) i));
            g.setColour (colours::textDim.interpolatedWith (Colours::white, jmax (sel, hoverIdx == i ? 0.5f : 0.0f)));
            g.drawText (names[i], Rectangle<float> (b.getX() + segW * (float) i, b.getY(), segW, b.getHeight()), Justification::centred);
        }
    }
    void mouseMove (const MouseEvent& e) override { const int h = jlimit (0, 2, (int) (e.position.x / ((float) getWidth() / 3.0f))); if (h != hoverIdx) { hoverIdx = h; repaint(); } }
    void mouseExit (const MouseEvent&) override { hoverIdx = -1; repaint(); }
    void mouseUp (const MouseEvent& e) override
    {
        if (! e.mouseWasClicked()) return;
        binding.setComplete ((float) jlimit (0, 2, (int) (e.position.x / ((float) getWidth() / 3.0f))));
    }
private:
    int index() const { return roundToInt (binding.value); }
    ParamBinding binding;
    Tween pill;
    int hoverIdx = -1;
};

// ============================================================================ 12 note buttons
// Click = choose root (key). Right-click or Shift-click = enable / disable that note as a target.
class NoteRow : public Component, public Animated, public SettableTooltipClient
{
public:
    NoteRow (RangedAudioParameter& keyParam, std::array<RangedAudioParameter*, 12> notes) : keyBinding (keyParam, [this] { repaint(); }), noteParams (notes)
    {
        for (auto& a : enabledAnim) { a.timeConstant = 0.12f; }
        for (auto& a : hoverAnim) a.timeConstant = 0.10f;
        rootAnim.timeConstant = 0.10f;
        setTooltip ("Click: choose the key root.  Right-click / Shift-click: enable or disable a note.");
    }

    void setState (uint16_t mask, bool flats) { noteMask = mask; useFlats = flats; }
    int root() const { return roundToInt (keyBinding.value); }

    void tick (float dt) override
    {
        bool ch = false;
        for (int i = 0; i < 12; ++i)
        {
            const auto k = (size_t) i;
            enabledAnim[k].target = ((noteMask >> i) & 1) ? 1.0f : 0.0f;
            hoverAnim[k].target = hoverIdx == i ? 1.0f : 0.0f;
            ch |= enabledAnim[k].tick (dt);
            ch |= hoverAnim[k].tick (dt);
        }
        if (root() != lastRoot) { lastRoot = root(); rootAnim.value = 0.0f; }
        rootAnim.target = 1.0f; ch |= rootAnim.tick (dt);
        if (ch || flatsShown != useFlats) { flatsShown = useFlats; repaint(); }
    }

    Rectangle<float> buttonBounds (int i) const
    {
        const float step = ((float) getWidth() - 40.0f) / 11.0f;
        return { step * (float) i, ((float) getHeight() - 40.0f) * 0.5f, 40.0f, 40.0f };
    }

    void paint (Graphics& g) override
    {
        static const bool black[12] = { 0,1,0,1,0,0,1,0,1,0,1,0 };
        for (int i = 0; i < 12; ++i)
        {
            auto r = buttonBounds (i).reduced (1.0f);
            const float en = enabledAnim[(size_t) i].value, hv = hoverAnim[(size_t) i].value;
            const bool isRoot = i == root();
            // body
            g.setGradientFill (ColourGradient (Colour (0xff1e1a25).interpolatedWith (Colour (0xff2a2333), hv * 0.7f), r.getCentreX(), r.getY(),
                                               Colour (0xff0e0c12), r.getCentreX(), r.getBottom(), false));
            g.fillEllipse (r);
            if (en > 0.01f)
            {
                g.setColour (colours::accent.withAlpha (0.10f * en));
                g.fillEllipse (r);
            }
            g.setColour (Colours::white.withAlpha (0.06f + 0.06f * hv));
            g.drawEllipse (r, 1.0f);
            if (isRoot)
            {
                const float a = rootAnim.value;
                g.setColour (colours::accent.withAlpha (0.35f * a));
                g.drawEllipse (r.expanded (2.5f), 5.0f);
                g.setColour (colours::violet.withAlpha (a));
                g.drawEllipse (r.reduced (0.5f), 2.0f);
            }
            const auto textCol = colours::text.withAlpha (0.42f + 0.58f * en);
            if (! black[i])
            {
                g.setColour (textCol);
                g.setFont (Fonts::regular (15.0f));
                g.drawText (pitchClassName (i, false), r, Justification::centred);
            }
            else
            {
                // context-sensitive spelling: the key's preferred spelling is on top and brighter
                const String first = pitchClassName (i, useFlats), second = pitchClassName (i, ! useFlats);
                g.setFont (Fonts::regular (12.5f));
                g.setColour (textCol);
                g.drawText (first, r.withTrimmedBottom (r.getHeight() * 0.42f).withTrimmedTop (3), Justification::centredBottom);
                g.setColour (textCol.withMultipliedAlpha (0.85f));
                g.drawText (second, r.withTrimmedTop (r.getHeight() * 0.52f).withTrimmedBottom (3), Justification::centredTop);
            }
        }
    }

    void mouseMove (const MouseEvent& e) override { const int h = hit (e.position); if (h != hoverIdx) { hoverIdx = h; } }
    void mouseExit (const MouseEvent&) override { hoverIdx = -1; }
    void mouseUp (const MouseEvent& e) override
    {
        if (! e.mouseWasClicked()) return;
        const int i = hit (e.position);
        if (i < 0) return;
        if (e.mods.isPopupMenu() || e.mods.isShiftDown())
        {
            auto* p = noteParams[(size_t) i];
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
            p->endChangeGesture();
        }
        else keyBinding.setComplete ((float) i);
    }

private:
    int hit (Point<float> p) const { for (int i = 0; i < 12; ++i) if (buttonBounds (i).contains (p)) return i; return -1; }

    ParamBinding keyBinding;
    std::array<RangedAudioParameter*, 12> noteParams;
    uint16_t noteMask = 0;
    bool useFlats = false, flatsShown = false;
    std::array<Follower, 12> enabledAnim, hoverAnim;
    Follower rootAnim;
    int lastRoot = -1, hoverIdx = -1;
};

// ============================================================================ Input Type
class InputTypeList;

class InputTypeBox : public Component, public Animated, public SettableTooltipClient
{
public:
    explicit InputTypeBox (RangedAudioParameter& p) : binding (p, [this] { repaint(); })
    {
        openAnim.duration = 0.22f;
        setMouseCursor (MouseCursor::PointingHandCursor);
        setTooltip ("Sets the pitch detection range and tracking for the source");
    }
    std::function<void (bool)> onOpenChanged;
    bool isOpen() const { return open; }
    void setOpen (bool o) { if (o == open) return; open = o; openAnim.set (o ? 1.0f : 0.0f); if (onOpenChanged) onOpenChanged (o); repaint(); }
    float openAmount() const { return openAnim.value; }
    Rectangle<int> valueArea() const { return getLocalBounds().withTrimmedLeft (116).reduced (0, 0); }

    void tick (float dt) override { if (openAnim.tick (dt)) repaint(); }
    void paint (Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        drawControlBox (g, b, 12.0f);
        g.setColour (colours::text);
        g.setFont (Fonts::regular (15.5f));
        g.drawText ("Input Type", b.withTrimmedLeft (18).withWidth (100), Justification::centredLeft);
        auto v = valueArea().toFloat().reduced (2, 2);
        drawControlBox (g, v, 10.0f, (isMouseOver (true) ? 0.5f : 0.0f) + openAnim.value * 0.5f);
        g.setColour (colours::text);
        g.setFont (Fonts::regular (15.0f));
        g.drawText (params::inputTypeChoices()[roundToInt (binding.value)], v.withTrimmedLeft (14), Justification::centredLeft);
        // chevron rotates when open
        const float cx = v.getRight() - 20, cy = v.getCentreY();
        Path chev; chev.startNewSubPath (-5, -2.5f); chev.lineTo (0, 2.5f); chev.lineTo (5, -2.5f);
        chev.applyTransform (AffineTransform::rotation (MathConstants<float>::pi * openAnim.value).translated (cx, cy));
        g.setColour (colours::text.withAlpha (0.85f));
        g.strokePath (chev, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));
    }
    void mouseEnter (const MouseEvent&) override { repaint(); }
    void mouseExit (const MouseEvent&) override { repaint(); }
    void mouseUp (const MouseEvent& e) override { if (e.mouseWasClicked() && valueArea().contains (e.getPosition())) setOpen (! open); }

    ParamBinding binding;
private:
    Tween openAnim;
    bool open = false;
};

/** The dropdown itself: expands downward with opacity + vertical movement + highlight transitions. */
class InputTypeList : public Component, public Animated
{
public:
    explicit InputTypeList (InputTypeBox& b) : box (b) { setInterceptsMouseClicks (true, false); setAlwaysOnTop (true); hoverF.timeConstant = 0.08f; }
    static constexpr int itemH = 27;
    static int fullHeight() { return 5 * itemH + 8; }

    void tick (float dt) override
    {
        const float a = box.openAmount();
        setVisible (a > 0.001f);
        if (std::abs (a - lastA) > 1e-4f) { lastA = a; repaint(); }
        if (hoverF.tick (dt)) repaint();
    }
    void paint (Graphics& g) override
    {
        const float a = box.openAmount();
        const float slide = (1.0f - a) * -8.0f;
        auto full = getLocalBounds().toFloat().reduced (2.0f, 0.0f).withHeight ((float) fullHeight()).translated (0, slide);
        auto shown = full.withHeight (full.getHeight() * (0.35f + 0.65f * a));
        Graphics::ScopedSaveState ss (g);
        g.beginTransparencyLayer (a);
        for (int i = 3; i > 0; --i)
        {
            g.setColour (Colours::black.withAlpha (0.12f));
            g.fillRoundedRectangle (shown.expanded ((float) i * 2).translated (0, (float) i * 2), 10.0f + (float) i * 2);
        }
        g.setColour (Colour (0xf2131019));
        g.fillRoundedRectangle (shown, 10.0f);
        g.setColour (Colours::white.withAlpha (0.08f));
        g.drawRoundedRectangle (shown.reduced (0.5f), 10.0f, 1.0f);
        g.reduceClipRegion (shown.toNearestInt());
        const int sel = roundToInt (box.binding.value);
        for (int i = 0; i < 5; ++i)
        {
            auto r = Rectangle<float> (full.getX() + 4, full.getY() + 4 + (float) (i * itemH), full.getWidth() - 8, (float) itemH - 2);
            if (i == sel)
            {
                g.setGradientFill (ColourGradient (colours::accent.withAlpha (0.65f), r.getX(), r.getY(), colours::accentDeep.withAlpha (0.55f), r.getRight(), r.getBottom(), false));
                g.fillRoundedRectangle (r, 6.0f);
                Path tick; tick.startNewSubPath (r.getX() + 13, r.getCentreY()); tick.lineTo (r.getX() + 17, r.getCentreY() + 4); tick.lineTo (r.getX() + 25, r.getCentreY() - 5);
                g.setColour (Colours::white);
                g.strokePath (tick, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));
            }
            else if (i == hoverIdx)
            {
                g.setColour (Colours::white.withAlpha (0.05f * hoverF.value));
                g.fillRoundedRectangle (r, 6.0f);
            }
            g.setColour (i == sel ? Colours::white : colours::text.withAlpha (0.85f));
            g.setFont (Fonts::regular (14.0f));
            g.drawText (params::inputTypeChoices()[i], r.withTrimmedLeft (38), Justification::centredLeft);
        }
        g.endTransparencyLayer();
    }
    void mouseMove (const MouseEvent& e) override { const int h = jlimit (-1, 4, (int) ((e.position.y - 4) / itemH)); if (h != hoverIdx) { hoverIdx = h; hoverF.value = 0; hoverF.target = 1; repaint(); } }
    void mouseExit (const MouseEvent&) override { hoverIdx = -1; repaint(); }
    void mouseUp (const MouseEvent& e) override
    {
        if (! e.mouseWasClicked()) return;
        const int i = (int) ((e.position.y - 4) / itemH);
        if (isPositiveAndBelow (i, 5)) box.binding.setComplete ((float) i);
        box.setOpen (false);
    }
private:
    InputTypeBox& box;
    float lastA = -1.0f;
    int hoverIdx = -1;
    Follower hoverF;
};

// ============================================================================ Vocal visualisation
// Bars = real input peak level history (one bar per ~21 ms); note = pitch detector output.
class VocalView : public Component, public Animated
{
public:
    explicit VocalView (VisualState& vs) : vis (vs) { for (auto& b : bars) b = 0.0f; }

    void setSpelling (bool flats) { useFlats = flats; }

    void tick (float dt) override
    {
        if (! reducedMotion()) time += dt;
        // pull new level samples from the audio thread (lock-free)
        const uint32_t w = vis.writeCount.load (std::memory_order_acquire);
        if (w - readCount > (uint32_t) VisualState::historySize) readCount = w - VisualState::historySize;
        while (readCount != w)
        {
            const float lv = vis.levels[(size_t) (readCount % VisualState::historySize)].load (std::memory_order_relaxed);
            acc = jmax (acc, lv);
            if (++accCount >= 4)   // ~21 ms per bar at 48 kHz
            {
                for (int i = 0; i < numBars - 1; ++i) target[(size_t) i] = target[(size_t) i + 1];
                target[(size_t) numBars - 1] = acc;
                acc = 0.0f; accCount = 0;
            }
            ++readCount;
        }
        const float k = 1.0f - std::exp (-dt / 0.05f);
        for (int i = 0; i < numBars; ++i) bars[(size_t) i] += (target[(size_t) i] - bars[(size_t) i]) * k;

        const bool v = vis.voiced.load();
        noteGlow.target = v ? 1.0f : 0.25f; noteGlow.tick (dt);
        if (v) { shownMidi = vis.detectedMidi.load(); haveNote = true; }
        frameAcc += dt;
        if (frameAcc >= 1.0f / 30.0f) { frameAcc = 0.0f; repaint(); }
    }

    void paint (Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        glass.draw (g, b.reduced (1.0f), 16.0f, 0.9f);

        // header chip
        auto chip = Rectangle<float> (18, 12, 104, 28);
        drawControlBox (g, chip, 9.0f);
        Path wave;
        for (int i = 0; i <= 24; ++i)
        {
            const float x = chip.getX() + 10 + (float) i, env = std::sin ((float) i / 24.0f * MathConstants<float>::pi);
            const float y = chip.getCentreY() + env * 6.0f * std::sin ((float) i * 0.9f);
            if (i == 0) wave.startNewSubPath (x, y); else wave.lineTo (x, y);
        }
        g.setColour (colours::violet);
        g.strokePath (wave, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setFont (Fonts::regular (15.0f));
        g.setColour (colours::violet.brighter (0.2f));
        g.drawText ("Vocal", chip.withTrimmedLeft (46), Justification::centredLeft);

        // waveform bars
        auto area = Rectangle<float> (18, 48, b.getWidth() - 130, b.getHeight() - 62);
        const float cy = area.getCentreY();
        const float step = area.getWidth() / (float) numBars;
        const float maxH = area.getHeight() * 0.5f;
        for (int i = 0; i < numBars; ++i)
        {
            const float pos = (float) i / (float) (numBars - 1);
            const float edge = std::sin (pos * MathConstants<float>::pi);                      // taper at ends
            const float lvl = jlimit (0.0f, 1.0f, std::pow (bars[(size_t) i] * 1.6f, 0.6f));
            const float idle = 0.03f + 0.025f * std::sin (time * 1.6f + (float) i * 0.35f);   // subtle idle breathing
            const float hgt = maxH * jmax (idle, lvl) * (0.35f + 0.65f * edge);
            const auto col = pos < 0.5f ? Colour (0xff9b4dff).interpolatedWith (colours::lavender, pos * 2.0f)
                                        : colours::lavender.interpolatedWith (colours::cyan, (pos - 0.5f) * 2.0f);
            const float x = area.getX() + step * ((float) i + 0.5f);
            g.setColour (col.withAlpha (0.07f));
            g.fillRect (x - 5.0f, cy - hgt - 4, 10.0f, hgt * 2 + 8);
            g.setColour (col.withAlpha (0.20f));
            g.fillRect (x - 2.8f, cy - hgt - 2, 5.6f, hgt * 2 + 4);
            g.setGradientFill (ColourGradient (col.brighter (0.25f), x, cy, col.withAlpha (0.75f), x, cy - hgt, false));
            g.fillRoundedRectangle (x - 1.3f, cy - hgt, 2.6f, hgt * 2, 1.3f);
        }

        // detected note
        auto noteBox = Rectangle<float> (b.getRight() - 104, cy - 40, 86, 80);
        drawControlBox (g, noteBox, 12.0f);
        if (haveNote)
        {
            const int n = (int) std::lround (shownMidi);
            const float cents = (shownMidi - (float) n) * 100.0f;
            const String name = pitchClassName (n % 12, useFlats);
            const float a = noteGlow.value;
            g.setFont (Fonts::semibold (36.0f));
            g.setColour (colours::violet.withAlpha (0.25f * a));
            g.drawText (name, noteBox.translated (0, -6).expanded (2), Justification::centred);
            g.setColour (colours::violet.interpolatedWith (colours::lavender, 0.3f).withAlpha (0.35f + 0.65f * a));
            g.drawText (name, noteBox.translated (0, -6), Justification::centred);
            g.setFont (Fonts::regular (11.5f));
            g.setColour (colours::textDim.withAlpha (0.4f + 0.6f * a));
            g.drawText (String (n / 12 - 1) + "   " + (cents >= 0 ? "+" : "") + String (roundToInt (cents)) + " ct",
                        noteBox.withTrimmedTop (noteBox.getHeight() - 22), Justification::centred);
        }
        else
        {
            g.setFont (Fonts::semibold (30.0f));
            g.setColour (colours::textFaint);
            g.drawText ("-", noteBox, Justification::centred);
        }
    }

private:
    static constexpr int numBars = 64;
    VisualState& vis;
    std::array<float, numBars> bars {}, target {};
    uint32_t readCount = 0;
    float acc = 0.0f; int accCount = 0;
    GlassCache glass;
    float time = 0.0f, shownMidi = 0.0f, frameAcc = 0.0f;
    bool haveNote = false, useFlats = false;
    Follower noteGlow;
};

// ============================================================================ Doubles Width meter
class WidthMeter : public Component, public Animated, public SettableTooltipClient
{
public:
    explicit WidthMeter (RangedAudioParameter& p) : binding (p, [this] { fill.target = binding.getNorm(); repaint(); })
    {
        fill.value = fill.target = binding.getNorm();
        fill.timeConstant = 0.07f;
        setTooltip ("Stereo width of the doubles. Drag, scroll, double-click to reset.");
    }
    void tick (float dt) override { if (fill.tick (dt)) repaint(); }
    Rectangle<float> meterArea() const { return getLocalBounds().toFloat().withTrimmedLeft (170).withTrimmedRight (210); }

    void paint (Graphics& g) override
    {
        g.setColour (colours::text);
        g.setFont (Fonts::regular (16.0f));
        auto lb = getLocalBounds().toFloat().withTrimmedLeft (19).withWidth (120);
        g.drawText ("Doubles", lb.withTrimmedBottom (lb.getHeight() * 0.5f).withTrimmedTop (8), Justification::bottomLeft);
        g.drawText ("Width", lb.withTrimmedTop (lb.getHeight() * 0.5f).withTrimmedBottom (8), Justification::topLeft);

        const auto m = meterArea();
        const int n = 15;
        const float stepX = m.getWidth() / (float) n;
        // lit segments grow symmetrically from the centre = stereo spread
        const float halfLit = fill.value * ((float) n * 0.5f);
        for (int i = 0; i < n; ++i)
        {
            const float x = m.getX() + stepX * ((float) i + 0.5f);
            const float dist = std::abs ((float) i - (float) (n - 1) * 0.5f);
            const float lit = jlimit (0.0f, 1.0f, halfLit - dist + 0.5f);
            const float centreBoost = 1.0f - dist / (float) n;
            auto r = Rectangle<float> (x - 2.5f, m.getCentreY() - 16, 5.0f, 32.0f);
            g.setColour (Colour (0xff2b2633));
            g.fillRoundedRectangle (r, 2.5f);
            if (lit > 0.0f)
            {
                const auto col = colours::lavender.interpolatedWith (Colours::white, 0.5f * centreBoost);
                g.setColour (colours::violet.withAlpha (0.25f * lit));
                g.fillRoundedRectangle (r.expanded (3.0f, 3.0f), 5.0f);
                g.setColour (col.withAlpha (lit * (0.75f + 0.25f * centreBoost)));
                g.fillRoundedRectangle (r, 2.5f);
            }
        }
        g.setColour (colours::text);
        g.setFont (Fonts::regular (20.0f));
        g.drawText (String (roundToInt (binding.value)) + "%", getLocalBounds().toFloat().withTrimmedRight (28).withTrimmedLeft ((float) getWidth() - 140), Justification::centredRight);
    }
    void mouseDown (const MouseEvent& e) override { lastPos = e.position; dragNorm = binding.getNorm(); binding.begin(); dragging = true; }
    void mouseDrag (const MouseEvent& e) override
    {
        if (! dragging) return;
        const auto d = e.position - lastPos; lastPos = e.position;
        const float sens = (e.mods.isShiftDown() ? 0.1f : 1.0f) / 260.0f;
        dragNorm = jlimit (0.0f, 1.0f, dragNorm + (d.x - d.y) * sens);
        binding.setNormInGesture (dragNorm);
        fill.value = fill.target;
    }
    void mouseUp (const MouseEvent&) override { if (dragging) { dragging = false; binding.end(); } }
    void mouseDoubleClick (const MouseEvent&) override { binding.setComplete (binding.getDefault()); }
    void mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w) override
    {
        const float step = e.mods.isShiftDown() ? 0.005f : 0.03f;
        binding.setComplete (binding.param.convertFrom0to1 (jlimit (0.0f, 1.0f, binding.getNorm() + (w.deltaY > 0 ? step : -step))));
    }
private:
    ParamBinding binding;
    Follower fill;
    Point<float> lastPos;
    float dragNorm = 0.0f;
    bool dragging = false;
};

// ============================================================================ piano keyboard
class PianoKeyboard : public Component, public Animated, public SettableTooltipClient
{
public:
    explicit PianoKeyboard (std::array<RangedAudioParameter*, 12> notes) : noteParams (notes)
    {
        for (auto& a : litAnim) a.timeConstant = 0.12f;
        detectedGlow.timeConstant = 0.06f;
        setBufferedToImage (true);   // only re-rendered when a key state actually changes
        setTooltip ("Click a key to enable or disable that note in the correction scale");
    }
    void setState (uint16_t mask, int rootPc, int detectedNote, bool voiced) { noteMask = mask; root = rootPc; detected = detectedNote; isVoiced = voiced; }

    void tick (float dt) override
    {
        bool ch = false;
        for (size_t i = 0; i < 12; ++i) { litAnim[i].target = ((noteMask >> i) & 1) ? 1.0f : 0.0f; ch |= litAnim[i].tick (dt); }
        detectedGlow.target = isVoiced ? 1.0f : 0.0f; ch |= detectedGlow.tick (dt);
        if (ch || detected != lastDetected || hoverKey != lastHover) { lastDetected = detected; lastHover = hoverKey; repaint(); }
    }

    void paint (Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setGradientFill (ColourGradient (Colour (0xff0e0c13), b.getX(), b.getY(), Colour (0xff050407), b.getX(), b.getBottom(), false));
        g.fillRoundedRectangle (b, 14.0f);
        g.setColour (Colours::white.withAlpha (0.05f));
        g.drawRoundedRectangle (b.reduced (0.5f), 14.0f, 1.0f);
        const auto keys = b.reduced (5.0f, 5.0f);
        const float ww = keys.getWidth() / (float) numWhite;

        std::vector<std::pair<Rectangle<float>, int>> whites;
        int wi = 0;
        for (int n = lowNote; n <= highNote; ++n)
        {
            if (isBlack (n)) continue;
            whites.push_back ({ Rectangle<float> (keys.getX() + ww * (float) wi, keys.getY(), ww, keys.getHeight()).reduced (1.0f, 0.0f), n });
            ++wi;
        }
        for (auto& [r, n] : whites) drawWhiteBody (g, r, n);
        // purple light bleeding from lit keys onto neighbours
        for (auto& [r, n] : whites)
        {
            const float lit = litAnim[(size_t) (n % 12)].value;
            if (lit < 0.01f) continue;
            auto gr = r.expanded (r.getWidth() * 0.55f, 0).withTrimmedTop (r.getHeight() * 0.25f);
            g.setGradientFill (ColourGradient (Colour (0xff8a4dff).withAlpha (0.20f * lit), gr.getCentreX(), gr.getBottom() - gr.getHeight() * 0.35f,
                                               Colour (0xff8a4dff).withAlpha (0.0f), gr.getRight(), gr.getBottom() - gr.getHeight() * 0.35f, true));
            g.fillRect (gr);
        }
        for (auto& [r, n] : whites) drawWhiteLight (g, r, n);
        for (int n = lowNote; n <= highNote; ++n)
            if (isBlack (n)) drawBlack (g, blackRect (n, keys, ww), n);
    }

    void mouseMove (const MouseEvent& e) override { hoverKey = keyAt (e.position); }
    void mouseExit (const MouseEvent&) override { hoverKey = -1; }
    void mouseUp (const MouseEvent& e) override
    {
        if (! e.mouseWasClicked()) return;
        const int n = keyAt (e.position);
        if (n < 0) return;
        auto* p = noteParams[(size_t) (n % 12)];
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
        p->endChangeGesture();
    }

private:
    static bool isBlack (int n) { static const bool b[12] = { 0,1,0,1,0,0,1,0,1,0,1,0 }; return b[n % 12]; }
    static constexpr int lowNote = 36, highNote = 91, numWhite = 33;   // C2 .. G6

    Rectangle<float> blackRect (int n, Rectangle<float> keys, float ww) const
    {
        int whiteBefore = 0;
        for (int k = lowNote; k < n; ++k) if (! isBlack (k)) ++whiteBefore;
        const float x = keys.getX() + ww * (float) whiteBefore - ww * 0.31f;
        return { x, keys.getY(), ww * 0.62f, keys.getHeight() * 0.6f };
    }
    int keyAt (Point<float> p) const
    {
        const auto keys = getLocalBounds().toFloat().reduced (5.0f, 5.0f);
        const float ww = keys.getWidth() / (float) numWhite;
        for (int n = lowNote; n <= highNote; ++n) if (isBlack (n) && blackRect (n, keys, ww).contains (p)) return n;
        int wi = 0;
        for (int n = lowNote; n <= highNote; ++n)
        {
            if (isBlack (n)) continue;
            if (Rectangle<float> (keys.getX() + ww * (float) wi, keys.getY(), ww, keys.getHeight()).contains (p)) return n;
            ++wi;
        }
        return -1;
    }

    void drawWhiteBody (Graphics& g, Rectangle<float> r, int n)
    {
        const bool hov = n == hoverKey;
        Path key; key.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), 6.0f, 6.0f, false, false, true, true);
        ColourGradient body (Colour (0xffcfcbd6), r.getX(), r.getY(), Colour (0xff9e98a8), r.getX(), r.getBottom(), false);
        body.addColour (0.75, Colour (0xffb9b4c2));
        if (hov) body.multiplyOpacity (1.0f);
        g.setGradientFill (body);
        g.fillPath (key);
        if (hov) { g.setColour (Colours::white.withAlpha (0.10f)); g.fillPath (key); }
        // subtle side shading + bottom lip (3D)
        g.setGradientFill (ColourGradient (Colours::black.withAlpha (0.14f), r.getX(), r.getY(), Colours::black.withAlpha (0.0f), r.getX() + 5, r.getY(), false));
        g.fillRect (r.withWidth (5.0f));
        auto lip = r.withTrimmedTop (r.getHeight() - 9.0f);
        g.setGradientFill (ColourGradient (Colour (0xffe6e3ea), lip.getX(), lip.getY(), Colour (0xff8d8797), lip.getX(), lip.getBottom(), false));
        Path lp; lp.addRoundedRectangle (lip.getX(), lip.getY(), lip.getWidth(), lip.getHeight(), 6.0f, 6.0f, false, false, true, true);
        g.fillPath (lp);
        g.setColour (Colours::black.withAlpha (0.55f));
        g.strokePath (key, PathStrokeType (1.0f));
    }
    void drawWhiteLight (Graphics& g, Rectangle<float> r, int n)
    {
        const float lit = litAnim[(size_t) (n % 12)].value;
        if (lit > 0.01f)
        {
            const bool isRoot = n % 12 == root;
            Path key; key.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight() - 9.0f, 6.0f, 6.0f, false, false, true, true);
            Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (key);
            const float k = (isRoot ? 1.0f : 0.82f) * lit;
            // light emitted from inside the key: strongest in the lower middle
            ColourGradient light (Colour (0xff7a3cff).withAlpha (0.0f), r.getCentreX(), r.getY() + r.getHeight() * 0.12f,
                                  Colour (0xff8f4dff).withAlpha (0.85f * k), r.getCentreX(), r.getBottom() - 10.0f, false);
            light.addColour (0.55, Colour (0xff9a5cff).withAlpha (0.55f * k));
            g.setGradientFill (light);
            g.fillRect (r);
            // brighter centre strip (light shining through the key), no hard spots
            ColourGradient strip (Colour (0xffd9c2ff).withAlpha (0.0f), r.getX(), 0, Colour (0xffd9c2ff).withAlpha (0.0f), r.getRight(), 0, false);
            strip.addColour (0.5, Colour (0xffd9c2ff).withAlpha (0.40f * k));
            g.setGradientFill (strip);
            g.fillRect (r.withTrimmedTop (r.getHeight() * 0.32f).withTrimmedBottom (12.0f));
        }
        drawDetected (g, r, n, false);
    }
    void drawBlack (Graphics& g, Rectangle<float> r, int n)
    {
        const float lit = litAnim[(size_t) (n % 12)].value;
        // shadow cast on the white keys
        g.setColour (Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (r.translated (2.5f, 2.0f).expanded (1.0f, 0.0f), 4.0f);
        Path key; key.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), 4.0f, 4.0f, false, false, true, true);
        g.setGradientFill (ColourGradient (Colour (0xff1c1a21), r.getX(), r.getY(), Colour (0xff050407), r.getX(), r.getBottom(), false));
        g.fillPath (key);
        // glossy face
        auto face = r.reduced (2.5f, 0.0f).withTrimmedBottom (7.0f);
        g.setGradientFill (ColourGradient (Colour (0xff2c2932), face.getX(), face.getY(), Colour (0xff0c0b0f), face.getX(), face.getBottom(), false));
        g.fillRoundedRectangle (face, 3.0f);
        if (lit > 0.01f)
        {
            g.setGradientFill (ColourGradient (Colour (0xff7a3fe0).withAlpha (0.0f), face.getCentreX(), face.getY(),
                                               Colour (0xff9a5cff).withAlpha (0.85f * lit), face.getCentreX(), face.getBottom(), false));
            g.fillRoundedRectangle (face, 3.0f);
        }
        g.setColour (Colours::white.withAlpha (n == hoverKey ? 0.22f : 0.10f));
        g.fillRoundedRectangle (face.withHeight (face.getHeight() * 0.10f).reduced (1.0f, 0.0f), 1.5f);
        // bevel at the front edge
        g.setGradientFill (ColourGradient (Colour (0xff3b3842), r.getX(), r.getBottom() - 7.0f, Colour (0xff111015), r.getX(), r.getBottom(), false));
        g.fillRect (r.withTrimmedTop (r.getHeight() - 7.0f).reduced (1.0f, 0.0f).withTrimmedBottom (2.0f));
        drawDetected (g, r, n, true);
    }
    void drawDetected (Graphics& g, Rectangle<float> r, int n, bool blackKey)
    {
        if (n != detected || detectedGlow.value < 0.01f) return;
        const float a = detectedGlow.value;
        const auto c = Point<float> (r.getCentreX(), r.getBottom() - (blackKey ? 12.0f : 16.0f));
        g.setColour (colours::cyan.withAlpha (0.3f * a));
        g.fillEllipse (c.x - 8, c.y - 8, 16, 16);
        g.setColour (colours::cyan.withAlpha (a));
        g.fillEllipse (c.x - 3.5f, c.y - 3.5f, 7, 7);
    }

    std::array<RangedAudioParameter*, 12> noteParams;
    std::array<Follower, 12> litAnim;
    Follower detectedGlow;
    uint16_t noteMask = 0;
    int root = 0, detected = -1, lastDetected = -2, hoverKey = -1, lastHover = -2;
    bool isVoiced = false;
};

// ============================================================================ small icon buttons
class IconButton : public Component, public Animated, public SettableTooltipClient
{
public:
    enum class Icon { Piano, Minimise, Close, Chevron };
    explicit IconButton (Icon i) : icon (i) { setMouseCursor (MouseCursor::PointingHandCursor); hover.timeConstant = 0.1f; active.duration = 0.3f; }
    std::function<void()> onClick;
    void setActive (bool a) { active.set (a ? 1.0f : 0.0f); }
    void tick (float dt) override { hover.target = isMouseOver() ? 1.0f : 0.0f; bool c = hover.tick (dt); c |= active.tick (dt); if (c) repaint(); }
    void mouseUp (const MouseEvent& e) override { if (e.mouseWasClicked() && onClick) onClick(); }

    void paint (Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        const float h = hover.value, a = active.value;
        switch (icon)
        {
            case Icon::Piano:
            {
                auto r = b.reduced (1.0f);
                g.setColour (colours::accent.withAlpha (0.18f + 0.25f * a + 0.1f * h));
                g.fillRoundedRectangle (r.expanded (2), 12.0f);
                g.setGradientFill (ColourGradient (Colour (0xff2a1d42).interpolatedWith (colours::accent.withAlpha (0.9f), 0.15f + 0.45f * a), r.getX(), r.getY(),
                                                   Colour (0xff160f22).interpolatedWith (colours::accentDeep, 0.3f * a), r.getX(), r.getBottom(), false));
                g.fillRoundedRectangle (r, 10.0f);
                g.setColour (colours::violet.withAlpha (0.55f + 0.3f * a));
                g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.2f);
                auto k = r.withSizeKeepingCentre (26, 17);
                g.setColour (colours::lavender.interpolatedWith (Colours::white, a * 0.6f));
                g.fillRoundedRectangle (k, 2.5f);
                g.setColour (Colour (0xff2a1d42));
                for (int i = 1; i < 4; ++i) g.drawLine (k.getX() + k.getWidth() * (float) i / 4.0f, k.getY() + 2, k.getX() + k.getWidth() * (float) i / 4.0f, k.getBottom() - 1, 1.2f);
                for (int i = 0; i < 3; ++i)
                {
                    const float x = k.getX() + k.getWidth() * ((float) i + 1) / 4.0f - 2.0f;
                    g.fillRect (x, k.getY(), 4.0f, k.getHeight() * 0.58f);
                }
                break;
            }
            case Icon::Minimise:
                g.setColour (colours::text.withAlpha (0.65f + 0.35f * h));
                g.drawLine (b.getCentreX() - 7, b.getCentreY(), b.getCentreX() + 7, b.getCentreY(), 1.6f);
                break;
            case Icon::Close:
                g.setColour (colours::text.withAlpha (0.65f + 0.35f * h));
                g.drawLine (b.getCentreX() - 6, b.getCentreY() - 6, b.getCentreX() + 6, b.getCentreY() + 6, 1.6f);
                g.drawLine (b.getCentreX() + 6, b.getCentreY() - 6, b.getCentreX() - 6, b.getCentreY() + 6, 1.6f);
                break;
            case Icon::Chevron:
            {
                auto r = b.reduced (1.0f);
                g.setColour (Colour (0xff1b1622).interpolatedWith (Colour (0xff2a2236), h));
                g.fillEllipse (r);
                g.setColour (Colours::white.withAlpha (0.08f));
                g.drawEllipse (r, 1.0f);
                Path chev; chev.startNewSubPath (-5, -2); chev.lineTo (0, 3); chev.lineTo (5, -2);
                chev.applyTransform (AffineTransform::rotation (MathConstants<float>::pi * a).translated (r.getCentreX(), r.getCentreY()));
                g.setColour (colours::violet);
                g.strokePath (chev, PathStrokeType (1.7f, PathStrokeType::curved, PathStrokeType::rounded));
                break;
            }
        }
    }
private:
    Icon icon;
    Follower hover;
    Tween active;
};

// ============================================================================ preset bar
class PresetBar : public Component, public SettableTooltipClient
{
public:
    PresetBar() { setMouseCursor (MouseCursor::PointingHandCursor); setTooltip ("Presets (key, scale and notes are kept)"); }
    std::function<void (int)> onStep;
    std::function<void()> onMenu;
    void setName (const String& n) { if (n != name) { name = n; repaint(); } }
    void paint (Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (1.0f);
        drawControlBox (g, b, b.getHeight() * 0.5f, isMouseOver (true) ? 0.4f : 0.0f);
        g.setColour (colours::text);
        g.setFont (Fonts::regular (14.5f));
        g.drawText (name, b.reduced (34, 0), Justification::centred, true);
        auto arrow = [&] (float cx, bool left)
        {
            Path p; const float cy = b.getCentreY();
            if (left) { p.startNewSubPath (cx + 2.5f, cy - 5); p.lineTo (cx - 2.5f, cy); p.lineTo (cx + 2.5f, cy + 5); }
            else      { p.startNewSubPath (cx - 2.5f, cy - 5); p.lineTo (cx + 2.5f, cy); p.lineTo (cx - 2.5f, cy + 5); }
            g.setColour (colours::violet);
            g.strokePath (p, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));
        };
        arrow (b.getX() + 17, true);
        arrow (b.getRight() - 17, false);
    }
    void mouseEnter (const MouseEvent&) override { repaint(); }
    void mouseExit (const MouseEvent&) override { repaint(); }
    void mouseUp (const MouseEvent& e) override
    {
        if (! e.mouseWasClicked()) return;
        if (e.position.x < 32) { if (onStep) onStep (-1); }
        else if (e.position.x > (float) getWidth() - 32) { if (onStep) onStep (1); }
        else if (onMenu) onMenu();
    }
private:
    String name { "Default" };
};

// ============================================================================ animated background
// Silky flowing ribbons of purple light, concentrated on the LEFT half (animated); the right side
// only has a few faint static ribbons. Lines are drawn at half resolution, the glow at quarter
// resolution (upscaling = free blur). ~30 fps, only the animated region is repainted.
class Background : public Component, public Animated
{
public:
    Background() { setInterceptsMouseClicks (false, false); setOpaque (true); }

    void tick (float dt) override
    {
        if (reducedMotion()) { if (! rendered) { render(); repaint(); } return; }
        time += dt; acc += dt;
        if (acc >= 1.0f / 24.0f || ! rendered)
        {
            acc = 0.0f;
            render();
            repaint (0, 0, (int) ((float) getWidth() * animatedFraction), getHeight());
        }
    }
    void resized() override { rendered = false; }

    void paint (Graphics& g) override
    {
        if (! rendered) render();
        g.drawImageAt (composite, 0, 0);   // plain blit: children repainting on top stay cheap
    }

private:
    static constexpr float animatedFraction = 0.62f;

    struct Ribbon { float y, amp, freq, speed, phase, spread, twist; int count; float alpha, xStart, xEnd; bool animated; };

    float ribbonY (const Ribbon& rb, float u, float kk, float t) const
    {
        const float ph = rb.animated ? t : 0.0f;
        const float twistAmt = std::sin (u * 3.1416f * rb.twist + rb.phase + ph * 0.07f);
        return rb.y
             + rb.amp * std::sin (u * rb.freq * 3.1416f + ph * rb.speed + rb.phase)
             + 0.25f * rb.amp * std::sin (u * 5.3f - ph * rb.speed * 1.6f + rb.phase * 2.0f)
             + kk * rb.spread * (0.25f + 0.95f * std::abs (twistAmt));
    }

    void render()
    {
        const int w = jmax (1, getWidth()), h = jmax (1, getHeight());
        if (lines.getWidth() != w || lines.getHeight() != h) lines = Image (Image::ARGB, w, h, true);
        const int gw = jmax (1, getWidth() / 4), gh = jmax (1, getHeight() / 4);
        if (glow.getWidth() != gw || glow.getHeight() != gh) glow = Image (Image::ARGB, gw, gh, true);

        static const Ribbon ribbons[] = {
            //  y     amp    freq speed phase spread twist cnt alpha  x0     x1    animated
            { 0.30f, 0.12f, 1.3f, 0.16f, 0.0f, 0.0105f, 1.1f, 26, 0.17f, -0.05f, 0.62f, true },
            { 0.62f, 0.10f, 1.0f, 0.12f, 2.1f, 0.0120f, 0.8f, 28, 0.19f, -0.05f, 0.62f, true },
            { 0.88f, 0.07f, 1.6f, 0.10f, 4.0f, 0.0080f, 1.4f, 20, 0.14f, -0.05f, 0.60f, true },
            { 0.10f, 0.05f, 1.2f, 0.00f, 1.0f, 0.0070f, 1.2f, 16, 0.10f, 0.66f, 1.05f, false },   // top right, static
            { 0.80f, 0.06f, 1.1f, 0.00f, 3.0f, 0.0070f, 0.9f, 14, 0.06f, 0.70f, 1.05f, false },   // bottom right, static, faint
        };

        {   // ---- glow layer (quarter res)
            Graphics g (glow);
            const float W = (float) gw, H = (float) gh;
            g.fillAll (colours::background);
            g.setGradientFill (ColourGradient (Colour (0xff2b1158).withAlpha (0.55f), W * 0.10f, H * 0.62f, Colour (0x002b1158), W * 0.50f, H * 0.62f, true));
            g.fillRect (0.0f, 0.0f, W, H);
            g.setGradientFill (ColourGradient (Colour (0xff1f0d42).withAlpha (0.45f), W * 0.30f, H * 0.12f, Colour (0x001f0d42), W * 0.62f, H * 0.12f, true));
            g.fillRect (0.0f, 0.0f, W, H);
            g.setGradientFill (ColourGradient (Colour (0xff1c0f38).withAlpha (0.40f), W * 0.88f, H * 0.02f, Colour (0x001c0f38), W * 0.62f, H * 0.35f, true));
            g.fillRect (0.0f, 0.0f, W, H);
            for (const auto& rb : ribbons)
            {
                Path p;
                for (int i = 0; i <= 48; ++i)
                {
                    const float u = (float) i / 48.0f;
                    const float x = W * (rb.xStart + (rb.xEnd - rb.xStart) * u);
                    const float y = H * ribbonY (rb, u, 0.0f, time);
                    if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
                }
                ColourGradient gr (Colour (0xff8a4dff).withAlpha (rb.alpha * 1.6f), W * rb.xStart, 0, Colour (0x008a4dff), W * rb.xEnd, 0, false);
                gr.addColour (0.15, Colour (0xff9a5cff).withAlpha (rb.alpha * 2.0f));
                gr.addColour (0.6, Colour (0xff7a45f0).withAlpha (rb.alpha * 1.4f));
                g.setGradientFill (gr);
                g.strokePath (p, PathStrokeType (7.0f, PathStrokeType::curved, PathStrokeType::rounded));
            }
        }
        {   // ---- silk lines (half res)
            Graphics g (lines);
            const float W = (float) w, H = (float) h;
            if (rendered) { const auto anim = Rectangle<int> (0, 0, (int) (W * animatedFraction) + 2, h); lines.clear (anim); g.reduceClipRegion (anim); }
            else lines.clear (lines.getBounds());
            for (const auto& rb : ribbons)
            {
                if (rendered && ! rb.animated) continue;
                for (int k = 0; k < rb.count; ++k)
                {
                    const float kk = (float) k - (float) (rb.count - 1) * 0.5f;
                    const float centre = 1.0f - std::abs (kk) / ((float) rb.count * 0.5f);
                    Path p;
                    for (int i = 0; i <= 90; ++i)
                    {
                        const float u = (float) i / 90.0f;
                        const float x = W * (rb.xStart + (rb.xEnd - rb.xStart) * u);
                        const float y = H * ribbonY (rb, u, kk, time);
                        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
                    }
                    const float a = rb.alpha * (0.25f + 0.75f * centre);
                    ColourGradient gr (Colour (0xffa678ff).withAlpha (a), W * rb.xStart, 0, Colour (0x00a678ff), W * rb.xEnd, 0, false);
                    gr.addColour (0.12, Colour (0xffb48cff).withAlpha (a * 1.2f));
                    gr.addColour (0.55, Colour (0xff8a5cf0).withAlpha (a * 0.85f));
                    g.setGradientFill (gr);
                    g.strokePath (p, PathStrokeType (1.0f));
                    if (k == rb.count / 2 || k == rb.count / 2 - 3)
                    {   // bright silk highlights
                        ColourGradient hi (Colour (0xffe0d0ff).withAlpha (rb.alpha * 2.6f), W * rb.xStart, 0, Colour (0x00e0d0ff), W * rb.xEnd, 0, false);
                        hi.addColour (0.3, Colour (0xffd2b8ff).withAlpha (rb.alpha * 3.2f));
                        g.setGradientFill (hi);
                        g.strokePath (p, PathStrokeType (1.5f));
                    }
                }
            }
        }
        // compose once per frame at full resolution (glow upscaled with cheap filtering = soft blur)
        if (composite.getWidth() != getWidth() || composite.getHeight() != getHeight())
            composite = Image (Image::RGB, jmax (1, getWidth()), jmax (1, getHeight()), true);
        {
            Graphics g (composite);
            const auto full = Rectangle<float> (0, 0, (float) getWidth(), (float) getHeight());
            if (rendered)   // after the first frame only the animated (left) region changes
                g.reduceClipRegion (0, 0, (int) ((float) getWidth() * animatedFraction) + 2, getHeight());
            g.setImageResamplingQuality (Graphics::mediumResamplingQuality);
            g.drawImage (glow, full, RectanglePlacement::stretchToFit);
            g.drawImageAt (lines, 0, 0);
        }
        rendered = true;
    }

    Image lines, glow, composite;
    float time = 0.0f, acc = 0.0f;
    bool rendered = false;
};

} // namespace ulz::ui
