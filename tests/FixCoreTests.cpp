// Regression tests for the FIXCORE review items: guard rules, controller lifetime, config locking,
// endpoint-volume device matching, optimiser HP floor, stacked EQ boosts, SystemConfig::fromVar.

#include "TestRegistry.h"
#include "core/Session.h"
#include "core/audio/EndpointVolume.h"
#include "core/bridge/GuardedDspController.h"
#include "core/tune/CrossoverPlanner.h"
#include "core/tune/advanced/Optimizer.h"
#include "core/sim/VirtualCar.h"
#include <cmath>

using namespace fa;
using fa::test::check;

namespace
{
struct Event { juce::String what; double gain = 0; double hp = 0; int slope = 0; bool muted = false; };

class RecDsp : public DspController
{
public:
    explicit RecDsp (bool canMute) : mute (canMute) {}
    juce::String getName() const override { return "rec"; }
    bool isAutomatic() const override { return true; }
    DspCapabilities getCapabilities() const override { return {}; }
    bool connect (juce::String&) override { return true; }
    bool isConnected() const override { return true; }
    bool applyChannel (const OutputChannel& c, juce::String&) override
    {
        log.push_back ({ "write", c.settings.gainDb, c.settings.highPass.freq, c.settings.highPass.slopeDbPerOct, c.settings.muted });
        return true;
    }
    bool setMuted (const OutputChannel&, bool m, juce::String&) override
    {
        if (! mute)
            return false;
        log.push_back ({ m ? "mute" : "unmute" });
        return true;
    }
    bool mute;
    std::vector<Event> log;
};

OutputChannel makeTweeter()
{
    auto cfg = SystemConfig::makeLayout (3, true);
    for (auto& c : cfg.channels)
        if (c.role == DriverRole::tweeter && c.side == Side::left)
        {
            auto t = c;
            t.driver.fsHz = 800.0;
            t.settings.highPass.enabled = true;
            t.settings.highPass.freq = 3000.0;
            t.settings.highPass.slopeDbPerOct = 24;
            t.settings.gainDb = -10.0;
            t.settings.muted = false;
            return t;
        }
    return {};
}
}

FA_TEST (fixcoreGuardSlopeNanAndSessionLimits)
{
    RecDsp rec (true);
    GuardedDspController g (rec);
    auto t = makeTweeter();

    t.settings.highPass.slopeDbPerOct = 6;
    juce::String err;
    check (g.applyChannel (t, err), "tweeter write accepted");
    check (! rec.log.empty() && rec.log.back().slope >= 12, "tweeter HP slope raised to >= 12 dB/oct", juce::String (rec.log.back().slope));

    rec.log.clear();
    auto bad = t;
    bad.settings.highPass.freq = std::nan ("");
    check (! g.applyChannel (bad, err) && rec.log.empty(), "NaN HP frequency rejected, nothing written");
    bad = t; bad.settings.gainDb = std::numeric_limits<double>::infinity();
    check (! g.applyChannel (bad, err) && rec.log.empty(), "infinite gain rejected");
    bad = t; bad.settings.peq.push_back (FilterSpec::peq (1000.0, std::nan (""), 1.0));
    check (! g.applyChannel (bad, err) && rec.log.empty(), "NaN PEQ Q rejected");

    // the session's own safety limits are used (stricter user factor -> higher floor)
    SafetyLimits strict;
    strict.tweeterMinHpFactor = 10.0;      // 8 kHz for Fs = 800 Hz
    g.setPlannerLimits (strict);
    rec.log.clear();
    t.settings.highPass.freq = 3000.0;
    g.applyChannel (t, err);
    double lastHp = 0.0;
    for (auto& e : rec.log)
        if (e.what == "write")
            lastHp = e.hp;
    check (lastHp >= 8000.0 - 1e-6, "stricter session safety limit enforced", juce::String (lastHp));

    Session s;
    auto cfg = s.getConfig();
    cfg.safety.tweeterMinHpFactor = 10.0;
    s.setConfig (cfg);
    auto& guard = static_cast<GuardedDspController&> (s.getGuardedDsp());
    check (std::abs (guard.getPlannerLimits().tweeterMinHpFactor - 10.0) < 1e-9, "Session builds the guard from config.safety");
}

FA_TEST (fixcoreGuardRampNeverWritesTweeterXoverUnmuted)
{
    juce::String err;
    {
        RecDsp rec (true);
        GuardedDspController g (rec);
        auto t = makeTweeter();
        g.applyChannel (t, err);                 // establishes previous state (gain -10, HP 3000)
        rec.log.clear();
        t.settings.gainDb = 0.0;                 // +10 dB: ramped
        t.settings.highPass.freq = 4000.0;       // crossover change
        check (g.applyChannel (t, err), "ramped write ok");
        check (! rec.log.empty() && rec.log.front().what == "mute", "tweeter muted before the first write of a ramp");
        bool allMuted = true;
        for (auto& e : rec.log)
            if (e.what == "write" && ! e.muted)
                allMuted = false;
        check (allMuted, "every write during the crossover change is muted");
        check (rec.log.back().what == "unmute", "unmuted only after the final write");
    }
    {
        RecDsp rec (false);                      // controller cannot mute
        GuardedDspController g (rec);
        auto t = makeTweeter();
        g.applyChannel (t, err);
        rec.log.clear();
        t.settings.gainDb = 0.0;
        t.settings.highPass.freq = 4000.0;
        g.applyChannel (t, err);
        bool ok = ! rec.log.empty() && std::abs (rec.log.front().hp - 4000.0) < 1e-6 && rec.log.front().gain <= -10.0 + 1e-9;
        check (ok, "no mute: new crossover is written before any gain increase");
    }
}

FA_TEST (fixcoreConfigLockedDuringAutoTuneAndLifetime)
{
    Session s;
    const auto before = s.getConfig();
    juce::String err;
    const bool started = s.startAutoTune (err);
    check (started, "auto tune started (demo)", err);
    if (started)
    {
        check (s.isConfigLocked(), "config locked while tuning");
        auto cfg = s.getConfig();
        cfg.name = "edited during tune";
        check (! s.setConfig (cfg), "setConfig refused during a tune");
        check (! s.setLayout (2, false), "setLayout refused during a tune");
        check (! s.setTarget (cfg.target), "setTarget refused during a tune");
        check (! s.updateChannel (cfg.channels.front()), "updateChannel refused during a tune");
        juce::String le;
        check (! s.loadSession (juce::File(), le) && le.isNotEmpty(), "loadSession refused during a tune", le);
        check (! s.useManualDsp(), "DSP switch refused during a tune");
        check (s.getConfig().name == before.name, "config untouched");
        s.getAutoTune().cancel();
        for (int i = 0; i < 300 && s.getAutoTune().isRunning(); ++i)
            juce::Thread::sleep (50);
    }
    check (! s.isConfigLocked(), "config unlocked after the tune");
    check (s.setConfig (before), "setConfig accepted when idle");

    // A background push in flight while the rig is rebuilt / the DSP replaced must neither crash nor block.
    bool automatic = false;
    s.pushConfigToDsp (automatic);
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    s.setConfig (before);                       // rebuilds the rig the worker is writing to
    s.pushConfigToDsp (automatic);
    s.setLayout (2, true);
    const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
    check (ms < 5000.0, "config/DSP replacement does not block on workers", juce::String (ms, 0) + " ms");
    juce::Thread::sleep (200);
}

FA_TEST (fixcoreSessionDspControllerReleasedWhenUnused)
{
    struct Probe : RecDsp
    {
        explicit Probe (std::shared_ptr<int> p) : RecDsp (true), token (std::move (p)) {}
        std::shared_ptr<int> token;
    };
    Session s;
    auto token = std::make_shared<int> (1);
    check (s.setDspController (std::make_unique<Probe> (token), Session::DspKind::uiAutomation), "controller installed");
    check (token.use_count() == 2, "session holds the controller");
    check (s.useManualDsp(), "switched to manual");
    check (token.use_count() == 1, "old controller released once no worker uses it");
}

FA_TEST (fixcoreEndpointVolumeNoUmikFallback)
{
    std::vector<juce::String> asked;
    auto reader = [&] (const juce::String& n)
    {
        asked.push_back (n);
        EndpointVolume e;
        if (n.containsIgnoreCase ("umik")) { e.found = e.valid = true; e.levelDb = -6.0; }
        return e;
    };
    auto ep = endpointVolumeForDevice ("Behringer UCM8000 ASIO", reader);
    check (! ep.valid, "unknown endpoint for a non-UMIK device is not valid");
    bool umikAsked = false;
    for (auto& n : asked)
        umikAsked = umikAsked || n.containsIgnoreCase ("umik");
    check (! umikAsked, "the UMIK endpoint is never read for another device");
    asked.clear();
    ep = endpointVolumeForDevice ({}, reader);
    check (! ep.valid && asked.empty(), "no device name -> unknown, nothing read");
    ep = endpointVolumeForDevice ("UMIK-1", reader);
    check (ep.valid, "UMIK device reads its own endpoint");
}

FA_TEST (fixcoreOptimizerClampsHpBelowFloor)
{
    auto cfg = SystemConfig::makeLayout (3, true);
    CrossoverPlanner::apply (cfg, CrossoverPlanner::planDefaults (cfg));
    VirtualCar car (cfg, 42);
    DriverDataMap data;
    for (auto& c : cfg.channels)
    {
        auto& dd = data[c.id];
        dd.measuredWith = c.settings;
        for (int p = 0; p < 2; ++p)
            dd.add (car.measure ({ { c.id, c.settings } }, p));
    }
    // a hand-edited config with every HP far below its safe floor
    for (auto& c : cfg.channels)
        if (c.role != DriverRole::subwoofer && c.settings.highPass.enabled)
            c.settings.highPass.freq = 20.0;
    advanced::refine (cfg, data, {});
    bool ok = true;
    for (auto& c : cfg.channels)
        if (c.role != DriverRole::subwoofer && c.settings.highPass.enabled)
            ok = ok && c.settings.highPass.freq >= CrossoverPlanner::safeHighPassHz (c, cfg.safety) - 1e-6;
    check (ok, "every optimiser HP is at or above its safe floor, even from a start below it");
}

FA_TEST (fixcoreVerifyBoostsDoNotStack)
{
    auto cfg = SystemConfig::makeLayout (3, true);
    OutputChannel ch = cfg.channels.front();
    ch.settings.highPass.enabled = false;
    ch.settings.peq.clear();
    const double maxBoost = cfg.safety.maxBoostDb;
    for (int pass = 0; pass < 6; ++pass)
    {
        auto band = FilterSpec::peq (1000.0, 1.0, 3.0);
        if (AutoTuneEngine::clipStackedBoost (ch, band, cfg))
            ch.settings.peq.push_back (band);
    }
    double total = 0.0;
    for (auto& b : ch.settings.peq)
        total += 20.0 * std::log10 (std::abs (b.response (1000.0, cfg.caps.sampleRate)));
    check (total <= maxBoost + 1e-6, "stacked verify boosts stay within maxBoostDb", juce::String (total, 2));
    check (total > 0.0, "some boost is still allowed");

    // near the HP the stricter low-frequency limit applies
    OutputChannel lf = cfg.channels.front();
    lf.settings.peq.clear();
    lf.settings.highPass.enabled = true;
    lf.settings.highPass.freq = 100.0;
    for (int pass = 0; pass < 4; ++pass)
    {
        auto band = FilterSpec::peq (120.0, 1.0, 3.0);
        if (AutoTuneEngine::clipStackedBoost (lf, band, cfg))
            lf.settings.peq.push_back (band);
    }
    double lt = 0.0;
    for (auto& b : lf.settings.peq)
        lt += 20.0 * std::log10 (std::abs (b.response (120.0, cfg.caps.sampleRate)));
    check (lt <= cfg.safety.maxLowFreqBoostDb + 1e-6, "low-frequency boost limit respected near the HP", juce::String (lt, 2));
}

FA_TEST (fixcoreFromVarNormalisesKindAndRole)
{
    auto cfg = SystemConfig::makeLayout (3, true);
    auto v = cfg.toVar();
    auto* chs = v.getProperty ("channels", {}).getArray();
    juce::var c0 = (*chs)[0];
    auto* o = c0.getDynamicObject();
    o->setProperty ("kind", (int) ChannelKind::passiveSet);
    o->setProperty ("role", (int) DriverRole::tweeter);
    chs->set (0, c0);
    auto loaded = SystemConfig::fromVar (v);
    check (loaded.channels[0].kind == ChannelKind::passiveSet && loaded.channels[0].role != DriverRole::tweeter,
           "passiveSet + tweeter normalised on load");

    o->setProperty ("role", 99);
    o->removeProperty ("kind");                 // legacy file with a corrupt role
    chs->set (0, c0);
    loaded = SystemConfig::fromVar (v);
    const int r = (int) loaded.channels[0].role;
    check (r >= 0 && r <= 4, "role clamped to a valid enum value", juce::String (r));

    o->setProperty ("role", (int) DriverRole::subwoofer);
    chs->set (0, c0);
    loaded = SystemConfig::fromVar (v);
    check (loaded.channels[0].kind == ChannelKind::sub, "legacy subwoofer role gives kind sub");
}
