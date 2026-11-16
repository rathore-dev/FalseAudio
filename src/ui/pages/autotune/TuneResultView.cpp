#include "TuneResultView.h"

namespace fa::ui
{
namespace
{
    constexpr int gap = theme::pad;
    constexpr int scoreCardW = 312;
    constexpr int topRowH = 360;
    constexpr int tileH = 84;
    constexpr int buttonRowH = 48;
    constexpr int cardHeaderH = 48;

    juce::String verdictFor (int score)
    {
        if (score >= 90) return "Reference-grade";
        if (score >= 80) return "Excellent";
        if (score >= 65) return "Good";
        if (score >= 45) return "Fair - check findings";
        return "Needs attention";
    }

    juce::String verdictDetail (const AutoTuneEngine::Result& r)
    {
        const double err = 0.5 * (r.leftErrorDb + r.rightErrorDb);
        juce::String s = "Average deviation from target " + juce::String (err, 1) + " dB, left/right match "
                         + juce::String (r.matchDb, 1) + " dB.";
        if (! r.report.warnings.isEmpty())
            s += " " + juce::String (r.report.warnings.size()) + (r.report.warnings.size() == 1 ? " finding." : " findings.");
        return s;
    }
}

//==============================================================================
class TuneResultView::ScorePanel : public juce::Component
{
public:
    ScorePanel()
    {
        addAndMakeVisible (ring);
        ring.setCaption ("TUNE SCORE");
    }

    void set (const AutoTuneEngine::Result& r)
    {
        result = r;
        result.measurements.clear();
        ring.setScore (r.score, true);
        repaint();
    }

    void resized() override
    {
        auto a = getLocalBounds();
        const int d = std::min (a.getWidth() - 24, 196);
        ring.setBounds (a.removeFromTop (d + 16).withSizeKeepingCentre (d, d));
    }

    void paint (juce::Graphics& g) override
    {
        auto a = getLocalBounds();
        a.removeFromTop (std::min (a.getWidth() - 24, 196) + 24);
        g.setColour (theme::scoreColour (result.score));
        g.setFont (theme::display (20.0f));
        g.drawText (verdictFor (result.score), a.removeFromTop (28), juce::Justification::centred);

        juce::AttributedString as;
        as.setJustification (juce::Justification::centred);
        as.append (verdictDetail (result), theme::font (12.5f), theme::textDim);
        juce::TextLayout tl;
        tl.createLayout (as, (float) a.getWidth() - 24.0f);
        tl.draw (g, a.reduced (12, 6).toFloat());
    }

private:
    ScoreRing ring;
    AutoTuneEngine::Result result;
};

//==============================================================================
class TuneResultView::Content : public juce::Component
{
public:
    explicit Content (TuneResultView& o) : owner (o)
    {
        addAndMakeVisible (scoreCard);
        scoreCard.addAndMakeVisible (scorePanel);

        addAndMakeVisible (plotCard);
        plotCard.addAndMakeVisible (plot);
        plotCard.addAndMakeVisible (sideSelect);
        plot.setUnits (FrequencyPlot::Units::dB);
        plot.setRanges (20.0, 20000.0, -24.0, 18.0);
        plot.setShowLegend (true);
        sideSelect.onChange = [this] (int i) { side = i; refreshPlot(); };

        for (auto* t : { &leftErr, &rightErr, &match, &duration, &sweeps })
            addAndMakeVisible (t);

        addChildComponent (judgeCard);
        judgeCard.addAndMakeVisible (judge);

        addAndMakeVisible (findingsCard);
        findingsCard.addAndMakeVisible (findings);
        addAndMakeVisible (notesCard);
        notesCard.addAndMakeVisible (notes);

        addAndMakeVisible (reportButton);
        addAndMakeVisible (againButton);
        reportButton.onClick = [this] { if (owner.onViewReport) owner.onViewReport(); };
        againButton.onClick  = [this] { if (owner.onRunAgain) owner.onRunAgain(); };
    }

    void set (const AutoTuneEngine::Result& r)
    {
        result = r;
        result.measurements.clear();       // IRs are large; only the count is shown
        const auto& cfg = r.tunedConfig;
        scorePanel.set (result);

        auto errColour = [] (double e) { return e <= 1.5 ? theme::good : e <= 3.0 ? theme::warn : theme::bad; };
        leftErr.set (juce::String (r.leftErrorDb, 1), errColour (r.leftErrorDb), "RMS vs target, 40 Hz - 12 kHz");
        rightErr.set (juce::String (r.rightErrorDb, 1), errColour (r.rightErrorDb), "RMS vs target, 40 Hz - 12 kHz");
        match.set (juce::String (r.matchDb, 1), errColour (r.matchDb), "Left vs right, 100 Hz - 10 kHz");
        duration.set (tune::formatDuration (r.seconds), theme::text, "Measure, solve, apply, verify");
        sweeps.set (juce::String ((int) r.measurements.size()), theme::text,
                    juce::String ((int) cfg.channels.size()) + " outputs, closed-loop verified");

        judge.set (r.sqScore, r.refineCostBefore, r.refineCostAfter);
        judgeCard.setVisible (judge.hasData());
        judgeCard.setHighlight (theme::scoreColour (juce::roundToInt (r.sqScore.total)).withAlpha (0.6f));

        findings.setFindings (tune::buildFindings (r, cfg));
        notes.setGroups (tune::groupNotes (r.report.notes, cfg));
        const int nFind = (int) tune::buildFindings (r, cfg).size();
        findingsCard.setTitle ("Findings", nFind == 1 ? "1 item" : juce::String (nFind) + " items");
        findingsCard.setHighlight (r.report.warnings.isEmpty() ? theme::good : theme::warn);
        notesCard.setTitle ("What changed", juce::String (r.report.notes.size()) + " decisions, grouped by output");

        sideSelect.setSelected (0);
        side = 0;
        refreshPlot();
        resized();
    }

    int idealHeight (int width) const
    {
        const bool stacked = width < 900;
        int h = stacked ? (topRowH - 40) + gap + topRowH : topRowH;
        h += gap + tileH;
        if (judge.hasData())
            h += gap + judge.getIdealHeight (width - 2 * theme::pad) + cardHeaderH + theme::pad;
        const int halfW = stacked ? width : (width - gap) / 2;
        const int fH = findings.getIdealHeight (halfW - 2 * theme::pad) + cardHeaderH + theme::pad;
        const int nH = notes.getIdealHeight (halfW - 2 * theme::pad) + cardHeaderH + theme::pad;
        h += gap + (stacked ? fH + gap + nH : std::max (fH, nH));
        h += gap + buttonRowH + gap;
        return h;
    }

    void resized() override
    {
        auto area = getLocalBounds();
        const int width = area.getWidth();
        const bool stacked = width < 900;

        if (stacked)
        {
            scoreCard.setBounds (area.removeFromTop (topRowH - 40));
            area.removeFromTop (gap);
            plotCard.setBounds (area.removeFromTop (topRowH));
        }
        else
        {
            auto row = area.removeFromTop (topRowH);
            scoreCard.setBounds (row.removeFromLeft (scoreCardW));
            row.removeFromLeft (gap);
            plotCard.setBounds (row);
        }
        scorePanel.setBounds (scoreCard.getContentArea());
        plot.setBounds (plotCard.getContentArea());
        sideSelect.setBounds (plotCard.getHeaderActionArea().removeFromRight (210).withSizeKeepingCentre (210, 28));

        area.removeFromTop (gap);
        auto tiles = area.removeFromTop (tileH);
        MetricTile* ts[] = { &leftErr, &rightErr, &match, &duration, &sweeps };
        const int tw = (tiles.getWidth() - 4 * gap) / 5;
        for (int i = 0; i < 5; ++i)
        {
            ts[i]->setBounds (tiles.removeFromLeft (tw));
            tiles.removeFromLeft (gap);
        }

        if (judge.hasData())
        {
            area.removeFromTop (gap);
            judgeCard.setBounds (area.removeFromTop (judge.getIdealHeight (width - 2 * theme::pad) + cardHeaderH + theme::pad));
            judge.setBounds (judgeCard.getContentArea());
        }

        area.removeFromTop (gap);
        const int halfW = stacked ? width : (width - gap) / 2;
        const int fH = findings.getIdealHeight (halfW - 2 * theme::pad) + cardHeaderH + theme::pad;
        const int nH = notes.getIdealHeight (halfW - 2 * theme::pad) + cardHeaderH + theme::pad;
        if (stacked)
        {
            findingsCard.setBounds (area.removeFromTop (fH));
            area.removeFromTop (gap);
            notesCard.setBounds (area.removeFromTop (nH));
        }
        else
        {
            auto row = area.removeFromTop (std::max (fH, nH));
            // each card takes only its own content height (no empty stretched box beside a taller one)
            findingsCard.setBounds (row.removeFromLeft (halfW).removeFromTop (fH));
            row.removeFromLeft (gap);
            notesCard.setBounds (row.removeFromTop (nH));
        }
        findings.setBounds (findingsCard.getContentArea());
        notes.setBounds (notesCard.getContentArea());

        area.removeFromTop (gap);
        auto buttons = area.removeFromTop (buttonRowH);
        reportButton.setBounds (buttons.removeFromLeft (200).withSizeKeepingCentre (200, 44));
        buttons.removeFromLeft (12);
        againButton.setBounds (buttons.removeFromLeft (150).withSizeKeepingCentre (150, 44));
    }

private:
    void refreshPlot()
    {
        tune::populateResultPlot (plot, result, side);
        plot.setTitle (side == 0 ? "Left seat response" : side == 1 ? "Right seat response" : "Both sides together");
    }

    TuneResultView& owner;
    AutoTuneEngine::Result result;
    int side = 0;

    Card scoreCard { "Score", "Overall result" };
    ScorePanel scorePanel;
    Card plotCard { "Before vs after", "1/6-octave smoothed, 0 dB = target midrange" };
    FrequencyPlot plot;
    SegmentedControl sideSelect { juce::StringArray { "Left", "Right", "L+R" } };

    MetricTile leftErr  { "Left error", "dB" };
    MetricTile rightErr { "Right error", "dB" };
    MetricTile match    { "L / R match", "dB" };
    MetricTile duration { "Duration", "" };
    MetricTile sweeps   { "Measurements", "" };

    Card judgeCard { "Simulated SQ judge", "How a competition judge would score the final settings" };
    JudgePanel judge;
    Card findingsCard { "Findings" };
    FindingsList findings;
    Card notesCard { "What changed" };
    NotesView notes;

    FaButton reportButton { "View full results", FaButton::Style::primary, icons::Id::document };
    FaButton againButton  { "Run again", FaButton::Style::secondary, icons::Id::refresh };
};

//==============================================================================
class TuneResultView::FailurePanel : public juce::Component
{
public:
    explicit FailurePanel (TuneResultView& o) : owner (o)
    {
        addAndMakeVisible (timeline);
        addAndMakeVisible (retry);
        retry.onClick = [this] { if (owner.onRunAgain) owner.onRunAgain(); };
    }

    void set (const juce::String& e, bool c, Stage stoppedAt, double secs)
    {
        error = e;
        cancelled = c;
        seconds = secs;
        timeline.reset();
        timeline.setStage (stoppedAt);
        timeline.setOutcome (c ? Stage::cancelled : Stage::failed);
        retry.setButtonText (c ? "Start again" : "Try again");
        repaint();
    }

    void resized() override
    {
        auto a = getLocalBounds();
        timeline.setBounds (a.removeFromTop (156).reduced (theme::pad, 12));
        a.removeFromTop (gap);
        card = a.removeFromTop (220);
        retry.setBounds (card.withSizeKeepingCentre (180, 44).withY (card.getBottom() - 44 - 28));
    }

    void paint (juce::Graphics& g) override
    {
        theme::fillCard (g, getLocalBounds().withHeight (156).toFloat());

        const auto colour = cancelled ? theme::warn : theme::bad;
        theme::fillCard (g, card.toFloat(), theme::bg2, colour.withAlpha (0.45f));
        {
            juce::Graphics::ScopedSaveState ss (g);
            juce::Path clip;
            clip.addRoundedRectangle (card.toFloat(), theme::radius);
            g.reduceClipRegion (clip);
            g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.12f), (float) card.getCentreX(), (float) card.getY(),
                                                     colour.withAlpha (0.0f), (float) card.getCentreX(), (float) card.getY() + 140.0f, false));
            g.fillRect (card);
        }

        auto a = card.reduced (32, 24);
        auto bubble = a.removeFromTop (44).withSizeKeepingCentre (44, 44).toFloat();
        g.setColour (colour.withAlpha (0.16f));
        g.fillEllipse (bubble);
        g.setColour (colour.withAlpha (0.6f));
        g.drawEllipse (bubble.reduced (0.5f), 1.0f);
        icons::draw (g, cancelled ? icons::Id::stop : icons::Id::warning, bubble.reduced (12.0f), colour);

        a.removeFromTop (12);
        g.setColour (theme::text);
        g.setFont (theme::display (22.0f));
        g.drawText (cancelled ? "Auto tune cancelled" : "Auto tune stopped", a.removeFromTop (30), juce::Justification::centred);

        juce::AttributedString as;
        as.setJustification (juce::Justification::centred);
        as.append (cancelled ? "Nothing was changed permanently. Your previous settings are still in the DSP." : error,
                   theme::font (13.5f), theme::textDim);
        if (! cancelled && seconds > 0.0)
            as.append ("  (after " + tune::formatDuration (seconds) + ")", theme::font (13.5f), theme::textMuted);
        juce::TextLayout tl;
        tl.createLayout (as, (float) std::min (640, a.getWidth()));
        tl.draw (g, a.removeFromTop (46).withSizeKeepingCentre (std::min (640, a.getWidth()), 46).toFloat());
    }

private:
    TuneResultView& owner;
    StageTimeline timeline;
    FaButton retry { "Try again", FaButton::Style::primary, icons::Id::refresh };
    juce::String error;
    bool cancelled = false;
    double seconds = 0.0;
    juce::Rectangle<int> card;
};

//==============================================================================
TuneResultView::TuneResultView (Session& s) : session (s)
{
    content = std::make_unique<Content> (*this);
    failure = std::make_unique<FailurePanel> (*this);
    viewport.setViewedComponent (content.get(), false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);
    addChildComponent (*failure);
}

TuneResultView::~TuneResultView() = default;

void TuneResultView::showResult (const AutoTuneEngine::Result& r)
{
    failure->setVisible (false);
    viewport.setVisible (true);
    content->set (r);
    resized();
    viewport.setViewPosition (0, 0);
}

void TuneResultView::showFailure (const juce::String& error, bool cancelled, Stage stoppedAt, double seconds)
{
    viewport.setVisible (false);
    failure->setVisible (true);
    failure->set (error, cancelled, stoppedAt, seconds);
    resized();
}

void TuneResultView::resized()
{
    viewport.setBounds (getLocalBounds());
    int w = viewport.getWidth();
    int h = content->idealHeight (w);
    if (h > viewport.getHeight())                         // leave room for the scrollbar only when it shows
    {
        w -= viewport.getScrollBarThickness() + 6;
        h = content->idealHeight (w);
    }
    content->setSize (w, std::max (viewport.getHeight(), h));
    failure->setBounds (getLocalBounds());
}

void TuneResultView::paint (juce::Graphics&) {}
} // namespace fa::ui
