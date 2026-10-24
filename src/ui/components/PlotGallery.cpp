#include "PlotGallery.h"
#include "core/sim/VirtualCar.h"
#include "core/measure/Measurement.h"
#include "core/dsp/Spectrum.h"

namespace fa::ui
{
PlotGallery::PlotGallery (bool animate)
{
    setOpaque (true);
    for (auto* c : std::initializer_list<juce::Component*> { &magnitude, &phase, &coherence, &rta, &eq, &groupDelay,
                                                             &impulse, &etc, &ringA, &ringB, &ringC, &car })
        addAndMakeVisible (c);

    cards = {
        { "Magnitude",      "per-driver 1/6 oct, L/R sums, target", &magnitude },
        { "Phase",          "wrapped, L midbass vs midrange",        &phase },
        { "EQ editor",      "drag nodes - shift/wheel = Q",           &eq },
        { "Coherence",      "alpha-weighted trace",                   &coherence },
        { "Live RTA",       "1/3 oct bars",                           &rta },
        { "Group delay",    "ms",                                     &groupDelay },
        { "Impulse",        "front left drivers",                     &impulse },
        { "ETC",            "dB envelope, L vs R",                    &etc },
        { "Scores",         "",                                       nullptr },
        { "Car",            "3-way + sub, driver seat",               &car },
    };

    buildDemoData (animate);
}

PlotGallery::~PlotGallery() = default;

void PlotGallery::buildDemoData (bool animate)
{
    config = SystemConfig::makeLayout (3, true);
    VirtualCar vcar (config);
    const auto& freqs = analysisFrequencies();

    // ---- per-channel measurements
    std::map<juce::String, Measurement> per;
    std::vector<std::pair<juce::String, ChannelSettings>> left, right;
    for (auto& ch : config.channels)
    {
        ChannelSettings s = ch.settings;
        // a plausible crossover so the sums look like a real car
        switch (ch.role)
        {
            case DriverRole::subwoofer: s.lowPass  = FilterSpec::crossover (FilterType::lowPass, 80.0); break;
            case DriverRole::midbass:   s.highPass = FilterSpec::crossover (FilterType::highPass, 80.0);
                                        s.lowPass  = FilterSpec::crossover (FilterType::lowPass, 400.0); break;
            case DriverRole::midrange:  s.highPass = FilterSpec::crossover (FilterType::highPass, 400.0);
                                        s.lowPass  = FilterSpec::crossover (FilterType::lowPass, 3500.0); break;
            case DriverRole::tweeter:   s.highPass = FilterSpec::crossover (FilterType::highPass, 3500.0); break;
            case DriverRole::fullrange: break;
        }
        per[ch.id] = vcar.measure ({ { ch.id, s } }, 0);
        if (ch.side != Side::right) left.push_back ({ ch.id, s });
        if (ch.side != Side::left)  right.push_back ({ ch.id, s });
    }
    const auto sumL = vcar.measure (left, 0);
    const auto sumR = vcar.measure (right, 0);

    // ---- magnitude
    magnitude.setUnits (FrequencyPlot::Units::dB);
    magnitude.setRanges (20.0, 20000.0, -40.0, 20.0);
    magnitude.setTitle ("System response - driver seat");
    for (auto& ch : config.channels)
    {
        if (ch.side == Side::right) continue;
        auto t = PlotTrace::fromResponse (ch.id, ch.label.replace ("Left ", "L "), theme::roleColour (ch.role), per[ch.id].response, 6.0);
        t.thickness = 1.5f;
        magnitude.setTrace (t);
    }
    {
        auto l = PlotTrace::fromResponse ("sumL", "Left", theme::curveLeft, sumL.response, 6.0);
        l.fillToBottom = true; l.thickness = 2.2f;
        magnitude.setTrace (l);
        auto r = PlotTrace::fromResponse ("sumR", "Right", theme::curveRight, sumR.response, 6.0);
        r.thickness = 2.2f;
        magnitude.setTrace (r);

        // target, offset to the left sum's mid level
        auto levels = config.target.levels (freqs);
        const double ref = averageLevelDb (sumL.response, 300.0, 3000.0);
        for (auto& v : levels) v += ref;
        auto tgt = PlotTrace::fromDb ("target", "Target", theme::curveTarget, freqs, levels);
        tgt.dashed = true; tgt.thickness = 1.5f;
        magnitude.setTrace (tgt);
    }
    magnitude.setBands ({ { 70.0, 92.0, theme::roleSub }, { 350.0, 460.0, theme::roleMidbass }, { 3000.0, 4000.0, theme::roleMidrange } });

    // ---- phase
    phase.setUnits (FrequencyPlot::Units::degrees);
    phase.setRanges (20.0, 20000.0, -180.0, 180.0);
    phase.setTitle ("Phase");
    phase.setTrace (PlotTrace::fromPhase ("mb", "L Midbass", theme::roleMidbass, smoothFractionalOctave (per["FL.midbass"].response, 12.0)));
    phase.setTrace (PlotTrace::fromPhase ("mid", "L Midrange", theme::roleMidrange, smoothFractionalOctave (per["FL.midrange"].response, 12.0)));
    phase.setTrace (PlotTrace::fromPhase ("sum", "Left sum", theme::curveLeft, smoothFractionalOctave (sumL.response, 12.0)));

    // ---- EQ editor
    eq.setUnits (FrequencyPlot::Units::dB);
    eq.setRanges (100.0, 10000.0, -15.0, 15.0);
    eq.setTitle ("L Midrange - PEQ");
    eq.setShowLegend (false);
    eqBase = smoothFractionalOctave (per["FL.midrange"].response, 6.0);
    {
        const double ref = averageLevelDb (eqBase, 500.0, 3000.0);
        eqBase.scale (magFromDb (-ref));
    }
    eqNodes = { { "b1", 520.0, -6.0, 2.0, theme::roleMidrange, false },
                { "b2", 1600.0, -3.5, 6.0, theme::roleMidrange, true },
                { "b3", 2800.0, 2.0, 1.2, theme::roleMidrange, false } };
    eq.setNodes (eqNodes);
    eq.onNodeChanged = [this] (const FrequencyPlot::Node& n)
    {
        for (auto& nd : eqNodes)
            if (nd.id == n.id) { nd.freq = n.freq; nd.value = n.value; nd.q = n.q; }
        updateEqCurve();
    };
    eq.onNodeSelected = [this] (const juce::String& id)
    {
        for (auto& nd : eqNodes) nd.selected = nd.id == id;
    };
    eq.onBackgroundDoubleClick = [this] (double f, double v)
    {
        for (auto& nd : eqNodes) nd.selected = false;
        eqNodes.push_back ({ "b" + juce::String (eqNodes.size() + 1), f, v, 2.0, theme::roleMidrange, true });
        eq.setNodes (eqNodes);
        updateEqCurve();
    };
    updateEqCurve();

    // ---- coherence (synthetic: high in the passband, falling at the edges, noisy dips)
    coherence.setUnits (FrequencyPlot::Units::coherence);
    coherence.setRanges (20.0, 20000.0, 0.0, 1.0);
    coherence.setTitle ("Coherence");
    coherence.setShowLegend (false);
    {
        std::vector<double> coh (freqs.size());
        juce::Random rng (3);
        for (size_t i = 0; i < freqs.size(); ++i)
        {
            const double f = freqs[i];
            double c = 1.0 / (1.0 + std::pow (35.0 / f, 4.0)) * 1.0 / (1.0 + std::pow (f / 16000.0, 6.0));
            c *= 1.0 - 0.45 * std::exp (-std::pow ((std::log (f / 180.0)) / 0.12, 2.0));   // cancellation dip
            c *= 1.0 - 0.3 * std::exp (-std::pow ((std::log (f / 2400.0)) / 0.08, 2.0));
            c = juce::jlimit (0.0, 1.0, c - rng.nextDouble() * 0.03);
            coh[i] = c;
        }
        auto t = PlotTrace::fromDb ("coh", "Coherence", theme::good, freqs, coh);
        t.alpha = coh;
        t.fillToBottom = true;
        coherence.setTrace (t);
        // and the magnitude plot gets an alpha-weighted copy of the left sum to show the effect
        auto weighted = PlotTrace::fromResponse ("sumLw", "Left (coh)", theme::accentHi, sumL.response, 6.0);
        weighted.alpha = coh;
        weighted.visible = false;
        magnitude.setTrace (weighted);
    }

    // ---- RTA bars: 1/3 octave bands sampled from the left sum
    rta.setUnits (FrequencyPlot::Units::dB);
    rta.setRanges (20.0, 20000.0, -40.0, 20.0);
    rta.setTitle ("RTA");
    rta.setShowLegend (false);
    {
        std::vector<double> bf, bv;
        auto sm = smoothFractionalOctave (sumL.response, 3.0);
        for (double f = 20.0; f < 20001.0; f *= std::pow (2.0, 1.0 / 3.0))
        {
            bf.push_back (f);
            bv.push_back (dbFromMag (std::abs (sm.interpolate (f))));
        }
        auto bars = PlotTrace::fromDb ("rta", "RTA", theme::accent, bf, bv);
        bars.bars = true;
        rta.setTrace (bars);
        auto peak = PlotTrace::fromDb ("peak", "Peak", theme::curveSum, bf, bv);
        for (auto& v : peak.values) v += 2.5;
        peak.thickness = 1.2f; peak.dashed = true;
        rta.setTrace (peak);
    }

    // ---- group delay
    groupDelay.setUnits (FrequencyPlot::Units::ms);
    groupDelay.setRanges (20.0, 20000.0, -2.0, 24.0);
    groupDelay.setTitle ("Group delay");
    {
        auto gdL = groupDelayMs (smoothFractionalOctave (sumL.absoluteResponse(), 6.0));
        auto gdR = groupDelayMs (smoothFractionalOctave (sumR.absoluteResponse(), 6.0));
        groupDelay.setTrace (PlotTrace::fromDb ("gdL", "Left", theme::curveLeft, freqs, gdL));
        groupDelay.setTrace (PlotTrace::fromDb ("gdR", "Right", theme::curveRight, freqs, gdR));
    }

    // ---- impulse (front-left drivers) + ETC (L vs R sum)
    {
        std::vector<TimePlot::Trace> tr;
        std::vector<std::pair<double, juce::Colour>> marks;
        std::map<juce::String, double> arrivalMs;
        for (auto& ch : config.channels)
        {
            auto& m = per[ch.id];
            arrivalMs[ch.id] = m.onsetMs;
            if (ch.side == Side::right) continue;
            TimePlot::Trace t;
            t.id = ch.id; t.label = ch.label.replace ("Left ", "L ");
            t.colour = theme::roleColour (ch.role);
            t.sampleRate = m.sampleRate;
            t.startMs = -m.zeroSample * 1000.0 / m.sampleRate;
            // keep ~40 ms of IR to stay light
            const size_t n = std::min (m.ir.size(), (size_t) (m.zeroSample + 0.04 * m.sampleRate));
            t.samples.assign (m.ir.begin(), m.ir.begin() + (long) n);
            tr.push_back (t);
            marks.push_back ({ m.onsetMs, theme::roleColour (ch.role) });
        }
        impulse.setTraces (tr);
        impulse.setRangeMs (0.0, 12.0);
        impulse.setMarkers (marks);

        std::vector<TimePlot::Trace> etcTr;
        for (auto* m : { &sumL, &sumR })
        {
            TimePlot::Trace t;
            t.id = m == &sumL ? "L" : "R"; t.label = m == &sumL ? "Left" : "Right";
            t.colour = m == &sumL ? theme::curveLeft : theme::curveRight;
            t.sampleRate = m->sampleRate;
            t.startMs = -m->zeroSample * 1000.0 / m->sampleRate;
            const size_t n = std::min (m->ir.size(), (size_t) (m->zeroSample + 0.06 * m->sampleRate));
            t.samples.assign (m->ir.begin(), m->ir.begin() + (long) n);
            etcTr.push_back (t);
        }
        etc.setTraces (etcTr);
        etc.setDbMode (true);
        etc.setRangeMs (0.0, 30.0);
        etc.setMarkers ({ { sumL.onsetMs, theme::curveLeft }, { sumR.onsetMs, theme::curveRight } });

        car.setConfig (config);
        car.setHighlight ("FL.midrange");
        car.setArrivals (arrivalMs);
        car.setSeat (0);
        car.onChannelClicked = [this] (const juce::String& id) { car.setHighlight (id); };
    }

    // ---- scores
    ringA.setCaption ("Overall");
    ringB.setCaption ("Tonal balance");
    ringC.setCaption ("Imaging");
    ringA.setScore (92, animate);
    ringB.setScore (71, animate);
    ringC.setScore (38, animate);
}

void PlotGallery::updateEqCurve()
{
    const auto& freqs = eqBase.freqs;
    std::vector<FilterSpec> chain;
    for (auto& nd : eqNodes)
        chain.push_back (FilterSpec::peq (nd.freq, nd.q, nd.value));
    auto filt = chainResponse (chain, freqs, 48000.0);

    auto before = PlotTrace::fromResponse ("before", "Before", theme::curveBefore, eqBase);
    before.thickness = 1.5f;
    eq.setTrace (before);
    auto f = PlotTrace::fromResponse ("filter", "EQ", theme::roleMidrange, filt);
    f.fillToBottom = false; f.thickness = 1.5f; f.dashed = true;
    eq.setTrace (f);
    auto after = PlotTrace::fromResponse ("after", "After", theme::curveAfter, eqBase * filt);
    after.thickness = 2.2f; after.fillToBottom = true;
    eq.setTrace (after);
}

void PlotGallery::previewHover (juce::Point<float> pos)
{
    for (auto& c : cards)
    {
        if (c.content == nullptr || ! c.content->getBounds().toFloat().contains (pos))
            continue;
        const auto local = pos - c.content->getPosition().toFloat();
        auto source = juce::Desktop::getInstance().getMainMouseSource();
        const auto now = juce::Time::getCurrentTime();
        juce::MouseEvent e (source, local, juce::ModifierKeys(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            c.content, c.content, now, local, now, 0, false);
        c.content->mouseMove (e);
        return;
    }
}

void PlotGallery::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg1);
    for (auto& c : cards)
    {
        auto r = c.bounds.toFloat();
        theme::fillCard (g, r);
        g.setColour (theme::text);
        g.setFont (theme::fontMedium (13.0f));
        const juce::Rectangle<float> titleRow (r.getX() + 14.0f, r.getY() + 8.0f, r.getWidth() - 28.0f, 18.0f);
        g.drawText (c.title, titleRow, juce::Justification::centredLeft, true);
        if (c.subtitle.isNotEmpty())
        {
            g.setColour (theme::textMuted);
            g.setFont (theme::font (11.0f));
            g.drawText (c.subtitle, titleRow, juce::Justification::centredRight, true);
        }
    }
}

void PlotGallery::resized()
{
    auto area = getLocalBounds().reduced (theme::pad);
    const int gap = 12;
    const int cols = 3;
    const int rows = 4;
    const int cw = (area.getWidth() - gap * (cols - 1)) / cols;
    const int rh = (area.getHeight() - gap * (rows - 1)) / rows;

    auto cell = [&] (int col, int row, int spanC = 1, int spanR = 1)
    {
        return juce::Rectangle<int> (area.getX() + col * (cw + gap), area.getY() + row * (rh + gap),
                                     cw * spanC + gap * (spanC - 1), rh * spanR + gap * (spanR - 1));
    };

    // row 0: magnitude (2 wide) + phase; row 1: eq, coherence, rta; row 2: gd, impulse, etc; row 3: scores (2) + car
    cards[0].bounds = cell (0, 0, 2);
    cards[1].bounds = cell (2, 0);
    cards[2].bounds = cell (0, 1);
    cards[3].bounds = cell (1, 1);
    cards[4].bounds = cell (2, 1);
    cards[5].bounds = cell (0, 2);
    cards[6].bounds = cell (1, 2);
    cards[7].bounds = cell (2, 2);
    cards[8].bounds = cell (0, 3, 2);
    cards[9].bounds = cell (2, 3);

    for (auto& c : cards)
        if (c.content != nullptr)
            c.content->setBounds (c.bounds.reduced (10).withTrimmedTop (22));

    auto scores = cards[8].bounds.reduced (10).withTrimmedTop (22);
    const int sw = scores.getWidth() / 3;
    ringA.setBounds (scores.removeFromLeft (sw).reduced (8));
    ringB.setBounds (scores.removeFromLeft (sw).reduced (8));
    ringC.setBounds (scores.reduced (8));
}
} // namespace fa::ui
