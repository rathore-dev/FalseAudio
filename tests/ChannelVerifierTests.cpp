// verify_channel against the virtual rig: which driver plays, mute / gain / polarity / delay changes,
// level cap, tweeter-safe start and the kill switch.
#include "TestRegistry.h"
#include "core/ai/ChannelVerifier.h"
#include "core/sim/VirtualRig.h"
#include "core/tune/CrossoverPlanner.h"

using fa::test::check;
using namespace fa;

namespace
{
struct Fixture
{
    SystemConfig cfg = SystemConfig::makeLayout (3, true);
    VirtualRig rig { cfg, 1 };
    ChannelVerifier::Options opts;
    std::unique_ptr<ChannelVerifier> v;

    Fixture()
    {
        rig.setSimulatedSweepMs (0);
        build();
    }
    void build()
    {
        v = std::make_unique<ChannelVerifier> ([this]() -> MeasurementSource& { return rig; },
                                               [this]() -> const SystemConfig& { return cfg; }, opts);
    }
    void solo (const OutputChannel& which)
    {
        juce::String e;
        for (auto& ch : cfg.channels)
            rig.setMuted (ch, ch.id != which.id, e);
    }
    void apply (OutputChannel ch)
    {
        juce::String e;
        rig.applyChannel (ch, e);
    }
};
} // namespace

FA_TEST (channelVerifierIdentifiesRoles)
{
    Fixture f;
    int ok = 0, total = 0;
    for (auto& ch : f.cfg.channels)
    {
        f.solo (ch);
        const auto r = f.v->verify (ch.dspOutput);
        ++total;
        const bool good = r.performed && ! r.silent && r.roleKnown && r.role == ch.role && r.matches;
        if (good)
            ++ok;
        check (good, "verify output " + ch.label, r.detail);
    }
    check (ok == total && total > 0, "every output identified", juce::String (ok) + "/" + juce::String (total));

    // Mismatch: the AI claims output of FL midbass but the tweeter plays.
    const auto* mid = f.cfg.find ("FL.midbass");
    const auto* tw = f.cfg.find ("FL.tweeter");
    if (mid != nullptr && tw != nullptr)
    {
        f.solo (*tw);
        const auto r = f.v->verify (mid->dspOutput);
        check (r.performed && ! r.matches && r.role == DriverRole::tweeter, "a tweeter playing on a claimed midbass output is a mismatch", r.detail);
    }
}

FA_TEST (channelVerifierDetectsChanges)
{
    Fixture f;
    const auto* base = f.cfg.find ("FL.midbass");
    check (base != nullptr, "layout has FL.midbass");
    if (base == nullptr)
        return;
    const auto ch = *base;
    f.solo (ch);

    const auto ref = f.v->verify (ch.dspOutput, "reference");
    check (ref.storedReference && f.v->hasReference (ch.dspOutput), "reference stored", ref.detail);

    auto same = f.v->verify (ch.dspOutput);
    check (same.hasReference && std::abs (same.levelChangeDb) < 0.5 && ! same.polarityFlipped && std::abs (same.delayChangeMs) < 0.05,
           "unchanged output reports no change", same.detail);

    auto g = ch;
    g.settings.gainDb = 6.0;
    f.apply (g);
    auto r = f.v->verify (ch.dspOutput);
    check (std::abs (r.levelChangeDb - 6.0) < 0.7 && ! r.polarityFlipped, "+6 dB gain detected", r.detail);

    auto p = ch;
    p.settings.inverted = true;
    f.apply (p);
    r = f.v->verify (ch.dspOutput);
    check (r.polarityFlipped && std::abs (r.levelChangeDb) < 0.7, "polarity flip detected", r.detail);

    auto d = ch;
    d.settings.delayMs = 1.0;
    f.apply (d);
    r = f.v->verify (ch.dspOutput);
    check (! r.polarityFlipped && std::abs (r.delayChangeMs - 1.0) < 0.15, "1 ms delay detected", r.detail);

    f.apply (ch);
    juce::String e;
    f.rig.setMuted (ch, true, e);
    r = f.v->verify (ch.dspOutput);
    check (r.performed && r.silent && r.hasReference && r.levelChangeDb < -30.0, "mute detected", r.detail);
}

FA_TEST (channelVerifierSafety)
{
    {
        Fixture f;
        f.opts.levelDbFS = 0.0;           // ask for full scale
        f.opts.maxLevelDbFS = 6.0;        // and a silly cap
        f.build();
        for (auto& ch : f.cfg.channels)
        {
            f.solo (ch);
            f.v->verify (ch.dspOutput);
        }
        const auto log = f.v->getPlayLog();
        double hi = -200.0;
        for (auto& s : log)
            hi = std::max (hi, s.levelDbFS);
        check (! log.empty() && hi <= ChannelVerifier::absoluteMaxLevelDbFS, "sweep level never exceeds the hard cap", juce::String (hi, 1) + " dBFS");
    }
    {
        Fixture f;
        const auto* tw = f.cfg.find ("FL.tweeter");
        f.solo (*tw);
        f.v->verify (tw->dspOutput);
        double prot = f.cfg.safety.minTweeterHpHz;
        for (auto& c : f.cfg.channels)
            if (c.role == DriverRole::tweeter)
                prot = std::max (prot, CrossoverPlanner::safeHighPassHz (c, f.cfg.safety));
        bool allSafe = true;
        for (auto& s : f.v->getPlayLog())
            allSafe = allSafe && s.fStartHz >= prot - 1.0;
        check (allSafe && ! f.v->getPlayLog().empty(), "tweeter output is only swept from the protected HP");

        const auto* sub = f.cfg.find ("SUB");
        if (sub == nullptr)
            for (auto& c : f.cfg.channels)
                if (c.role == DriverRole::subwoofer)
                    sub = &c;
        f.solo (*sub);
        const auto before = f.v->getPlayLog().size();
        auto r = f.v->verify (sub->dspOutput);
        const auto log = f.v->getPlayLog();
        check (r.matches && log.size() > before && log[before].fStartHz >= prot - 1.0, "sub test begins with a tweeter-safe sweep", r.detail);
    }
    {
        Fixture f;
        std::atomic<bool> kill { true };
        f.opts.shouldAbort = [&kill] { return kill.load(); };
        f.build();
        f.solo (f.cfg.channels.front());
        const auto r = f.v->verify (f.cfg.channels.front().dspOutput);
        check (! r.performed && r.aborted && f.v->getPlayLog().empty(), "kill switch stops the test before any sound", r.detail);
    }
}
