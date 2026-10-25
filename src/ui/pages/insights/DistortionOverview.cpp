#include "DistortionOverview.h"

namespace fa::ui
{
namespace
{
    constexpr int rankW    = 280;
    constexpr int rankRowH = 40;
    constexpr int plotH    = 320;
}

DistortionOverview::DistortionOverview()
    : Card ("Distortion overview", "THD of every driver vs frequency  |  dashed lines mark 0.1 %, 1 % and 10 %")
{
    addAndMakeVisible (plot);
    plot.setUnits (FrequencyPlot::Units::dB);
    plot.setRanges (20.0, 20000.0, -60.0, 0.0);
    plot.setShowLegend (true);
    plot.setTitle ("THD, dB re fundamental");
}

void DistortionOverview::set (const insights::Insights& in, const SystemConfig& cfg)
{
    plot.clearTraces();
    ranks.clear();
    flagged = 0;

    auto refLine = [this] (const juce::String& id, double db)
    {
        auto t = PlotTrace::fromDb (id, id, theme::textMuted, { 20.0, 20000.0 }, { db, db });
        t.dashed = true;
        t.thickness = 1.0f;
        plot.setTrace (t);
    };
    refLine ("0.1 %", -60.0);
    refLine ("1 %", -40.0);
    refLine ("10 %", -20.0);

    for (const auto& d : in.drivers)
    {
        if (d.thdCurve.empty())
            continue;
        const auto colour = insightsui::channelColour (cfg, d.channelId);
        const auto label = insightsui::channelLabel (cfg, d.channelId);

        std::vector<double> f, db;
        Rank rank { label, colour, 0.0, 0.0, d.thdAt100HzPercent };
        for (const auto& [hz, pct] : d.thdCurve)
        {
            f.push_back (hz);
            db.push_back (insightsui::thdDb (pct));
            if (pct > rank.peakPercent)
            {
                rank.peakPercent = pct;
                rank.peakHz = hz;
            }
        }
        if (d.peakThdPercent > rank.peakPercent)
            rank.peakPercent = d.peakThdPercent;

        auto t = PlotTrace::fromDb (d.channelId, label, colour, f, db);
        t.thickness = rank.peakPercent >= 3.0 ? 2.25f : 1.75f;
        plot.setTrace (t);
        ranks.push_back (rank);
    }

    std::stable_sort (ranks.begin(), ranks.end(), [] (const Rank& a, const Rank& b) { return a.peakPercent > b.peakPercent; });
    for (const auto& r : ranks)
        if (r.peakPercent >= 3.0)
            ++flagged;

    setTitle ("Distortion overview",
              flagged == 0 ? "All drivers stay under 3 % THD in their passband  |  dashed lines mark 0.1 %, 1 % and 10 %"
                           : juce::String (flagged) + (flagged == 1 ? " driver exceeds" : " drivers exceed")
                             + " 3 % THD  |  dashed lines mark 0.1 %, 1 % and 10 %");
    setHighlight (flagged == 0 ? theme::good : theme::warn);
    resized();
    repaint();
}

int DistortionOverview::idealHeight (int width) const noexcept
{
    const int rankH = 24 + (int) ranks.size() * rankRowH;
    if (stacked (width))
        return insightsui::cardHeaderH + plotH + insightsui::gap + rankH + theme::pad;
    return insightsui::cardHeaderH + std::max (plotH, rankH) + theme::pad;
}

void DistortionOverview::resized()
{
    auto a = getContentArea();
    if (stacked (getWidth()))
    {
        plot.setBounds (a.removeFromTop (plotH));
        a.removeFromTop (insightsui::gap);
        rankArea = a;
    }
    else
    {
        rankArea = a.removeFromRight (std::min (rankW, a.getWidth() / 3));
        a.removeFromRight (insightsui::gap);
        plot.setBounds (a);
    }
}

void DistortionOverview::paint (juce::Graphics& g)
{
    Card::paint (g);
    if (ranks.empty())
        return;

    auto a = rankArea;
    theme::drawEyebrow (g, "WORST OFFENDERS  (PEAK THD)", a.removeFromTop (18));
    a.removeFromTop (6);

    const double maxPct = std::max (1.0, ranks.front().peakPercent);
    const double scaleTop = std::log10 (maxPct * 10.0 / 0.1);   // log scale from 0.1 % to 10x the worst

    for (size_t i = 0; i < ranks.size(); ++i)
    {
        const auto& r = ranks[i];
        auto row = a.removeFromTop (rankRowH);
        const auto sev = insightsui::thdColour (r.peakPercent);

        auto top = row.removeFromTop (18);
        if (r.peakPercent >= 3.0)
        {
            icons::draw (g, icons::Id::warning, top.removeFromLeft (16).toFloat().reduced (1.0f), theme::warn);
            top.removeFromLeft (6);
        }
        else
        {
            g.setColour (r.colour);
            g.fillEllipse (top.removeFromLeft (16).withSizeKeepingCentre (8, 8).toFloat());
            top.removeFromLeft (6);
        }

        const auto valueTxt = insightsui::formatPercent (r.peakPercent) + " @ " + theme::formatHz (r.peakHz, 0);
        const int vw = (int) std::ceil (theme::textWidth (theme::mono (11.5f), valueTxt)) + 4;
        g.setColour (sev);
        g.setFont (theme::mono (11.5f));
        g.drawText (valueTxt, top.removeFromRight (vw), juce::Justification::centredRight, true);

        g.setColour (theme::text);
        g.setFont (theme::fontMedium (12.0f));
        g.drawText (r.label, top, juce::Justification::centredLeft, true);

        auto bar = row.withTrimmedTop (4).removeFromTop (6).withTrimmedLeft (22).toFloat();
        g.setColour (theme::bg4);
        g.fillRoundedRectangle (bar, 3.0f);
        const double frac = juce::jlimit (0.0, 1.0, std::log10 (std::max (r.peakPercent, 0.1) / 0.1) / std::max (0.1, scaleTop));
        auto fill = bar.withWidth (bar.getWidth() * (float) frac);
        if (fill.getWidth() > 1.0f)
        {
            theme::drawGlow (g, fill, sev.withAlpha (0.3f), 3.0f, 4.0f);
            g.setColour (sev);
            g.fillRoundedRectangle (fill, 3.0f);
        }
    }
}
} // namespace fa::ui
