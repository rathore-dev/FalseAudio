#include "WiringCard.h"

namespace fa::ui
{
namespace
{
    constexpr int rowH = 46, headH = ikit::tableHead;

    struct Cols { int name, current, drop, atLoad, loss, awg, badge; bool showLoss; };

    Cols columnsFor (int width)
    {
        Cols c {};
        c.showLoss = width >= 760;
        c.badge   = 118;
        c.awg     = 84;
        c.loss    = c.showLoss ? 76 : 0;
        c.atLoad  = 84;
        c.current = 76;
        const int fixed = c.badge + c.awg + c.loss + c.atLoad + c.current;
        const int rest  = juce::jmax (200, width - fixed);
        c.name = juce::jmax (120, (int) (rest * 0.42));
        c.drop = rest - c.name;
        return c;
    }
}

WiringCard::WiringCard() : Card ("Wiring", "Power and speaker runs checked against the current each amp really pulls")
{
    addAndMakeVisible (table);
    addAndMakeVisible (legend);
}

void WiringCard::setChecks (const std::vector<install::WireCheck>& checks, double supplyV)
{
    table.checks = checks;
    table.supplyV = supplyV > 1.0 ? supplyV : 14.2;
    table.repaint();
}

int WiringCard::heightFor (int width) const
{
    return ikit::cardHead + table.heightFor() + ikit::gap + legend.heightFor (width - 2 * ikit::gap2) + ikit::gap2;
}

void WiringCard::resized()
{
    auto r = getContentArea();
    table.setBounds (r.removeFromTop (table.heightFor()));
    r.removeFromTop (ikit::gap);
    legend.setBounds (r.removeFromTop (legend.heightFor (r.getWidth())));
}

int WiringCard::Table::heightFor() const
{
    return headH + juce::jmax (1, (int) checks.size()) * rowH;
}

void WiringCard::Table::mouseMove (const juce::MouseEvent& e)
{
    const int row = (e.y - headH) / rowH;
    const int r = (e.y >= headH && row < (int) checks.size()) ? row : -1;
    if (r != hoverRow) { hoverRow = r; repaint(); }
}

void WiringCard::Table::mouseExit (const juce::MouseEvent&) { hoverRow = -1; repaint(); }

void WiringCard::Table::paint (juce::Graphics& g)
{
    const auto cols = columnsFor (getWidth());
    auto bounds = getLocalBounds();

    // header
    auto head = bounds.removeFromTop (headH);
    auto drawHead = [&g] (juce::Rectangle<int> r, const juce::String& t, juce::Justification j = juce::Justification::centredRight)
    {
        theme::drawEyebrow (g, t, r, theme::textMuted, j);
    };
    {
        auto h = head;
        drawHead (h.removeFromLeft (cols.name), "Run", juce::Justification::centredLeft);
        drawHead (h.removeFromLeft (cols.current), "Current");
        drawHead (h.removeFromLeft (cols.drop).reduced (ikit::gap2, 0), "Voltage drop", juce::Justification::centredLeft);
        drawHead (h.removeFromLeft (cols.atLoad), "At load");
        if (cols.showLoss) drawHead (h.removeFromLeft (cols.loss), "Loss");
        drawHead (h.removeFromLeft (cols.awg), "Use");
        drawHead (h.removeFromLeft (cols.badge), "Status", juce::Justification::centredRight);
    }
    g.setColour (theme::border);
    g.fillRect (bounds.getX(), head.getBottom() - 1, bounds.getWidth(), 1);

    if (checks.empty())
    {
        g.setColour (theme::textMuted);
        g.setFont (theme::font (13.0f));
        g.drawText ("No wire runs to check yet - add amplifiers and their power wire on the Equipment page.",
                    bounds.removeFromTop (rowH), juce::Justification::centredLeft, true);
        return;
    }

    const auto mono = theme::mono (13.0f);
    for (size_t i = 0; i < checks.size(); ++i)
    {
        auto& w = checks[i];
        auto row = bounds.removeFromTop (rowH);
        if ((int) i == hoverRow)
        {
            g.setColour (theme::bg3.withAlpha (0.6f));
            g.fillRoundedRectangle (row.toFloat().reduced (-4.0f, 2.0f), theme::radiusSm);
        }
        if (i + 1 < checks.size())
        {
            g.setColour (theme::border.withAlpha (0.6f));
            g.fillRect (row.getX(), row.getBottom() - 1, row.getWidth(), 1);
        }

        const double dropPct = supplyV > 0 ? w.voltageDropV / supplyV : 0.0;
        const auto status = ! w.ok ? theme::bad : (dropPct > 0.03 ? theme::warn : theme::good);

        auto r = row;
        // name + left status strip
        auto name = r.removeFromLeft (cols.name);
        g.setColour (status);
        g.fillRoundedRectangle (name.removeFromLeft (3).toFloat().reduced (0.0f, 12.0f), 1.5f);
        name.removeFromLeft (ikit::gap + 2);
        g.setColour (theme::text);
        g.setFont (theme::fontMedium (13.0f));
        g.drawText (w.what, name, juce::Justification::centredLeft, true);

        g.setFont (mono);
        g.setColour (theme::text);
        g.drawText (ikit::fmtAmps (w.currentA), r.removeFromLeft (cols.current), juce::Justification::centredRight, false);

        // drop bar + value
        auto dropArea = r.removeFromLeft (cols.drop).reduced (ikit::gap2, 0);
        auto dropText = dropArea.removeFromRight (64);
        g.setColour (status);
        g.drawText (juce::String (w.voltageDropV, 2) + " V", dropText, juce::Justification::centredRight, false);
        auto bar = dropArea.withTrimmedRight (ikit::gap).toFloat().withSizeKeepingCentre ((float) (dropArea.getWidth() - ikit::gap), 6.0f);
        ikit::drawGauge (g, bar, (float) (dropPct / 0.10), status, 0.3f);
        g.setFont (theme::font (10.0f));
        g.setColour (theme::textMuted);
        g.drawText (juce::String (dropPct * 100.0, 1) + " %", bar.toNearestInt().withY ((int) bar.getBottom() + 3).withHeight (12),
                    juce::Justification::centredLeft, false);

        g.setFont (mono);
        g.setColour (theme::text);
        g.drawText (ikit::fmtVolts (w.voltageAtLoadV), r.removeFromLeft (cols.atLoad), juce::Justification::centredRight, false);
        if (cols.showLoss)
        {
            g.setColour (theme::textDim);
            g.drawText ((w.powerLossW >= 10 ? juce::String (juce::roundToInt (w.powerLossW)) : juce::String (w.powerLossW, 1)) + " W", r.removeFromLeft (cols.loss), juce::Justification::centredRight, false);
        }
        g.setColour (w.ok ? theme::text : theme::bad);
        g.setFont (theme::fontBold (13.0f));
        g.drawText (ikit::fmtAwg (w.recommendedAwg), r.removeFromLeft (cols.awg), juce::Justification::centredRight, false);

        auto badge = r.removeFromLeft (cols.badge);
        ikit::drawBadge (g, badge, w.ok ? (dropPct > 0.03 ? "Marginal" : "OK") : "Upgrade", status, juce::Justification::centredRight);
    }
}
} // namespace fa::ui
