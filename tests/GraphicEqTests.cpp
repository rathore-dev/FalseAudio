// Tests for graphic-EQ DSP support (fa::GraphicEQ, graphic mode in SystemSolver / refine / AutoTuneEngine).

#include "TestRegistry.h"
#include "core/tune/GraphicEQ.h"
#include "core/tune/SystemSolver.h"
#include "core/tune/CrossoverPlanner.h"
#include "core/tune/AutoTuneEngine.h"
#include "core/tune/advanced/Optimizer.h"
#include "core/sim/VirtualCar.h"
#include "core/sim/VirtualRig.h"
#include "core/bridge/DspController.h"
#include "core/dsp/Spectrum.h"
#include <iostream>

using namespace fa;

namespace
{
DspCapabilities graphicCaps (int bands, double step = 0.5, double q = 0.0, bool perChannel = true)
{
    DspCapabilities c;
    c.eqType = DspCapabilities::EqType::graphic;
    c.graphicBandsHz = DspCapabilities::standardGraphicBands (bands);
    c.graphicQ = q;
    c.graphicGainStepDb = step;
    c.graphicPerChannel = perChannel;
    return c;
}

FrequencyResponse synthetic (const std::vector<FilterSpec>& shape, double fs = 48000.0)
{
    return chainResponse (shape, analysisFrequencies(), fs);
}

bool onGrid (const GraphicEQ::Result& r, double step)
{
    for (auto g : r.gainsDb)
        if (std::abs (g / step - std::round (g / step)) > 1e-6)
            return false;
    return true;
}

bool layoutFixed (const std::vector<FilterSpec>& peq, const DspCapabilities& caps)
{
    const auto c = caps.graphicCentres();
    if (peq.size() != c.size())
        return false;
    for (size_t i = 0; i < c.size(); ++i)
        if (peq[i].type != FilterType::peaking || std::abs (peq[i].freq - c[i]) > 1e-9
            || std::abs (peq[i].q - std::round (GraphicEQ::bandQ (caps, i, peq[i].gainDb) * 100.0) / 100.0) > 1e-9)
            return false;
    return true;
}

double maxGain (const GraphicEQ::Result& r) { double m = -1e9; for (auto g : r.gainsDb) m = std::max (m, g); return m; }
double minGain (const GraphicEQ::Result& r) { double m = 1e9;  for (auto g : r.gainsDb) m = std::min (m, g); return m; }

double sideError (const Measurement& m, const TargetCurve& target)
{
    const auto sm = smoothFractionalOctave (m.response, 6.0);
    const auto& f = sm.freqs;
    double ref = 0.0, acc = 0.0;
    int n = 0;
    for (size_t i = 0; i < f.size(); ++i)
        if (f[i] >= 200 && f[i] <= 2000) { ref += sm.magnitudeDb (i) - target.levelAt (f[i]); ++n; }
    ref /= std::max (1, n);
    n = 0;
    for (size_t i = 0; i < f.size(); ++i)
        if (f[i] >= 40 && f[i] <= 12000) { acc += std::pow (sm.magnitudeDb (i) - target.levelAt (f[i]) - ref, 2.0); ++n; }
    return std::sqrt (acc / std::max (1, n));
}

double sideErrorOf (const VirtualCar& car, const SystemConfig& cfg, Side side, int positions)
{
    double e = 0.0;
    for (int p = 0; p < positions; ++p)
    {
        std::vector<std::pair<juce::String, ChannelSettings>> playing;
        for (auto& c : cfg.channels)
            if (c.side == side || c.side == Side::mono)
                playing.emplace_back (c.id, c.settings);
        e += sideError (car.measure (playing, p), cfg.target) / positions;
    }
    return e;
}
} // namespace

//==============================================================================
FA_TEST (graphicEqCapabilities)
{
    std::cout << "Graphic EQ capabilities (layouts, serialisation)\n";
    const auto b31 = DspCapabilities::standardGraphicBands (31);
    const auto b30 = DspCapabilities::standardGraphicBands (30);
    const auto b13 = DspCapabilities::standardGraphicBands (13);
    const auto b15 = DspCapabilities::standardGraphicBands (15);
    const auto b10 = DspCapabilities::standardGraphicBands (10);
    const auto b7  = DspCapabilities::standardGraphicBands (7);
    fa::test::check (b31.size() == 31 && b31.front() == 20.0 && b31.back() == 20000.0 && b31[17] == 1000.0, "31-band ISO 1/3 oct");
    fa::test::check (b30.size() == 30 && b30.front() == 25.0 && b30.back() == 20000.0, "30-band 25 Hz..20 kHz");
    fa::test::check (b13.size() == 13 && b13.front() == 50.0 && b13.back() == 12500.0, "13-band head-unit layout");
    fa::test::check (b15.size() == 15 && b15.front() == 25.0 && b15.back() == 16000.0, "15-band 2/3 oct");
    fa::test::check (b10.size() == 10 && b10.front() == 31.5 && b10.back() == 16000.0, "10-band octave");
    bool ascending = b7.size() == 7;
    for (size_t i = 1; i < b7.size(); ++i)
        ascending = ascending && b7[i] > b7[i - 1];
    fa::test::check (ascending, "Other counts log-spaced", juce::String (b7.front()) + " .. " + juce::String (b7.back()));

    // Backward compatibility: old caps JSON has no eqType -> parametric.
    DspCapabilities old;
    auto v = old.toVar();
    v.getDynamicObject()->removeProperty ("eqType");
    fa::test::check (! DspCapabilities::fromVar (v).isGraphic(), "Old caps load as parametric");

    auto g = graphicCaps (13, 1.0, 0.0, false);
    g.graphicMaxBoostDb = 6.0;
    const auto r = DspCapabilities::fromVar (juce::JSON::parse (juce::JSON::toString (g.toVar())));
    fa::test::check (r.isGraphic() && r.graphicBandsHz == g.graphicBandsHz && r.graphicGainStepDb == 1.0
                         && r.graphicMaxBoostDb == 6.0 && ! r.graphicPerChannel && r.graphicQ == 0.0,
                     "Graphic caps round-trip");

    // Proportional Q: narrower with more gain, nominal at 12 dB.
    const auto c31 = graphicCaps (31);
    const double q3 = GraphicEQ::bandQ (c31, 15, 3.0), q12 = GraphicEQ::bandQ (c31, 15, 12.0);
    fa::test::check (q3 < q12 && std::abs (q12 - 4.32) < 0.15, "Proportional-Q model",
                     "Q(3 dB) " + juce::String (q3, 2) + ", Q(12 dB) " + juce::String (q12, 2));
    fa::test::check (GraphicEQ::bandQ (graphicCaps (31, 0.5, 2.5), 15, 3.0) == 2.5, "Constant-Q model");
}

FA_TEST (graphicEqSolve)
{
    std::cout << "GraphicEQ::solve on synthetic responses\n";
    const auto& f = analysisFrequencies();
    const std::vector<double> flat (f.size(), 0.0);

    const auto measured = synthetic ({ FilterSpec::peq (120.0, 1.5, 7.0), FilterSpec::peq (420.0, 2.5, -5.0),
                                       FilterSpec::peq (2500.0, 1.2, 5.0), FilterSpec::peq (7000.0, 3.0, -4.0) });
    GraphicEQ::Settings s;
    s.fLow = 30.0;
    s.fHigh = 16000.0;

    for (auto [bands, step, q] : { std::tuple<int, double, double> { 31, 0.5, 0.0 }, { 31, 0.5, 4.3 },
                                   { 30, 1.0, 0.0 }, { 13, 1.0, 0.0 }, { 15, 0.5, 0.0 } })
    {
        const auto caps = graphicCaps (bands, step, q);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        const auto r = GraphicEQ::solve (measured, flat, caps, s);
        const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
        const juce::String name = juce::String (bands) + "-band " + (q > 0 ? "constant" : "proportional") + "-Q, "
                                + juce::String (step, 1) + " dB steps";
        fa::test::check (r.rmsErrorAfterDb < 0.5 * r.rmsErrorBeforeDb, name + ": error reduced",
                         juce::String (r.rmsErrorBeforeDb, 2) + " -> " + juce::String (r.rmsErrorAfterDb, 2) + " dB in "
                             + juce::String (ms, 0) + " ms");
        fa::test::check (onGrid (r, step) && layoutFixed (r.filters, caps), name + ": gains on grid, centres/Q fixed");
        fa::test::check (maxGain (r) <= s.maxBoostDb + 1e-9 && minGain (r) >= caps.graphicMaxCutDb - 1e-9,
                         name + ": within limits", juce::String (minGain (r), 1) + " .. " + juce::String (maxGain (r), 1) + " dB");
    }

    // Boost limits: a broad 10 dB hole may only be filled up to the limit.
    {
        auto caps = graphicCaps (31);
        caps.graphicMaxBoostDb = 3.0;
        const auto hole = synthetic ({ FilterSpec::peq (1000.0, 0.8, -10.0) });
        const auto r = GraphicEQ::solve (hole, flat, caps, s);
        fa::test::check (maxGain (r) <= 3.0 + 1e-9 && maxGain (r) >= 2.0, "Boost capped by caps.graphicMaxBoostDb",
                         juce::String (maxGain (r), 1) + " dB");

        auto s2 = s;
        s2.lfBoostLimitBelowHz = 200.0;
        s2.lfBoostLimitDb = 1.0;
        const auto lf = GraphicEQ::solve (synthetic ({ FilterSpec::peq (80.0, 0.8, -8.0) }), flat, graphicCaps (31), s2);
        double lfMax = -1e9;
        const auto c = graphicCaps (31).graphicCentres();
        for (size_t j = 0; j < c.size(); ++j)
            if (c[j] < 200.0)
                lfMax = std::max (lfMax, lf.gainsDb[j]);
        fa::test::check (lfMax <= 1.0 + 1e-9, "Low-frequency excursion limit", juce::String (lfMax, 1) + " dB");
    }

    // Narrow deep dips (cancellations) are not chased.
    {
        const auto notch = synthetic ({ FilterSpec::peq (1000.0, 12.0, -20.0) });
        const auto r = GraphicEQ::solve (notch, flat, graphicCaps (31), s);
        fa::test::check (maxGain (r) <= 1.5, "Ignores a narrow deep dip", "max boost " + juce::String (maxGain (r), 1) + " dB");
    }

    // Overlap handled jointly: a single broad peak is cut by a smooth group, not a zig-zag.
    {
        const auto r = GraphicEQ::solve (synthetic ({ FilterSpec::peq (500.0, 0.7, 6.0) }), flat, graphicCaps (31), s);
        int signFlips = 0;
        for (size_t j = 1; j < r.gainsDb.size(); ++j)
            if (r.gainsDb[j] * r.gainsDb[j - 1] < -0.25)
                ++signFlips;
        fa::test::check (r.rmsErrorAfterDb < 1.0 && signFlips <= 2, "Broad peak: smooth joint solution",
                         juce::String (r.rmsErrorAfterDb, 2) + " dB, " + juce::String (signFlips) + " sign flips");
    }

    // Exported text.
    {
        OutputChannel ch;
        ch.label = "Left Midbass";
        const auto caps = graphicCaps (31);
        std::vector<double> g (31, 0.0);
        g[1] = -1.5;
        ch.settings.peq = GraphicEQ::makeGraphic (caps, g);
        const auto lines = ManualDspController::describeChannel (ch, &caps);
        const auto text = lines.joinIntoString ("\n");
        fa::test::check (text.contains ("31-band GEQ: 20 Hz 0.0, 25 Hz -1.5, 31.5 Hz 0.0") && text.contains ("20 kHz 0.0")
                             && ! text.contains ("EQ 1:"),
                         "Manual instructions print the GEQ", lines[lines.size() - 1].substring (0, 60));
        fa::test::check (ManualDspController::describeChannel (ch).joinIntoString ("\n").contains ("31-band GEQ"),
                         "Graphic layout detected without caps");
    }
}

FA_TEST (graphicEqSolverAndRefine)
{
    std::cout << "SystemSolver + refine in graphic mode (virtual car)\n";
    for (bool perChannel : { true, false })
    {
        auto cfg = SystemConfig::makeLayout (3, true);
        CrossoverPlanner::apply (cfg, CrossoverPlanner::planDefaults (cfg));
        cfg.caps = graphicCaps (31, 0.5, 0.0, perChannel);
        VirtualCar car (cfg, 42);

        const int positions = 3;
        DriverDataMap data;
        for (auto& c : cfg.channels)
        {
            auto& dd = data[c.id];
            dd.measuredWith = c.settings;
            for (int p = 0; p < positions; ++p)
                dd.add (car.measure ({ { c.id, c.settings } }, p));
        }
        const double before = sideErrorOf (car, cfg, Side::left, positions);

        SystemSolver solver (cfg, data, {});
        solver.solveAll();
        const double solvedL = sideErrorOf (car, cfg, Side::left, positions);
        const double solvedR = sideErrorOf (car, cfg, Side::right, positions);

        bool layouts = true, onStep = true;
        for (auto& c : cfg.channels)
        {
            layouts = layouts && layoutFixed (c.settings.peq, cfg.caps);
            for (auto& b : c.settings.peq)
                onStep = onStep && std::abs (b.gainDb / 0.5 - std::round (b.gainDb / 0.5)) < 1e-6
                         && b.gainDb <= cfg.safety.maxBoostDb + 1e-9;
        }
        bool shared = true;
        if (! perChannel)
            for (auto side : { Side::left, Side::right })
            {
                const auto chans = cfg.channelsFor (side);
                for (auto* c : chans)
                    shared = shared && GraphicEQ::gainsOf (cfg.caps, c->settings.peq) == GraphicEQ::gainsOf (cfg.caps, chans[0]->settings.peq);
            }

        const juce::String mode = perChannel ? "per-output GEQ" : "per-side GEQ";
        fa::test::check (layouts && onStep, mode + ": every channel is a 31-band GEQ on the 0.5 dB grid");
        fa::test::check (shared, mode + ": side channels share one GEQ when not per-channel");
        fa::test::check (solvedL < before && solvedL < 3.0 && solvedR < 3.5, mode + ": side error after solve",
                         "L " + juce::String (before, 2) + " -> " + juce::String (solvedL, 2) + " dB, R " + juce::String (solvedR, 2) + " dB");

        auto refined = cfg;
        advanced::Options ao;
        ao.maxIterations = 60;
        const auto out = advanced::refine (refined, data, ao);
        bool fixed = true;
        for (size_t i = 0; i < cfg.channels.size(); ++i)
        {
            const auto& a = cfg.channels[i].settings.peq;
            const auto& b = refined.channels[i].settings.peq;
            fixed = fixed && a.size() == b.size() && layoutFixed (b, cfg.caps);
            for (size_t j = 0; fixed && j < a.size(); ++j)
                fixed = a[j].freq == b[j].freq && std::abs (b[j].gainDb / 0.5 - std::round (b[j].gainDb / 0.5)) < 1e-6;
            if (! perChannel)
                fixed = fixed && GraphicEQ::gainsOf (cfg.caps, a) == GraphicEQ::gainsOf (cfg.caps, b);
        }
        fa::test::check (fixed && out.costAfter <= out.costBefore + 1e-9, mode + ": refine keeps centres/Q model, gains on grid",
                         "cost " + juce::String (out.costBefore, 3) + " -> " + juce::String (out.costAfter, 3));
    }
}

FA_TEST (graphicEqAutoTune)
{
    std::cout << "AutoTuneEngine one-click run with a 31-band graphic EQ DSP (virtual rig)\n";
    auto cfg = SystemConfig::makeLayout (3, true);
    cfg.caps = graphicCaps (31, 0.5, 0.0, true);
    VirtualRig rig (cfg, 2);
    rig.setSimulatedSweepMs (0);

    AutoTuneEngine engine;
    engine.onMicPositionChanged = [&rig] (int p) { rig.setMicPosition (p); };
    AutoTuneEngine::Options o;
    o.micPositions = 4;
    engine.start (cfg, rig, rig, o);
    while (engine.isRunning())
        juce::Thread::sleep (20);

    const auto r = engine.getLastResult();
    fa::test::check (r.success, "Pipeline completes", r.error);
    fa::test::check (r.leftErrorDb < 2.5 && r.rightErrorDb < 3.0, "Verified side errors",
                     "L " + juce::String (r.leftErrorDb, 2) + " dB, R " + juce::String (r.rightErrorDb, 2) + " dB, score "
                         + juce::String (r.score));
    bool layouts = true;
    for (auto& c : r.tunedConfig.channels)
        layouts = layouts && layoutFixed (c.settings.peq, r.tunedConfig.caps);
    fa::test::check (layouts, "Tuned channels keep the fixed 31-band layout (no bands added by verify)");
}
