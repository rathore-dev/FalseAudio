#include "SummaryHeader.h"

namespace fa::ui
{
SummaryHeader::SummaryHeader() = default;

void SummaryHeader::setPlan (const install::Plan& plan, const install::Installation& inst,
                             const gear::GearDatabase& db, const Health& health)
{
    tiles.clear();
    ampPills.clear();
    healthPills.clear();

    double systemW = 0;
    for (auto& ch : plan.channels)
        systemW += ch.ampPowerAtLoadW;

    const auto supplyFoot = "at " + ikit::fmtVolts (inst.batteryVoltageEngineOn) + " (engine on)";

    tiles.push_back ({ "System power", ikit::fmtWatts (systemW), "", "sum of amp power into the real loads", theme::accent, icons::Id::bolt });
    tiles.push_back ({ "Current draw", juce::String (plan.totalCurrentA, plan.totalCurrentA >= 100 ? 0 : 1), "A", supplyFoot,
                       plan.totalCurrentA > juce::jmax (1.0, inst.alternatorA) && inst.alternatorA > 0 ? theme::warn : theme::text, icons::Id::activity });
    tiles.push_back ({ "Main fuse", juce::String (juce::roundToInt (plan.recommendedMainFuseA)), "A", "within 45 cm of the battery", theme::warn, icons::Id::warning });
    tiles.push_back ({ "Source level", juce::String (plan.sourceVoltageV, 2), "V", "signal reaching the amps", theme::roleFullrange, icons::Id::waveform });

    juce::String volFoot = "last clean step";
    if (auto* hu = db.find (inst.headUnitId))
        if (hu->source && hu->source->maxVolumeStep > 0)
            volFoot = "of " + juce::String (hu->source->maxVolumeStep) + " on the " + hu->model;
    tiles.push_back ({ "Head unit max", juce::String (juce::roundToInt (plan.headUnitMaxCleanVolume)), "", volFoot, theme::roleMidrange, icons::Id::radio });

    for (auto& w : plan.wires)
    {
        if (! w.what.endsWithIgnoreCase ("power wire"))
            continue;
        const auto name = w.what.dropLastCharacters (10).trim().upToFirstOccurrenceOf (" (", false, false);
        const auto c = ! w.ok ? theme::bad
                              : (w.voltageAtLoadV < inst.batteryVoltageEngineOn - 0.5 ? theme::warn : theme::good);
        ampPills.push_back ({ name + "  " + ikit::fmtVolts (w.voltageAtLoadV), c });
    }

    if (health.danger > 0)
        healthPills.push_back ({ juce::String (health.danger) + (health.danger == 1 ? " critical item" : " critical items"), theme::bad });
    if (health.important > 0)
        healthPills.push_back ({ juce::String (health.important) + (health.important == 1 ? " important" : " important"), theme::warn });
    if (health.wireUpgrades > 0)
        healthPills.push_back ({ juce::String (health.wireUpgrades) + (health.wireUpgrades == 1 ? " wire upgrade" : " wire upgrades"), theme::bad });
    if (health.channelWarnings > 0)
        healthPills.push_back ({ juce::String (health.channelWarnings) + " channel " + (health.channelWarnings == 1 ? "warning" : "warnings"), theme::warn });
    if (healthPills.empty())
        healthPills.push_back ({ "All checks OK", theme::good });
    if (health.stepsTotal > 0)
        healthPills.push_back ({ juce::String (health.stepsDone) + " / " + juce::String (health.stepsTotal) + " steps done",
                                 health.stepsDone == health.stepsTotal ? theme::good : theme::accent });

    repaint();
}

int SummaryHeader::tileColumns (int width) const
{
    const int n = (int) tiles.size();
    if (n == 0) return 1;
    return juce::jlimit (1, n, (width + ikit::gap) / (tileMinW + ikit::gap));
}

juce::Rectangle<int> SummaryHeader::tilesArea (int width, int& rows) const
{
    const int cols = tileColumns (width);
    rows = tiles.empty() ? 0 : ((int) tiles.size() + cols - 1) / cols;
    return { 0, 0, width, rows * tileH + juce::jmax (0, rows - 1) * ikit::gap };
}

int SummaryHeader::heightFor (int width) const
{
    int rows = 0;
    int h = tilesArea (width, rows).getHeight();
    if (! ampPills.empty() || ! healthPills.empty())
        h += ikit::gap2 + pillH;
    return h;
}

void SummaryHeader::resized() {}

void SummaryHeader::paint (juce::Graphics& g)
{
    const int width = getWidth();
    int rows = 0;
    auto area = tilesArea (width, rows);
    const int cols = tileColumns (width);
    const int cellW = (width - ikit::gap * (cols - 1)) / cols;

    for (size_t i = 0; i < tiles.size(); ++i)
    {
        const int col = (int) i % cols, row = (int) i / cols;
        auto r = juce::Rectangle<int> (col * (cellW + ikit::gap), row * (tileH + ikit::gap), cellW, tileH);
        auto& t = tiles[i];

        theme::fillCard (g, r.toFloat(), theme::bg2, theme::border);
        // soft tint in the corner
        g.setGradientFill (juce::ColourGradient (t.tint.withAlpha (0.10f), (float) r.getX(), (float) r.getY(),
                                                 t.tint.withAlpha (0.0f), (float) r.getX() + 160.0f, (float) r.getBottom(), false));
        g.fillRoundedRectangle (r.toFloat().reduced (1.0f), theme::radius - 1.0f);

        auto inner = r.reduced (ikit::gap2, 12);
        auto top = inner.removeFromTop (16);
        icons::draw (g, t.icon, top.removeFromRight (16).toFloat(), t.tint.withAlpha (0.9f));
        theme::drawEyebrow (g, t.label, top, theme::textMuted);

        inner.removeFromTop (4);
        auto valueRow = inner.removeFromTop (32);
        const auto valueFont = theme::display (26.0f);
        g.setFont (valueFont);
        g.setColour (theme::text);
        const int vw = (int) theme::textWidth (valueFont, t.value) + 2;
        g.drawText (t.value, valueRow.removeFromLeft (vw), juce::Justification::centredLeft, false);
        if (t.unit.isNotEmpty())
        {
            g.setFont (theme::fontMedium (13.0f));
            g.setColour (t.tint == theme::text ? theme::textDim : t.tint);
            g.drawText (t.unit, valueRow.withTrimmedLeft (4).withTrimmedTop (9), juce::Justification::centredLeft, false);
        }
        g.setFont (theme::font (11.0f));
        g.setColour (theme::textMuted);
        g.drawText (t.foot, inner.removeFromTop (16), juce::Justification::centredLeft, true);
    }

    // ---- pills row: voltage at each amp on the left, health on the right
    auto pillRow = juce::Rectangle<int> (0, area.getBottom() + ikit::gap2, width, pillH);
    const auto pillFont = theme::fontMedium (11.5f);

    int x = pillRow.getX();
    for (auto& p : ampPills)
    {
        const int w = (int) theme::textWidth (pillFont, p.text) + 34;
        if (x + w > pillRow.getRight() - 200) break;
        auto pr = juce::Rectangle<int> (x, pillRow.getY(), w, pillH).toFloat();
        theme::fillPill (g, pr, theme::bg2, theme::border);
        auto dot = pr.removeFromLeft (22).withSizeKeepingCentre (8.0f, 8.0f).translated (4.0f, 0.0f);
        theme::drawGlow (g, dot, p.colour, 4.0f, 4.0f);
        g.setColour (p.colour);
        g.fillEllipse (dot);
        g.setColour (theme::text);
        g.setFont (pillFont);
        g.drawText (p.text, pr.toNearestInt().withTrimmedLeft (2).withTrimmedRight (10), juce::Justification::centredLeft, false);
        x += w + ikit::gap;
    }

    int rx = pillRow.getRight();
    for (int i = (int) healthPills.size(); --i >= 0;)
    {
        auto& p = healthPills[(size_t) i];
        const int w = (int) theme::textWidth (pillFont, p.text) + 24;
        rx -= w;
        auto pr = juce::Rectangle<int> (rx, pillRow.getY(), w, pillH).toFloat();
        theme::fillPill (g, pr, p.colour.withAlpha (0.14f), p.colour.withAlpha (0.55f));
        g.setColour (p.colour);
        g.setFont (pillFont);
        g.drawText (p.text, pr.toNearestInt(), juce::Justification::centred, false);
        rx -= ikit::gap;
    }
}
} // namespace fa::ui
