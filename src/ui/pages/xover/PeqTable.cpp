#include "PeqTable.h"

namespace fa::ui
{
static constexpr int headerHeight = 44;
static constexpr int columnsHeight = 20;

namespace
{
    int typeIndex (FilterType t)
    {
        switch (t)
        {
            case FilterType::lowShelf:  return 1;
            case FilterType::highShelf: return 2;
            default:                    return 0;
        }
    }
    FilterType typeFor (int i)
    {
        return i == 1 ? FilterType::lowShelf : (i == 2 ? FilterType::highShelf : FilterType::peaking);
    }
}

//==============================================================================
class PeqTable::Row : public juce::Component
{
public:
    Row (PeqTable& o, int idx)
        : owner (o), index (idx),
          freq ("f", "Hz", 10.0, 24000.0, 1.0, 0),
          gain ("Gain", "dB", -15.0, 12.0, 0.1, 1),
          q ("Q", "", 0.4, 15.0, 0.05, 2),
          type ({ "PK", "LS", "HS" }),
          del ("", FaButton::Style::ghost, icons::Id::trash)
    {
        setRepaintsOnMouseActivity (true);
        for (auto* c : std::initializer_list<juce::Component*> { &freq, &gain, &q, &type, &enable, &del })
            addAndMakeVisible (c);

        freq.onChange = [this] (double v) { spec.freq = v; emit(); };
        gain.onChange = [this] (double v) { spec.gainDb = v; emit(); };
        q.onChange    = [this] (double v) { spec.q = v; emit(); };
        type.onChange = [this] (int i) { spec.type = typeFor (i); emit(); };
        enable.onClick = [this] { spec.enabled = enable.getToggleState(); emit(); };
        del.setTooltip ("Remove this band");
        del.onClick = [this] { if (owner.onBandDeleted) owner.onBandDeleted (index); };
    }

    void set (const FilterSpec& s, const DspCapabilities& caps, juce::Colour c, bool sel)
    {
        spec = s;
        colour = c;
        selected = sel;
        gain.setRange (caps.peqMinGainDb, caps.peqMaxGainDb, 0.1);
        q.setRange (caps.peqMinQ, caps.peqMaxQ, 0.05);
        freq.setValue (s.freq);
        gain.setValue (s.gainDb);
        q.setValue (s.q);
        type.setSelected (typeIndex (s.type));
        enable.setToggleState (s.enabled, juce::dontSendNotification);
        const float a = s.enabled ? 1.0f : 0.5f;
        freq.setAlpha (a); gain.setAlpha (a); q.setAlpha (a); type.setAlpha (a);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().reduced (4, 2).toFloat();
        if (selected)
        {
            g.setColour (theme::bg3);
            g.fillRoundedRectangle (r, theme::radiusSm);
            g.setColour (colour.withAlpha (0.5f));
            g.drawRoundedRectangle (r.reduced (0.5f), theme::radiusSm, 1.0f);
        }
        else if (isMouseOver (true))
        {
            g.setColour (theme::bg3.withAlpha (0.5f));
            g.fillRoundedRectangle (r, theme::radiusSm);
        }

        // index + colour dot
        auto idx = getLocalBounds().removeFromLeft (44).reduced (12, 0);
        auto dot = idx.removeFromLeft (8).withSizeKeepingCentre (8, 8).toFloat();
        g.setColour (spec.enabled ? colour : theme::textMuted);
        g.fillEllipse (dot);
        g.setColour (theme::textDim);
        g.setFont (theme::mono (11.0f));
        g.drawText (juce::String (index + 1), idx.withTrimmedLeft (4), juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 6);
        r.removeFromLeft (40);
        const int h = r.getHeight();
        const bool compact = getWidth() < 640;

        freq.setBounds (r.removeFromLeft (compact ? 140 : 168).withHeight (h));
        r.removeFromLeft (theme::grid);
        gain.setBounds (r.removeFromLeft (compact ? 130 : 156).withHeight (h));
        r.removeFromLeft (theme::grid);
        q.setBounds (r.removeFromLeft (compact ? 100 : 116).withHeight (h));
        r.removeFromLeft (theme::grid);
        type.setBounds (r.removeFromLeft (compact ? 112 : 132).withHeight (h));
        r.removeFromLeft (theme::grid);

        del.setBounds (r.removeFromRight (32).withHeight (h));
        r.removeFromRight (theme::grid);
        enable.setBounds (r.removeFromRight (48).withHeight (h));
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        if (owner.onBandSelected)
            owner.onBandSelected (index);
    }

private:
    void emit()
    {
        if (owner.onBandSelected && ! selected)
            owner.onBandSelected (index);
        if (owner.onBandChanged)
            owner.onBandChanged (index, spec);
    }

    PeqTable& owner;
    int index;
    FilterSpec spec;
    juce::Colour colour = theme::accent;
    bool selected = false;

    ValueField freq, gain, q;
    SegmentedControl type;
    ToggleSwitch enable { "" };
    FaButton del;
};

//==============================================================================
PeqTable::PeqTable()
{
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false, false, false);
    viewport.setScrollBarThickness (6);
    addAndMakeVisible (viewport);

    addButton.setComponentID ("xo.addband");
    addButton.onClick = [this] { if (onAddBand) onAddBand(); };
    addAndMakeVisible (addButton);

    resetButton.setTooltip ("Remove every band on this channel");
    resetButton.onClick = [this] { if (onResetAll) onResetAll(); };
    addAndMakeVisible (resetButton);
}

PeqTable::~PeqTable() = default;

void PeqTable::setBands (const std::vector<FilterSpec>& bands, const DspCapabilities& c, juce::Colour a, int selectedIndex)
{
    caps = c;
    accent = a;
    selected = selectedIndex;
    count = (int) bands.size();

    while (rows.size() > bands.size())
        rows.pop_back();
    while (rows.size() < bands.size())
    {
        rows.push_back (std::make_unique<Row> (*this, (int) rows.size()));
        content.addAndMakeVisible (*rows.back());
    }
    for (size_t i = 0; i < bands.size(); ++i)
        rows[i]->set (bands[i], caps, accent, (int) i == selected);

    addButton.setEnabled (count < caps.peqBandsPerChannel);
    addButton.setAlpha (count < caps.peqBandsPerChannel ? 1.0f : 0.5f);
    resetButton.setVisible (count > 0);
    layoutRows();
    repaint();
}

void PeqTable::setSelected (int index)
{
    selected = index;
    repaint();
}

void PeqTable::paint (juce::Graphics& g)
{
    theme::fillCard (g, getLocalBounds().toFloat(), theme::bg2, theme::border);

    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (theme::pad, 0);
    g.setColour (theme::text);
    g.setFont (theme::fontMedium (14.0f));
    g.drawText ("Parametric EQ", header, juce::Justification::centredLeft);
    const int tw = (int) std::ceil (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), "Parametric EQ"));
    g.setColour (count >= caps.peqBandsPerChannel ? theme::warn : theme::textMuted);
    g.setFont (theme::mono (11.0f));
    g.drawText (juce::String (count) + " / " + juce::String (caps.peqBandsPerChannel), header.withTrimmedLeft (tw + 10), juce::Justification::centredLeft);

    // column captions
    auto cols = getLocalBounds().withTrimmedTop (headerHeight).removeFromTop (columnsHeight).reduced (theme::pad, 0);
    g.setColour (theme::border);
    g.fillRect (1, headerHeight, getWidth() - 2, 1);
    g.setColour (theme::textMuted);
    g.setFont (theme::fontMedium (10.0f));
    const bool compact = getWidth() < 640;
    cols.removeFromLeft (40);
    g.drawText ("FREQUENCY", cols.removeFromLeft (compact ? 140 : 168), juce::Justification::centredLeft); cols.removeFromLeft (theme::grid);
    g.drawText ("GAIN", cols.removeFromLeft (compact ? 130 : 156), juce::Justification::centredLeft);        cols.removeFromLeft (theme::grid);
    g.drawText ("Q", cols.removeFromLeft (compact ? 100 : 116), juce::Justification::centredLeft);            cols.removeFromLeft (theme::grid);
    g.drawText ("TYPE", cols.removeFromLeft (compact ? 112 : 132), juce::Justification::centredLeft);
    cols.removeFromRight (32 + theme::grid);
    g.drawText ("ON", cols.removeFromRight (48), juce::Justification::centred);

    if (count == 0)
    {
        auto area = getLocalBounds().withTrimmedTop (headerHeight + columnsHeight).reduced (theme::pad);
        g.setColour (theme::textDim);
        g.setFont (theme::fontMedium (13.0f));
        g.drawText ("No EQ bands on this channel", area.withSizeKeepingCentre (area.getWidth(), 20).translated (0, -10), juce::Justification::centred);
        g.setColour (theme::textMuted);
        g.setFont (theme::font (11.5f));
        g.drawText ("Press Add band, or double-click the plot where you want one.", area.withSizeKeepingCentre (area.getWidth(), 16).translated (0, 12), juce::Justification::centred);
    }
}

void PeqTable::resized()
{
    auto r = getLocalBounds();
    auto header = r.removeFromTop (headerHeight).reduced (theme::pad, 7);
    addButton.setBounds (header.removeFromRight (116));
    header.removeFromRight (theme::grid);
    resetButton.setBounds (header.removeFromRight (96));

    r.removeFromTop (columnsHeight);
    viewport.setBounds (r.reduced (1, 0).withTrimmedBottom (4));
    layoutRows();
}

void PeqTable::layoutRows()
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
