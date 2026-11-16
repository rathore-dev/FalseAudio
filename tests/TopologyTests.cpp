// Speaker topologies beyond the fully active layout: passive component sets, full-range drivers
// (pair and a single mono driver in a test box). End-to-end Demo tunes plus config round-trips.

#include "TestRegistry.h"
#include "core/tune/AutoTuneEngine.h"
#include "core/tune/PassiveNotch.h"
#include "core/sim/VirtualRig.h"
#include "core/install/InstallPlanner.h"
#include <iostream>

using namespace fa;
using fa::test::check;

namespace
{
AutoTuneEngine::Result runTune (SystemConfig cfg, std::function<void (VirtualRig&)> tweak = {})
{
    VirtualRig rig (cfg, 2);
    rig.setSimulatedSweepMs (0);
    if (tweak)
        tweak (rig);

    AutoTuneEngine engine;
    AutoTuneEngine::Options o;
    o.micPositions = 2;          // two mics, one pass
    o.refineIterations = 60;
    engine.start (cfg, rig, rig, o);
    while (engine.isRunning())
        juce::Thread::sleep (20);
    return engine.getLastResult();
}

bool anyNoteContains (const AutoTuneEngine::Result& r, const juce::String& text)
{
    for (auto& n : r.report.notes)
        if (n.containsIgnoreCase (text))
            return true;
    return false;
}

/** Largest EQ gain of one channel's own PEQ chain inside [lo, hi]. */
double maxEqBoostIn (const OutputChannel& ch, double lo, double hi, double fs)
{
    ChannelSettings only;
    only.peq = ch.settings.peq;
    const auto freqs = makeLogFrequencies (lo, hi, 24);
    const auto r = only.response (freqs, fs, false);
    double mx = -100.0;
    for (size_t i = 0; i < r.size(); ++i)
        mx = std::max (mx, r.magnitudeDb (i));
    return mx;
}

void commonChecks (const juce::String& name, const AutoTuneEngine::Result& r)
{
    check (r.success, name + ": pipeline completes", r.error);
    check (r.score >= 0 && r.score <= 100, name + ": score in range", juce::String (r.score));
    check (r.score >= 40, name + ": score is sensible", juce::String (r.score));
    check (r.sqScore.total >= 0.0 && r.sqScore.total <= 100.0, name + ": judge score in range", juce::String (r.sqScore.total, 1));
    for (auto& c : r.tunedConfig.channels)
    {
        check (std::isfinite (c.settings.gainDb) && c.settings.delayMs >= 0.0, name + ": " + c.id + " delay valid",
               juce::String (c.settings.delayMs, 2) + " ms");
        for (auto& b : c.settings.peq)
            if (b.enabled && b.gainDb > r.tunedConfig.safety.maxBoostDb + 0.01)
                check (false, name + ": " + c.id + " boost within safety", juce::String (b.gainDb, 1));
    }
}
} // namespace

//==============================================================================
FA_TEST (topologyConfigRoundTrip)
{
    std::cout << "Topology: config round trip and legacy configs\n";
    auto cfg = SystemConfig::makePreset (SystemConfig::Preset::passive2WaySub);
    cfg.find ("FL.set")->passive.crossoverHz = 2400.0;
    cfg.find ("FL.set")->passive.coaxial = true;
    cfg.find ("FL.set")->passive.tweeterModel = "Test set";
    cfg.find ("FL.set")->driver.fsHz = 55.0;

    const auto json = juce::JSON::toString (cfg.toVar());
    const auto back = SystemConfig::fromVar (juce::JSON::parse (json));
    check (back.channels.size() == 3, "Passive 2-way + sub keeps 3 channels", juce::String ((int) back.channels.size()));
    const auto* fl = back.find ("FL.set");
    check (fl != nullptr && fl->kind == ChannelKind::passiveSet && fl->isPassiveSet(), "Passive kind survives");
    check (fl != nullptr && fl->passive.crossoverHz == 2400.0 && fl->passive.coaxial && fl->passive.tweeterModel == "Test set",
           "Passive set info survives");
    check (back.find ("SUB") != nullptr && back.find ("SUB")->kind == ChannelKind::sub, "Sub kind survives");
    check (back.isStereo() && back.hasPassiveSets(), "Stereo + passive flags");

    for (auto p : { SystemConfig::Preset::active3WaySub, SystemConfig::Preset::active2WaySub, SystemConfig::Preset::passive2WaySub,
                    SystemConfig::Preset::passiveFront, SystemConfig::Preset::fullRangePair, SystemConfig::Preset::singleDriver })
    {
        const auto c = SystemConfig::makePreset (p);
        const auto r = SystemConfig::fromVar (c.toVar());
        bool same = c.channels.size() == r.channels.size();
        for (size_t i = 0; same && i < c.channels.size(); ++i)
            same = c.channels[i].kind == r.channels[i].kind && c.channels[i].role == r.channels[i].role
                   && c.channels[i].side == r.channels[i].side;
        check (same, "Preset round trips: " + SystemConfig::presetName (p));
    }
    check (! SystemConfig::makePreset (SystemConfig::Preset::singleDriver).isStereo(), "Single driver is not stereo");
    check (SystemConfig::makePreset (SystemConfig::Preset::passiveFront).find ("SUB") == nullptr, "Passive front only has no sub");

    // Legacy: a saved config from before topologies has no "kind" / "passive" properties.
    auto legacyVar = SystemConfig::makeLayout (3, true).toVar();
    if (auto* chs = legacyVar.getProperty ("channels", {}).getArray())
        for (auto& c : *chs)
            if (auto* o = c.getDynamicObject())
            {
                o->removeProperty ("kind");
                o->removeProperty ("passive");
            }
    const auto legacy = SystemConfig::fromVar (legacyVar);
    bool legacyOk = legacy.channels.size() == 7;
    for (auto& c : legacy.channels)
        legacyOk = legacyOk && c.kind == (c.role == DriverRole::subwoofer ? ChannelKind::sub : ChannelKind::activeDriver);
    check (legacyOk, "Legacy config (no kind) loads as fully active");
    check (legacy.isStereo() && ! legacy.hasPassiveSets(), "Legacy config is stereo and not passive");

    auto fr = SystemConfig::makeLayout (1, false).toVar();
    if (auto* chs = fr.getProperty ("channels", {}).getArray())
        for (auto& c : *chs)
            if (auto* o = c.getDynamicObject())
                o->removeProperty ("kind");
    const auto legacyFr = SystemConfig::fromVar (fr);
    check (legacyFr.channels.size() == 2 && legacyFr.channels[0].kind == ChannelKind::fullRange, "Legacy full-range pair keeps its kind");
}

//==============================================================================
FA_TEST (topologyPassive2WayPlusSub)
{
    std::cout << "Topology: passive 2-way front + sub, Demo tune\n";
    auto cfg = SystemConfig::makePreset (SystemConfig::Preset::passive2WaySub);
    for (auto* id : { "FL.set", "FR.set" })
    {
        cfg.find (id)->passive.crossoverHz = 2500.0;
        cfg.find (id)->driver.fsHz = 55.0;
        cfg.find (id)->driver.sizeInches = 6.5;
    }
    const auto r = runTune (cfg);
    commonChecks ("Passive 2-way + sub", r);

    bool noXover = true, hpOk = true;
    for (auto side : { "FL.set", "FR.set" })
    {
        const auto* c = r.tunedConfig.find (side);
        noXover = noXover && c != nullptr && ! c->settings.lowPass.enabled;
        hpOk = hpOk && c != nullptr && c->settings.highPass.enabled && c->settings.highPass.freq >= 55.0 && c->settings.highPass.freq <= 200.0;
    }
    check (noXover, "No crossover (LP) set inside a passive pair");
    check (hpOk, "Passive set HP is inside the woofer's range (Fs..200 Hz)");
    check (r.tunedConfig.find ("FL.tweeter") == nullptr, "No separate tweeter channel exists");
    check (r.tunedConfig.find ("SUB") != nullptr && r.tunedConfig.find ("SUB")->settings.lowPass.enabled, "Sub still gets its LP");
    check (anyNoteContains (r, "passive set"), "Report explains the passive topology");
    check (anyNoteContains (r, "crossover stage skipped"), "Report lists the skipped crossover stage");
    check (anyNoteContains (r, "crossover phase search skipped"), "Report lists the skipped crossover phase stage");

    // The simulated passive network and tweeter offset make a crossover-region notch.
    size_t notches = 0;
    for (auto& [id, v] : r.report.passiveNotches)
        notches += v.size();
    check (notches > 0, "Crossover-region notch detected in the passive sets", juce::String ((int) notches));
    check (anyNoteContains (r, "Insight:"), "Notch is flagged as an insight");

    double worst = -100.0;
    for (auto side : { "FL.set", "FR.set" })
    {
        const auto* c = r.tunedConfig.find (side);
        auto it = r.report.passiveNotches.find (side);
        if (c == nullptr || it == r.report.passiveNotches.end())
            continue;
        for (auto& n : it->second)
            worst = std::max (worst, maxEqBoostIn (*c, n.loHz, n.hiHz, r.tunedConfig.caps.sampleRate));
    }
    check (worst <= 1.5, "EQ does not boost into the passive notch", "max " + juce::String (worst, 2) + " dB");
    check (std::abs (r.leftErrorDb) < 6.0 && std::abs (r.rightErrorDb) < 6.0, "Verified side errors are bounded",
           "L " + juce::String (r.leftErrorDb, 1) + " R " + juce::String (r.rightErrorDb, 1));
}

FA_TEST (topologyPassiveFrontOnly)
{
    std::cout << "Topology: passive front only (no sub), Demo tune\n";
    auto cfg = SystemConfig::makePreset (SystemConfig::Preset::passiveFront);
    for (auto* id : { "FL.set", "FR.set" })
    {
        cfg.find (id)->passive.crossoverHz = 2800.0;
        cfg.find (id)->driver.fsHz = 60.0;
    }
    const auto r = runTune (cfg);
    commonChecks ("Passive front only", r);

    bool ok = true, noLp = true;
    for (auto side : { "FL.set", "FR.set" })
    {
        const auto* c = r.tunedConfig.find (side);
        ok = ok && c != nullptr && c->settings.highPass.freq >= 60.0 && c->settings.highPass.freq <= 150.0;
        noLp = noLp && c != nullptr && ! c->settings.lowPass.enabled;
    }
    check (ok, "HP protects the woofer (Fs..150 Hz) with no sub");
    check (noLp, "No LP / crossover on passive sets");
    check (anyNoteContains (r, "No subwoofer"), "Report notes there is no sub");
    check (r.tunedConfig.channels.size() == 2, "Two channels only");
}

FA_TEST (topologyFullRangePair)
{
    std::cout << "Topology: full-range pair, Demo tune\n";
    auto cfg = SystemConfig::makePreset (SystemConfig::Preset::fullRangePair);
    for (auto& c : cfg.channels)
    {
        c.driver.sizeInches = 4.0;
        c.driver.fsHz = 70.0;
        c.driver.xmaxMm = 3.0;     // small excursion: HP well above Fs
    }
    const auto r = runTune (cfg);
    commonChecks ("Full-range pair", r);

    bool ok = true, noLp = true;
    for (auto& c : r.tunedConfig.channels)
    {
        ok = ok && c.settings.highPass.enabled && c.settings.highPass.freq >= 70.0 && c.settings.highPass.freq <= 400.0;
        noLp = noLp && ! c.settings.lowPass.enabled;
    }
    check (ok, "Excursion-limited HP above Fs (70..400 Hz)");
    check (noLp, "No LP on full-range drivers");
    check (r.tunedConfig.isStereo(), "Pair is stereo (L/R steps run)");
    check (anyNoteContains (r, "full-range"), "Report explains the full-range topology");
}

FA_TEST (topologySingleMonoDriver)
{
    std::cout << "Topology: single mono driver in a test box, Demo tune\n";
    auto cfg = SystemConfig::makePreset (SystemConfig::Preset::singleDriver);
    cfg.channels[0].driver.sizeInches = 5.25;
    cfg.channels[0].driver.fsHz = 80.0;
    const auto r = runTune (cfg);
    commonChecks ("Single driver", r);

    check (r.tunedConfig.channels.size() == 1, "Only one channel");
    const auto& c = r.tunedConfig.channels[0];
    check (c.settings.highPass.enabled && c.settings.highPass.freq >= 80.0 && c.settings.highPass.freq <= 300.0,
           "HP from the driver's usable range", juce::String (c.settings.highPass.freq, 0) + " Hz");
    check (! c.settings.lowPass.enabled, "No LP");
    check (c.settings.delayMs == 0.0, "No delay (nothing to align with)", juce::String (c.settings.delayMs, 2));

    bool rightStep = false;
    for (auto& m : r.measurements)
        if (m.channelId.contains ("Verify R") || m.channelId.contains ("L+R"))
            rightStep = true;
    check (! rightStep, "No L/R verify steps ran");
    check (anyNoteContains (r, "single side / single driver"), "Report explains the skipped L/R steps");
    check (r.matchDb < 1e-9, "No L/R mismatch to report", juce::String (r.matchDb, 3));
}

FA_TEST (topologyInstallPlannerMapsComponentSets)
{
    std::cout << "Topology: InstallPlanner maps component sets on one channel to a passive set\n";
    gear::Product set;
    set.id = "test-set";
    set.brand = "Test";
    set.model = "Set 6.5";
    set.category = gear::Category::componentSet;
    gear::ComponentSetSpecs spec;
    spec.crossoverHz = 3200.0;
    spec.crossoverSlopeDbPerOct = 12.0;
    gear::DriverSpecs w, t;
    w.sizeInches = 6.5; w.fsHz = 58.0; w.powerRmsW = 60.0; w.xmaxMm = 5.0;
    t.sizeInches = 1.0; t.fsHz = 1400.0;
    spec.woofer.driver = w;
    spec.tweeter.driver = t;
    set.componentSet = spec;

    install::ProductLookup lookup = [&set] (const juce::String& id) -> const gear::Product* { return id == set.id ? &set : nullptr; };

    {
        // One output per side carries the whole set.
        auto cfg = SystemConfig::makeLayout (2, true);
        install::Installation inst;
        for (auto* id : { "FL.midbass", "FR.midbass" })
            inst.drivers.push_back ({ id, set.id, 1, "single" });
        install::applyToConfig ({}, inst, lookup, cfg);

        const auto* l = cfg.find ("FL.midbass");
        check (l != nullptr && l->isPassiveSet() && l->role == DriverRole::midbass, "Set on one channel becomes a passive set");
        check (l != nullptr && l->passive.crossoverHz == 3200.0 && l->passive.slopeDbPerOct == 12, "Passive crossover info comes from the gear DB");
        check (l != nullptr && l->driver.fsHz == 58.0 && l->driver.sizeInches == 6.5, "Woofer specs go to the channel driver");
        check (cfg.find ("FL.tweeter") == nullptr && cfg.find ("FR.tweeter") == nullptr, "Orphaned active tweeter channels are dropped");
        bool contiguous = true;
        std::vector<int> outs;
        for (auto& c : cfg.channels)
            outs.push_back (c.dspOutput);
        std::sort (outs.begin(), outs.end());
        for (size_t i = 0; i < outs.size(); ++i)
            contiguous = contiguous && outs[i] == (int) i;
        check (contiguous, "DSP outputs stay contiguous after dropping the tweeters");
    }
    {
        // Woofer and tweeter on separate outputs: stays active.
        auto cfg = SystemConfig::makeLayout (2, true);
        install::Installation inst;
        for (auto* id : { "FL.midbass", "FL.tweeter", "FR.midbass", "FR.tweeter" })
            inst.drivers.push_back ({ id, set.id, 1, "single" });
        install::applyToConfig ({}, inst, lookup, cfg);
        check (cfg.find ("FL.tweeter") != nullptr && ! cfg.find ("FL.midbass")->isPassiveSet() && ! cfg.find ("FL.tweeter")->isPassiveSet(),
               "Set split over two outputs stays active");
        check (cfg.find ("FL.tweeter")->driver.sizeInches == 1.0, "Tweeter channel gets the tweeter specs");
    }
}
