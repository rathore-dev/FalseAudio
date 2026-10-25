#include "NavRail.h"

namespace fa::ui
{
using namespace juce;

namespace
{
    constexpr int logoAreaHeight = 72;
    constexpr int footerHeight   = 26;
    constexpr int pillInset      = 8;
}

/** One rail entry. Paints only its icon and label; the rail paints the gliding pill behind. */
class NavRail::NavButton : public Button
{
public:
    NavButton (NavRail& r, const Item& i, int idx) : Button (i.label), rail (r), item (i), index (idx)
    {
        setMouseCursor (MouseCursor::PointingHandCursor);
        setTooltip (i.tooltip.isNotEmpty() ? i.tooltip : i.label);
        setClickingTogglesState (false);
    }

    void setCompact (bool c) { compact = c; repaint(); }

    void paintButton (Graphics& g, bool highlighted, bool down) override
    {
        const bool selected = rail.selectedId == item.id;
        const auto r = getLocalBounds().toFloat();

        if ((highlighted || down) && ! selected)
        {
            g.setColour (Colours::white.withAlpha (down ? 0.06f : 0.035f));
            g.fillRoundedRectangle (r.reduced ((float) pillInset, 3.0f), 12.0f);
        }

        const Colour c = selected ? theme::accent : (highlighted ? theme::text : theme::textDim);
        if (compact)
        {
            const float s = 20.0f;
            icons::draw (g, item.icon, Rectangle<float> (r.getCentreX() - s * 0.5f, r.getCentreY() - s * 0.5f, s, s), c);
        }
        else
        {
            const float s = 22.0f;
            const float top = r.getY() + (r.getHeight() - (s + 3.0f + 13.0f)) * 0.5f;
            icons::draw (g, item.icon, Rectangle<float> (r.getCentreX() - s * 0.5f, top, s, s), c);
            g.setColour (c);
            g.setFont (theme::fontMedium (10.0f));
            g.drawFittedText (item.label, Rectangle<int> (2, (int) (top + s + 2.0f), getWidth() - 4, 14), Justification::centred, 1, 0.8f);
        }

        if (hasKeyboardFocus (false))
            theme::drawFocusRing (g, r.reduced ((float) pillInset, 3.0f), 12.0f);
    }

    void clicked() override
    {
        rail.setSelected (item.id);
        if (rail.onSelect)
            rail.onSelect (item.id);
    }

    NavRail& rail;
    Item item;
    int index;
    bool compact = false;
};

NavRail::NavRail()
{
    setWantsKeyboardFocus (false);
}

NavRail::~NavRail() = default;

void NavRail::setItems (std::vector<Item> newItems)
{
    buttons.clear();
    items = std::move (newItems);
    for (size_t i = 0; i < items.size(); ++i)
    {
        auto b = std::make_unique<NavButton> (*this, items[i], (int) i);
        addAndMakeVisible (*b);
        buttons.push_back (std::move (b));
    }
    indicator.snap ((float) jmax (0, indexOf (selectedId)));
    resized();
}

int NavRail::indexOf (PageId id) const
{
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i].id == id)
            return (int) i;
    return -1;
}

void NavRail::setSelected (PageId id, bool animate)
{
    selectedId = id;
    const int idx = jmax (0, indexOf (id));
    if (animate) indicator.setTarget ((float) idx);
    else         indicator.snap ((float) idx);
    repaint();
}

NavRail::Layout NavRail::computeLayout() const
{
    Layout l;
    const int n = (int) items.size();
    l.itemY.assign ((size_t) n, 0);
    l.sectionY.assign ((size_t) n, -1);
    if (n == 0)
        return l;

    int numSections = 0;
    String last;
    for (auto& it : items)
        if (it.section.isNotEmpty() && it.section != last) { ++numSections; last = it.section; }

    const int available = getHeight() - logoAreaHeight - footerHeight;

    // Prefer full icon+label items; shrink, then fall back to compact icon-only.
    int itemH = (available - numSections * 22) / n;
    if (itemH >= 48)
    {
        l.itemHeight = jmin (theme::navItemHeight, itemH);
        l.sectionHeight = 22;
        l.compact = false;
    }
    else
    {
        l.compact = true;
        l.sectionHeight = 14;
        l.itemHeight = jlimit (36, 48, (available - numSections * l.sectionHeight) / n);
    }

    int y = logoAreaHeight;
    last = {};
    for (int i = 0; i < n; ++i)
    {
        const auto& it = items[(size_t) i];
        if (it.section.isNotEmpty() && it.section != last)
        {
            l.sectionY[(size_t) i] = y;
            y += l.sectionHeight;
            last = it.section;
        }
        l.itemY[(size_t) i] = y;
        y += l.itemHeight;
    }
    return l;
}

void NavRail::resized()
{
    layout = computeLayout();
    for (size_t i = 0; i < buttons.size(); ++i)
    {
        buttons[i]->setBounds (0, layout.itemY[i], getWidth(), layout.itemHeight);
        buttons[i]->setCompact (layout.compact);
    }
}

Rectangle<float> NavRail::pillBounds (int index) const
{
    if (index < 0 || index >= (int) layout.itemY.size())
        return {};
    return Rectangle<float> ((float) pillInset, (float) layout.itemY[(size_t) index] + 3.0f,
                             (float) getWidth() - pillInset * 2.0f, (float) layout.itemHeight - 6.0f);
}

void NavRail::paint (Graphics& g)
{
    auto r = getLocalBounds();
    g.fillAll (theme::bg0);
    g.setColour (theme::border);
    g.fillRect (r.removeFromRight (1));

    // brand mark
    {
        Rectangle<float> tile ((float) getWidth() * 0.5f - 20.0f, 16.0f, 40.0f, 40.0f);
        theme::drawGlow (g, tile, theme::accent, 12.0f, 10.0f);
        g.setGradientFill (ColourGradient (theme::accentHi, tile.getX(), tile.getY(), theme::accentDim, tile.getRight(), tile.getBottom(), false));
        g.fillRoundedRectangle (tile, 12.0f);
        g.setColour (Colours::white.withAlpha (0.25f));
        g.drawRoundedRectangle (tile.reduced (1.0f), 11.0f, 1.0f);
        icons::draw (g, icons::Id::bolt, tile.reduced (9.0f), theme::onAccent);
    }

    // gliding selection pill, interpolated between neighbouring item slots
    if (! items.empty())
    {
        const float pos = indicator.get();
        const int i0 = jlimit (0, (int) items.size() - 1, (int) std::floor (pos));
        const int i1 = jlimit (0, (int) items.size() - 1, i0 + 1);
        const float t = jlimit (0.0f, 1.0f, pos - (float) i0);
        const auto a = pillBounds (i0), b = pillBounds (i1);
        Rectangle<float> pill (a.getX(), a.getY() + (b.getY() - a.getY()) * t, a.getWidth(), a.getHeight());

        theme::drawGlow (g, pill, theme::accent, 12.0f, 6.0f);
        g.setColour (theme::bg3);
        g.fillRoundedRectangle (pill, 12.0f);
        g.setColour (theme::accent.withAlpha (0.35f));
        g.drawRoundedRectangle (pill.reduced (0.5f), 12.0f, 1.0f);

        // accent bar on the rail's edge
        Rectangle<float> bar (0.0f, pill.getCentreY() - 12.0f, 3.0f, 24.0f);
        g.setColour (theme::accent);
        g.fillRoundedRectangle (bar.withX (-1.5f), 1.5f);
    }

    // section labels / separators
    String last;
    for (size_t i = 0; i < items.size(); ++i)
    {
        const int sy = layout.sectionY[i];
        if (sy < 0)
            continue;
        if (layout.compact)
        {
            g.setColour (theme::border);
            g.fillRect (16, sy + layout.sectionHeight / 2, getWidth() - 32, 1);
        }
        else
        {
            theme::drawEyebrow (g, items[i].section, Rectangle<int> (0, sy + 4, getWidth(), layout.sectionHeight - 4),
                                theme::textMuted, Justification::centred);
        }
    }

    // footer
    g.setColour (theme::textMuted);
    g.setFont (theme::mono (10.0f));
    g.drawText ("v" + String (ProjectInfo::versionString), getLocalBounds().removeFromBottom (footerHeight).withTrimmedBottom (8),
                Justification::centred, false);
}
} // namespace fa::ui
