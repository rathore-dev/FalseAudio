#pragma once

#include "../../Page.h"
#include "../../AppShell.h"
#include "../../components/Widgets.h"
#include "core/install/InstallPlanner.h"
#include <set>

/** Small helpers private to the Install & Gains page. Header-only; everything paints from
    theme tokens. */
namespace fa::ui::ikit
{
constexpr int gap      = theme::grid;        // 8
constexpr int gap2     = theme::pad;         // 16
constexpr int margin   = theme::padLg;       // 24
constexpr int btnH     = theme::controlHeight;
constexpr int cardHead = 44;
constexpr int rowH     = 48;
constexpr int tableHead = 28;

// ---- Formatting -------------------------------------------------------------------------------
/** "31.6", "4", "1/0" - sensible precision for a value in `unit`. */
inline juce::String fmtValue (double v, const juce::String& unit)
{
    const auto u = unit.toLowerCase();
    if (u == "awg")
        return v <= 0.0 ? juce::String ("1/0") : juce::String (juce::roundToInt (v));
    if (u == "step" || u == "steps" || u == "db" || u == "%")
        return juce::String (v, std::abs (v - std::round (v)) < 0.05 ? 0 : 1);
    const double a = std::abs (v);
    if (a >= 100.0) return juce::String (juce::roundToInt (v));
    if (a >= 10.0)  return juce::String (v, 1);
    return juce::String (v, 2);
}

inline juce::String fmtWatts (double w)   { return w >= 1000.0 ? juce::String (w / 1000.0, 2) + " kW" : juce::String (juce::roundToInt (w)) + " W"; }
inline juce::String fmtVolts (double v)   { return juce::String (v, v >= 10.0 ? 1 : 2) + " V"; }
inline juce::String fmtAmps (double a)    { return juce::String (a, a >= 100.0 ? 0 : 1) + " A"; }
inline juce::String fmtOhms (double o)    { return juce::String (o, o >= 10.0 ? 1 : 2) + juce::String (juce::CharPointer_UTF8 (" Ω")); }
inline juce::String fmtAwg (double awg)   { return fmtValue (awg, "AWG") + " AWG"; }

/** "250 Hz", "2.5 kHz" - frequencies without stray decimals. */
inline juce::String fmtHz (double hz)
{
    if (hz >= 1000.0)
    {
        auto s = juce::String (hz / 1000.0, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
        return s + " kHz";
    }
    return juce::String (juce::roundToInt (hz)) + " Hz";
}

/** Word-wrapped layout of `text` in `font` at `width` (JUCE 8 safe; one place for measuring and drawing). */
inline juce::TextLayout layoutText (const juce::Font& font, juce::Colour c, const juce::String& text, int width)
{
    juce::AttributedString s (text);
    s.setFont (font);
    s.setColour (c);
    s.setJustification (juce::Justification::topLeft);
    s.setWordWrap (juce::AttributedString::byWord);
    juce::TextLayout tl;
    tl.createLayout (s, (float) juce::jmax (10, width));
    return tl;
}

/** Height of multi-line `text` laid out in `font` at `width`. */
inline int textHeight (const juce::Font& font, const juce::String& text, int width)
{
    if (text.isEmpty() || width <= 0)
        return 0;
    return (int) std::ceil (layoutText (font, juce::Colours::white, text, width).getHeight()) + 2;
}

inline void drawMultiline (juce::Graphics& g, const juce::Font& font, juce::Colour c, const juce::String& text,
                           juce::Rectangle<int> area)
{
    if (text.isEmpty() || area.isEmpty())
        return;
    layoutText (font, c, text, area.getWidth()).draw (g, area.toFloat());
}

// ---- Severity -----------------------------------------------------------------------------------
using Severity = install::Instruction::Severity;

inline juce::Colour severityColour (Severity s)
{
    switch (s)
    {
        case Severity::danger:      return theme::bad;
        case Severity::important:   return theme::warn;
        case Severity::recommended: return theme::accent;
        case Severity::info:
        default:                    return theme::textDim;
    }
}

inline juce::String severityName (Severity s)
{
    switch (s)
    {
        case Severity::danger:      return "Danger";
        case Severity::important:   return "Important";
        case Severity::recommended: return "Recommended";
        case Severity::info:
        default:                    return "Info";
    }
}

// ---- Instruction areas --------------------------------------------------------------------------
/** Checklist groups in the order the user should work through them. */
enum class Area { missing, wiring, headUnit, dsp, amps, speakers, safety, measurement, other };
constexpr int numAreas = 9;

inline juce::String areaName (Area a)
{
    switch (a)
    {
        case Area::missing:     return "Before you start";
        case Area::wiring:      return "Wiring";
        case Area::headUnit:    return "Head unit";
        case Area::dsp:         return "DSP";
        case Area::amps:        return "Amplifiers";
        case Area::speakers:    return "Speakers";
        case Area::safety:      return "Safety";
        case Area::measurement: return "Measurement";
        case Area::other:
        default:                return "Other";
    }
}

inline icons::Id areaIcon (Area a)
{
    switch (a)
    {
        case Area::missing:     return icons::Id::search;
        case Area::wiring:      return icons::Id::plug;
        case Area::headUnit:    return icons::Id::radio;
        case Area::dsp:         return icons::Id::chip;
        case Area::amps:        return icons::Id::bolt;
        case Area::speakers:    return icons::Id::speaker;
        case Area::safety:      return icons::Id::warning;
        case Area::measurement: return icons::Id::mic;
        case Area::other:
        default:                return icons::Id::info;
    }
}

/** Maps the planner's area strings ("Amp 1 (JL XD600/6)", "Speaker wiring", "Crossover limits", "Mic")
    to a checklist group. Prefixes first (product names in brackets may contain anything), then keywords. */
inline Area classifyArea (const juce::String& areaText)
{
    const auto a = areaText.trim().toLowerCase();
    auto starts = [&a] (const char* s) { return a.startsWith (s); };
    if (starts ("missing"))                                        return Area::missing;
    if (starts ("safety"))                                         return Area::safety;
    if (starts ("speaker"))                                        return Area::speakers;
    if (starts ("head unit") || starts ("head-unit"))              return Area::headUnit;
    if (starts ("amp"))                                            return Area::amps;
    if (starts ("dsp") || starts ("crossover") || starts ("dac"))  return Area::dsp;
    if (starts ("wiring") || starts ("power"))                     return Area::wiring;
    if (starts ("mic") || starts ("interface") || starts ("measure")) return Area::measurement;

    auto has = [&a] (const char* s) { return a.contains (s); };
    if (has ("safety") || has ("protect"))                                                  return Area::safety;
    if (has ("speaker") || has ("driver") || has ("sub") || has ("tweeter") || has ("woofer")) return Area::speakers;
    if (has ("wire") || has ("wiring") || has ("fuse") || has ("ground") || has ("battery") || has ("alternator"))
                                                                                            return Area::wiring;
    if (has ("head unit") || has ("source") || has ("radio") || has ("stereo"))             return Area::headUnit;
    if (has ("dsp") || has ("processor"))                                                   return Area::dsp;
    if (has ("amp"))                                                                        return Area::amps;
    if (has ("mic") || has ("interface") || has ("umik"))                                   return Area::measurement;
    return Area::other;
}

/** Stable key for persisting the tick state of one instruction. */
inline juce::String instructionKey (const install::Instruction& i)
{
    return i.area + "|" + i.title + "|" + juce::String (i.value, 3) + i.unit;
}

// ---- Checklist tick persistence ----------------------------------------------------------------
/** Tiny JSON settings file: { "<instruction key>": true, ... }. */
class TickStore
{
public:
    explicit TickStore (juce::File file) : f (std::move (file)) { load(); }

    bool isDone (const juce::String& key) const { return done.count (key) > 0; }
    void setDone (const juce::String& key, bool b)
    {
        if (b) done.insert (key); else done.erase (key);
        save();
    }
    void clearAll (const juce::StringArray& keys)
    {
        for (auto& k : keys) done.erase (k);
        save();
    }

private:
    void load()
    {
        auto v = juce::JSON::parse (f);
        if (auto* obj = v.getDynamicObject())
            for (auto& p : obj->getProperties())
                if ((bool) p.value)
                    done.insert (p.name.toString());
    }
    void save() const
    {
        auto* obj = new juce::DynamicObject();
        for (auto& k : done)
            obj->setProperty (juce::Identifier (k), true);
        f.getParentDirectory().createDirectory();
        f.replaceWithText (juce::JSON::toString (juce::var (obj), true));
    }
    juce::File f;
    std::set<juce::String> done;
};

// ---- Layout helpers ----------------------------------------------------------------------------
/** Vertically scrolling page body (same contract as the one used by the System page). */
class ScrollBody : public juce::Component
{
public:
    ScrollBody()
    {
        viewport.setViewedComponent (&contentComp, false);
        viewport.setScrollBarsShown (true, false, true, false);
        viewport.setScrollBarThickness (8);
        addAndMakeVisible (viewport);
    }
    juce::Component& content() noexcept { return contentComp; }
    juce::Viewport& getViewport() noexcept { return viewport; }
    std::function<int (int width)> layout;

    void resized() override { viewport.setBounds (getLocalBounds()); relayout(); }
    void relayout()
    {
        const int w = juce::jmax (320, viewport.getWidth() - viewport.getScrollBarThickness());
        const int h = layout ? layout (w) : 0;
        contentComp.setSize (w, juce::jmax (h, viewport.getHeight()));
    }
private:
    juce::Viewport viewport;
    juce::Component contentComp;
};

/** Multi-line secondary text block. */
class Note : public juce::Component
{
public:
    explicit Note (const juce::String& t = {}, float size = 12.5f, juce::Colour c = theme::textDim)
        : text (t), fontSize (size), colour (c) {}
    void setText (const juce::String& t) { text = t; repaint(); }
    void setTextColour (juce::Colour c) { colour = c; repaint(); }
    const juce::String& getText() const noexcept { return text; }
    int heightFor (int width) const { return textHeight (theme::font (fontSize), text, width) + 2; }
    void paint (juce::Graphics& g) override { drawMultiline (g, theme::font (fontSize), colour, text, getLocalBounds()); }
private:
    juce::String text;
    float fontSize;
    juce::Colour colour;
};

/** Thin horizontal bar with an optional marker, used for voltage-drop and power-ratio gauges. */
inline void drawGauge (juce::Graphics& g, juce::Rectangle<float> r, float fraction, juce::Colour c,
                       float markerFraction = -1.0f)
{
    const float rad = r.getHeight() * 0.5f;
    g.setColour (theme::bg4);
    g.fillRoundedRectangle (r, rad);
    auto fill = r.withWidth (juce::jlimit (0.0f, 1.0f, fraction) * r.getWidth());
    if (fill.getWidth() > 0.5f)
    {
        g.setColour (c.withAlpha (0.25f));
        g.fillRoundedRectangle (fill.expanded (0.0f, 1.5f), rad + 1.5f);
        g.setColour (c);
        g.fillRoundedRectangle (fill, rad);
    }
    if (markerFraction >= 0.0f)
    {
        const float x = r.getX() + juce::jlimit (0.0f, 1.0f, markerFraction) * r.getWidth();
        g.setColour (theme::text.withAlpha (0.7f));
        g.fillRect (x - 0.5f, r.getY() - 3.0f, 1.0f, r.getHeight() + 6.0f);
    }
}

/** Small rounded tick box (20 px) with animated-looking fill when checked. */
inline void drawTickBox (juce::Graphics& g, juce::Rectangle<float> box, bool checked, bool hover)
{
    const float rad = 5.0f;
    if (checked)
    {
        theme::drawGlow (g, box, theme::good, rad, 6.0f);
        g.setColour (theme::good);
        g.fillRoundedRectangle (box, rad);
        icons::draw (g, icons::Id::check, box.reduced (4.0f), theme::onAccent);
    }
    else
    {
        g.setColour (hover ? theme::bg4 : theme::bg3);
        g.fillRoundedRectangle (box, rad);
        g.setColour (hover ? theme::accent.withAlpha (0.7f) : theme::borderHi);
        g.drawRoundedRectangle (box.reduced (0.5f), rad, 1.0f);
    }
}

/** Status badge text drawn as a pill: "OK", "Upgrade", "Warning". */
inline int drawBadge (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& text, juce::Colour c,
                      juce::Justification just = juce::Justification::centredLeft)
{
    const auto font = theme::fontBold (10.5f);
    const int w = (int) theme::textWidth (font, text.toUpperCase()) + 16;
    auto pill = area.withSizeKeepingCentre (w, 20).toFloat();
    if (just.testFlags (juce::Justification::left))        pill.setX ((float) area.getX());
    else if (just.testFlags (juce::Justification::right))  pill.setX ((float) (area.getRight() - w));
    theme::fillPill (g, pill, c.withAlpha (0.14f), c.withAlpha (0.55f));
    g.setColour (c);
    g.setFont (font);
    g.drawText (text.toUpperCase(), pill.toNearestInt(), juce::Justification::centred, false);
    return w;
}
} // namespace fa::ui::ikit
