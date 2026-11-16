#pragma once

#include "../../Page.h"
#include "../../AppShell.h"
#include "../../components/Widgets.h"
#include "../../components/Plots.h"
#include <optional>

/** Small helpers shared by the System, Hardware and Alignment pages (owned by the same worker).
    Everything here is header-only and paints from theme tokens. */
namespace fa::ui::kit
{
constexpr int fieldH   = 56;                      // height of a ValueField (label + value)
constexpr int btnH     = theme::controlHeight;    // buttons / combos / segmented controls
constexpr int gap      = theme::grid;             // 8
constexpr int gap2     = theme::pad;              // 16
constexpr int margin   = theme::padLg;            // 24
constexpr int cardHead = 44;                      // title row inside a Card

/** Vertically scrolling page body. Add children to content(); set `layout` to return the
    content height for a given width. Call relayout() whenever the content changes size. */
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

    void resized() override
    {
        viewport.setBounds (getLocalBounds());
        relayout();
    }

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

/** Uppercase, letter-spaced caption used above groups of controls. */
class Caption : public juce::Component
{
public:
    explicit Caption (const juce::String& t = {}, juce::Colour c = theme::textMuted) : text (t), colour (c) {}
    void setText (const juce::String& t) { text = t; repaint(); }
    void paint (juce::Graphics& g) override
    {
        g.setColour (colour);
        g.setFont (theme::fontBold (10.5f));
        g.drawText (text.toUpperCase(), getLocalBounds(), juce::Justification::centredLeft, false);
    }
private:
    juce::String text;
    juce::Colour colour;
};

/** Multi-line secondary text block (hints, notes). */
class Note : public juce::Component
{
public:
    explicit Note (const juce::String& t = {}, float size = 12.5f, juce::Colour c = theme::textDim)
        : text (t), fontSize (size), colour (c) {}
    void setText (const juce::String& t) { text = t; repaint(); }
    void setTextColour (juce::Colour c) { colour = c; repaint(); }
    const juce::String& getText() const noexcept { return text; }
    void paint (juce::Graphics& g) override
    {
        g.setColour (colour);
        g.setFont (theme::font (fontSize));
        g.drawFittedText (text, getLocalBounds(), juce::Justification::topLeft, 20, 1.0f);
    }
    /** Height needed for `width`. */
    int heightFor (int width) const
    {
        juce::GlyphArrangement ga;
        ga.addJustifiedText (theme::font (fontSize), text, 0.0f, 0.0f, (float) width, juce::Justification::topLeft);
        const auto bb = ga.getBoundingBox (0, -1, true);
        return juce::jmax ((int) std::ceil (fontSize * 1.4f), (int) std::ceil (bb.getHeight()) + 6);
    }
private:
    juce::String text;
    float fontSize;
    juce::Colour colour;
};

/** Pill toggle for binary states that deserve more presence than a switch (e.g. polarity). */
class PillToggle : public juce::Component
{
public:
    PillToggle (const juce::String& offText, const juce::String& onText, juce::Colour onColour)
        : offLabel (offText), onLabel (onText), onCol (onColour) {}

    void setOn (bool b) { if (on != b) { on = b; repaint(); } }
    bool isOn() const noexcept { return on; }
    std::function<void (bool)> onChange;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        const auto fill   = on ? onCol.withAlpha (0.18f) : (hover ? theme::bg3 : theme::bg2);
        const auto stroke = on ? onCol.withAlpha (0.8f) : (hover ? theme::borderHi : theme::border);
        g.setColour (fill);
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (stroke);
        g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
        g.setColour (on ? onCol : theme::textDim);
        g.setFont (theme::fontMedium (11.5f));
        g.drawText (on ? onLabel : offLabel, getLocalBounds(), juce::Justification::centred, false);
    }
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit  (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! getLocalBounds().contains (e.getPosition()))
            return;
        on = ! on;
        repaint();
        if (onChange)
            onChange (on);
    }
private:
    juce::String offLabel, onLabel;
    juce::Colour onCol;
    bool on = false, hover = false;
};

inline void styleCombo (juce::ComboBox& c)
{
    c.setColour (juce::ComboBox::backgroundColourId, theme::bg3);
    c.setColour (juce::ComboBox::outlineColourId, theme::border);
    c.setColour (juce::ComboBox::textColourId, theme::text);
    c.setColour (juce::ComboBox::arrowColourId, theme::textDim);
    c.setColour (juce::ComboBox::focusedOutlineColourId, theme::accent);
    c.setColour (juce::ComboBox::buttonColourId, theme::bg3);
}

inline void styleEditor (juce::TextEditor& e, const juce::String& placeholder = {})
{
    e.setColour (juce::TextEditor::backgroundColourId, theme::bg3);
    e.setColour (juce::TextEditor::outlineColourId, theme::border);
    e.setColour (juce::TextEditor::focusedOutlineColourId, theme::accent);
    e.setColour (juce::TextEditor::textColourId, theme::text);
    e.setColour (juce::TextEditor::highlightColourId, theme::accentDim);
    e.setColour (juce::CaretComponent::caretColourId, theme::accent);
    e.setFont (theme::font (13.0f));
    e.setIndents (8, 6);
    e.setSelectAllWhenFocused (true);
    if (placeholder.isNotEmpty())
        e.setTextToShowWhenEmpty (placeholder, theme::textMuted);
}

inline void styleLabel (juce::Label& l, float size = 12.0f, juce::Colour c = theme::textDim,
                        juce::Justification j = juce::Justification::centredLeft)
{
    l.setFont (theme::font (size));
    l.setColour (juce::Label::textColourId, c);
    l.setJustificationType (j);
    l.setBorderSize ({ 0, 0, 0, 0 });
}

/** Lays `items` out in a wrapping grid of equal cells; returns the total height used. */
inline int gridLayout (juce::Rectangle<int> area, const std::vector<juce::Component*>& items,
                       int minCellW, int cellH, int gapX = gap, int gapY = gap)
{
    if (items.empty())
        return 0;
    const int cols  = juce::jmax (1, (area.getWidth() + gapX) / (minCellW + gapX));
    const int cellW = (area.getWidth() - gapX * (cols - 1)) / cols;
    int x = area.getX(), y = area.getY(), col = 0;
    for (auto* c : items)
    {
        c->setBounds (x, y, cellW, cellH);
        if (++col == cols) { col = 0; x = area.getX(); y += cellH + gapY; }
        else x += cellW + gapX;
    }
    const int rows = (int) (items.size() + (size_t) cols - 1) / cols;
    return rows * cellH + (rows - 1) * gapY;
}

/** Tiny per-user UI preferences (e.g. right-hand drive) kept next to the session settings. */
inline juce::PropertiesFile& uiPrefs (Session& s)
{
    static std::unique_ptr<juce::PropertiesFile> prefs;
    if (prefs == nullptr)
    {
        juce::PropertiesFile::Options o;
        o.millisecondsBeforeSaving = 300;
        prefs = std::make_unique<juce::PropertiesFile> (s.getAppDataFolder().getChildFile ("ui.settings"), o);
    }
    return *prefs;
}

/** Which routed microphone the Live analyzer (RTA / transfer function) listens to. Chosen on
    the Rig page only; the Live tab just shows it. */
inline int liveAnalyzerMic (Session& s)            { return uiPrefs (s).getIntValue ("liveAnalyzerMic", 0); }
inline void setLiveAnalyzerMic (Session& s, int i) { uiPrefs (s).setValue ("liveAnalyzerMic", i); }

inline bool isRightHandDrive (Session& s)          { return uiPrefs (s).getBoolValue ("rightHandDrive", false); }
inline void setRightHandDrive (Session& s, bool b) { uiPrefs (s).setValue ("rightHandDrive", b); }

/** "  -  " style separator using a real middle dot (UTF-8 safe). */
inline const juce::String& sep()
{
    static const juce::String s (juce::CharPointer_UTF8 ("  \xc2\xb7  "));
    return s;
}

inline juce::String channelShortName (const OutputChannel& ch)
{
    juce::String s = ch.side == Side::left ? "L " : ch.side == Side::right ? "R " : "";
    return s + ch.label.replace ("Left ", "").replace ("Right ", "");
}
} // namespace fa::ui::kit
