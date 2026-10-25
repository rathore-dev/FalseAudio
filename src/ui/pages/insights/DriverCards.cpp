#include "DriverCards.h"

namespace fa::ui
{
namespace
{
    constexpr int headerH   = 48;
    constexpr int statsH    = 58;
    constexpr int plotH     = 132;
    constexpr int eyebrowH  = 18;
    constexpr int pillRowH  = 26;
    constexpr int stripeW   = 4;
    constexpr int findingGap = 4;

    const juce::Font findingFont() { return theme::font (12.0f); }
}

DriverCard::DriverCard()
{
    addAndMakeVisible (thdPlot);
    thdPlot.setUnits (FrequencyPlot::Units::dB);
    thdPlot.setRanges (20.0, 20000.0, -60.0, 0.0);
    thdPlot.setShowLegend (false);
    thdPlot.setTitle ("THD  (dB re fundamental: -40 = 1 %, -20 = 10 %)");
}

void DriverCard::set (const insights::DriverDiagnostics& d, const SystemConfig& cfg)
{
    diag = d;
    title = insightsui::channelLabel (cfg, d.channelId);
    colour = insightsui::channelColour (cfg, d.channelId);
    if (auto* ch = cfg.find (d.channelId))
        subtitle = toString (ch->role) + "  |  " + toString (ch->side) + "  |  DSP out " + juce::String (ch->dspOutput + 1);
    else
        subtitle = d.channelId;

    thdPlot.clearTraces();
    {
        std::vector<double> f, db;
        for (const auto& [hz, pct] : d.thdCurve)
        {
            f.push_back (hz);
            db.push_back (insightsui::thdDb (pct));
        }
        if (! f.empty())
        {
            auto t = PlotTrace::fromDb ("thd", "THD", colour, f, db);
            t.fillToBottom = true;
            t.thickness = 2.0f;
            thdPlot.setTrace (t);

            auto ref = PlotTrace::fromDb ("ref1", "1 %", theme::textMuted, { 20.0, 20000.0 }, { -40.0, -40.0 });
            ref.dashed = true;
            ref.thickness = 1.0f;
            thdPlot.setTrace (ref);
        }
    }
    resized();
    repaint();
}

int DriverCard::findingsHeight (int width) const
{
    if (diag.findings.isEmpty())
        return 0;
    int h = eyebrowH;
    for (const auto& f : diag.findings)
        h += std::max (18, insightsui::wrappedHeight (f, width - 26, findingFont())) + findingGap;
    return h;
}

int DriverCard::idealHeight (int width) const
{
    const int inner = width - stripeW - 2 * theme::pad;
    int h = theme::pad + headerH + statsH + plotH + 8;
    if (! diag.resonances.empty())
        h += eyebrowH + pillRowH + 6;
    h += findingsHeight (inner);
    return h + theme::pad;
}

void DriverCard::resized()
{
    auto a = getLocalBounds().withTrimmedLeft (stripeW).reduced (theme::pad);
    a.removeFromTop (headerH + statsH);
    thdPlot.setBounds (a.removeFromTop (plotH));
}

void DriverCard::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    theme::fillCard (g, bounds);

    {
        juce::Graphics::ScopedSaveState ss (g);
        juce::Path clip;
        clip.addRoundedRectangle (bounds, theme::radius);
        g.reduceClipRegion (clip);
        g.setColour (colour);
        g.fillRect (0, 0, stripeW, getHeight());
        theme::drawGlow (g, juce::Rectangle<float> (0.0f, 0.0f, (float) stripeW, (float) getHeight()), colour.withAlpha (0.35f), 0.0f, 10.0f);
    }

    auto a = getLocalBounds().withTrimmedLeft (stripeW).reduced (theme::pad);

    // ---- header ------------------------------------------------------------
    auto header = a.removeFromTop (headerH);
    {
        const auto polarityText = diag.polarityReversed ? "Wired reversed - corrected" : "Polarity OK";
        const auto polarityColour = diag.polarityReversed ? theme::warn : theme::good;
        const int pw = insightsui::pillWidth (polarityText);
        auto pillArea = header.removeFromRight (pw).withSizeKeepingCentre (pw, 22).withY (header.getY() + 2);
        insightsui::drawPill (g, pillArea, polarityText, polarityColour);
        header.removeFromRight (8);

        g.setColour (theme::text);
        g.setFont (theme::fontMedium (14.5f));
        g.drawText (title, header.removeFromTop (22), juce::Justification::centredLeft, true);
        g.setColour (theme::textMuted);
        g.setFont (theme::font (11.5f));
        g.drawText (subtitle, header.removeFromTop (18), juce::Justification::centredLeft, true);
    }

    // ---- stats -------------------------------------------------------------
    auto stats = a.removeFromTop (statsH);
    const int sw = stats.getWidth() / 4;
    insightsui::drawStat (g, stats.removeFromLeft (sw), "USABLE BAND",
                          insightsui::formatBandHz (diag.usableLowHz, diag.usableHighHz), theme::text, "-6 dB points");
    insightsui::drawStat (g, stats.removeFromLeft (sw), "ARRIVAL",
                          theme::formatMs (diag.arrivalMs, 2), theme::text, "at the listening seat");
    insightsui::drawStat (g, stats.removeFromLeft (sw), "SNR",
                          juce::String (diag.snrDb, 0) + " dB", insightsui::snrColour (diag.snrDb),
                          diag.snrDb >= 45.0 ? "clean capture" : diag.snrDb >= 30.0 ? "usable" : "noisy");
    insightsui::drawStat (g, stats, "PEAK THD",
                          diag.thdCurve.empty() ? juce::String ("-") : insightsui::formatPercent (diag.peakThdPercent),
                          insightsui::thdColour (diag.peakThdPercent),
                          diag.thdAt100HzPercent > 0.0 ? insightsui::formatPercent (diag.thdAt100HzPercent) + " at 100 Hz" : "in passband");

    a.removeFromTop (plotH + 8);

    // ---- resonances --------------------------------------------------------
    if (! diag.resonances.empty())
    {
        theme::drawEyebrow (g, "RESONANCES", a.removeFromTop (eyebrowH));
        auto row = a.removeFromTop (pillRowH).withTrimmedBottom (4);
        int shown = 0;
        for (const auto& [hz, db] : diag.resonances)
        {
            const auto txt = theme::formatHz (hz, 0) + "  " + theme::formatDb (db, 1);
            const int w = insightsui::pillWidth (txt);
            if (w > row.getWidth())
            {
                const int rest = (int) diag.resonances.size() - shown;
                if (rest > 0 && row.getWidth() > 30)
                {
                    g.setColour (theme::textMuted);
                    g.setFont (theme::font (11.0f));
                    g.drawText ("+" + juce::String (rest), row, juce::Justification::centredLeft, true);
                }
                break;
            }
            insightsui::drawPill (g, row, txt, insightsui::heatColour (db, 3.0, 9.0));
            row.removeFromLeft (w + 6);
            ++shown;
        }
        a.removeFromTop (6);
    }

    // ---- findings ----------------------------------------------------------
    if (! diag.findings.isEmpty())
    {
        theme::drawEyebrow (g, "FINDINGS", a.removeFromTop (eyebrowH));
        for (const auto& f : diag.findings)
        {
            const auto style = insightsui::styleForFinding (f);
            const int h = std::max (18, insightsui::wrappedHeight (f, a.getWidth() - 26, findingFont()));
            auto line = a.removeFromTop (h);
            icons::draw (g, style.icon, line.removeFromLeft (18).withHeight (18).toFloat().reduced (1.0f), style.colour);
            line.removeFromLeft (8);
            insightsui::drawWrapped (g, f, line, findingFont(), theme::textDim);
            a.removeFromTop (findingGap);
        }
    }
}

//==============================================================================
void DriverCardGrid::set (const insights::Insights& in, const SystemConfig& cfg)
{
    cards.clear();
    // Config order (so cards follow the system layout), then any diagnostics for unknown ids.
    std::vector<const insights::DriverDiagnostics*> ordered;
    for (const auto& ch : cfg.channels)
        for (const auto& d : in.drivers)
            if (d.channelId == ch.id)
                ordered.push_back (&d);
    for (const auto& d : in.drivers)
        if (std::find (ordered.begin(), ordered.end(), &d) == ordered.end())
            ordered.push_back (&d);

    for (auto* d : ordered)
    {
        auto card = std::make_unique<DriverCard>();
        card->set (*d, cfg);
        addAndMakeVisible (*card);
        cards.push_back (std::move (card));
    }
    resized();
}

int DriverCardGrid::idealHeight (int width) const
{
    if (cards.empty())
        return 0;
    const int cols = columnsFor (width);
    const int colW = (width - (cols - 1) * insightsui::gap) / cols;
    int total = 0;
    for (size_t i = 0; i < cards.size(); i += (size_t) cols)
    {
        int rowH = 0;
        for (size_t j = i; j < cards.size() && j < i + (size_t) cols; ++j)
            rowH = std::max (rowH, cards[j]->idealHeight (colW));
        total += rowH + (i > 0 ? insightsui::gap : 0);
    }
    return total;
}

void DriverCardGrid::resized()
{
    if (cards.empty())
        return;
    const int width = getWidth();
    const int cols = columnsFor (width);
    const int colW = (width - (cols - 1) * insightsui::gap) / cols;
    int y = 0;
    for (size_t i = 0; i < cards.size(); i += (size_t) cols)
    {
        int rowH = 0;
        for (size_t j = i; j < cards.size() && j < i + (size_t) cols; ++j)
            rowH = std::max (rowH, cards[j]->idealHeight (colW));
        int x = 0;
        for (size_t j = i; j < cards.size() && j < i + (size_t) cols; ++j)
        {
            cards[j]->setBounds (x, y, colW, rowH);
            x += colW + insightsui::gap;
        }
        y += rowH + insightsui::gap;
    }
}
} // namespace fa::ui
