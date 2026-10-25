#include "Widgets.h"
#include "../AppShell.h"

namespace fa::ui
{
using namespace juce;

namespace
{
    constexpr int cardHeaderHeight = 48;

    Colour dim (Colour c, bool enabled) { return enabled ? c : c.withMultipliedAlpha (0.4f); }
}

// =============================================================================
// Card
// =============================================================================
Card::Card (const String& t, const String& sub) : title (t), subtitle (sub)
{
    setPaintingIsUnclipped (false);
}

void Card::setTitle (const String& t, const String& sub)
{
    title = t;
    subtitle = sub;
    repaint();
}

Rectangle<int> Card::getContentArea() const
{
    auto r = getLocalBounds().reduced (theme::pad);
    if (title.isNotEmpty())
        r.removeFromTop (cardHeaderHeight - theme::pad);
    return r;
}

Rectangle<int> Card::getHeaderActionArea() const
{
    const int w = jmax (0, getWidth() / 2 - theme::pad);
    return { getWidth() - theme::pad - w, (cardHeaderHeight - theme::controlHeight) / 2, w, theme::controlHeight };
}

void Card::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const bool lit = ! highlight.isTransparent();

    if (lit)
        theme::drawGlow (g, r, highlight, theme::radius, 10.0f);

    theme::fillCard (g, r, theme::bg2, lit ? highlight.withAlpha (0.55f) : theme::border, theme::radius);

    if (lit)
    {
        // accent rail on the left edge
        g.saveState();
        Path clip;
        clip.addRoundedRectangle (r, theme::radius);
        g.reduceClipRegion (clip);
        g.setColour (highlight);
        g.fillRect (r.removeFromLeft (3.0f));
        g.restoreState();
    }

    if (title.isEmpty())
        return;

    auto header = getLocalBounds().withHeight (cardHeaderHeight).reduced (theme::pad, 0);
    header.removeFromRight (getHeaderActionArea().getWidth() + 8);

    const auto titleFont = theme::fontMedium (14.0f);
    g.setFont (titleFont);
    g.setColour (theme::text);
    const int titleW = (int) std::ceil (theme::textWidth (titleFont, title));
    g.drawText (title, header.removeFromLeft (jmin (titleW, header.getWidth())), Justification::centredLeft, true);

    if (subtitle.isNotEmpty() && header.getWidth() > 40)
    {
        header.removeFromLeft (10);
        g.setFont (theme::font (12.0f));
        g.setColour (theme::textMuted);
        g.drawText (subtitle, header, Justification::centredLeft, true);
    }
}

// =============================================================================
// FaButton
// =============================================================================
FaButton::FaButton (const String& text, Style s, std::optional<icons::Id> i)
    : Button (text), style (s), icon (i)
{
    setMouseCursor (MouseCursor::PointingHandCursor);
}

void FaButton::buttonStateChanged()
{
    hoverAnim.setTarget (isOver() || isDown() ? 1.0f : 0.0f);
}

void FaButton::paintButton (Graphics& g, bool highlighted, bool down)
{
    const bool enabled = isEnabled();
    const bool on = getToggleState();
    const float h = hoverAnim.get();
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    const float rad = hero ? 12.0f : jmin (theme::radiusSm + 1.0f, r.getHeight() * 0.5f);
    const Style s = (on && style == Style::ghost) ? Style::secondary : style;

    Colour fill, border, fg;
    switch (s)
    {
        case Style::primary:
            fill   = theme::accent.interpolatedWith (theme::accentHi, h * 0.6f);
            if (down) fill = theme::accentDim.interpolatedWith (theme::accent, 0.5f);
            border = fill.brighter (0.25f);
            fg     = theme::onAccent;
            break;
        case Style::secondary:
            fill   = on ? theme::accent.withAlpha (0.14f) : theme::bg3.interpolatedWith (theme::bg4, h);
            if (down) fill = theme::bg4.brighter (0.05f);
            border = on ? theme::accent.withAlpha (0.55f) : theme::border.interpolatedWith (theme::borderHi, h);
            fg     = on ? theme::accentHi : theme::text;
            break;
        case Style::ghost:
            fill   = Colours::white.withAlpha ((down ? 0.1f : 0.06f) * h);
            border = Colours::transparentBlack;
            fg     = theme::textDim.interpolatedWith (theme::text, h);
            break;
        case Style::danger:
            fill   = theme::bad.withAlpha (0.12f + 0.1f * h + (down ? 0.08f : 0.0f));
            border = theme::bad.withAlpha (0.45f + 0.25f * h);
            fg     = theme::bad.brighter (0.1f);
            break;
    }

    if (enabled && s == Style::primary)
        theme::drawGlow (g, r, theme::accent, rad, hero ? 14.0f + 6.0f * h : 6.0f + 4.0f * h);

    if (hero && s == Style::primary)
    {
        g.setGradientFill (ColourGradient (fill.brighter (0.1f), r.getX(), r.getY(), fill.darker (0.08f), r.getX(), r.getBottom(), false));
        g.setOpacity (enabled ? 1.0f : 0.4f);
        g.fillRoundedRectangle (r, rad);
    }
    else
    {
        g.setColour (dim (fill, enabled));
        g.fillRoundedRectangle (r, rad);
    }

    if (s != Style::ghost)
    {
        g.setColour (Colours::white.withAlpha (s == Style::primary ? 0.18f : 0.04f));
        g.drawRoundedRectangle (r.reduced (1.0f), rad - 1.0f, 1.0f);
        g.setColour (dim (border, enabled));
        g.drawRoundedRectangle (r, rad, 1.0f);
    }

    if (hasKeyboardFocus (false) && enabled)
        theme::drawFocusRing (g, r, rad);

    // content: icon + text centred
    const float textSize = hero ? 16.0f : jlimit (11.0f, 13.5f, r.getHeight() * 0.42f);
    const auto font = hero ? theme::fontBold (textSize) : theme::fontMedium (textSize);
    const float iconSize = hero ? 20.0f : 15.0f;
    const float gap = 8.0f;
    const String label = getButtonText();
    const float textW = label.isEmpty() ? 0.0f : theme::textWidth (font, label);
    float totalW = textW + (icon.has_value() ? iconSize + (label.isEmpty() ? 0.0f : gap) : 0.0f);
    const float maxW = r.getWidth() - (hero ? 32.0f : 16.0f);
    totalW = jmin (totalW, maxW);

    float x = r.getCentreX() - totalW * 0.5f;
    const Colour contentColour = dim (fg, enabled);

    if (icon.has_value())
    {
        icons::draw (g, *icon, Rectangle<float> (x, r.getCentreY() - iconSize * 0.5f, iconSize, iconSize), contentColour);
        x += iconSize + (label.isEmpty() ? 0.0f : gap);
    }

    if (label.isNotEmpty())
    {
        g.setColour (contentColour);
        g.setFont (font);
        g.drawText (label, Rectangle<float> (x, r.getY(), jmax (0.0f, r.getRight() - x - (hero ? 16.0f : 8.0f)), r.getHeight()),
                    Justification::centredLeft, true);
    }

    ignoreUnused (highlighted);
}

// =============================================================================
// SegmentedControl
// =============================================================================
SegmentedControl::SegmentedControl (StringArray opts) : options (std::move (opts))
{
    setMouseCursor (MouseCursor::PointingHandCursor);
    setRepaintsOnMouseActivity (true);
}

void SegmentedControl::setOptions (StringArray opts)
{
    options = std::move (opts);
    selected = jlimit (0, jmax (0, options.size() - 1), selected);
    slide.snap ((float) selected);
    repaint();
}

void SegmentedControl::setSelected (int index, NotificationType n)
{
    index = jlimit (0, jmax (0, options.size() - 1), index);
    const bool changed = index != selected;
    selected = index;
    slide.setTarget ((float) selected);
    repaint();
    if (changed && n != dontSendNotification && onChange)
        onChange (selected);
}

Rectangle<float> SegmentedControl::segmentBounds (float index) const
{
    auto r = getLocalBounds().toFloat().reduced (3.0f);
    if (options.isEmpty())
        return r;
    const float w = r.getWidth() / (float) options.size();
    return { r.getX() + w * index, r.getY(), w, r.getHeight() };
}

int SegmentedControl::indexAt (Point<int> p) const
{
    if (options.isEmpty() || getWidth() <= 0)
        return -1;
    auto r = getLocalBounds().reduced (3, 0);
    if (! getLocalBounds().contains (p))
        return -1;
    const float w = (float) r.getWidth() / (float) options.size();
    return jlimit (0, options.size() - 1, (int) std::floor ((float) (p.x - r.getX()) / w));
}

void SegmentedControl::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const bool enabled = isEnabled();
    theme::fillPill (g, r.reduced (0.5f), dim (theme::bg1, enabled), dim (theme::border, enabled));

    if (options.isEmpty())
        return;

    animPos = slide.get();
    auto thumb = segmentBounds (animPos);
    if (enabled)
        theme::drawGlow (g, thumb, theme::accent, thumb.getHeight() * 0.5f, 4.0f);
    theme::fillPill (g, thumb, dim (theme::bg4, enabled), dim (theme::accent.withAlpha (0.5f), enabled));
    g.setColour (Colours::white.withAlpha (0.04f));
    g.drawRoundedRectangle (thumb.reduced (1.0f), thumb.getHeight() * 0.5f - 1.0f, 1.0f);

    g.setFont (theme::fontMedium (jlimit (11.0f, 12.5f, r.getHeight() * 0.4f)));
    for (int i = 0; i < options.size(); ++i)
    {
        const float closeness = jlimit (0.0f, 1.0f, 1.0f - std::abs (animPos - (float) i));
        Colour c = theme::textDim.interpolatedWith (theme::accentHi, closeness);
        if (i == hover && i != selected)
            c = theme::text;
        g.setColour (dim (c, enabled));
        g.drawText (options[i], segmentBounds ((float) i).toNearestInt(), Justification::centred, true);
    }

    if (hasKeyboardFocus (false) && enabled)
        theme::drawFocusRing (g, r.reduced (0.5f), r.getHeight() * 0.5f);
}

void SegmentedControl::mouseDown (const MouseEvent& e)
{
    if (! isEnabled())
        return;
    const int i = indexAt (e.getPosition());
    if (i >= 0)
        setSelected (i, sendNotificationSync);
}

void SegmentedControl::mouseMove (const MouseEvent& e)
{
    const int i = indexAt (e.getPosition());
    if (i != hover)
    {
        hover = i;
        repaint();
    }
}

void SegmentedControl::mouseExit (const MouseEvent&)
{
    hover = -1;
    repaint();
}

// =============================================================================
// ToggleSwitch
// =============================================================================
ToggleSwitch::ToggleSwitch (const String& label) : ToggleButton (label)
{
    setMouseCursor (MouseCursor::PointingHandCursor);
}

void ToggleSwitch::paintButton (Graphics& g, bool highlighted, bool down)
{
    const bool on = getToggleState();
    const bool enabled = isEnabled();
    knob.setTarget (on ? 1.0f : 0.0f);
    const float t = knob.get();

    const float trackW = 36.0f, trackH = 20.0f;
    auto r = getLocalBounds().toFloat();
    Rectangle<float> track (1.0f, r.getCentreY() - trackH * 0.5f, trackW, trackH);

    const Colour trackOff = highlighted ? theme::bg4 : theme::bg3;
    const Colour trackOn  = theme::accent;
    const Colour trackCol = trackOff.interpolatedWith (trackOn, t);

    if (enabled && t > 0.01f)
        theme::drawGlow (g, track, theme::accent.withAlpha (t), trackH * 0.5f, 5.0f);

    theme::fillPill (g, track, dim (trackCol, enabled),
                     dim (theme::borderHi.interpolatedWith (theme::accentHi, t), enabled));

    const float knobR = 7.0f;
    const float kx = track.getX() + 3.0f + knobR + t * (trackW - 6.0f - knobR * 2.0f);
    const float squeeze = down ? 1.5f : 0.0f;
    Rectangle<float> k (kx - knobR - squeeze, track.getCentreY() - knobR, knobR * 2.0f + squeeze * 2.0f, knobR * 2.0f);
    g.setColour (dim (theme::textDim.interpolatedWith (Colours::white, t), enabled));
    g.fillRoundedRectangle (k, knobR);

    g.setColour (dim (theme::text, enabled));
    g.setFont (theme::font (13.0f));
    g.drawFittedText (getButtonText(), getLocalBounds().withTrimmedLeft ((int) trackW + 12), Justification::centredLeft, 2);

    if (hasKeyboardFocus (false) && enabled)
        theme::drawFocusRing (g, track, trackH * 0.5f);
}

// =============================================================================
// ValueField
// =============================================================================
ValueField::ValueField (const String& l, const String& u, double min, double max, double st, int dec)
    : label (l), unit (u), minV (min), maxV (max), step (st), decimals (dec)
{
    value = jlimit (minV, maxV, 0.0);
    setMouseCursor (MouseCursor::UpDownResizeCursor);
    setWantsKeyboardFocus (true);

    editor.setFont (theme::mono (15.0f));
    editor.setJustification (Justification::centredLeft);
    editor.setIndents (6, 0);
    editor.setBorder (BorderSize<int> (0));
    editor.setSelectAllWhenFocused (true);
    editor.setInputRestrictions (16, "0123456789.-+,");
    editor.setColour (TextEditor::backgroundColourId, Colours::transparentBlack);
    editor.setColour (TextEditor::outlineColourId, Colours::transparentBlack);
    editor.setColour (TextEditor::focusedOutlineColourId, Colours::transparentBlack);
    editor.onReturnKey = [this] { commitEditor (true); };
    editor.onEscapeKey = [this] { commitEditor (false); };
    editor.onFocusLost = [this] { commitEditor (true); };
    addChildComponent (editor);
}

void ValueField::setValue (double v, NotificationType n)
{
    if (step > 0.0)
        v = minV + std::round ((v - minV) / step) * step;
    v = jlimit (minV, maxV, v);
    const bool changed = ! approximatelyEqual (v, value);
    value = v;
    repaint();
    if (changed && n != dontSendNotification && onChange)
        onChange (value);
}

void ValueField::setRange (double min, double max, double st)
{
    minV = min;
    maxV = max;
    step = st;
    setValue (value);
}

Rectangle<int> ValueField::valueArea() const
{
    auto r = getLocalBounds();
    if (getHeight() >= 44)
    {
        r.removeFromTop (18);
        return r.reduced (1);
    }
    return r.reduced (1);
}

void ValueField::resized()
{
    auto va = valueArea();
    const int unitW = unit.isEmpty() ? 0 : (int) std::ceil (theme::textWidth (theme::font (11.0f), unit)) + 10;
    editor.setBounds (va.withTrimmedRight (unitW).reduced (4, 2));
}

void ValueField::paint (Graphics& g)
{
    const bool enabled = isEnabled();
    const bool tall = getHeight() >= 44;
    auto r = getLocalBounds().toFloat();
    const bool active = editor.isVisible() || dragging;

    theme::fillCard (g, r, dim (active ? theme::bg2 : theme::bg1, enabled),
                     dim (active ? theme::accent : (hovering ? theme::borderHi : theme::border), enabled), theme::radiusSm);

    auto va = valueArea();
    const auto valueFont = theme::mono (tall ? 15.0f : 13.0f);
    const auto unitFont = theme::font (11.0f);
    const String valueText = String (value, decimals);

    if (tall)
    {
        theme::drawEyebrow (g, label, getLocalBounds().withHeight (18).reduced (10, 0).translated (0, 4), dim (theme::textMuted, enabled));
    }
    else
    {
        g.setFont (theme::font (12.0f));
        g.setColour (dim (theme::textDim, enabled));
        auto labelArea = va.removeFromLeft (jmax (0, va.getWidth() / 2)).reduced (8, 0);
        g.drawText (label, labelArea, Justification::centredLeft, true);
    }

    if (editor.isVisible())
        return;

    auto area = va.reduced (10, 0);
    if (unit.isNotEmpty())
    {
        const int unitW = (int) std::ceil (theme::textWidth (unitFont, unit));
        g.setFont (unitFont);
        g.setColour (dim (theme::textMuted, enabled));
        g.drawText (unit, area.removeFromRight (unitW), Justification::centredRight, false);
        area.removeFromRight (6);
    }

    g.setFont (valueFont);
    g.setColour (dim (active ? theme::accentHi : theme::text, enabled));
    g.drawText (valueText, area, tall ? Justification::centredLeft : Justification::centredRight, true);

    if (hasKeyboardFocus (false) && enabled && ! editor.isVisible())
        theme::drawFocusRing (g, r.reduced (1.0f), theme::radiusSm);
}

void ValueField::mouseDown (const MouseEvent& e)
{
    if (! isEnabled() || editor.isVisible())
        return;
    dragStart = value;
    dragAccum = 0.0f;
    dragging = true;
    ignoreUnused (e);
    repaint();
}

void ValueField::mouseDrag (const MouseEvent& e)
{
    if (! isEnabled() || editor.isVisible())
        return;
    // 4 px per step; shift = fine (1/10 step); ctrl = coarse (10 steps)
    double perStep = 4.0;
    double mult = 1.0;
    if (e.mods.isShiftDown()) mult = 0.1;
    if (e.mods.isCtrlDown())  mult = 10.0;
    const double steps = std::round (-(double) e.getDistanceFromDragStartY() / perStep);
    setValue (dragStart + steps * step * mult, sendNotificationSync);
}

void ValueField::mouseUp (const MouseEvent& e)
{
    const bool wasClick = dragging && e.mouseWasClicked();
    dragging = false;
    repaint();
    if (wasClick && isEnabled())
        grabKeyboardFocus();
}

void ValueField::mouseDoubleClick (const MouseEvent&)
{
    if (isEnabled())
        showEditor();
}

void ValueField::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& wheel)
{
    if (! isEnabled() || editor.isVisible())
        return;
    const double dir = wheel.deltaY > 0 ? 1.0 : (wheel.deltaY < 0 ? -1.0 : 0.0);
    if (dir == 0.0)
        return;
    const double mult = e.mods.isShiftDown() ? 0.1 : (e.mods.isCtrlDown() ? 10.0 : 1.0);
    setValue (value + dir * step * mult, sendNotificationSync);
}

void ValueField::mouseEnter (const MouseEvent&) { hovering = true;  repaint(); }
void ValueField::mouseExit (const MouseEvent&)  { hovering = false; repaint(); }

void ValueField::showEditor()
{
    editor.setText (String (value, decimals), dontSendNotification);
    editor.setVisible (true);
    editor.grabKeyboardFocus();
    editor.selectAll();
    repaint();
}

void ValueField::commitEditor (bool apply)
{
    if (! editor.isVisible())
        return;
    if (apply)
    {
        const String t = editor.getText().replace (",", ".").trim();
        if (t.isNotEmpty())
            setValue (t.getDoubleValue(), sendNotificationSync);
    }
    editor.setVisible (false);
    repaint();
}

// =============================================================================
// Chip
// =============================================================================
Chip::Chip (const String& t, Colour c) : text (t), colour (c) {}

void Chip::set (const String& t, Colour c)
{
    text = t;
    colour = c;
    repaint();
}

int Chip::getIdealWidth() const
{
    return (int) std::ceil (theme::textWidth (theme::fontMedium (11.0f), text)) + 30;
}

void Chip::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const float h = jmin (r.getHeight(), 24.0f);
    r = r.withSizeKeepingCentre (r.getWidth(), h);
    const bool clickable = onClick != nullptr;
    const float boost = (clickable && hovering) ? 0.08f : 0.0f;

    theme::fillPill (g, r.reduced (0.5f), colour.withAlpha (0.13f + boost), colour.withAlpha (0.45f + boost * 2.0f));

    auto inner = r.reduced (9.0f, 0.0f);
    const float dot = 6.0f;
    g.setColour (colour);
    g.fillEllipse (inner.getX(), inner.getCentreY() - dot * 0.5f, dot, dot);
    inner.removeFromLeft (dot + 6.0f);

    g.setFont (theme::fontMedium (11.0f));
    g.setColour (colour.brighter (0.25f));
    g.drawText (text, inner.toNearestInt(), Justification::centredLeft, true);
}

void Chip::mouseUp (const MouseEvent& e)
{
    if (onClick && e.mouseWasClicked() && getLocalBounds().contains (e.getPosition()))
        onClick();
}

void Chip::mouseEnter (const MouseEvent&)
{
    hovering = true;
    setMouseCursor (onClick ? MouseCursor::PointingHandCursor : MouseCursor::NormalCursor);
    repaint();
}

void Chip::mouseExit (const MouseEvent&)
{
    hovering = false;
    repaint();
}

// =============================================================================
// LevelMeter
// =============================================================================
void LevelMeter::setLevel (float dbfs)
{
    dbfs = jlimit (-90.0f, 0.0f, dbfs);
    const auto now = Time::getMillisecondCounter();
    if (dbfs >= peak)
    {
        peak = dbfs;
        peakTime = now;
    }
    else if (now - peakTime > 1200)
    {
        peak = jmax (dbfs, peak - 0.6f);  // gentle fall after hold
    }
    // ballistic release so the bar doesn't flicker
    level = dbfs > level ? dbfs : jmax (dbfs, level - 1.5f);
    repaint();
}

void LevelMeter::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const float rad = jmin (3.0f, r.getHeight() * 0.5f);
    auto toX = [&] (float db) { return r.getX() + (jlimit (-90.0f, 0.0f, db) + 90.0f) / 90.0f * r.getWidth(); };

    g.setColour (theme::bg1);
    g.fillRoundedRectangle (r, rad);

    // zone ticks
    g.setColour (theme::border);
    for (float db : { -60.0f, -40.0f, -20.0f, -12.0f, -6.0f, -3.0f })
        g.fillRect (toX (db), r.getY() + 1.0f, 1.0f, r.getHeight() - 2.0f);

    const float fillW = toX (level) - r.getX();
    if (fillW > 0.5f)
    {
        g.saveState();
        g.reduceClipRegion (Rectangle<float> (r.getX(), r.getY(), fillW, r.getHeight()).toNearestInt());
        ColourGradient grad (theme::good, r.getX(), 0.0f, theme::bad, r.getRight(), 0.0f, false);
        grad.addColour (0.80, theme::good);
        grad.addColour (0.87, theme::warn);
        grad.addColour (0.96, theme::bad);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (r, rad);
        g.restoreState();
    }

    if (peak > -89.0f)
    {
        const Colour pc = peak > -3.0f ? theme::bad : (peak > -12.0f ? theme::warn : theme::text);
        g.setColour (pc);
        g.fillRect (toX (peak) - 1.0f, r.getY(), 2.0f, r.getHeight());
    }

    g.setColour (theme::border);
    g.drawRoundedRectangle (r.reduced (0.5f), rad, 1.0f);
}

// =============================================================================
// ProgressBarFa
// =============================================================================
ProgressBarFa::ProgressBarFa()
{
    startTimerHz (60);
}

void ProgressBarFa::timerCallback()
{
    bool dirty = false;
    if (target < 0.0)
    {
        phase += 0.012f;
        if (phase > 1.0f)
            phase -= 1.0f;
        dirty = true;
    }
    else
    {
        const double next = shown + (target - shown) * 0.18;
        if (std::abs (next - shown) > 0.0005)
        {
            shown = next;
            dirty = true;
        }
        else if (! approximatelyEqual (shown, target))
        {
            shown = target;
            dirty = true;
        }
    }
    if (dirty && isShowing())
        repaint();
}

void ProgressBarFa::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const float rad = r.getHeight() * 0.5f;
    g.setColour (theme::bg4);
    g.fillRoundedRectangle (r, rad);

    if (target < 0.0)
    {
        const float bandW = jmax (24.0f, r.getWidth() * 0.28f);
        const float x = r.getX() - bandW + phase * (r.getWidth() + bandW);
        g.saveState();
        g.reduceClipRegion (r.toNearestInt());
        ColourGradient band (theme::accent.withAlpha (0.0f), x, 0.0f, theme::accent.withAlpha (0.0f), x + bandW, 0.0f, false);
        band.addColour (0.5, theme::accent);
        g.setGradientFill (band);
        g.fillRoundedRectangle (Rectangle<float> (x, r.getY(), bandW, r.getHeight()), rad);
        g.restoreState();
        return;
    }

    const float w = jlimit (0.0f, 1.0f, (float) shown) * r.getWidth();
    if (w > 0.5f)
    {
        auto fill = r.withWidth (jmax (r.getHeight(), w));
        theme::drawGlow (g, fill, theme::accent, rad, 3.0f);
        g.setGradientFill (ColourGradient (theme::accentDim.interpolatedWith (theme::accent, 0.6f), fill.getX(), 0.0f,
                                           theme::accentHi, fill.getRight(), 0.0f, false));
        g.fillRoundedRectangle (fill, rad);
    }
}

// =============================================================================
// EmptyState
// =============================================================================
EmptyState::EmptyState (icons::Id i, const String& t, const String& h, const String& actionText)
    : icon (i), title (t), hint (h)
{
    if (actionText.isNotEmpty())
    {
        action = std::make_unique<FaButton> (actionText, FaButton::Style::secondary);
        action->onClick = [this] { if (onAction) onAction(); };
        addAndMakeVisible (*action);
    }
}

void EmptyState::resized()
{
    if (action != nullptr)
    {
        const int w = jmin (getWidth() - 32, jmax (120, (int) std::ceil (theme::textWidth (theme::fontMedium (13.0f), action->getButtonText())) + 40));
        const int total = 64 + 12 + 22 + 6 + 36 + 20 + theme::controlHeight;
        const int top = jmax (0, (getHeight() - total) / 2);
        action->setBounds ((getWidth() - w) / 2, top + total - theme::controlHeight, w, theme::controlHeight);
    }
}

void EmptyState::paint (Graphics& g)
{
    const int total = 64 + 12 + 22 + 6 + 36 + (action != nullptr ? 20 + theme::controlHeight : 0);
    int y = jmax (0, (getHeight() - total) / 2);
    const int cx = getWidth() / 2;

    Rectangle<float> tile ((float) cx - 32.0f, (float) y, 64.0f, 64.0f);
    g.setColour (theme::bg3);
    g.fillRoundedRectangle (tile, 18.0f);
    g.setColour (theme::border);
    g.drawRoundedRectangle (tile.reduced (0.5f), 18.0f, 1.0f);
    icons::draw (g, icon, tile.reduced (19.0f), theme::textDim);
    y += 64 + 12;

    g.setColour (theme::text);
    g.setFont (theme::fontMedium (15.0f));
    g.drawFittedText (title, getLocalBounds().withY (y).withHeight (22).reduced (16, 0), Justification::centred, 1);
    y += 22 + 6;

    g.setColour (theme::textDim);
    g.setFont (theme::font (13.0f));
    g.drawFittedText (hint, Rectangle<int> (cx - jmin (220, getWidth() / 2 - 16), y, jmin (440, getWidth() - 32), 36),
                      Justification::centredTop, 2, 0.9f);
}

// =============================================================================
// MessageSheet
// =============================================================================
namespace
{
    constexpr int sheetPad = 24;
    constexpr int sheetIconTile = 44;
    constexpr int sheetButtonH = 36;
    constexpr int sheetStepLine = 19;
    constexpr int sheetMessageH = 44;

    int stepsBoxHeight (int numSteps)
    {
        return numSteps > 0 ? jlimit (1, 9, numSteps) * sheetStepLine + 20 : 0;
    }
}

MessageSheet::MessageSheet (const String& t, const String& m, const StringArray& steps, const String& okText,
                            const String& cancelText, icons::Id i)
    : title (t), message (m), icon (i),
      ok (okText.isNotEmpty() ? okText : "OK", FaButton::Style::primary),
      cancel (cancelText, FaButton::Style::ghost),
      copy ("Copy", FaButton::Style::ghost, icons::Id::copy)
{
    addAndMakeVisible (ok);
    addChildComponent (cancel);
    cancel.setVisible (cancelText.isNotEmpty());

    stepsView.setMultiLine (true, false);
    stepsView.setReadOnly (true);
    stepsView.setCaretVisible (false);
    stepsView.setScrollbarsShown (true);
    stepsView.setFont (theme::mono (12.5f));
    stepsView.setIndents (10, 8);
    stepsView.setBorder (BorderSize<int> (1));
    stepsView.setColour (TextEditor::backgroundColourId, theme::bg1);
    stepsView.setColour (TextEditor::textColourId, theme::text);
    stepsView.setColour (TextEditor::outlineColourId, theme::border);
    stepsView.setColour (TextEditor::focusedOutlineColourId, theme::border);

    String joined;
    for (int k = 0; k < steps.size(); ++k)
        joined << String (k + 1).paddedLeft (' ', 2) << ".  " << steps[k] << (k + 1 < steps.size() ? "\n" : "");
    stepsView.setText (joined, dontSendNotification);
    stepsView.setVisible (steps.size() > 0);
    addChildComponent (stepsView);

    copy.setVisible (steps.size() > 0);
    addChildComponent (copy);

    auto finish = [this] (bool result)
    {
        Component::SafePointer<MessageSheet> safe (this);
        auto* shell = findParentComponentOfClass<AppShell>();
        if (onResult)
            onResult (result);
        // The page may already have closed (and destroyed) us; only close if we're still on screen.
        if (safe != nullptr && shell != nullptr && safe->isShowing())
            shell->closeSheet();
    };

    ok.onClick     = [finish] { finish (true); };
    cancel.onClick = [finish] { finish (false); };
    copy.onClick   = [this]
    {
        SystemClipboard::copyTextToClipboard (stepsView.getText());
        if (auto* shell = findParentComponentOfClass<AppShell>())
            shell->toast ("Steps copied to clipboard", AppShell::ToastKind::good);
    };

    setWantsKeyboardFocus (true);
}

int MessageSheet::idealHeight (int numSteps)
{
    int h = sheetPad + sheetIconTile + 16 + sheetMessageH;
    if (numSteps > 0)
        h += 12 + 24 + 6 + stepsBoxHeight (numSteps);
    h += 24 + sheetButtonH + sheetPad;
    return h;
}

void MessageSheet::resized()
{
    auto r = getLocalBounds().reduced (sheetPad);

    auto buttons = r.removeFromBottom (sheetButtonH);
    const int okW = jmax (96, (int) std::ceil (theme::textWidth (theme::fontMedium (13.0f), ok.getButtonText())) + 36);
    ok.setBounds (buttons.removeFromRight (okW));
    if (cancel.isVisible())
    {
        buttons.removeFromRight (8);
        const int cW = jmax (80, (int) std::ceil (theme::textWidth (theme::fontMedium (13.0f), cancel.getButtonText())) + 32);
        cancel.setBounds (buttons.removeFromRight (cW));
    }

    if (stepsView.isVisible())
    {
        r.removeFromBottom (24);
        const int stepsTop = r.getY() + sheetIconTile + 16 + sheetMessageH + 12 + 30;
        auto steps = Rectangle<int> (r.getX(), stepsTop, r.getWidth(), jmax (40, r.getBottom() - stepsTop));
        stepsView.setBounds (steps);
        auto header = Rectangle<int> (r.getX(), steps.getY() - 30, r.getWidth(), 24);
        const int copyW = (int) std::ceil (theme::textWidth (theme::fontMedium (12.0f), "Copy")) + 44;
        copy.setBounds (header.removeFromRight (copyW).withHeight (26).withY (header.getY() - 1));
    }
}

void MessageSheet::paint (Graphics& g)
{
    auto r = getLocalBounds().reduced (sheetPad);

    Rectangle<float> tile (r.toFloat().getX(), r.toFloat().getY(), (float) sheetIconTile, (float) sheetIconTile);
    g.setColour (theme::accent.withAlpha (0.12f));
    g.fillRoundedRectangle (tile, 12.0f);
    g.setColour (theme::accent.withAlpha (0.35f));
    g.drawRoundedRectangle (tile.reduced (0.5f), 12.0f, 1.0f);
    icons::draw (g, icon, tile.reduced (11.0f), theme::accent);

    auto titleArea = Rectangle<int> (r.getX() + sheetIconTile + 16, r.getY(), r.getWidth() - sheetIconTile - 16, sheetIconTile);
    g.setColour (theme::text);
    g.setFont (theme::display (18.0f));
    g.drawFittedText (title, titleArea, Justification::centredLeft, 2);

    auto msgArea = Rectangle<int> (r.getX(), r.getY() + sheetIconTile + 16, r.getWidth(), sheetMessageH);
    if (stepsView.isVisible())
        msgArea.setBottom (stepsView.getY() - 30 - 8);
    else
        msgArea.setBottom (ok.getY() - 24);

    g.setColour (theme::textDim);
    g.setFont (theme::font (13.5f));
    g.drawFittedText (message, msgArea, Justification::topLeft, jmax (1, msgArea.getHeight() / 18), 0.95f);

    if (stepsView.isVisible())
        theme::drawEyebrow (g, "Steps", Rectangle<int> (r.getX(), stepsView.getY() - 30, r.getWidth() / 2, 24), theme::textMuted);
}
} // namespace fa::ui
