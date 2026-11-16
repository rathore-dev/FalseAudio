#pragma once

#include "../Page.h"
#include "../theme/Animation.h"

/** Shared widgets. API is a CONTRACT (pages code against it); implementation is owned by the
    shell/widgets worker in Widgets.cpp. Every widget paints itself from theme tokens. */
namespace fa::ui
{
/** Rounded elevated panel with optional title row and an optional right-aligned header area. */
class Card : public juce::Component
{
public:
    explicit Card (const juce::String& title = {}, const juce::String& subtitle = {});
    void setTitle (const juce::String& t, const juce::String& subtitle = {});
    /** Area inside the card below the title (in local coords) - lay children out here. */
    juce::Rectangle<int> getContentArea() const;
    /** Area on the right of the title row for small controls. */
    juce::Rectangle<int> getHeaderActionArea() const;
    void paint (juce::Graphics&) override;
    void setHighlight (juce::Colour c) { highlight = c; repaint(); }
private:
    juce::String title, subtitle;
    juce::Colour highlight = juce::Colours::transparentBlack;
};

/** Button styles: primary (accent fill, glow), secondary (outlined), ghost (text only), danger. */
class FaButton : public juce::Button
{
public:
    enum class Style { primary, secondary, ghost, danger };
    FaButton (const juce::String& text, Style style = Style::secondary,
              std::optional<icons::Id> icon = std::nullopt);
    void setStyle (Style s) { style = s; repaint(); }
    void setIcon (std::optional<icons::Id> i) { icon = i; repaint(); }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    /** Large hero variant (e.g. "Start Auto Tune"). */
    void setHero (bool h) { hero = h; repaint(); }
private:
    void buttonStateChanged() override;
    Style style;
    std::optional<icons::Id> icon;
    bool hero = false;
    anim::Value hoverAnim { *this, 0.0f, 16.0f };
};

/** Pill-shaped segmented selector. onChange gets the new index. */
class SegmentedControl : public juce::Component,
                         public juce::SettableTooltipClient
{
public:
    explicit SegmentedControl (juce::StringArray options = {});
    void setOptions (juce::StringArray options);
    void setSelected (int index, juce::NotificationType n = juce::dontSendNotification);
    int getSelected() const noexcept { return selected; }
    std::function<void (int)> onChange;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
private:
    juce::StringArray options;
    int selected = 0, hover = -1;
    float animPos = 0.0f;
    anim::Value slide { *this, 0.0f, 16.0f };
    int indexAt (juce::Point<int>) const;
    juce::Rectangle<float> segmentBounds (float index) const;
};

/** Toggle switch with label on the right. */
class ToggleSwitch : public juce::ToggleButton
{
public:
    explicit ToggleSwitch (const juce::String& label = {});
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
private:
    anim::Value knob { *this, 0.0f, 18.0f };
    bool hasPainted = false;   // first paint snaps the knob to the current state (no OFF->ON flash)
};

/** Numeric field: label, value, unit; drag vertically or type. Ranges and step are enforced. */
class ValueField : public juce::Component,
                   public juce::SettableTooltipClient
{
public:
    ValueField (const juce::String& label, const juce::String& unit, double min, double max,
                double step, int decimals);
    void setValue (double v, juce::NotificationType n = juce::dontSendNotification);
    double getValue() const noexcept { return value; }
    void setRange (double min, double max, double step);
    std::function<void (double)> onChange;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
private:
    void showEditor();
    void commitEditor (bool apply);
    juce::Rectangle<int> valueArea() const;
    juce::String label, unit;
    double value = 0, minV = 0, maxV = 1, step = 0.1, dragStart = 0;
    int decimals = 1;
    juce::TextEditor editor;
    bool hovering = false, dragging = false;
    float dragAccum = 0.0f;
};

/** Small status chip (e.g. "LIVE", "DEMO", "Reversed polarity"). */
class Chip : public juce::Component,
             public juce::SettableTooltipClient
{
public:
    Chip (const juce::String& text = {}, juce::Colour colour = theme::accent);
    void set (const juce::String& text, juce::Colour colour);
    int getIdealWidth() const;
    void paint (juce::Graphics&) override;
    std::function<void()> onClick;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
private:
    juce::String text;
    juce::Colour colour;
    bool hovering = false;
};

/** Horizontal level meter in dBFS (-90..0) with peak hold; call setLevel from a timer. */
class LevelMeter : public juce::Component
{
public:
    void setLevel (float dbfs);
    void paint (juce::Graphics&) override;
private:
    float level = -90.0f, peak = -90.0f;
    juce::uint32 peakTime = 0;
};

/** Thin animated progress bar (determinate 0..1, or indeterminate shimmer when < 0). */
class ProgressBarFa : public juce::Component, private juce::Timer
{
public:
    ProgressBarFa();
    void setProgress (double p) { target = p; }
    void paint (juce::Graphics&) override;
private:
    void timerCallback() override;
    double target = 0, shown = 0;
    float phase = 0;
};

/** Centred empty state: icon, title, one-line hint, optional action button. */
class EmptyState : public juce::Component
{
public:
    EmptyState (icons::Id icon, const juce::String& title, const juce::String& hint,
                const juce::String& actionText = {});
    std::function<void()> onAction;
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    icons::Id icon;
    juce::String title, hint;
    std::unique_ptr<FaButton> action;
};

/** Generic content for AppShell::showSheet: title, message, optional monospace step list with
    Copy, and up to two buttons. onResult(true/false) is called, then the shell closes the sheet. */
class MessageSheet : public juce::Component
{
public:
    MessageSheet (const juce::String& title, const juce::String& message, const juce::StringArray& steps,
                  const juce::String& okText, const juce::String& cancelText, icons::Id icon);
    std::function<void (bool)> onResult;
    void paint (juce::Graphics&) override;
    void resized() override;
    static int idealHeight (int numSteps);
private:
    juce::String title, message;
    icons::Id icon;
    juce::TextEditor stepsView;
    FaButton ok, cancel, copy;
};
} // namespace fa::ui
