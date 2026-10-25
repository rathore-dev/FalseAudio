#include "ChannelList.h"

namespace fa::ui
{
static constexpr int rowHeight = 58;
static constexpr int headerHeight = 44;

class ChannelList::Row : public juce::Component
{
public:
    explicit Row (ChannelList& o) : owner (o) { setRepaintsOnMouseActivity (true); }

    void set (const OutputChannel& ch, bool sel)
    {
        id = ch.id;
        label = ch.label.isNotEmpty() ? ch.label : ch.id;
        colour = theme::sideTint (theme::roleColour (ch.role), ch.side);
        side = ch.side == Side::left ? "L" : (ch.side == Side::right ? "R" : "M");
        muted = ch.settings.muted;
        selected = sel;

        juce::StringArray parts;
        const auto& s = ch.settings;
        if (s.highPass.enabled && s.highPass.freq > 10.5)
            parts.add ("HP " + theme::formatHz (s.highPass.freq, 0) + " " + juce::String (s.highPass.slopeDbPerOct));
        if (s.lowPass.enabled && s.lowPass.freq < 23000.0)
            parts.add ("LP " + theme::formatHz (s.lowPass.freq, 0) + " " + juce::String (s.lowPass.slopeDbPerOct));
        int active = 0;
        for (const auto& b : s.peq) if (b.enabled) ++active;
        parts.add (juce::String (active) + (active == 1 ? " band" : " bands"));
        summary = parts.joinIntoString (juce::String (juce::CharPointer_UTF8 ("  \xC2\xB7  ")));
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
        }
        else if (hover)
        {
            g.setColour (theme::bg3.withAlpha (0.55f));
            g.fillRoundedRectangle (r.toFloat().reduced (4.0f, 2.0f), theme::radiusSm);
        }

        // role stripe
        auto stripe = juce::Rectangle<float> (10.0f, 10.0f, 4.0f, (float) r.getHeight() - 20.0f);
        g.setColour (muted ? colour.withAlpha (0.35f) : colour);
        g.fillRoundedRectangle (stripe, 2.0f);
        if (selected && ! muted)
            theme::drawGlow (g, stripe, colour, 2.0f, 5.0f);

        auto inner = r.reduced (24, 9).withTrimmedLeft (0);

        // side badge on the right
        auto badge = inner.removeFromRight (22).withSizeKeepingCentre (22, 18).toFloat();
        g.setColour (colour.withAlpha (0.14f));
        g.fillRoundedRectangle (badge, 5.0f);
        g.setColour (colour);
        g.setFont (theme::fontBold (10.0f));
        g.drawText (side, badge, juce::Justification::centred);
        inner.removeFromRight (8);

        g.setColour (muted ? theme::textDim : theme::text);
        g.setFont (theme::fontMedium (13.0f));
        g.drawFittedText (label + (muted ? "  (muted)" : ""), inner.removeFromTop (18), juce::Justification::centredLeft, 1);

        inner.removeFromTop (3);
        g.setColour (theme::textDim);
        g.setFont (theme::mono (10.5f));
        g.drawFittedText (summary, inner, juce::Justification::centredLeft, 1);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (getLocalBounds().contains (e.getPosition()) && owner.onSelect)
            owner.onSelect (id);
    }

private:
    ChannelList& owner;
    juce::String id, label, side, summary;
    juce::Colour colour;
    bool selected = false, muted = false;
};

//==============================================================================
ChannelList::ChannelList()
{
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false, false, false);
    viewport.setScrollBarThickness (6);
    addAndMakeVisible (viewport);
}

ChannelList::~ChannelList() = default;

void ChannelList::setChannels (const SystemConfig& config, const juce::String& selectedId)
{
    const auto& chs = config.channels;
    count = (int) chs.size();

    while (rows.size() > chs.size())
        rows.pop_back();
    while (rows.size() < chs.size())
    {
        rows.push_back (std::make_unique<Row> (*this));
        content.addAndMakeVisible (*rows.back());
    }
    for (size_t i = 0; i < chs.size(); ++i)
        rows[i]->set (chs[i], chs[i].id == selectedId);

    layoutRows();
    repaint();
}

void ChannelList::paint (juce::Graphics& g)
{
    theme::fillCard (g, getLocalBounds().toFloat(), theme::bg2, theme::border);
    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (theme::pad, 0);
    g.setColour (theme::text);
    g.setFont (theme::fontMedium (14.0f));
    g.drawText ("Channels", header, juce::Justification::centredLeft);
    g.setColour (theme::textMuted);
    g.setFont (theme::mono (11.0f));
    g.drawText (juce::String (count), header, juce::Justification::centredRight);
    g.setColour (theme::border);
    g.fillRect (1, headerHeight, getWidth() - 2, 1);
}

void ChannelList::resized()
{
    viewport.setBounds (getLocalBounds().withTrimmedTop (headerHeight + 4).reduced (1, 0).withTrimmedBottom (4));
    layoutRows();
}

void ChannelList::layoutRows()
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
