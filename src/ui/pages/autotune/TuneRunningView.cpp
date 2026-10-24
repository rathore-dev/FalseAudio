#include "TuneRunningView.h"
#include "core/dsp/Spectrum.h"

namespace fa::ui
{
namespace
{
    constexpr int timelineH = 132;
    constexpr int statusH = 64;
}

TuneRunningView::TuneRunningView (Session& s) : session (s)
{
    addAndMakeVisible (timeline);
    addAndMakeVisible (plot);
    addAndMakeVisible (cancelButton);

    plot.setUnits (FrequencyPlot::Units::dB);
    plot.setRanges (20.0, 20000.0, -48.0, 12.0);
    plot.setShowLegend (true);
    plot.setTitle ("Live measurements - relative level");

    cancelButton.onClick = [this] { if (onCancel) onCancel(); };
}

TuneRunningView::~TuneRunningView() = default;

void TuneRunningView::begin()
{
    timeline.reset();
    plot.clearTraces();
    message = "Starting";
    stage = Stage::idle;
    measurementCount = 0;
    haveReference = false;
    overallFraction = 0.0;
    frozenSeconds = -1.0;
    startMs = juce::Time::getMillisecondCounterHiRes();
    cancelButton.setEnabled (true);
    startTimerHz (10);
    repaint();
}

void TuneRunningView::setStage (Stage s)
{
    stage = s;
    timeline.setStage (s);
    const int idx = StageTimeline::stageIndex (s);
    if (idx >= 0)
        overallFraction = (double) idx / AutoTuneEngine::numWorkStages;
    repaint();
}

void TuneRunningView::setProgress (Stage s, double fraction, const juce::String& msg)
{
    timeline.setProgress (s, fraction);
    if (msg.isNotEmpty())
        message = msg;
    const int idx = StageTimeline::stageIndex (s);
    if (idx >= 0)
        overallFraction = std::max (overallFraction, ((double) idx + juce::jlimit (0.0, 1.0, fraction)) / AutoTuneEngine::numWorkStages);
    repaint (statusRow);
}

void TuneRunningView::setOutcome (Stage finalStage)
{
    timeline.setOutcome (finalStage);
    frozenSeconds = getElapsedSeconds();
    cancelButton.setEnabled (false);
    stopTimer();
    repaint();
}

double TuneRunningView::getElapsedSeconds() const
{
    if (frozenSeconds >= 0.0)
        return frozenSeconds;
    return (juce::Time::getMillisecondCounterHiRes() - startMs) / 1000.0;
}

void TuneRunningView::addMeasurement (const Measurement& m)
{
    ++measurementCount;
    if (m.response.isEmpty())
        return;

    const auto& cfg = session.getConfig();
    auto sm = smoothFractionalOctave (m.response, 6.0);
    auto db = sm.magnitudeDb();

    // Normalise the display to the first real driver measurement so the scale is meaningful
    // whatever the absolute calibration of the mic.
    if (! haveReference && cfg.find (m.channelId) != nullptr)
    {
        std::vector<double> inBand;
        for (size_t i = 0; i < sm.freqs.size(); ++i)
            if (sm.freqs[i] >= 40.0 && sm.freqs[i] <= 16000.0)
                inBand.push_back (db[i]);
        if (! inBand.empty())
        {
            std::sort (inBand.begin(), inBand.end());
            referenceDb = inBand[(size_t) ((double) (inBand.size() - 1) * 0.95)];
            haveReference = true;
        }
    }
    if (haveReference)
        for (auto& v : db) v -= referenceDb;

    const bool isDriver = cfg.find (m.channelId) != nullptr;
    const auto colour = tune::colourForChannelId (cfg, m.channelId);
    juce::String label = tune::labelForChannelId (cfg, m.channelId);
    if (! isDriver)
        label = m.channelId;

    auto t = PlotTrace::fromDb (m.channelId, label, colour, sm.freqs, db);
    t.thickness = isDriver ? 1.8f : 2.4f;
    t.fillToBottom = isDriver;
    t.dashed = m.channelId.startsWithIgnoreCase ("Level");
    if (! isDriver && ! m.channelId.startsWithIgnoreCase ("Verify"))
        t.dashed = true;
    plot.setTrace (t);

    // Verify curves are the headline once they exist: dim the individual drivers a touch.
    if (m.channelId.startsWithIgnoreCase ("Verify"))
        for (auto& c : cfg.channels)
            plot.setTraceVisible (c.id, false);

    repaint (statusRow);
}

void TuneRunningView::timerCallback()
{
    repaint (statusRow);
}

void TuneRunningView::resized()
{
    auto area = getLocalBounds();
    timelineCard = area.removeFromTop (timelineH + 24);
    timeline.setBounds (timelineCard.reduced (theme::pad, 12));
    area.removeFromTop (theme::pad);

    statusRow = area.removeFromTop (statusH);
    cancelButton.setBounds (statusRow.reduced (theme::pad, 0).removeFromRight (120).withSizeKeepingCentre (120, 36));
    area.removeFromTop (theme::pad);

    plotCard = area;
    plot.setBounds (plotCard.reduced (theme::pad, theme::pad).withTrimmedTop (22));
}

void TuneRunningView::paint (juce::Graphics& g)
{
    theme::fillCard (g, timelineCard.toFloat());

    // Status card: pulse dot, message, overall progress bar, elapsed, measurement counter.
    theme::fillCard (g, statusRow.toFloat(), theme::bg2, theme::border);
    auto s = statusRow.reduced (theme::pad, 0);
    s.removeFromRight (136);

    const bool running = frozenSeconds < 0.0;
    auto dot = s.removeFromLeft (22).withSizeKeepingCentre (10, 10).toFloat();
    const float blink = running ? 0.6f + 0.4f * std::sin ((float) juce::Time::getMillisecondCounter() * 0.006f) : 1.0f;
    g.setColour (theme::accent.withAlpha (0.25f * blink));
    g.fillEllipse (dot.expanded (5.0f));
    g.setColour (theme::accent);
    g.fillEllipse (dot);

    s.removeFromLeft (8);
    auto right = s.removeFromRight (220);
    auto textCol = s;

    g.setColour (theme::textMuted);
    g.setFont (theme::fontBold (10.5f));
    auto top = textCol.removeFromTop (statusH / 2).withTrimmedTop (10);
    g.drawText ((AutoTuneEngine::stageName (stage) + "  -  step " + juce::String (std::max (1, StageTimeline::stageIndex (stage) + 1))
                 + " of " + juce::String (AutoTuneEngine::numWorkStages)).toUpperCase(),
                top, juce::Justification::centredLeft);
    g.setColour (theme::text);
    g.setFont (theme::fontMedium (14.5f));
    g.drawText (message, textCol.withTrimmedBottom (8), juce::Justification::centredLeft, true);

    // Elapsed + count
    g.setColour (theme::textMuted);
    g.setFont (theme::fontBold (10.5f));
    auto rtop = right.removeFromTop (statusH / 2).withTrimmedTop (10);
    g.drawText ("ELAPSED", rtop.removeFromLeft (110), juce::Justification::centredLeft);
    g.drawText ("SWEEPS", rtop, juce::Justification::centredLeft);
    auto rbot = right.withTrimmedBottom (8);
    g.setColour (theme::text);
    g.setFont (theme::mono (15.0f));
    g.drawText (tune::formatDuration (getElapsedSeconds()), rbot.removeFromLeft (110), juce::Justification::centredLeft);
    g.drawText (juce::String (measurementCount), rbot, juce::Justification::centredLeft);

    // Thin overall progress along the bottom edge of the status card.
    auto bar = statusRow.toFloat().removeFromBottom (3.0f).reduced (theme::radius, 0.0f);
    g.setColour (theme::bg4);
    g.fillRoundedRectangle (bar, 1.5f);
    g.setColour (theme::accent);
    g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * (float) overallFraction), 1.5f);

    theme::fillCard (g, plotCard.toFloat());
    g.setColour (theme::textMuted);
    g.setFont (theme::fontBold (10.5f));
    g.drawText ("LIVE MEASUREMENTS", plotCard.reduced (theme::pad, 0).withHeight (34).withY (plotCard.getY() + 2),
                juce::Justification::centredLeft);
    g.setColour (theme::textMuted);
    g.setFont (theme::font (11.5f));
    g.drawText ("Each driver appears as it is measured; verify sweeps replace them",
                plotCard.reduced (theme::pad, 0).withHeight (34).withY (plotCard.getY() + 2).withTrimmedRight (140),
                juce::Justification::centredRight);
}
} // namespace fa::ui
