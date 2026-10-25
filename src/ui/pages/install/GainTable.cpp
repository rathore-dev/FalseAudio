#include "GainTable.h"

namespace fa::ui
{
namespace
{
    constexpr int baseRowH = 52, warnLineH = 17, headH = ikit::tableHead;

    struct Cols { int name, load, ampW, rmsW, ratio, target, gain, excHp, tuneHp, status; bool compact; };

    Cols columnsFor (int width)
    {
        Cols c {};
        c.compact = width < 980;
        c.status  = c.compact ? 0 : 96;
        c.tuneHp  = 92;
        c.excHp   = 96;
        c.gain    = 82;
        c.target  = 86;
        c.ratio   = c.compact ? 96 : 128;
        c.rmsW    = c.compact ? 0 : 84;
        c.ampW    = 84;
        c.load    = 72;
        const int fixed = c.status + c.tuneHp + c.excHp + c.gain + c.target + c.ratio + c.rmsW + c.ampW + c.load;
        c.name = juce::jmax (130, width - fixed);
        return c;
    }

    juce::Colour ratioColour (double ratio)
    {
        if (ratio <= 0.0) return theme::textMuted;
        if (ratio >= 0.75 && ratio <= 1.5) return theme::good;
        if (ratio >= 0.5 && ratio <= 2.0)  return theme::warn;
        return theme::bad;
    }
}

GainTable::GainTable() : Card ("Per-channel gains", "What each amp channel really delivers, the DMM voltage to set it, and the safe high-pass")
{
    addAndMakeVisible (table);
    addAndMakeVisible (applyBtn);
    addAndMakeVisible (note);
    applyBtn.onClick = [this] { if (onApply) onApply(); };
    note.setText ("Set gains with a 0 dBFS 50 Hz (sub) / 1 kHz (others) test tone at the head unit's max clean volume, "
                  "no speaker connected, until the DMM reads the target voltage. Ratio bar: amp power vs driver RMS (marker = 1.0x).");
}

void GainTable::setPlan (const std::vector<install::ChannelPlan>& channels, const SystemConfig& config)
{
    table.rows.clear();
    for (auto& cp : channels)
    {
        Row r;
        r.plan = cp;
        r.label = cp.channelId;
        r.roleColour = theme::textDim;
        if (auto* ch = config.find (cp.channelId))
        {
            r.label = ch->label;
            r.roleColour = theme::sideTint (theme::roleColour (ch->role), ch->side);
            r.hasHp = ch->hasHighPass() || ch->role == DriverRole::subwoofer;
            r.tuneHpHz = ch->settings.highPass.enabled ? ch->settings.highPass.freq : 0.0;
        }
        r.hpUnsafe = cp.excursionLimitedHpHz > 0.0 && r.tuneHpHz + 0.5 < cp.excursionLimitedHpHz;
        table.rows.push_back (std::move (r));
    }
    const int unsafe = countUnsafeHp();
    applyBtn.setStyle (unsafe > 0 ? FaButton::Style::primary : FaButton::Style::secondary);
    applyBtn.setButtonText (unsafe > 0 ? "Apply safe limits (" + juce::String (unsafe) + ")" : "Apply safe limits to tuning");
    table.repaint();
}

int GainTable::countUnsafeHp() const
{
    int n = 0;
    for (auto& r : table.rows) if (r.hpUnsafe) ++n;
    return n;
}

int GainTable::heightFor (int width) const
{
    return ikit::cardHead + table.heightFor() + ikit::gap + note.heightFor (width - 2 * ikit::gap2) + ikit::gap2;
}

void GainTable::resized()
{
    auto actions = getHeaderActionArea();
    const int bw = juce::jmin (actions.getWidth(), 236);
    applyBtn.setBounds (actions.removeFromRight (bw).withSizeKeepingCentre (bw, ikit::btnH));

    auto r = getContentArea();
    table.setBounds (r.removeFromTop (table.heightFor()));
    r.removeFromTop (ikit::gap);
    note.setBounds (r.removeFromTop (note.heightFor (r.getWidth())));
}

int GainTable::Table::rowHeight (const Row& r) const
{
    return baseRowH + (r.plan.warnings.isEmpty() ? 0 : r.plan.warnings.size() * warnLineH + 6);
}

int GainTable::Table::heightFor() const
{
    int h = headH;
    if (rows.empty()) return h + baseRowH;
    for (auto& r : rows) h += rowHeight (r);
    return h;
}

void GainTable::Table::mouseMove (const juce::MouseEvent& e)
{
    int y = headH, found = -1;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const int h = rowHeight (rows[i]);
        if (e.y >= y && e.y < y + h) { found = (int) i; break; }
        y += h;
    }
    if (found != hoverRow) { hoverRow = found; repaint(); }
}

void GainTable::Table::mouseExit (const juce::MouseEvent&) { hoverRow = -1; repaint(); }

void GainTable::Table::paint (juce::Graphics& g)
{
    const auto cols = columnsFor (getWidth());
    auto bounds = getLocalBounds();
    auto head = bounds.removeFromTop (headH);
    {
        auto h = head;
        auto eb = [&g] (juce::Rectangle<int> r, const juce::String& t, juce::Justification j = juce::Justification::centredRight)
        { theme::drawEyebrow (g, t, r, theme::textMuted, j); };
        eb (h.removeFromLeft (cols.name), "Channel", juce::Justification::centredLeft);
        eb (h.removeFromLeft (cols.load), "Load");
        eb (h.removeFromLeft (cols.ampW), "Amp");
        if (! cols.compact) eb (h.removeFromLeft (cols.rmsW), "Driver RMS");
        eb (h.removeFromLeft (cols.ratio).reduced (ikit::gap2, 0), "Ratio", juce::Justification::centredLeft);
        eb (h.removeFromLeft (cols.target), "DMM target");
        eb (h.removeFromLeft (cols.gain), "Gain");
        eb (h.removeFromLeft (cols.excHp), "Safe HP");
        eb (h.removeFromLeft (cols.tuneHp), "Tune HP");
        if (! cols.compact) eb (h.removeFromLeft (cols.status), "Status");
    }
    g.setColour (theme::border);
    g.fillRect (bounds.getX(), head.getBottom() - 1, bounds.getWidth(), 1);

    if (rows.empty())
    {
        g.setColour (theme::textMuted);
        g.setFont (theme::font (13.0f));
        g.drawText ("No channels planned yet - assign speakers to amp channels on the Equipment page.",
                    bounds.removeFromTop (baseRowH), juce::Justification::centredLeft, true);
        return;
    }

    const auto mono = theme::mono (13.0f);
    for (size_t i = 0; i < rows.size(); ++i)
    {
        auto& r = rows[i];
        auto& p = r.plan;
        auto full = bounds.removeFromTop (rowHeight (r));
        if ((int) i == hoverRow)
        {
            g.setColour (theme::bg3.withAlpha (0.6f));
            g.fillRoundedRectangle (full.toFloat().reduced (-4.0f, 2.0f), theme::radiusSm);
        }
        if (i + 1 < rows.size())
        {
            g.setColour (theme::border.withAlpha (0.6f));
            g.fillRect (full.getX(), full.getBottom() - 1, full.getWidth(), 1);
        }
        auto row = full.withHeight (baseRowH);
        auto c = row;

        // channel name + role dot
        auto name = c.removeFromLeft (cols.name);
        auto dot = name.removeFromLeft (14).toFloat().withSizeKeepingCentre (8.0f, 8.0f);
        theme::drawGlow (g, dot, r.roleColour, 4.0f, 4.0f);
        g.setColour (r.roleColour);
        g.fillEllipse (dot);
        name.removeFromLeft (ikit::gap);
        g.setColour (theme::text);
        g.setFont (theme::fontMedium (13.0f));
        g.drawText (r.label, name.withTrimmedTop (8).withHeight (18), juce::Justification::centredLeft, true);
        g.setColour (theme::textMuted);
        g.setFont (theme::font (11.0f));
        g.drawText (p.recommendedLpHz > 0 ? "band " + ikit::fmtHz (p.recommendedHpHz) + " - " + ikit::fmtHz (p.recommendedLpHz)
                                          : "from " + ikit::fmtHz (p.recommendedHpHz),
                    name.withTrimmedTop (27).withHeight (15), juce::Justification::centredLeft, true);

        g.setFont (mono);
        g.setColour (theme::text);
        g.drawText (ikit::fmtOhms (p.loadOhms), c.removeFromLeft (cols.load), juce::Justification::centredRight, false);
        g.drawText (juce::String (juce::roundToInt (p.ampPowerAtLoadW)) + " W", c.removeFromLeft (cols.ampW), juce::Justification::centredRight, false);
        if (! cols.compact)
        {
            g.setColour (theme::textDim);
            g.drawText (p.driverRmsW > 0 ? juce::String (juce::roundToInt (p.driverRmsW)) + " W" : juce::String ("-"),
                        c.removeFromLeft (cols.rmsW), juce::Justification::centredRight, false);
        }

        // ratio gauge: 0..2x, marker at 1.0
        auto ratioArea = c.removeFromLeft (cols.ratio).reduced (ikit::gap2, 0);
        const auto rc = ratioColour (p.powerRatio);
        auto rt = ratioArea.removeFromRight (cols.compact ? 40 : 44);
        g.setColour (rc);
        g.setFont (mono);
        g.drawText (p.powerRatio > 0 ? juce::String (p.powerRatio, p.powerRatio >= 10 ? 0 : 1) + "x" : juce::String ("-"), rt,
                    juce::Justification::centredRight, false);
        if (p.powerRatio > 0)
            ikit::drawGauge (g, ratioArea.withTrimmedRight (ikit::gap).toFloat().withSizeKeepingCentre ((float) ratioArea.getWidth() - ikit::gap, 6.0f),
                             (float) (p.powerRatio / 2.0), rc, 0.5f);

        // target voltage (big) + gain setting
        g.setColour (theme::accent);
        g.setFont (theme::fontBold (14.0f));
        g.drawText (ikit::fmtVolts (p.targetVoltageRms), c.removeFromLeft (cols.target), juce::Justification::centredRight, false);
        g.setColour (theme::text);
        g.setFont (mono);
        g.drawText (p.gainSettingV > 0 ? ikit::fmtVolts (p.gainSettingV) : juce::String ("-"), c.removeFromLeft (cols.gain), juce::Justification::centredRight, false);

        // HP: safe vs tune
        const auto hpCol = r.hpUnsafe ? theme::warn : theme::textDim;
        g.setColour (p.excursionLimitedHpHz > 0 ? theme::text : theme::textMuted);
        g.drawText (p.excursionLimitedHpHz > 0 ? ikit::fmtHz (p.excursionLimitedHpHz) : juce::String ("-"),
                    c.removeFromLeft (cols.excHp), juce::Justification::centredRight, false);
        auto tune = c.removeFromLeft (cols.tuneHp);
        if (r.hpUnsafe)
        {
            icons::draw (g, icons::Id::warning, tune.removeFromLeft (22).toFloat().withSizeKeepingCentre (13.0f, 13.0f).translated (6.0f, 0.0f), theme::warn);
        }
        g.setColour (r.tuneHpHz > 0 ? hpCol : theme::textMuted);
        g.drawText (r.tuneHpHz > 0 ? ikit::fmtHz (r.tuneHpHz) : juce::String (r.hasHp ? "off" : "-"), tune,
                    juce::Justification::centredRight, false);

        if (! cols.compact)
        {
            auto st = c.removeFromLeft (cols.status);
            if (r.hpUnsafe)                    ikit::drawBadge (g, st, "Raise HP", theme::warn, juce::Justification::centredRight);
            else if (! p.warnings.isEmpty())   ikit::drawBadge (g, st, "Check", theme::warn, juce::Justification::centredRight);
            else                               ikit::drawBadge (g, st, "OK", theme::good, juce::Justification::centredRight);
        }

        // warnings lines
        if (! p.warnings.isEmpty())
        {
            auto wa = full.withTrimmedTop (baseRowH - 4).withTrimmedLeft (22).withTrimmedRight (ikit::gap);
            g.setFont (theme::font (11.5f));
            for (auto& w : p.warnings)
            {
                auto line = wa.removeFromTop (warnLineH);
                icons::draw (g, icons::Id::warning, line.removeFromLeft (14).toFloat().withSizeKeepingCentre (11.0f, 11.0f), theme::warn.withAlpha (0.8f));
                line.removeFromLeft (6);
                g.setColour (theme::warn.withAlpha (0.9f));
                g.drawText (w, line, juce::Justification::centredLeft, true);
            }
        }
    }
}
} // namespace fa::ui
