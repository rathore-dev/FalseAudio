// Tests for the competition-grade refinement pass (fa::advanced::refine) and the simulated judge.

#include "TestRegistry.h"
#include "core/tune/advanced/Optimizer.h"
#include "core/tune/advanced/PredictionModel.h"
#include "core/tune/CrossoverPlanner.h"
#include "core/sim/VirtualCar.h"
#include "core/dsp/Spectrum.h"
#include <iostream>

using namespace fa;

namespace
{
double sideError (const Measurement& m, const TargetCurve& target)
{
    // Same metric as CoreTests' sideErrorDb: 1/6 oct, anchored 200 Hz..2 kHz, RMS 40 Hz..12 kHz.
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

struct Verified { double left = 0, right = 0, match = 0; double avg() const { return 0.5 * (left + right); } };

/** "Measures" the configured system on the virtual car at every mic position (as the app's verify step does). */
Verified verify (const VirtualCar& car, const SystemConfig& cfg, int positions)
{
    Verified v;
    for (int p = 0; p < positions; ++p)
    {
        FrequencyResponse sides[2];
        for (int s = 0; s < 2; ++s)
        {
            std::vector<std::pair<juce::String, ChannelSettings>> playing;
            for (auto& c : cfg.channels)
                if (c.side == (s == 0 ? Side::left : Side::right) || c.side == Side::mono)
                    playing.emplace_back (c.id, c.settings);
            const auto m = car.measure (playing, p);
            (s == 0 ? v.left : v.right) += sideError (m, cfg.target) / positions;
            sides[s] = smoothFractionalOctave (m.response, 3.0);
        }
        double acc = 0.0;
        int n = 0;
        for (size_t i = 0; i < sides[0].size(); ++i)
            if (sides[0].freqs[i] >= 100.0 && sides[0].freqs[i] <= 10000.0)
                { acc += std::pow (sides[0].magnitudeDb (i) - sides[1].magnitudeDb (i), 2.0); ++n; }
        v.match += std::sqrt (acc / std::max (1, n)) / positions;
    }
    return v;
}

struct Scenario
{
    SystemConfig untuned, solved, refined;
    DriverDataMap data;
    advanced::Outcome outcome;
    Verified vSolved, vRefined;
    double ms = 0.0;
};

Scenario runScenario (const juce::String& name, int ways, std::function<void (VirtualCar&)> setup, juce::uint32 seed = 42)
{
    Scenario sc;
    auto cfg = SystemConfig::makeLayout (ways, true);
    CrossoverPlanner::apply (cfg, CrossoverPlanner::planDefaults (cfg));
    VirtualCar car (cfg, seed);
    if (setup)
        setup (car);
    sc.untuned = cfg;

    const int positions = 3;
    for (auto& c : cfg.channels)
    {
        auto& dd = sc.data[c.id];
        dd.measuredWith = c.settings;
        for (int p = 0; p < positions; ++p)
            dd.add (car.measure ({ { c.id, c.settings } }, p));
    }

    sc.solved = cfg;
    SystemSolver solver (sc.solved, sc.data, {});
    solver.solveAll();
    solver.analyseSum();

    sc.refined = sc.solved;
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    sc.outcome = advanced::refine (sc.refined, sc.data, {});
    sc.ms = juce::Time::getMillisecondCounterHiRes() - t0;

    sc.vSolved = verify (car, sc.solved, positions);
    sc.vRefined = verify (car, sc.refined, positions);

    std::cout << "    " << name << ": cost " << juce::String (sc.outcome.costBefore, 3) << " -> "
              << juce::String (sc.outcome.costAfter, 3) << " (" << juce::String (sc.ms / 1000.0, 2) << " s)"
              << "; measured side error L/R " << juce::String (sc.vSolved.left, 2) << "/" << juce::String (sc.vSolved.right, 2)
              << " -> " << juce::String (sc.vRefined.left, 2) << "/" << juce::String (sc.vRefined.right, 2)
              << " dB, L/R match " << juce::String (sc.vSolved.match, 2) << " -> " << juce::String (sc.vRefined.match, 2) << " dB\n";
    for (auto& n : sc.outcome.notes)
        std::cout << "      " << n << "\n";
    return sc;
}

bool configsEqual (const SystemConfig& a, const SystemConfig& b)
{
    return juce::JSON::toString (a.toVar()) == juce::JSON::toString (b.toVar());
}

bool respectsLimits (const SystemConfig& c)
{
    for (auto& ch : c.channels)
    {
        const auto& s = ch.settings;
        if (s.delayMs < -1e-9 || s.delayMs > c.caps.maxDelayMs + 1e-9) return false;
        if (s.gainDb < c.caps.minChannelGainDb - 1e-9 || s.gainDb > c.caps.maxChannelGainDb + 1e-9) return false;
        if ((int) s.peq.size() > c.caps.peqBandsPerChannel) return false;
        for (auto& b : s.peq)
            if (b.gainDb > std::min (c.safety.maxBoostDb, c.caps.peqMaxGainDb) + 1e-6 || b.gainDb < c.caps.peqMinGainDb - 1e-6)
                return false;
        if (ch.role != DriverRole::subwoofer && s.highPass.enabled
            && s.highPass.freq < CrossoverPlanner::safeHighPassHz (ch, c.safety) - 1e-6)
            return false;
        const double q = std::round (s.delayMs / c.caps.delayStepMs) * c.caps.delayStepMs;
        if (std::abs (q - s.delayMs) > 1e-6) return false;
    }
    return true;
}

juce::String describe (const advanced::JudgeScore& j)
{
    juce::String s = juce::String (j.total, 1) + " [";
    for (auto& [k, v] : j.categories)
        s << k << " " << juce::String (v, 0) << ", ";
    return s.trimCharactersAtEnd (", ") + "]";
}

/** The tuned config with an installer/tuner mistake: midrange polarity flipped and delays off. */
SystemConfig broken (SystemConfig c)
{
    for (auto& ch : c.channels)
    {
        if (ch.role == DriverRole::midrange && ch.side == Side::left)
            ch.settings.inverted = ! ch.settings.inverted;
        if (ch.role == DriverRole::tweeter && ch.side == Side::right)
            ch.settings.delayMs += 1.2;
        if (ch.id == "FL.midbass")
            ch.settings.delayMs = std::max (0.0, ch.settings.delayMs - 1.0);
    }
    return c;
}
} // namespace

FA_TEST (advancedModelMatchesSolverPrediction)
{
    auto cfg = SystemConfig::makeLayout (3, true);
    CrossoverPlanner::apply (cfg, CrossoverPlanner::planDefaults (cfg));
    VirtualCar car (cfg);
    DriverDataMap data;
    for (auto& c : cfg.channels)
    {
        auto& dd = data[c.id];
        dd.measuredWith = c.settings;
        for (int p = 0; p < 2; ++p)
            dd.add (car.measure ({ { c.id, c.settings } }, p));
    }

    // Different settings than measured: the model must divide out the old and apply the new.
    for (auto& c : cfg.channels)
    {
        c.settings.delayMs += 0.37;
        c.settings.gainDb -= 1.5;
        c.settings.peq.push_back (FilterSpec::peq (700.0, 2.0, -3.0));
    }
    cfg.channels[1].settings.inverted = true;

    advanced::detail::Model model (cfg, data);
    SystemSolver solver (cfg, data, {});
    double worst = 0.0;
    for (size_t k = 0; k < model.channels().size(); ++k)
    {
        const auto& ch = cfg.channels[model.channels()[k].cfgIndex];
        std::vector<std::vector<cplx>> contrib;
        model.contribution (k, ch.settings, model.filterPart (ch.settings), contrib);
        for (int p = 0; p < 2; ++p)
        {
            const auto ref = solver.predict (ch.id, ch.settings, p);
            for (size_t i = 0; i < model.size(); ++i)
            {
                const cplx r = ref.interpolate (model.freqs()[i]);
                worst = std::max (worst, std::abs (contrib[(size_t) p][i] - r) / std::max (1e-6, std::abs (r)));
            }
        }
    }
    fa::test::check (worst < 1e-3, "Optimizer model == SystemSolver::predict", "worst relative error " + juce::String (worst, 8));
}

FA_TEST (advancedRefineAndJudge)
{
    std::cout << "  Refinement after SystemSolver on virtual cars (3 mic positions, verified by re-measuring):\n";

    auto base = runScenario ("3-way+sub, reversed FR midrange", 3, [] (VirtualCar& car)
    {
        car.driver ("FR.midrange").wiredInverted = true;
    });

    auto lr = runScenario ("3-way+sub, big L/R path difference (+0.7 m right)", 3, [] (VirtualCar& car)
    {
        for (auto id : { "FR.midbass", "FR.midrange", "FR.tweeter" })
            car.driver (id).distanceM += 0.7;
    });

    auto door = runScenario ("3-way+sub, strong door resonance + cancellation", 3, [] (VirtualCar& car)
    {
        car.driver ("FL.midbass").resonances.push_back (FilterSpec::peq (125.0, 4.0, 9.0));
        car.driver ("FL.midbass").resonances.push_back (FilterSpec::peq (240.0, 5.0, -12.0));
        car.driver ("FR.midbass").resonances.push_back (FilterSpec::peq (210.0, 3.0, 6.0));
        car.driver ("SUB").distanceM = 2.6;
    });

    auto four = runScenario ("4-way+sub, reversed FL tweeter", 4, [] (VirtualCar& car)
    {
        car.driver ("FL.tweeter").wiredInverted = true;
    });

    bool neverWorse = true, limitsOk = true, unchangedWhenNoGain = true;
    int improvedMeasured = 0;
    for (auto* sc : { &base, &lr, &door, &four })
    {
        neverWorse = neverWorse && sc->outcome.costAfter <= sc->outcome.costBefore + 1e-12;
        limitsOk = limitsOk && respectsLimits (sc->refined);
        if (sc->outcome.costAfter >= sc->outcome.costBefore)
            unchangedWhenNoGain = unchangedWhenNoGain && configsEqual (sc->refined, sc->solved);
        if (sc->vRefined.avg() < sc->vSolved.avg() - 0.02)
            ++improvedMeasured;
    }
    fa::test::check (neverWorse, "Refine never increases the predicted cost");
    fa::test::check (unchangedWhenNoGain, "Config restored exactly when nothing improves");
    fa::test::check (limitsOk, "Refined configs respect DSP caps and SafetyLimits (tweeter HP, boost, delay grid)");
    fa::test::check (improvedMeasured >= 1, "Re-measured side error improves vs SystemSolver alone",
                     juce::String (improvedMeasured) + " of 4 scenarios");

    // The re-measured result must not get noticeably worse anywhere.
    bool noRegression = true;
    for (auto* sc : { &base, &lr, &door, &four })
        noRegression = noRegression && sc->vRefined.avg() <= sc->vSolved.avg() + 0.1 && sc->vRefined.match <= sc->vSolved.match + 0.15;
    fa::test::check (noRegression, "No measured regression (side error +0.1 dB / L-R match +0.15 dB at most)");

    // Restore guarantee when nothing can be gained (refine an already-refined config).
    {
        auto again = base.refined;
        advanced::Options o;
        o.maxIterations = 50;
        const auto out = advanced::refine (again, base.data, o);
        fa::test::check (out.costAfter <= out.costBefore && (out.costAfter < out.costBefore || configsEqual (again, base.refined)),
                         "Second refine pass is a no-op or an improvement",
                         juce::String (out.costBefore, 4) + " -> " + juce::String (out.costAfter, 4));
    }

    // Judge ranking.
    std::cout << "  Simulated judge:\n";
    for (auto* sc : { &base, &four })
    {
        const auto jUntuned = advanced::judge (sc->untuned, sc->data);
        const auto jSolved  = advanced::judge (sc->solved, sc->data);
        const auto jRefined = advanced::judge (sc->refined, sc->data);
        const auto jBroken  = advanced::judge (broken (sc->refined), sc->data);
        std::cout << "    untuned " << describe (jUntuned) << "\n    solved  " << describe (jSolved)
                  << "\n    refined " << describe (jRefined) << "\n    broken  " << describe (jBroken) << "\n";
        for (auto& c : jBroken.comments)
            std::cout << "      broken: " << c << "\n";
        fa::test::check (jRefined.total > jUntuned.total && jSolved.total > jUntuned.total, "Judge: tuned > untuned",
                         juce::String (jRefined.total, 1) + " / " + juce::String (jSolved.total, 1) + " vs " + juce::String (jUntuned.total, 1));
        fa::test::check (jSolved.total > jBroken.total + 5.0 && jRefined.total > jBroken.total + 5.0, "Judge: tuned >> broken (inverted mid, wrong delays)",
                         juce::String (jRefined.total, 1) + " vs " + juce::String (jBroken.total, 1));
        auto invOnly = sc->refined, delayOnly = sc->refined;
        for (auto& ch : invOnly.channels)
            if (ch.role == DriverRole::midrange && ch.side == Side::left)
                ch.settings.inverted = ! ch.settings.inverted;
        for (auto& ch : delayOnly.channels)
            if (ch.role == DriverRole::tweeter && ch.side == Side::right)
                ch.settings.delayMs += 1.2;
        const double jInv = advanced::judge (invOnly, sc->data).total, jDel = advanced::judge (delayOnly, sc->data).total;
        fa::test::check (jInv < jRefined.total && jDel < jRefined.total && jInv < 80.0,
                         "Judge: each mistake alone scores lower (inverted midrange heavily)",
                         "inverted mid " + juce::String (jInv, 1) + ", tweeter +1.2 ms " + juce::String (jDel, 1)
                             + ", both " + juce::String (jBroken.total, 1));
        fa::test::check (jUntuned.total >= 0.0 && jRefined.total <= 100.0, "Judge score in 0..100");
    }
}

