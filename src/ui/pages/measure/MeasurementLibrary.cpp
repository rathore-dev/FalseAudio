#include "MeasurementLibrary.h"

namespace fa::ui
{
static constexpr int rowHeight = 60;
static constexpr int headerHeight = 44;

//==============================================================================
class MeasurementLibrary::Row : public juce::Component,
                                public juce::SettableTooltipClient
{
public:
    Row (MeasurementLibrary& o) : owner (o)
    {
        setRepaintsOnMouseActivity (true);
    }

    void set (const Item& i, bool sel)
    {
        item = i;
        selected = sel;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        const bool hover = isMouseOver();

        if (selected)
        {
            g.setColour (theme::bg3);
            g.fillRoundedRectangle (r.toFloat().reduced (4.0f, 2.0f), theme::radiusSm);
            g.setColour (item.colour);
            g.fillRoundedRectangle (juce::Rectangle<float> (4.0f, 10.0f, 3.0f, (float) r.getHeight() - 20.0f), 1.5f);
        }
        else if (hover)
        {
            g.setColour (theme::bg3.withAlpha (0.55f));
            g.fillRoundedRectangle (r.toFloat().reduced (4.0f, 2.0f), theme::radiusSm);
        }

        auto inner = r.reduced (14, 8);

        // colour chip
        auto chipArea = inner.removeFromLeft (14);
        auto chip = chipArea.withSizeKeepingCentre (10, 10).toFloat();
        g.setColour (item.visible ? item.colour : theme::textMuted);
        g.fillEllipse (chip);
        if (item.visible)
            theme::drawGlow (g, chip, item.colour, 5.0f, 5.0f);
        inner.removeFromLeft (10);

        // action buttons on the right
        auto actions = inner.removeFromRight (56);
        eyeRect   = actions.removeFromLeft (26).withSizeKeepingCentre (26, 26);
        actions.removeFromLeft (4);
        trashRect = actions.removeFromLeft (26).withSizeKeepingCentre (26, 26);

        const bool showActions = hover || selected;
        if (showActions)
        {
            auto drawAction = [&] (juce::Rectangle<int> rc, icons::Id id, juce::Colour c, bool hot)
            {
                if (hot)
                {
                    g.setColour (theme::bg4);
                    g.fillRoundedRectangle (rc.toFloat(), 6.0f);
                }
                icons::draw (g, id, rc.toFloat().reduced (6.0f), c);
            };
            const auto mouse = getMouseXYRelative();
            drawAction (eyeRect, item.visible ? icons::Id::eye : icons::Id::eyeOff,
                        item.visible ? theme::textDim : theme::textMuted, eyeRect.contains (mouse));
            drawAction (trashRect, icons::Id::trash,
                        trashRect.contains (mouse) ? theme::bad : theme::textMuted, trashRect.contains (mouse));
        }
        else if (! item.visible)
        {
            icons::draw (g, icons::Id::eyeOff, eyeRect.toFloat().reduced (6.0f), theme::textMuted.withAlpha (0.7f));
        }

        inner.removeFromRight (8);

        // line 1: name + time
        auto line1 = inner.removeFromTop (18);
        g.setFont (theme::font (11.0f));
        g.setColour (theme::textMuted);
        const auto timeText = item.time.formatted ("%H:%M");
        const int tw = (int) std::ceil (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), timeText)) + 4;
        g.drawText (timeText, line1.removeFromRight (tw), juce::Justification::centredRight);

        if (item.fromTune)
        {
            g.setFont (theme::fontBold (9.0f));
            auto tag = line1.removeFromRight (40).withSizeKeepingCentre (38, 16).toFloat();
            g.setColour (theme::accent.withAlpha (0.14f));
            g.fillRoundedRectangle (tag, 8.0f);
            g.setColour (theme::accent);
            g.drawText ("TUNE", tag, juce::Justification::centred);
            line1.removeFromRight (4);
        }

        g.setFont (theme::fontMedium (13.0f));
        g.setColour (item.visible ? theme::text : theme::textDim);
        g.drawFittedText (item.name, line1, juce::Justification::centredLeft, 1);

        // line 2: channel - mic - SNR
        inner.removeFromTop (2);
        auto line2 = inner.removeFromTop (16);
        g.setFont (theme::font (11.0f));
        g.setColour (theme::textDim);

        juce::String meta = item.channel;
        meta << "  \xC2\xB7  mic " << (item.micPosition + 1);
        g.drawFittedText (meta, line2.removeFromLeft (juce::jmax (60, line2.getWidth() - 72)), juce::Justification::centredLeft, 1);

        const auto snrColour = item.clipped ? theme::bad : (item.snrDb >= 40.0 ? theme::good : (item.snrDb >= 25.0 ? theme::warn : theme::bad));
        g.setColour (snrColour);
        g.setFont (theme::mono (11.0f));
        g.drawText (item.clipped ? "CLIP" : ("SNR " + juce::String (juce::roundToInt (item.snrDb))), line2, juce::Justification::centredRight);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! getLocalBounds().contains (e.getPosition()))
            return;

        if (eyeRect.contains (e.getPosition()))
        {
            if (owner.onVisibilityChanged) owner.onVisibilityChanged (item.id, ! item.visible);
            return;
        }
        if (trashRect.contains (e.getPosition()))
        {
            if (owner.onDelete) owner.onDelete (item.id);
            return;
        }
        if (owner.onSelect) owner.onSelect (item.id);
    }

    void mouseMove (const juce::MouseEvent&) override { repaint(); }

private:
    MeasurementLibrary& owner;
    Item item;
    bool selected = false;
    juce::Rectangle<int> eyeRect, trashRect;
};

//==============================================================================
MeasurementLibrary::MeasurementLibrary()
{
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false, false, false);
    viewport.setScrollBarThickness (6);
    addAndMakeVisible (viewport);

    addAndMakeVisible (clearButton);
    clearButton.onClick = [this] { if (onClearAll) onClearAll(); };
}

MeasurementLibrary::~MeasurementLibrary() = default;

void MeasurementLibrary::setItems (const std::vector<Item>& items, const juce::String& selectedId)
{
    count = (int) items.size();

    while (rows.size() > items.size())
        rows.pop_back();

    while (rows.size() < items.size())
    {
        rows.push_back (std::make_unique<Row> (*this));
        content.addAndMakeVisible (*rows.back());
    }

    for (size_t i = 0; i < items.size(); ++i)
        rows[i]->set (items[i], items[i].id == selectedId);

    clearButton.setVisible (count > 0);
    layoutRows();
    repaint();
}

void MeasurementLibrary::paint (juce::Graphics& g)
{
    theme::fillCard (g, getLocalBounds().toFloat(), theme::bg2, theme::border);

    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (theme::pad, 0);
    g.setColour (theme::text);
    g.setFont (theme::fontMedium (14.0f));
    g.drawText ("Library", header, juce::Justification::centredLeft);

    const auto titleW = (int) std::ceil (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), "Library"));
    g.setColour (theme::textMuted);
    g.setFont (theme::mono (11.0f));
    g.drawText (juce::String (count), header.withTrimmedLeft (titleW + 8), juce::Justification::centredLeft);

    g.setColour (theme::border);
    g.fillRect (1, headerHeight, getWidth() - 2, 1);

    if (count == 0)
    {
        auto area = getLocalBounds().withTrimmedTop (headerHeight).reduced (theme::pad);
        icons::draw (g, icons::Id::waveform, area.withSizeKeepingCentre (28, 28).translated (0, -24).toFloat(), theme::textMuted);
        g.setColour (theme::textDim);
        g.setFont (theme::fontMedium (13.0f));
        g.drawText ("No measurements yet", area.withSizeKeepingCentre (area.getWidth(), 20).translated (0, 8), juce::Justification::centred);
        g.setColour (theme::textMuted);
        g.setFont (theme::font (11.5f));
        g.drawFittedText ("Pick a feed and press Measure.\nAuto Tune sweeps land here too.",
                          area.withSizeKeepingCentre (area.getWidth(), 36).translated (0, 38), juce::Justification::centred, 2);
    }
}

void MeasurementLibrary::resized()
{
    auto r = getLocalBounds();
    auto header = r.removeFromTop (headerHeight);
    clearButton.setBounds (header.removeFromRight (84).reduced (8, 8));
    viewport.setBounds (r.reduced (1, 0).withTrimmedTop (4).withTrimmedBottom (4));
    layoutRows();
}

void MeasurementLibrary::layoutRows()
{
    const int w = viewport.getMaximumVisibleWidth();
    content.setSize (w, (int) rows.size() * rowHeight);
    int y = 0;
    for (auto& row : rows)
    {
        row->setBounds (0, y, w, rowHeight);
        y += rowHeight;
    }
}
} // namespace fa::ui
