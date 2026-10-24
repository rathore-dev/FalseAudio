#include "ReportDocument.h"

namespace fa::ui
{
namespace
{
    constexpr int docMargin = 40;     // document margins
    constexpr int sheetMaxWidth = 1120;
    constexpr int sectionGap = 36;
    constexpr int titleH = 30;
    constexpr int plotH = 280;
    constexpr int tileH = 84;

    juce::String polarityText (bool inverted) { return inverted ? "Inverted" : "Normal"; }

    juce::String shortAlignment (Alignment a)
    {
        switch (a)
        {
            case Alignment::linkwitzRiley: return "LR";
            case Alignment::butterworth:   return "BW";
            case Alignment::bessel:        return "BE";
        }
        return {};
    }

    juce::String filterText (const FilterSpec& f, bool present)
    {
        if (! present || ! f.enabled)
            return "-";
        return theme::formatHz (f.freq, 0) + "  " + shortAlignment (f.alignment) + " " + juce::String (f.slopeDbPerOct) + " dB/oct";
    }
}

//==============================================================================
void SettingsTable::setConfig (const SystemConfig& cfg)
{
    config = cfg;
    repaint();
}

int SettingsTable::rowHeight (const OutputChannel& ch) const
{
    const int bands = (int) ch.settings.peq.size();
    return std::max (44, 14 + 18 * std::max (1, bands) + 10);
}

int SettingsTable::getIdealHeight (int width) const
{
    juce::ignoreUnused (width);
    int h = 32; // header
    for (auto& ch : config.channels)
        h += rowHeight (ch);
    return h + 2;
}

void SettingsTable::paint (juce::Graphics& g)
{
    auto area = getLocalBounds();
    const bool small = compact (getWidth());

    // Column plan (stripe is drawn in the first 6px of the row).
    struct Col { juce::String title; int width; juce::Justification just; };
    const int eqW = std::max (200, getWidth() - (small ? 520 : 724));
    std::vector<Col> cols = {
        { "Out",       48,  juce::Justification::centredLeft },
        { "Channel",   small ? 150 : 190, juce::Justification::centredLeft },
        { "Gain",      72,  juce::Justification::centredRight },
        { "Delay",     small ? 90 : 118, juce::Justification::centredRight },
        { "Polarity",  small ? 0 : 76,  juce::Justification::centredLeft },
        { "High-pass", small ? 0 : 162, juce::Justification::centredLeft },
        { "Low-pass",  small ? 0 : 162, juce::Justification::centredLeft },
        { "EQ",        eqW, juce::Justification::centredLeft },
    };

    auto header = area.removeFromTop (32);
    g.setColour (theme::bg3.withAlpha (0.6f));
    g.fillRoundedRectangle (header.toFloat(), theme::radiusSm);
    {
        auto h = header.withTrimmedLeft (14);
        g.setColour (theme::textMuted);
        g.setFont (theme::fontBold (10.5f));
        for (auto& c : cols)
        {
            if (c.width == 0) continue;
            g.drawText (c.title.toUpperCase(), h.removeFromLeft (c.width).reduced (6, 0), c.just);
        }
    }

    const auto mono = theme::mono (12.5f);
    const auto sans = theme::font (12.5f);
    bool odd = false;
    for (auto& ch : config.channels)
    {
        auto row = area.removeFromTop (rowHeight (ch));
        if (odd)
        {
            g.setColour (juce::Colours::white.withAlpha (0.018f));
            g.fillRect (row);
        }
        odd = ! odd;
        g.setColour (theme::border);
        g.fillRect (row.getX(), row.getBottom() - 1, row.getWidth(), 1);

        const auto colour = theme::sideTint (theme::roleColour (ch.role), ch.side);
        g.setColour (colour);
        g.fillRoundedRectangle (row.removeFromLeft (6).reduced (1, 8).toFloat(), 2.0f);
        row.removeFromLeft (8);

        const auto& s = ch.settings;
        auto cell = [&] (int i) { return row.removeFromLeft (cols[(size_t) i].width).reduced (6, 0); };
        auto topLine = [] (juce::Rectangle<int> r) { return r.withHeight (32).translated (0, 6); };

        g.setFont (mono);
        g.setColour (theme::textDim);
        g.drawText (juce::String (ch.dspOutput + 1), topLine (cell (0)), cols[0].just);

        {
            auto c = cell (1);
            g.setColour (theme::text);
            g.setFont (theme::fontMedium (13.0f));
            g.drawText (ch.label, topLine (c).withTrimmedBottom (12).translated (0, 2), juce::Justification::centredLeft, true);
            g.setColour (colour.withAlpha (0.9f));
            g.setFont (theme::font (10.5f));
            g.drawText (toString (ch.role).toUpperCase() + (s.muted ? "  -  MUTED" : ""), topLine (c).withTrimmedTop (18), juce::Justification::centredLeft);
        }

        g.setFont (mono);
        g.setColour (theme::text);
        g.drawText (theme::formatDb (s.gainDb, 1), topLine (cell (2)), cols[2].just);

        {
            auto c = cell (3);
            g.setColour (theme::text);
            g.drawText (theme::formatMs (s.delayMs, 2), topLine (c).withTrimmedBottom (12).translated (0, 2), cols[3].just);
            g.setColour (theme::textMuted);
            g.setFont (theme::font (10.5f));
            g.drawText (tune::formatDelayCm (s.delayMs), topLine (c).withTrimmedTop (18), cols[3].just);
            g.setFont (mono);
        }

        if (! small)
        {
            auto c = cell (4);
            g.setColour (s.inverted ? theme::warn : theme::textDim);
            g.setFont (sans);
            g.drawText (polarityText (s.inverted), topLine (c), cols[4].just);

            g.setColour (theme::textDim);
            g.setFont (mono);
            g.drawText (filterText (s.highPass, ch.hasHighPass()), topLine (cell (5)), cols[5].just, true);
            g.drawText (filterText (s.lowPass, ch.hasLowPass()), topLine (cell (6)), cols[6].just, true);
        }

        {
            auto c = cell (7);
            auto lines = c.withTrimmedTop (7);
            g.setFont (mono);
            if (s.peq.empty())
            {
                g.setColour (theme::textMuted);
                g.drawText ("-", lines.removeFromTop (18), juce::Justification::centredLeft);
            }
            for (auto& band : s.peq)
            {
                auto line = lines.removeFromTop (18);
                const bool boost = band.gainDb > 0.05;
                g.setColour (boost ? theme::warn.withAlpha (0.9f) : theme::textDim);
                const auto txt = theme::formatHz (band.freq, 1).paddedRight (' ', 9) + theme::formatDb (band.gainDb, 1).paddedLeft (' ', 9)
                                 + "   Q " + juce::String (band.q, 2);
                g.drawText (txt, line, juce::Justification::centredLeft, true);
            }
            if (small)
            {
                // Fold the hidden columns into the EQ column as a subtitle.
                g.setColour (theme::textMuted);
                g.setFont (theme::font (10.5f));
                juce::String extra = polarityText (s.inverted);
                if (ch.hasHighPass()) extra += "  |  HP " + filterText (s.highPass, true);
                if (ch.hasLowPass())  extra += "  |  LP " + filterText (s.lowPass, true);
                g.drawText (extra, lines.removeFromTop (16), juce::Justification::centredLeft, true);
            }
        }
    }
}

//==============================================================================
ReportDocument::ReportDocument (Session& s) : session (s)
{
    addAndMakeVisible (ring);
    ring.setCaption ("SCORE");
    for (auto* t : { &leftErr, &rightErr, &match, &duration, &sweeps })
        addAndMakeVisible (t);
    for (auto* p : { &plotL, &plotR, &plotSum })
    {
        addAndMakeVisible (p);
        p->setUnits (FrequencyPlot::Units::dB);
        p->setRanges (20.0, 20000.0, -24.0, 18.0);
        p->setShowLegend (true);
    }
    plotL.setTitle ("Left seat");
    plotR.setTitle ("Right seat");
    plotSum.setTitle ("Both sides (L+R) after tuning");
    addAndMakeVisible (judgePanel);
    addAndMakeVisible (table);
    addAndMakeVisible (findings);
    addAndMakeVisible (notes);
}

void ReportDocument::setResult (const AutoTuneEngine::Result& r)
{
    completedAt = juce::Time::getCurrentTime();
    for (auto& m : r.measurements)
        if (&m == &r.measurements.front() || m.timestamp > completedAt)
            completedAt = m.timestamp;

    result = r;
    result.measurements.clear();           // IRs are large; the document only needs the count

    ring.setScore (r.score, true);
    auto errColour = [] (double e) { return e <= 1.5 ? theme::good : e <= 3.0 ? theme::warn : theme::bad; };
    leftErr.set (juce::String (r.leftErrorDb, 1), errColour (r.leftErrorDb), "RMS vs target, 40 Hz - 12 kHz");
    rightErr.set (juce::String (r.rightErrorDb, 1), errColour (r.rightErrorDb), "RMS vs target, 40 Hz - 12 kHz");
    match.set (juce::String (r.matchDb, 1), errColour (r.matchDb), "Left vs right, 100 Hz - 10 kHz");
    duration.set (tune::formatDuration (r.seconds), theme::text, "Whole run, including verification");
    sweeps.set (juce::String ((int) r.measurements.size()), theme::text,
                juce::String ((int) r.tunedConfig.channels.size()) + " outputs");

    judgePanel.set (r.sqScore, r.refineCostBefore, r.refineCostAfter);
    judgePanel.setVisible (judgePanel.hasData());
    tune::populateResultPlot (plotL, r, 0);
    tune::populateResultPlot (plotR, r, 1);
    tune::populateResultPlot (plotSum, r, 2);
    table.setConfig (r.tunedConfig);
    findings.setFindings (tune::buildFindings (r, r.tunedConfig));
    notes.setGroups (tune::groupNotes (r.report.notes, r.tunedConfig));
    resized();
    repaint();
}

ReportDocument::Layout ReportDocument::computeLayout (int width) const
{
    Layout L;
    const int sheetW = std::min (sheetMaxWidth, width);
    const int margin = (width - sheetW) / 2 + docMargin;    // sheet is centred when the page is wider
    const int contentW = sheetW - docMargin * 2;
    const bool wide = contentW >= 760;
    int y = docMargin;

    L.sheet = { margin - docMargin, 0, sheetW, 0 };
    L.header = { margin, y, contentW, 118 };
    L.ring = { margin + contentW - 112, y - 6, 112, 118 };
    y += 118 + 24;

    L.metrics = { margin, y, contentW, tileH };
    y += tileH + sectionGap;

    if (judgePanel.hasData())
    {
        L.judgeTitle = { margin, y, contentW, titleH };
        y += titleH + 10;
        const int jH = judgePanel.getIdealHeight (contentW);
        L.judge = { margin, y, contentW, jH };
        y += jH + sectionGap;
    }

    L.plotsTitle = { margin, y, contentW, titleH };
    y += titleH + 8;
    if (wide)
    {
        const int half = (contentW - 16) / 2;
        L.plotL = { margin, y, half, plotH };
        L.plotR = { margin + half + 16, y, contentW - half - 16, plotH };
        y += plotH + 16;
    }
    else
    {
        L.plotL = { margin, y, contentW, plotH };
        y += plotH + 16;
        L.plotR = { margin, y, contentW, plotH };
        y += plotH + 16;
    }
    L.plotSum = { margin, y, contentW, 220 };
    y += 220 + sectionGap;

    L.tableTitle = { margin, y, contentW, titleH };
    y += titleH + 8;
    const int tableH = table.getIdealHeight (contentW);
    L.table = { margin, y, contentW, tableH };
    y += tableH + sectionGap;

    L.findingsTitle = { margin, y, contentW, titleH };
    y += titleH + 4;
    const int fH = findings.getIdealHeight (contentW);
    L.findings = { margin, y, contentW, fH };
    y += fH + sectionGap;

    L.notesTitle = { margin, y, contentW, titleH };
    y += titleH + 4;
    const int nH = notes.getIdealHeight (contentW);
    L.notes = { margin, y, contentW, nH };
    y += nH + sectionGap;

    L.footer = { margin, y, contentW, 24 };
    y += 24 + docMargin;
    L.total = y;
    L.sheet.setHeight (y);
    return L;
}

int ReportDocument::idealHeight (int width) const
{
    return computeLayout (width).total;
}

void ReportDocument::resized()
{
    const auto L = computeLayout (getWidth());
    ring.setBounds (L.ring);

    auto tiles = L.metrics;
    MetricTile* ts[] = { &leftErr, &rightErr, &match, &duration, &sweeps };
    const int tw = (tiles.getWidth() - 4 * 12) / 5;
    for (int i = 0; i < 5; ++i)
    {
        ts[i]->setBounds (tiles.removeFromLeft (tw));
        tiles.removeFromLeft (12);
    }

    judgePanel.setBounds (L.judge);
    plotL.setBounds (L.plotL);
    plotR.setBounds (L.plotR);
    plotSum.setBounds (L.plotSum);
    table.setBounds (L.table);
    findings.setBounds (L.findings);
    notes.setBounds (L.notes);
}

void ReportDocument::drawSectionTitle (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title, const juce::String& aside) const
{
    g.setColour (theme::text);
    g.setFont (theme::display (17.0f));
    g.drawText (title, r.removeFromTop (22), juce::Justification::centredLeft);
    if (aside.isNotEmpty())
    {
        g.setColour (theme::textMuted);
        g.setFont (theme::font (11.5f));
        g.drawText (aside, r.withY (r.getY() - 22).withHeight (22), juce::Justification::centredRight);
    }
    g.setColour (theme::borderHi);
    g.fillRect (r.getX(), r.getY() + 4, r.getWidth(), 1);
}

void ReportDocument::paint (juce::Graphics& g)
{
    const auto L = computeLayout (getWidth());

    // The "sheet"
    auto sheet = L.sheet.toFloat();
    theme::fillCard (g, sheet, theme::bg2, theme::border, theme::radius);
    g.setColour (theme::accent.withAlpha (0.9f));
    g.fillRoundedRectangle (sheet.withHeight (3.0f).reduced (theme::radius, 0.0f), 1.5f);
    const auto& cfg = result.tunedConfig;

    // Header
    auto h = L.header.withTrimmedRight (140);
    g.setColour (theme::accent);
    g.setFont (theme::fontBold (11.0f));
    g.drawText ("FALSEAUDIO  -  AUTO TUNE REPORT", h.removeFromTop (18), juce::Justification::centredLeft);
    h.removeFromTop (6);
    g.setColour (theme::text);
    g.setFont (theme::display (30.0f));
    g.drawText (cfg.name.isNotEmpty() ? cfg.name : "Untitled car", h.removeFromTop (40), juce::Justification::centredLeft, true);
    h.removeFromTop (6);
    g.setColour (theme::textDim);
    g.setFont (theme::font (13.0f));
    const juce::String meta = completedAt.formatted ("%d %B %Y, %H:%M") + "   |   " + tune::layoutSummary (cfg)
                              + "   |   Target: " + cfg.target.name
                              + "   |   " + (session.getMode() == Session::Mode::demo ? "Demo (virtual car)" : "Live")
                              + "   |   Seat: " + (cfg.listeningSeat == 0 ? "driver" : cfg.listeningSeat == 1 ? "passenger" : "centre");
    g.drawText (meta, h.removeFromTop (20), juce::Justification::centredLeft, true);
    g.setColour (theme::textMuted);
    g.setFont (theme::font (12.0f));
    g.drawText ("DSP: " + tune::dspKindName (session) + "   |   " + juce::String (cfg.caps.peqBandsPerChannel)
                + " PEQ / channel   |   delay step " + juce::String (cfg.caps.delayStepMs, 2) + " ms",
                h.removeFromTop (18), juce::Justification::centredLeft, true);

    if (judgePanel.hasData())
        drawSectionTitle (g, L.judgeTitle, "Simulated SQ judge",
                          "Weighted categories, 0-100 each" + juce::String (result.refineCostBefore > 0.0 ? "  |  after competition refine" : ""));
    drawSectionTitle (g, L.plotsTitle, "Before and after", "1/6-octave smoothed  |  0 dB = target midrange level  |  dashed = target");
    drawSectionTitle (g, L.tableTitle, "Final DSP settings", juce::String ((int) cfg.channels.size()) + " outputs - type these into any DSP");
    drawSectionTitle (g, L.findingsTitle, "Findings", result.report.warnings.isEmpty() ? "No warnings" : juce::String (result.report.warnings.size()) + " warnings");
    drawSectionTitle (g, L.notesTitle, "What the tuner decided", juce::String (result.report.notes.size()) + " notes");

    g.setColour (theme::border);
    g.fillRect (L.footer.getX(), L.footer.getY() - 10, L.footer.getWidth(), 1);
    g.setColour (theme::textMuted);
    g.setFont (theme::font (11.0f));
    g.drawText ("Generated by FalseAudio  -  " + completedAt.formatted ("%Y-%m-%d %H:%M"), L.footer, juce::Justification::centredLeft);
    g.drawText ("Score " + juce::String (result.score) + " / 100", L.footer, juce::Justification::centredRight);
}
} // namespace fa::ui
