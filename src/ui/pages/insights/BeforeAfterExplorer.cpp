#include "BeforeAfterExplorer.h"
#include "core/measure/Measurement.h"
#include "core/dsp/Spectrum.h"

namespace fa::ui
{
namespace
{
    constexpr int bottomRowH = 34;

    double meanOver (const std::vector<double>& freqs, const std::vector<double>& db, double f1, double f2)
    {
        double acc = 0.0;
        int n = 0;
        for (size_t i = 0; i < freqs.size() && i < db.size(); ++i)
            if (freqs[i] >= f1 && freqs[i] <= f2)
                { acc += db[i]; ++n; }
        return n > 0 ? acc / n : 0.0;
    }

    const juce::Colour eqLeftColour  { 0xffA78BFA };   // violet
    const juce::Colour eqRightColour { 0xffFB923C };   // orange
}

BeforeAfterExplorer::BeforeAfterExplorer()
    : Card ("Before / after explorer", "Hover for exact values  |  wheel zooms, drag pans, double-click resets")
{
    addAndMakeVisible (plot);
    plot.setUnits (FrequencyPlot::Units::dB);
    plot.setRanges (20.0, 20000.0, -24.0, 18.0);
    plot.setShowLegend (true);

    addAndMakeVisible (mode);
    mode.onChange = [this] (int i) { modeIndex = i; rebuild(); };

    for (auto* t : { &showBefore, &showAfter, &showTarget, &showEq })
    {
        addAndMakeVisible (t);
        t->setToggleState (true, juce::dontSendNotification);
        t->onClick = [this] { rebuild(); };
    }
}

void BeforeAfterExplorer::set (const AutoTuneEngine::Result& r, const insights::Insights& in)
{
    result = r;
    result.measurements.clear();
    bands = in.bands;
    reference = meanOver (analysisFrequencies(), r.targetDb, 200.0, 2000.0);
    rebuild();
}

void BeforeAfterExplorer::setHighlightBand (double centreHz)
{
    if (centreHz <= 0.0)
    {
        plot.setBands ({});
        return;
    }
    const double k = std::pow (2.0, 1.0 / 6.0);
    plot.setBands ({ { centreHz / k, centreHz * k, theme::accent.withAlpha (0.14f) } });
}

void BeforeAfterExplorer::rebuild()
{
    plot.clearTraces();
    const auto& freqs = analysisFrequencies();
    const bool sumMode = modeIndex == 2;
    showBefore.setEnabled (! sumMode);
    showBefore.setAlpha (sumMode ? 0.45f : 1.0f);

    auto shifted = [this] (const FrequencyResponse& resp, double fraction)
    {
        auto sm = fraction > 0.0 ? smoothFractionalOctave (resp, fraction) : resp;
        auto db = sm.magnitudeDb();
        for (auto& v : db)
            v -= reference;
        return std::make_pair (sm.freqs, db);
    };

    auto addResponse = [&] (const juce::String& id, const juce::String& label, juce::Colour c,
                            const FrequencyResponse& resp, float thickness, bool dashed, bool fill)
    {
        if (resp.isEmpty())
            return;
        auto [f, db] = shifted (resp, 6.0);
        auto t = PlotTrace::fromDb (id, label, c, f, db);
        t.thickness = thickness;
        t.dashed = dashed;
        t.fillToBottom = fill;
        plot.setTrace (t);
    };

    auto addEq = [&] (const juce::String& id, const juce::String& label, juce::Colour c, bool right)
    {
        if (bands.empty())
            return;
        std::vector<double> f, db;
        f.reserve (bands.size());
        db.reserve (bands.size());
        for (const auto& b : bands)
        {
            f.push_back (b.freqHz);
            db.push_back (right ? b.eqAppliedRightDb : b.eqAppliedLeftDb);
        }
        auto t = PlotTrace::fromDb (id, label, c, f, db);
        t.thickness = 1.5f;
        t.dashed = true;
        plot.setTrace (t);
    };

    if (showTarget.getToggleState() && ! result.targetDb.empty())
    {
        std::vector<double> t = result.targetDb;
        for (auto& v : t)
            v -= reference;
        auto target = PlotTrace::fromDb ("target", "Target", theme::curveTarget, freqs, t);
        target.dashed = true;
        target.thickness = 1.5f;
        plot.setTrace (target);
    }

    const bool before = showBefore.getToggleState() && ! sumMode;
    const bool after  = showAfter.getToggleState();
    const bool eq     = showEq.getToggleState();

    switch (modeIndex)
    {
        case 0:
            if (before) addResponse ("beforeL", "Before L", theme::curveBefore, result.leftBefore, 1.75f, false, false);
            if (after)  addResponse ("afterL",  "After L",  theme::curveLeft,   result.leftAfter,  2.25f, false, true);
            if (eq)     addEq ("eqL", "Net EQ L", eqLeftColour, false);
            break;

        case 1:
            if (before) addResponse ("beforeR", "Before R", theme::curveBefore, result.rightBefore, 1.75f, false, false);
            if (after)  addResponse ("afterR",  "After R",  theme::curveRight,  result.rightAfter,  2.25f, false, true);
            if (eq)     addEq ("eqR", "Net EQ R", eqRightColour, true);
            break;

        case 2:
            if (after)
            {
                addResponse ("afterL",  "After L",   theme::curveLeft.withAlpha (0.55f),  result.leftAfter,  1.0f, false, false);
                addResponse ("afterR",  "After R",   theme::curveRight.withAlpha (0.55f), result.rightAfter, 1.0f, false, false);
                addResponse ("afterLR", "After L+R", theme::curveSum,                     result.bothAfter,  2.25f, false, true);
            }
            if (eq)
            {
                addEq ("eqL", "Net EQ L", eqLeftColour, false);
                addEq ("eqR", "Net EQ R", eqRightColour, true);
            }
            break;

        default:
            if (before)
            {
                addResponse ("beforeL", "Before L", theme::curveBefore, result.leftBefore,  1.5f, false, false);
                addResponse ("beforeR", "Before R", theme::curveBefore, result.rightBefore, 1.5f, true,  false);
            }
            if (after)
            {
                addResponse ("afterL", "After L", theme::curveLeft,  result.leftAfter,  2.0f, false, false);
                addResponse ("afterR", "After R", theme::curveRight, result.rightAfter, 2.0f, false, false);
            }
            if (eq)
            {
                addEq ("eqL", "Net EQ L", eqLeftColour, false);
                addEq ("eqR", "Net EQ R", eqRightColour, true);
            }
            break;
    }
    repaint();
}

void BeforeAfterExplorer::resized()
{
    auto content = getContentArea();
    auto bottom = content.removeFromBottom (bottomRowH);
    plot.setBounds (content);

    const int tw = std::min (104, bottom.getWidth() / 5);
    for (auto* t : { &showBefore, &showAfter, &showTarget, &showEq })
        t->setBounds (bottom.removeFromLeft (tw).withSizeKeepingCentre (tw, 26));
    statsArea = bottom;

    auto action = getHeaderActionArea();
    const int mw = std::min (300, action.getWidth());
    mode.setBounds (action.removeFromRight (mw).withSizeKeepingCentre (mw, 28));
}

void BeforeAfterExplorer::paint (juce::Graphics& g)
{
    Card::paint (g);
    if (result.targetDb.empty())
        return;

    auto s = statsArea;
    auto stat = [&] (const juce::String& caption, double db, bool errorColoured)
    {
        const auto value = juce::String (db, 1) + " dB";
        const auto f = theme::mono (12.0f);
        const int vw = (int) std::ceil (theme::textWidth (f, value));
        const int cw = (int) std::ceil (theme::textWidth (theme::font (11.0f), caption));
        auto r = s.removeFromRight (vw + cw + 10);
        g.setFont (theme::font (11.0f));
        g.setColour (theme::textMuted);
        g.drawText (caption, r.removeFromLeft (cw), juce::Justification::centredLeft, false);
        r.removeFromLeft (6);
        g.setFont (f);
        g.setColour (errorColoured ? insightsui::heatColour (db, 1.5, 4.5) : theme::text);
        g.drawText (value, r, juce::Justification::centredLeft, false);
        s.removeFromRight (18);
    };

    stat ("L/R match", result.matchDb, true);
    stat ("R error", result.rightErrorDb, true);
    stat ("L error", result.leftErrorDb, true);
}
} // namespace fa::ui
