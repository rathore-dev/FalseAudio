// Tests for the methodology rules: constrained optimiser, imaging by level only, the acting L+R check,
// the final noise check, the tweeter HP floor and the per-role sweep level.

#include "TestRegistry.h"
#include "core/tune/AutoTuneEngine.h"
#include "core/tune/MethodologyStages.h"
#include "core/tune/advanced/Optimizer.h"
#include "core/tune/CrossoverPlanner.h"
#include "core/bridge/SafetyRules.h"
#include "core/sim/VirtualRig.h"
#include <iostream>

using namespace fa;
using fa::test::check;

namespace
{
struct Solved
{
    SystemConfig cfg;
    DriverDataMap data;
};

Solved solvedSystem (int ways, bool sub = true)
{
    Solved s;
    s.cfg = SystemConfig::makeLayout (ways, sub);
    CrossoverPlanner::apply (s.cfg, CrossoverPlanner::planDefaults (s.cfg));
    VirtualCar car (s.cfg, 42);
    for (auto& c : s.cfg.channels)
    {
        auto& dd = s.data[c.id];
        dd.measuredWith = c.settings;
        for (int p = 0; p < 3; ++p)
            dd.add (car.measure ({ { c.id, c.settings } }, p));
    }
    SystemSolver solver (s.cfg, s.data, {});
    solver.solveAll();
    return s;
}

OutputChannel* find (SystemConfig& cfg, DriverRole role, Side side)
{
    for (auto& c : cfg.channels)
        if (c.role == role && c.side == side)
            return &c;
    return nullptr;
}

class SpyRig : public VirtualRig
{
public:
    using VirtualRig::VirtualRig;
    std::vector<Measurement> measureSweep (const SweepRequest& r, std::atomic<bool>& cancel, juce::String& err) override
    {
        levels.emplace_back (r.label, r.sweep.levelDbFS);
        return VirtualRig::measureSweep (r, cancel, err);
    }
    std::vector<std::pair<juce::String, double>> levels;
};

AutoTuneEngine::Result runEngine (const SystemConfig& cfg, VirtualRig& rig)
{
    AutoTuneEngine engine;
    AutoTuneEngine::Options o;
    o.micPositions = 2;
    o.refineIterations = 40;
    engine.start (cfg, rig, rig, o);
    while (engine.isRunning())
        juce::Thread::sleep (20);
    return engine.getLastResult();
}

bool anyContains (const juce::StringArray& a, const juce::String& t)
{
    for (auto& s : a)
        if (s.containsIgnoreCase (t))
            return true;
    return false;
}
} // namespace

//==============================================================================
FA_TEST (methodologyOptimiserRules)
{
    std::cout << "Methodology: constrained optimiser\n";
    for (int ways : { 2, 3 })
    {
        auto sc = solvedSystem (ways);
        auto refined = sc.cfg;
        advanced::Options ao;
        ao.maxIterations = 60;
        const auto out = advanced::refine (refined, sc.data, ao);
        const juce::String tag = juce::String (ways) + "-way: ";
        std::cout << "    " << tag << "cost " << out.costBefore << " -> " << out.costAfter << "\n";

        bool delaysSame = true, polaritySame = true, lrXoverEqual = true, allPassNone = true;
        for (size_t i = 0; i < refined.channels.size(); ++i)
        {
            const auto& a = sc.cfg.channels[i].settings;
            const auto& b = refined.channels[i].settings;
            delaysSame = delaysSame && std::abs (a.delayMs - b.delayMs) < 1e-9;
            polaritySame = polaritySame && a.inverted == b.inverted;
            for (auto& band : b.peq)
                allPassNone = allPassNone && band.type != FilterType::allPass;
        }
        for (auto& l : refined.channels)
        {
            if (l.side != Side::left)
                continue;
            for (auto& r : refined.channels)
                if (r.side == Side::right && r.role == l.role)
                {
                    lrXoverEqual = lrXoverEqual
                        && l.settings.highPass.enabled == r.settings.highPass.enabled
                        && std::abs (l.settings.highPass.freq - r.settings.highPass.freq) < 1e-6
                        && l.settings.lowPass.enabled == r.settings.lowPass.enabled
                        && std::abs (l.settings.lowPass.freq - r.settings.lowPass.freq) < 1e-6;
            }
        }

        // Sub LP stays at (lowest front HP) / sqrt 2, as the planner set it.
        double frontHp = 0.0, subLp = 0.0;
        for (auto& c : refined.channels)
        {
            if (c.role == DriverRole::subwoofer) subLp = c.settings.lowPass.freq;
            else if (c.side == Side::left && c.settings.highPass.enabled)
                frontHp = frontHp > 0.0 ? std::min (frontHp, c.settings.highPass.freq) : c.settings.highPass.freq;
        }
        const double ratio = frontHp / subLp;
        const double plannedRatio = [&]
        {
            double hp = 0.0, lp = 0.0;
            for (auto& c : sc.cfg.channels)
            {
                if (c.role == DriverRole::subwoofer) lp = c.settings.lowPass.freq;
                else if (c.side == Side::left && c.settings.highPass.enabled)
                    hp = hp > 0.0 ? std::min (hp, c.settings.highPass.freq) : c.settings.highPass.freq;
            }
            return hp / lp;
        }();
        check (std::abs (ratio - std::sqrt (2.0)) < 0.1 && std::abs (ratio - plannedRatio) < 0.1,
               tag + "sub LP = front HP / sqrt 2", "ratio " + juce::String (ratio, 3));
        check (lrXoverEqual, tag + "left and right crossovers stay equal");
        check (delaysSame, tag + "optimiser leaves every delay untouched");
        check (polaritySame && allPassNone, tag + "no polarity or all-pass changes");
    }
}

FA_TEST (methodologyImagingLevelOnly)
{
    std::cout << "Methodology: imaging by level only\n";
    auto sc = solvedSystem (2);
    auto* rm = find (sc.cfg, DriverRole::midbass, Side::right);
    auto* rt = find (sc.cfg, DriverRole::tweeter, Side::right);
    rm->settings.gainDb += 2.0;   // the right midbass plays 2 dB hot: the image drifts right
    const auto before = sc.cfg;

    SystemSolver solver (sc.cfg, sc.data, {});
    const auto res = method::balanceImagingByLevel (sc.cfg, solver, sc.data);
    for (auto& n : res.notes)
        std::cout << "      " << n << "\n";

    bool onlyGain = true;
    for (size_t i = 0; i < sc.cfg.channels.size(); ++i)
    {
        auto a = before.channels[i].settings;
        auto b = sc.cfg.channels[i].settings;
        a.gainDb = b.gainDb = 0.0;
        onlyGain = onlyGain && juce::JSON::toString (a.toVar()) == juce::JSON::toString (b.toVar());
    }
    check (res.changed, "imaging stage corrects a level imbalance");
    check (onlyGain, "imaging changes only levels (delay, polarity, crossovers, EQ untouched)");
    check (rm->settings.gainDb < before.channels[(size_t) (rm - sc.cfg.channels.data())].settings.gainDb,
           "the louder (right) midbass is turned down, nothing is boosted");
    bool noBoost = true;
    for (size_t i = 0; i < sc.cfg.channels.size(); ++i)
        noBoost = noBoost && sc.cfg.channels[i].settings.gainDb <= before.channels[i].settings.gainDb + 1e-9;
    check (noBoost, "imaging never raises a level");
    juce::ignoreUnused (rt);
}

FA_TEST (methodologySumCheckFixesPolarity)
{
    std::cout << "Methodology: L+R check\n";
    auto sc = solvedSystem (2);
    const auto good = sc.cfg;
    for (auto& c : sc.cfg.channels)
        if (c.side == Side::right)
            c.settings.inverted = ! c.settings.inverted;   // right side wired in reverse: L+R cancels

    SystemSolver solver (sc.cfg, sc.data, {});
    double goodMean = 0.0, badMean = 0.0, afterMean = 0.0;
    {
        auto g = good;
        SystemSolver gs (g, sc.data, {});
        gs.findSumDips (nullptr, &goodMean);
    }
    const auto dipsBefore = solver.findSumDips (nullptr, &badMean);
    const auto res = method::correctSumCancellation (sc.cfg, solver);
    for (auto& n : res.notes)
        std::cout << "      " << n << "\n";
    solver.findSumDips (nullptr, &afterMean);

    bool restored = true;
    for (size_t i = 0; i < good.channels.size(); ++i)
        restored = restored && good.channels[i].settings.inverted == sc.cfg.channels[i].settings.inverted;
    check (! dipsBefore.empty(), "simulated reversed side produces an L+R cancellation", juce::String (res.worstBeforeDb, 1) + " dB");
    check (res.corrected && afterMean <= goodMean + 0.05 && afterMean < badMean - 1.0,
           "L+R check removes the cancellation", "average " + juce::String (badMean, 1) + " -> " + juce::String (afterMean, 1)
           + " dB (healthy system " + juce::String (goodMean, 1) + " dB)");
    check (restored, "polarity of the reversed side is restored");

    // Nothing to fix: left alone.
    auto clean = solvedSystem (2);
    SystemSolver s2 (clean.cfg, clean.data, {});
    const auto snapshot = juce::JSON::toString (clean.cfg.toVar());
    const auto r2 = method::correctSumCancellation (clean.cfg, s2);
    check (! r2.corrected && juce::JSON::toString (clean.cfg.toVar()) == snapshot, "L+R check leaves a clean system alone");
}

FA_TEST (methodologyNoiseCheck)
{
    std::cout << "Methodology: noise check and pipeline stages\n";
    auto cfg = SystemConfig::makeLayout (2, true);
    SpyRig rig (cfg, 2);
    rig.setSimulatedSweepMs (0);
    rig.setChannelNoise ("FR.tweeter", -62.0, false);   // hissy right tweeter
    rig.setChannelNoise ("SUB", -60.0, true);           // sub with a ground-loop hum
    const auto r = runEngine (cfg, rig);

    check (r.success, "tune with a noisy rig completes", r.error);
    for (auto& w : r.report.warnings)
        if (w.containsIgnoreCase ("hiss") || w.containsIgnoreCase ("hum"))
            std::cout << "      " << w << "\n";
    bool hiss = false, hum = false, falseAlarm = false;
    for (auto& w : r.report.warnings)
    {
        if (w.startsWith ("Right Tweeter") && w.containsIgnoreCase ("hiss")) hiss = true;
        if (w.startsWith ("Subwoofer") && w.containsIgnoreCase ("ground loop")) hum = true;
        if ((w.containsIgnoreCase ("hiss") || w.containsIgnoreCase ("ground loop")) && ! w.startsWith ("Right Tweeter") && ! w.startsWith ("Subwoofer"))
            falseAlarm = true;
    }
    check (hiss, "noisy tweeter is flagged as hiss with a gain-structure fix");
    check (hum, "humming sub is flagged as a ground loop");
    check (! falseAlarm, "quiet channels are not flagged");
    check (anyContains (r.report.notes, "Imaging") && anyContains (r.report.notes, "L+R check") && anyContains (r.report.notes, "Noise check"),
           "report shows the imaging, L+R and noise stages");

    // Quiet rig: no noise findings.
    SpyRig quiet (cfg, 2);
    quiet.setSimulatedSweepMs (0);
    const auto q = runEngine (cfg, quiet);
    bool anyNoise = false;
    for (auto& w : q.report.warnings)
        anyNoise = anyNoise || w.containsIgnoreCase ("hiss") || w.containsIgnoreCase ("ground loop");
    check (q.success && ! anyNoise, "a quiet rig raises no noise warning");

    // Sweep levels by role, measured on the same noisy run.
    bool tweeterOk = true, allOk = true;
    for (auto& [label, level] : rig.levels)
    {
        if (label.contains ("tweeter")) tweeterOk = tweeterOk && level <= method::sweepCapDbFS (DriverRole::tweeter) + 1e-9;
        allOk = allOk && level <= -12.0 + 1e-9;
    }
    check (! rig.levels.empty() && tweeterOk && allOk, "no sweep exceeds its role cap (tweeters lowest)");
}

FA_TEST (methodologyTweeterHpFloor)
{
    std::cout << "Methodology: tweeter HP floor\n";
    auto cfg = SystemConfig::makeLayout (2, true);
    for (double fs : { 400.0, 500.0, 900.0, 1500.0 })
    {
        for (auto& c : cfg.channels)
            if (c.role == DriverRole::tweeter)
                c.driver.fsHz = fs;
        const double floorHz = safety::minTweeterHpHz (fs);
        bool ok = true;
        for (auto& c : cfg.channels)
            if (c.role == DriverRole::tweeter)
                ok = ok && CrossoverPlanner::safeHighPassHz (c, cfg.safety) >= floorHz - 1e-9;
        for (auto& d : CrossoverPlanner::planDefaults (cfg))
            for (auto& c : cfg.channels)
                if (c.id == d.channelId && c.role == DriverRole::tweeter)
                    ok = ok && d.hpHz >= floorHz - 1e-9;
        check (ok, "Fs " + juce::String (fs, 0) + " Hz: tweeter HP >= " + juce::String (floorHz, 0) + " Hz");
    }
}

FA_TEST (methodologySweepLevelPerRole)
{
    std::cout << "Methodology: sweep level per role\n";
    SafetyLimits sl;
    bool ok = true;
    for (auto role : { DriverRole::subwoofer, DriverRole::midbass, DriverRole::midrange, DriverRole::tweeter, DriverRole::fullrange })
        for (double req : { -30.0, -18.0, -6.0, 0.0 })
            ok = ok && AutoTuneEngine::sweepLevelForRole (role, req, sl) <= method::sweepCapDbFS (role) + 1e-9
                    && AutoTuneEngine::sweepLevelForRole (role, req, sl) <= sl.sweepLevelDbFS + 1e-9;
    check (ok, "role level never exceeds the role cap or SafetyLimits::sweepLevelDbFS");
    check (method::sweepCapDbFS (DriverRole::tweeter) < method::sweepCapDbFS (DriverRole::midbass), "tweeters get the lowest cap");
    SafetyLimits low;
    low.sweepLevelDbFS = -40.0;
    check (AutoTuneEngine::sweepLevelForRole (DriverRole::midbass, -6.0, low) <= -40.0, "the safety level is a hard ceiling");

    // End to end: a lowered safety ceiling holds for every sweep, including the level check.
    auto cfg = SystemConfig::makeLayout (2, true);
    cfg.safety.sweepLevelDbFS = -30.0;
    SpyRig rig (cfg, 2);
    rig.setSimulatedSweepMs (0);
    const auto r = runEngine (cfg, rig);
    bool held = ! rig.levels.empty();
    for (auto& [label, level] : rig.levels)
        held = held && level <= -30.0 + 1e-9;
    check (r.success || r.error.isNotEmpty(), "run finishes", r.error);
    check (held, "every sweep of a full run stays at or below the safety ceiling");
}
