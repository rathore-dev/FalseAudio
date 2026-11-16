// Install planner: wire/fuse physics, load combining, gain voltages, excursion-limited HP, full plans.

#include "TestRegistry.h"
#include "core/install/InstallPlanner.h"
#include <cmath>
#include <map>

using namespace fa;
using namespace fa::install;
using fa::test::check;

namespace
{
    gear::Product makeSource (const juce::String& id, gear::Category cat, double preoutV, int maxStep, double maxInV = 0)
    {
        gear::Product p;
        p.id = id; p.category = cat; p.brand = "Test"; p.model = id;
        gear::SourceSpecs s;
        s.preoutVoltageV = preoutV; s.preoutChannels = 6; s.maxVolumeStep = maxStep; s.maxInputVoltageV = maxInV;
        if (cat == gear::Category::dsp)
        {
            s.peqBandsPerChannel = 31; s.maxDelayMs = 15.0; s.sampleRateHz = 96000.0;
        }
        p.source = s;
        return p;
    }

    gear::Product makeAmp (const juce::String& id, int channels, std::map<juce::String, double> rms, double minOhms)
    {
        gear::Product p;
        p.id = id; p.category = channels == 1 ? gear::Category::ampMono : gear::Category::ampMulti; p.brand = "Test"; p.model = id;
        gear::AmpSpecs a;
        a.channels = channels; a.rmsPerChannelW = rms; a.minStableOhms = minOhms;
        a.inputSensitivityMinV = 0.2; a.inputSensitivityMaxV = 8.0;
        a.amplifierClass = "D"; a.fuseRatingA = 60; a.hasBassBoost = true; a.bassBoostMaxDb = 12; a.hasSubsonic = channels == 1;
        p.amp = a;
        return p;
    }

    gear::DriverSpecs tweeter()
    {
        gear::DriverSpecs d;
        d.sizeInches = 1.0; d.nominalOhms = 4; d.fsHz = 700; d.qts = 0.6; d.sdCm2 = 4.9; d.xmaxMm = 0.3;
        d.sensitivityDb = 90; d.sensitivityRef = "2.83V/1m"; d.powerRmsW = 60;
        return d;
    }

    gear::DriverSpecs midbass()
    {
        gear::DriverSpecs d;
        d.sizeInches = 6.5; d.nominalOhms = 4; d.fsHz = 55; d.qts = 0.45; d.sdCm2 = 132; d.xmaxMm = 5.0;
        d.sensitivityDb = 89; d.sensitivityRef = "1W/1m"; d.powerRmsW = 100;
        return d;
    }

    gear::DriverSpecs midrange()
    {
        gear::DriverSpecs d;
        d.sizeInches = 3.0; d.nominalOhms = 4; d.fsHz = 120; d.qts = 0.7; d.sdCm2 = 33; d.xmaxMm = 1.5;
        d.sensitivityDb = 88; d.sensitivityRef = "2.83V/1m"; d.powerRmsW = 50;
        return d;
    }

    gear::DriverSpecs dvcSub()
    {
        gear::DriverSpecs d;
        d.sizeInches = 12; d.nominalOhms = 2; d.coilOhms = 2; d.voiceCoils = 2; d.reOhms = 1.7; d.fsHz = 30; d.qts = 0.5;
        d.sdCm2 = 480; d.xmaxMm = 15; d.bl = 18; d.mmsGrams = 200; d.powerRmsW = 300;
        return d;
    }

    gear::Product driverProduct (const juce::String& id, gear::Category cat, const gear::DriverSpecs& d)
    {
        gear::Product p;
        p.id = id; p.category = cat; p.brand = "Test"; p.model = id; p.driver = d;
        return p;
    }

    struct Rig
    {
        std::map<juce::String, gear::Product> products;
        SystemConfig config = SystemConfig::makeLayout (3, true);
        Installation inst;

        void add (const gear::Product& p) { products[p.id] = p; }
        ProductLookup lookup() const
        {
            return [this] (const juce::String& id) -> const gear::Product*
            {
                auto it = products.find (id);
                return it != products.end() ? &it->second : nullptr;
            };
        }
    };

    Rig makeFullRig()
    {
        Rig r;
        r.add (makeSource ("hu", gear::Category::headUnit, 4.0, 40));
        r.add (makeSource ("dsp", gear::Category::dsp, 4.0, 0, 4.0));
        r.add (makeAmp ("amp6", 6, { { "4ohm", 75.0 }, { "2ohm", 100.0 } }, 2.0));
        r.add (makeAmp ("mono", 1, { { "2ohm", 500.0 }, { "1ohm", 800.0 } }, 1.0));
        r.add (driverProduct ("tw", gear::Category::driverTweeter, tweeter()));
        r.add (driverProduct ("mid", gear::Category::driverMidrange, midrange()));
        r.add (driverProduct ("mb", gear::Category::driverMidbass, midbass()));
        r.add (driverProduct ("sub", gear::Category::subwoofer, dvcSub()));

        r.inst.headUnitId = "hu";
        r.inst.dspId = "dsp";
        Installation::AmpUse a6;
        a6.ampId = "amp6";
        a6.channelIds = { "FL.tweeter", "FR.tweeter", "FL.midrange", "FR.midrange", "FL.midbass", "FR.midbass" };
        Installation::AmpUse m;
        m.ampId = "mono"; m.channelIds = { "SUB" }; m.powerWireAwg = 4; m.powerWireLengthM = 5.0;
        r.inst.amps = { a6, m };

        for (auto side : { "FL", "FR" })
        {
            r.inst.drivers.push_back ({ juce::String (side) + ".tweeter", "tw", 1, "single" });
            r.inst.drivers.push_back ({ juce::String (side) + ".midrange", "mid", 1, "single" });
            r.inst.drivers.push_back ({ juce::String (side) + ".midbass", "mb", 1, "single" });
        }
        r.inst.drivers.push_back ({ "SUB", "sub", 2, "dvc-series" });
        return r;
    }

    const ChannelPlan* planFor (const Plan& p, const juce::String& id)
    {
        for (auto& c : p.channels)
            if (c.channelId == id)
                return &c;
        return nullptr;
    }

    int firstIndex (const Plan& p, const juce::String& areaPrefix)
    {
        for (size_t i = 0; i < p.instructions.size(); ++i)
            if (p.instructions[i].area.startsWith (areaPrefix))
                return (int) i;
        return -1;
    }

    bool near (double a, double b, double relTol) { return std::abs (a - b) <= relTol * std::abs (b); }
}

FA_TEST (installWireAndFuse)
{
    const double r4 = calc::copperResistancePerMetre (4, "OFC");
    check (near (r4, 0.000815, 0.02), "4 AWG OFC resistance ~0.815 mohm/m", juce::String (r4 * 1000.0, 4) + " mohm/m");
    check (near (calc::copperResistancePerMetre (4, "CCA") / r4, 1.6, 1.0e-6), "CCA = 1.6 x copper");
    check (near (calc::mm2FromAwg (0), 53.5, 0.02), "1/0 AWG ~ 53.5 mm2", juce::String (calc::mm2FromAwg (0), 1));
    check (std::abs (calc::awgFromMm2 (calc::mm2FromAwg (8)) - 8.0) < 1.0e-6, "AWG <-> mm2 round trip");

    // 100 A through 5 m power + 0.5 m ground of 4 AWG OFC: 100 * 0.000815 * 5.5 = 0.448 V
    const double drop = 100.0 * r4 * 5.5;
    check (near (drop, 0.448, 0.03), "Voltage drop 100 A over 5.5 m of 4 AWG", juce::String (drop, 3) + " V");
    check (calc::recommendedAwg (100.0, 5.5, "OFC") <= 4.0, "100 A / 5.5 m needs 4 AWG or thicker",
           juce::String (calc::recommendedAwg (100.0, 5.5, "OFC")));
    check (calc::recommendedAwg (150.0, 5.5, "OFC") < 4.0, "150 A / 5.5 m needs thicker than 4 AWG");
    check (calc::recommendedAwg (150.0, 5.5, "CCA") <= calc::recommendedAwg (150.0, 5.5, "OFC"), "CCA needs equal or thicker gauge");

    const double amps = calc::ampCurrentDraw (800.0, 80.0, 14.0);
    check (near (amps, 71.43, 0.01), "Current: 800 W / 80 % / 14 V", juce::String (amps, 2) + " A");
    check (calc::standardFuseA (1.25 * amps) == 90.0 || calc::standardFuseA (1.25 * amps) == 100.0,
           "Fuse rounded up to standard size", juce::String (calc::standardFuseA (1.25 * amps)));
    check (calc::standardFuseA (50.0) == 50.0 && calc::standardFuseA (50.1) == 60.0, "Standard fuse sizes");
}

FA_TEST (installLoadsAndPower)
{
    check (near (calc::combineLoad (2, 2, 1, "parallel"), 1.0, 1e-9), "1x DVC 2 ohm parallel = 1 ohm");
    check (near (calc::combineLoad (2, 2, 1, "series"), 4.0, 1e-9), "1x DVC 2 ohm series = 4 ohm");
    check (near (calc::combineLoad (2, 2, 2, "parallel"), 0.5, 1e-9), "2x DVC 2 ohm all parallel = 0.5 ohm");
    check (near (calc::combineLoad (2, 2, 2, "series"), 8.0, 1e-9), "2x DVC 2 ohm all series = 8 ohm");
    check (near (calc::combineLoad (2, 2, 2, "dvc-series"), 2.0, 1e-9), "2x DVC 2 ohm coils series, subs parallel = 2 ohm");
    check (near (calc::combineLoad (2, 2, 2, "dvc-parallel"), 2.0, 1e-9), "2x DVC 2 ohm coils parallel, subs series = 2 ohm");
    check (near (calc::combineLoad (4, 1, 2, "parallel"), 2.0, 1e-9), "2x SVC 4 ohm parallel = 2 ohm");
    check (calc::wiringDiagram (2, 2, "dvc-series", 2).contains ("PARALLEL"), "Wiring diagram text");

    check (near (calc::voltageForPower (500, 2), 31.62, 0.001), "500 W @ 2 ohm = 31.6 V", juce::String (calc::voltageForPower (500, 2), 2));

    const auto amp = *makeAmp ("a", 4, { { "4ohm", 75.0 }, { "2ohm", 100.0 } }, 2.0).amp;
    check (near (calc::ampPowerAtLoad (amp, 4, false), 75, 1e-9), "Amp power at rated load");
    const double p3 = calc::ampPowerAtLoad (amp, 3, false);
    check (p3 > 75 && p3 < 100, "Amp power interpolated at 3 ohm", juce::String (p3, 1));
    check (near (calc::ampPowerAtLoad (amp, 8, false), 37.5, 1e-9), "Amp power at 8 ohm (constant voltage)");
    check (near (calc::ampPowerAtLoad (amp, 4, true), 200, 1e-9), "Bridged estimate = 2 x per-channel at half load");

    check (near (calc::gainKnobVoltage (4.0, 100.0, 50.0), 5.657, 0.001), "Gain knob for half power at 4 V source");
    gear::SourceSpecs hu;
    hu.maxVolumeStep = 40;
    check (calc::maxCleanVolumeStep (hu) == 32, "Clean volume rule = 80 % of max");
    hu.cleanVolumeStep = 35;
    check (calc::maxCleanVolumeStep (hu) == 35, "Published clean step wins");
}

FA_TEST (installExcursionHighPass)
{
    const auto tw = tweeter();
    const double hpTw = calc::excursionLimitedHighPass (tw, 60.0);
    check (hpTw > 1.2 * tw.fsHz && hpTw < 6000.0, "Tweeter excursion HP at 60 W is plausible", juce::String (hpTw, 0) + " Hz");
    check (calc::excursionLimitedHighPass (tw, 15.0) < hpTw, "Less power -> lower excursion HP");

    const auto mb = midbass();
    const double hpMb = calc::excursionLimitedHighPass (mb, 100.0);
    check (hpMb > mb.fsHz && hpMb < 250.0, "Midbass excursion HP at 100 W is plausible", juce::String (hpMb, 0) + " Hz");
    check (calc::excursionMm (mb, 100.0, 20.0) > mb.xmaxMm, "Unfiltered midbass exceeds Xmax at 20 Hz",
           juce::String (calc::excursionMm (mb, 100.0, 20.0), 1) + " mm");

    // Motor path (Bl/Mms) and sensitivity path should broadly agree for a consistent driver.
    auto viaBl = mb;
    viaBl.bl = 6.5; viaBl.mmsGrams = 18; viaBl.reOhms = 3.4;
    const double hpBl = calc::excursionLimitedHighPass (viaBl, 100.0);
    check (hpBl > 40 && hpBl < 300, "Midbass excursion HP via Bl/Mms", juce::String (hpBl, 0) + " Hz");

    auto noXmax = mb;
    noXmax.xmaxMm = 0;
    check (calc::excursionLimitedHighPass (noXmax, 100.0) == 0.0, "No Xmax -> no excursion limit computed");
}

FA_TEST (installFullPlan)
{
    auto rig = makeFullRig();
    const auto plan = buildPlan (rig.inst, rig.config, rig.lookup());

    check (plan.instructions.size() > 20, "Plan has instructions", juce::String ((int) plan.instructions.size()));
    const int iw = firstIndex (plan, "Wiring"), ih = firstIndex (plan, "Head unit"), id = firstIndex (plan, "DSP"),
              ia = firstIndex (plan, "Amp"), is = firstIndex (plan, "Speaker wiring"), isf = firstIndex (plan, "Safety");
    check (iw == 0 && iw < ih && ih < id && id < ia && ia < is && is < isf, "Instructions ordered wiring->HU->DSP->amps->speakers->safety",
           juce::String (iw) + " " + juce::String (ih) + " " + juce::String (id) + " " + juce::String (ia) + " " + juce::String (is) + " " + juce::String (isf));
    check (firstIndex (plan, "Missing specs") < 0, "Fully specified gear: nothing reported missing");

    const auto* sub = planFor (plan, "SUB");
    check (sub != nullptr && near (sub->loadOhms, 2.0, 1e-9), "Sub load 2 ohm");
    check (sub != nullptr && near (sub->targetVoltageRms, 31.62, 0.002), "Sub gain target 31.6 V (500 W @ 2 ohm)",
           sub != nullptr ? juce::String (sub->targetVoltageRms, 2) : juce::String());
    check (sub != nullptr && sub->recommendedLpHz > 40 && sub->recommendedLpHz < 90, "Sub LP half an octave below front HP",
           sub != nullptr ? juce::String (sub->recommendedLpHz, 0) : juce::String());

    const auto* tw = planFor (plan, "FL.tweeter");
    check (tw != nullptr && tw->recommendedHpHz >= 1.6 * 700.0, "Tweeter HP >= 1.6 x Fs",
           tw != nullptr ? juce::String (tw->recommendedHpHz, 0) : juce::String());
    check (tw != nullptr && near (tw->targetVoltageRms, std::sqrt (60.0 * 4.0), 1e-6), "Tweeter gain limited to driver RMS (60 W, amp 75 W)");

    const auto* mb = planFor (plan, "FL.midbass");
    check (mb != nullptr && mb->targetVoltageRms <= std::sqrt (100.0 * 4.0) + 1e-9, "Midbass never above its RMS rating");

    check (plan.headUnitMaxCleanVolume == 32, "Head unit clean step 32/40");
    check (near (plan.sourceVoltageV, 4.0, 1e-9), "Signal at amps = DSP output 4 V");
    check (plan.totalCurrentA > 30 && plan.recommendedMainFuseA >= 1.25 * plan.totalCurrentA, "Total current & main fuse",
           juce::String (plan.totalCurrentA, 1) + " A / " + juce::String (plan.recommendedMainFuseA, 0) + " A");

    bool bassBoost = false, xoverOff = false, gain316 = false;
    for (auto& i : plan.instructions)
    {
        bassBoost |= i.title.contains ("Bass boost 0 dB");
        xoverOff  |= i.title.contains ("Crossover switches OFF");
        gain316   |= i.area.startsWith ("Amp 2") && i.title.contains ("31.6 V");
    }
    check (bassBoost && xoverOff && gain316, "Amp instructions: bass boost 0, filters off, sub gain 31.6 V");

    // Dangerous load is flagged
    auto bad = rig;
    bad.inst.drivers.back().wiring = "parallel";   // 0.5 ohm on a 1 ohm-stable amp
    const auto badPlan = buildPlan (bad.inst, bad.config, bad.lookup());
    bool flagged = false;
    for (auto& w : badPlan.warnings)
        flagged |= w.contains ("DANGER");
    check (flagged, "0.5 ohm on 1-ohm-stable amp is flagged");

    // applyToConfig
    auto cfg = rig.config;
    applyToConfig (plan, rig.inst, rig.lookup(), cfg);
    check (cfg.caps.peqBandsPerChannel == 31 && near (cfg.caps.maxDelayMs, 15, 1e-9) && near (cfg.caps.sampleRate, 96000, 1e-9),
           "DSP caps applied");
    const auto* ch = cfg.find ("FL.tweeter");
    check (ch != nullptr && near (ch->driver.fsHz, 700, 1e-9) && ch->settings.highPass.freq >= tw->recommendedHpHz - 1e-6,
           "Driver info + safe tweeter HP applied", ch != nullptr ? juce::String (ch->settings.highPass.freq, 0) : juce::String());
    const auto* sc = cfg.find ("SUB");
    check (sc != nullptr && near (sc->driver.powerRmsW, 600, 1e-9), "Sub power = 2 x 300 W");

    // Serialisation round trip
    const auto back = Installation::fromVar (rig.inst.toVar());
    check (back.amps.size() == 2 && back.drivers.size() == 7 && back.amps[0].channelIds.size() == 6
           && back.drivers.back().wiring == "dvc-series" && back.dspId == "dsp", "Installation toVar/fromVar round trip");
}

FA_TEST (installPartialUserGear)
{
    // A user-added amp with only a fuse rating, and a driver with only impedance + RMS.
    Rig r;
    r.config = SystemConfig::makeLayout (2, false);
    gear::Product amp;
    amp.id = "user-amp"; amp.category = gear::Category::ampMulti; amp.brand = "User"; amp.model = "Amp";
    gear::AmpSpecs as;
    as.fuseRatingA = 40;
    amp.amp = as;
    r.add (amp);
    gear::DriverSpecs d;
    d.nominalOhms = 4; d.powerRmsW = 80;
    r.add (driverProduct ("user-mb", gear::Category::driverMidbass, d));
    gear::DriverSpecs t;   // tweeter with nothing but Fs
    t.fsHz = 900;
    r.add (driverProduct ("user-tw", gear::Category::driverTweeter, t));

    Installation::AmpUse u;
    u.ampId = "user-amp"; u.channelIds = { "FL.midbass", "FR.midbass", "FL.tweeter", "FR.tweeter" };
    r.inst.amps = { u };
    r.inst.drivers = { { "FL.midbass", "user-mb" }, { "FR.midbass", "user-mb" }, { "FL.tweeter", "user-tw" }, { "FR.tweeter", "user-tw" } };

    const auto plan = buildPlan (r.inst, r.config, r.lookup());
    check (! plan.instructions.empty(), "Partial specs still produce a plan");

    bool askXmax = false, askAmpPower = false, askSource = false, askTwRms = false, zeroGain = false, fuseEstimate = false;
    for (auto& i : plan.instructions)
    {
        if (i.area == "Missing specs")
        {
            check (i.severity == Instruction::Severity::info, "Missing-spec items are info", i.title);
            askXmax     |= i.title.contains ("Xmax") && i.title.contains ("user-mb");
            askAmpPower |= i.title.contains ("RMS power per channel") && i.title.contains ("User Amp");
            askSource   |= i.title.contains ("output (pre-out) voltage");
            askTwRms    |= i.title.contains ("RMS power rating") && i.title.contains ("user-tw");
        }
        zeroGain     |= i.title.contains ("set gain to 0.0 V");
        fuseEstimate |= i.why.contains ("ESTIMATE") && i.why.contains ("fuse rating");
    }
    check (askXmax, "Asks for Xmax/Sd to compute the excursion-safe HP");
    check (askAmpPower, "Asks for the amp's power ratings");
    check (askSource, "Asks for the source voltage");
    check (askTwRms, "Asks for the tweeter's RMS rating");
    check (! zeroGain, "No meaningless 0 V gain instruction");
    check (fuseEstimate, "Power wire sized from the amp fuse rating, labelled as an estimate");

    const auto* mb = planFor (plan, "FL.midbass");
    check (mb != nullptr && near (mb->targetVoltageRms, std::sqrt (80.0 * 4.0), 1e-6), "Gain target from driver RMS when amp power unknown");
    const auto* tw = planFor (plan, "FL.tweeter");
    check (tw != nullptr && tw->recommendedHpHz >= 2.0 * 900.0 - 1e-6, "Tweeter without Xmax: HP >= 2 x Fs",
           tw != nullptr ? juce::String (tw->recommendedHpHz, 0) : juce::String());
}

//==============================================================================
// Review Q1 regressions
FA_TEST (installQ1SupplySagSolver)
{
    // 1500 W @ 80 % = 1875 W in, 14.2 V, 5.5 m round trip of 4 AWG CCA (1.304 mohm/m -> 7.17 mohm).
    const double r = calc::copperResistancePerMetre (4, "CCA") * 5.5;
    double v = 0;
    bool collapsed = true;
    const double i = calc::ampSupplyCurrent (1500.0, 80.0, 14.2, r, &v, &collapsed);
    check (! collapsed && near (i * v, 1875.0, 1.0e-9) && near (v, 14.2 - i * r, 1.0e-12), "Sag solver: I x V_amp = P_in exactly",
           juce::String (i, 1) + " A @ " + juce::String (v, 2) + " V");
    check (near (i, 142.2, 0.01), "1500 W class D through 4 AWG CCA draws ~142 A", juce::String (i, 1));

    // 18 AWG over 5.5 m cannot deliver 1875 W at all: the old 8-step iteration went negative / oscillated.
    const double r18 = calc::copperResistancePerMetre (18, "OFC") * 5.5;
    const double i18 = calc::ampSupplyCurrent (1500.0, 80.0, 14.2, r18, &v, &collapsed);
    check (collapsed && i18 > 0 && v > 0 && std::isfinite (i18), "Hopelessly thin wire reports collapse, finite positive numbers",
           juce::String (i18, 1) + " A, " + juce::String (v, 2) + " V");
}

FA_TEST (installQ1FuseAwareGauge)
{
    // 82 A on 1 m of CCA: 1.25 x 82 = 102.5 A -> a 125 A fuse. 4 AWG CCA is rated ~105 A, so 4 AWG is NOT enough
    // (the fuse would exceed the wire). Previously recommendedAwg compared 102.5 <= 105 and returned 4 AWG.
    check (calc::standardFuseA (1.25 * 82.0) == 125.0, "Fuse for 82 A is 125 A");
    check (calc::recommendedAwg (82.0, 1.0, "CCA") < 4.0, "Gauge chosen so the ROUNDED fuse fits the wire",
           juce::String (calc::recommendedAwg (82.0, 1.0, "CCA")));
    check (calc::wireAmpacityA (20, "OFC") < calc::wireAmpacityA (18, "OFC"), "Thinner than 18 AWG gets a lower rating",
           juce::String (calc::wireAmpacityA (20, "OFC"), 1) + " A");
}

namespace
{
    Rig makeMono1500Rig (double awg, const juce::String& material)
    {
        Rig r;
        r.config = SystemConfig::makeLayout (2, true);
        r.add (makeSource ("dsp", gear::Category::dsp, 4.0, 0, 4.0));
        auto amp = makeAmp ("mono1500", 1, { { "1ohm", 1500.0 }, { "2ohm", 1000.0 }, { "4ohm", 600.0 } }, 1.0);
        amp.amp->fuseRatingA = 150;
        r.add (amp);
        auto d = dvcSub();                 // DVC 2 ohm, coils in parallel -> 1 ohm
        d.powerRmsW = 1500;
        r.add (driverProduct ("sub", gear::Category::subwoofer, d));
        r.inst.dspId = "dsp";
        Installation::AmpUse m;
        m.ampId = "mono1500"; m.channelIds = { "SUB" }; m.powerWireAwg = awg; m.powerWireMaterial = material;
        m.powerWireLengthM = 5.0; m.groundWireLengthM = 0.5;
        r.inst.amps = { m };
        r.inst.drivers.push_back ({ "SUB", "sub", 1, "parallel" });
        return r;
    }

    const Instruction* findInstr (const Plan& p, const juce::String& titlePart)
    {
        for (auto& i : p.instructions)
            if (i.title.contains (titlePart))
                return &i;
        return nullptr;
    }
}

FA_TEST (installQ1WorkedMono1500)
{
    // Worked example: 1500 W RMS @ 1 ohm mono class D, 5 m power + 0.5 m ground, 4 AWG CCA vs 1/0 OFC.
    for (int pass = 0; pass < 2; ++pass)
    {
        const bool cca = pass == 0;
        auto rig = makeMono1500Rig (cca ? 4.0 : 0.0, cca ? "CCA" : "OFC");
        const auto plan = buildPlan (rig.inst, rig.config, rig.lookup());
        const auto* sub = planFor (plan, "SUB");
        check (sub != nullptr && near (sub->loadOhms, 1.0, 1e-9) && near (sub->ampPowerAtLoadW, 1500, 1e-9), "1500 W @ 1 ohm");
        check (sub != nullptr && near (sub->targetVoltageRms, std::sqrt (1500.0), 1e-6), "Gain target 38.7 V");
        check (! plan.wires.empty(), "Power wire checked");
        if (plan.wires.empty())
            continue;
        const auto& w = plan.wires.front();
        const auto* fuseI = findInstr (plan, "within 45 cm");
        if (cca)
        {
            check (near (w.currentA, 142.2, 0.01) && ! w.ok && w.voltageDropV > 0.9, "4 AWG CCA: ~142 A, ~1.0 V drop -> NOT ok",
                   juce::String (w.currentA, 1) + " A, " + juce::String (w.voltageDropV, 2) + " V");
            check (w.recommendedAwg <= -1.0, "-> 2/0 CCA or thicker (200 A fuse > 1/0 CCA ~175 A)", juce::String (w.recommendedAwg));
            check (fuseI != nullptr && near (fuseI->value, 200, 1e-9) && fuseI->title.contains ("ONLY on")
                   && fuseI->why.contains ("NEVER put this fuse on your 4 AWG"), "Fuse text never tells the user to fuse 4 AWG CCA at 200 A",
                   fuseI != nullptr ? fuseI->title : juce::String());
        }
        else
        {
            check (near (w.currentA, 134.3, 0.01) && w.ok && w.voltageDropV < 0.3, "1/0 OFC: ~134 A, ~0.24 V drop -> ok",
                   juce::String (w.currentA, 1) + " A, " + juce::String (w.voltageDropV, 2) + " V");
            check (fuseI != nullptr && near (fuseI->value, 175, 1e-9) && ! fuseI->title.contains ("ONLY"), "1/0 OFC fused at 175 A",
                   fuseI != nullptr ? fuseI->title : juce::String());
        }
    }
}

FA_TEST (installQ1WorkedDrivers)
{
    // 6.5" midbass at 100 W (sensitivity path).
    const double hp = calc::excursionLimitedHighPass (midbass(), 100.0);
    check (hp > 60 && hp < 200, "6.5in midbass 100 W excursion HP plausible", juce::String (hp, 0) + " Hz");

    // 1" tweeter, Fs 900 Hz, 50 W.
    auto tw = tweeter();
    tw.fsHz = 900; tw.powerRmsW = 50;
    const double hpTw = calc::excursionLimitedHighPass (tw, 50.0);
    check (hpTw > 900 && hpTw < 6000, "1in tweeter Fs 900 Hz 50 W: excursion HP plausible", juce::String (hpTw, 0) + " Hz");

    // DVC data published inconsistently: nominal per coil (2 ohm) but Re/Bl for coils in series (3.4 ohm).
    // The drive voltage must come from the T/S configuration's impedance (else excursion is under-estimated).
    auto a = dvcSub();
    a.nominalOhms = 4; a.reOhms = 3.4; a.bl = 25;
    auto b = a;
    b.nominalOhms = 2;
    const double xa = calc::excursionMm (a, 300, 25), xb = calc::excursionMm (b, 300, 25);
    check (xb >= xa * 0.999, "Inconsistent DVC nominal does not under-estimate excursion",
           juce::String (xa, 2) + " vs " + juce::String (xb, 2) + " mm");
}

FA_TEST (installQ1GainProcedureSafety)
{
    auto rig = makeFullRig();
    const auto plan = buildPlan (rig.inst, rig.config, rig.lookup());
    bool reEnable = true, anyGain = false;
    for (auto& i : plan.instructions)
        if (i.title.contains ("set gain to"))
        {
            anyGain = true;
            reEnable = reEnable && i.detail.contains ("RE-ENABLE");
        }
    check (anyGain && reEnable, "DMM procedure: re-enable DSP filters before reconnecting speakers");

    // Bridged amp with unknown min load must not print "at least 0.0 ohm".
    auto br = rig;
    br.products["amp6"].amp->minStableOhms = 0;
    br.inst.amps[0].bridged = true;
    const auto bp = buildPlan (br.inst, br.config, br.lookup());
    bool zeroOhm = false;
    for (auto& i : bp.instructions)
        zeroOhm = zeroOhm || i.why.contains ("at least 0.0 ohm");
    check (! zeroOhm, "Bridged minimum load text never says 0.0 ohm");
}

FA_TEST (installQ1FiveChannelSubGroup)
{
    // 5-channel amp: 4 x 75 W @ 4 ohm / 100 W @ 2 ohm (min 2 ohm) + 1 sub channel 300 W @ 4 / 500 W @ 2 / 600 W @ 1 (min 1 ohm).
    Rig r;
    r.config = SystemConfig::makeLayout (2, true);
    r.add (makeSource ("dsp", gear::Category::dsp, 4.0, 0, 4.0));
    auto amp5 = makeAmp ("amp5", 5, { { "4ohm", 75.0 }, { "2ohm", 100.0 } }, 2.0);
    gear::AmpChannelGroup subGroup;
    subGroup.channels = 1; subGroup.label = "sub"; subGroup.minStableOhms = 1.0;
    subGroup.rmsPerChannelW = { { "4ohm", 300.0 }, { "2ohm", 500.0 }, { "1ohm", 600.0 } };
    amp5.amp->channelGroups = { subGroup };
    amp5.amp->recommendedFuseA = 100;
    amp5.amp->ratedVoltageV = 14.4;
    r.add (amp5);
    r.add (driverProduct ("mb", gear::Category::driverMidbass, midbass()));
    r.add (driverProduct ("tw", gear::Category::driverTweeter, tweeter()));
    auto sub = dvcSub();
    sub.powerRmsW = 600;
    r.add (driverProduct ("sub", gear::Category::subwoofer, sub));
    r.inst.dspId = "dsp";
    Installation::AmpUse u;
    u.ampId = "amp5"; u.channelIds = { "FL.tweeter", "FR.tweeter", "FL.midbass", "FR.midbass", "SUB" };
    u.powerWireAwg = 4; u.powerWireMaterial = "OFC";
    r.inst.amps = { u };
    r.inst.drivers = { { "FL.tweeter", "tw" }, { "FR.tweeter", "tw" }, { "FL.midbass", "mb" }, { "FR.midbass", "mb" },
                       { "SUB", "sub", 1, "parallel" } };   // DVC 2 ohm coils parallel -> 1 ohm

    const auto plan = buildPlan (r.inst, r.config, r.lookup());
    const auto* sp = planFor (plan, "SUB");
    check (sp != nullptr && near (sp->loadOhms, 1.0, 1e-9) && near (sp->ampPowerAtLoadW, 600.0, 1e-9),
           "5-ch amp: sub channel uses its own group's rating (600 W @ 1 ohm, not the mains' 100 W)",
           sp != nullptr ? juce::String (sp->ampPowerAtLoadW, 0) : juce::String());
    bool loadDanger = false;
    for (auto& w : plan.warnings)
        loadDanger = loadDanger || (w.startsWith ("SUB") && w.contains ("DANGER"));
    check (! loadDanger, "1 ohm on the 1-ohm-stable sub group is NOT flagged against the mains' 2 ohm minimum");
    check (sp != nullptr && near (sp->targetVoltageRms, std::sqrt (600.0 * 1.0), 1e-6), "Sub gain target sqrt(600 x 1) = 24.5 V");
    const auto* mp = planFor (plan, "FL.midbass");
    check (mp != nullptr && near (mp->ampPowerAtLoadW, 75.0, 1e-9), "Mains still use the main group (75 W @ 4 ohm)");

    // Current: 2 x 75 + 2 x 75 + 600 = 900 W out
    check (! plan.wires.empty() && plan.wires.front().currentA > 900.0 / 0.8 / 14.2, "Current draw includes the sub group's 600 W",
           plan.wires.empty() ? juce::String() : juce::String (plan.wires.front().currentA, 1) + " A");

    const auto* fuseI = findInstr (plan, "within 45 cm");
    check (fuseI != nullptr && near (fuseI->value, 100.0, 1e-9) && fuseI->why.contains ("manual recommends"),
           "Manual's recommended external fuse (100 A) is used", fuseI != nullptr ? fuseI->title : juce::String());

    bool ratedNote = false;
    for (auto& i : plan.instructions)
        ratedNote = ratedNote || (i.title.contains ("set gain to") && i.detail.contains ("ratings are at 14.4 V"));
    check (ratedNote, "Gain instruction warns that ratings are at 14.4 V (car runs 14.2 V)");

    // Same amp, the mains' 2 ohm minimum still applies to the main group.
    auto low = r;
    low.inst.drivers[2].count = 4; low.inst.drivers[2].wiring = "parallel";   // 4 x 4 ohm parallel = 1 ohm on a main channel
    const auto lp = buildPlan (low.inst, low.config, low.lookup());
    bool mainDanger = false;
    for (auto& w : lp.warnings)
        mainDanger = mainDanger || (w.startsWith ("FL.midbass") && w.contains ("DANGER"));
    check (mainDanger, "1 ohm on a 2-ohm-stable MAIN channel is still flagged");
}

FA_TEST (installQ1ManualFuseNeedsWire)
{
    // Manual says 250 A but the run is 4 AWG OFC (~150 A): the plan must demand thicker wire, never a 250 A fuse on 4 AWG.
    auto rig = makeMono1500Rig (4.0, "OFC");
    rig.products["mono1500"].amp->recommendedFuseA = 250;
    rig.inst.amps[0].powerWireLengthM = 1.0;
    const auto plan = buildPlan (rig.inst, rig.config, rig.lookup());
    check (! plan.wires.empty() && ! plan.wires.front().ok && plan.wires.front().recommendedAwg <= 0.0,
           "250 A manual fuse -> 1/0 or thicker required", plan.wires.empty() ? juce::String() : juce::String (plan.wires.front().recommendedAwg));
    const auto* fuseI = findInstr (plan, "within 45 cm");
    check (fuseI != nullptr && fuseI->title.contains ("ONLY on") && fuseI->severity == Instruction::Severity::danger,
           "Fuse instruction is danger + tied to the thicker wire", fuseI != nullptr ? fuseI->title : juce::String());
}

FA_TEST (installQ1ChargingProducts)
{
    auto rig = makeMono1500Rig (0.0, "OFC");
    gear::Product alt;
    alt.id = "alt"; alt.category = gear::Category::alternator; alt.brand = "Test"; alt.model = "alt";
    gear::AlternatorSpecs as;
    as.ratedA = 150; as.idleA = 60;
    alt.alternator = as;
    rig.add (alt);
    gear::Product bat;
    bat.id = "bat"; bat.category = gear::Category::battery; bat.brand = "Test"; bat.model = "bat";
    gear::BatterySpecs bs;
    bs.ah = 70;
    bat.battery = bs;
    rig.add (bat);
    rig.inst.alternatorId = "alt";
    rig.inst.batteryId = "bat";
    const auto plan = buildPlan (rig.inst, rig.config, rig.lookup());
    bool overAlt = false, idle = false, engineOff = false;
    for (auto& w : plan.warnings)
    {
        overAlt = overAlt || w.contains ("150 A alternator");
        idle = idle || w.contains ("At idle");
    }
    for (auto& i : plan.instructions)
        engineOff = engineOff || (i.title.contains ("Engine-off") && i.why.contains ("70 Ah"));
    check (overAlt, "Alternator product rating used for the current budget (134 A + 50 A > 150 A)");
    check (idle, "Idle output warning (17 A music + 50 A > 60 A idle)");
    check (engineOff, "Battery product Ah used for engine-off runtime");
    const auto back = Installation::fromVar (rig.inst.toVar());
    check (back.alternatorId == "alt" && back.batteryId == "bat", "alternator/battery ids round trip");
}

FA_TEST (installQ1DvcTsWiring)
{
    // Same physical DVC sub described three ways must give the same excursion.
    auto par = dvcSub();          // Bl 18 = one coil's (parallel / per-coil)
    par.tsWiring = "parallel";
    auto ser = par;
    ser.tsWiring = "series"; ser.bl = 36;   // coils in series: Bl doubles
    const double xp = calc::excursionMm (par, 300, 25), xs = calc::excursionMm (ser, 300, 25);
    check (near (xs, xp, 1e-9), "DVC: series-measured and parallel-measured T/S agree", juce::String (xp, 2) + " / " + juce::String (xs, 2) + " mm");

    // Both coils driven: force per sqrt(W) is sqrt 2 x one coil's -> excursion well above the single-coil formula.
    auto svcEquivalent = par;
    svcEquivalent.voiceCoils = 1; svcEquivalent.nominalOhms = 2; svcEquivalent.coilOhms = 2;
    const double x1 = calc::excursionMm (svcEquivalent, 300, 25);
    check (near (xp, std::sqrt (2.0) * x1, 1e-6), "DVC both coils = sqrt 2 x one coil at the same power",
           juce::String (xp, 2) + " vs " + juce::String (x1, 2) + " mm");
}

FA_TEST (installWireMeasuredMm2)
{
    // "12 AWG" CCA that is really 2.01 mm2 must be planned with its measured area, not the AWG label.
    auto run = [] (double awgLabel, double mm2)
    {
        auto rig = makeMono1500Rig (4.0, "CCA");
        gear::Product w;
        w.id = "wire"; w.category = gear::Category::powerWire; w.brand = "Test"; w.model = "wire";
        gear::WireSpecs ws;
        ws.awg = awgLabel; ws.mm2 = mm2; ws.material = "CCA";
        w.wire = ws;
        rig.add (w);
        rig.inst.amps[0].powerWireId = "wire";
        const auto plan = buildPlan (rig.inst, rig.config, rig.lookup());
        return plan.wires.empty() ? 0.0 : plan.wires.front().voltageDropV / plan.wires.front().currentA;   // = R round trip
    };
    const double rLabel = run (12.0, 0.0);
    const double rMeasured = run (12.0, 2.01);
    const double expected = calc::copperResistancePerMetreMm2 (2.01, "CCA") * 5.5;
    check (rLabel > 0 && rMeasured > rLabel * 1.5, "Measured 2.01 mm2 gives clearly higher resistance than the 12 AWG label",
           juce::String (rLabel * 1000, 2) + " vs " + juce::String (rMeasured * 1000, 2) + " mohm");
    check (near (rMeasured, expected, 1e-6), "Resistance = rho_CCA / measured mm2 x round trip");
}

FA_TEST (installRecommendedHpBySlopeAndComponentSet)
{
    gear::DriverSpecs d = midbass();
    d.recommendedHpHz = 500;
    d.recommendedHpBySlope = { { 12.0, 400.0 }, { 24.0, 250.0 } };
    check (gear::recommendedHpForSlope (d, 24.0) == 250.0 && gear::recommendedHpForSlope (d, 12.0) == 400.0
               && gear::recommendedHpForSlope (d, 48.0) == 500.0, "Slope table lookup with single-value fallback");

    auto rig = makeFullRig();
    rig.add (driverProduct ("mb2", gear::Category::driverMidbass, d));
    rig.inst.drivers[2].productId = "mb2";   // FL.midbass
    const auto plan = buildPlan (rig.inst, rig.config, rig.lookup());
    const auto* cp = planFor (plan, "FL.midbass");
    check (cp != nullptr && cp->recommendedHpHz >= 250.0 && cp->recommendedHpHz < 400.0, "Planner uses the LR4 (24 dB) entry",
           cp != nullptr ? juce::String (cp->recommendedHpHz, 1) : juce::String());

    // component set: tweeter channel takes the tweeter's data, woofer channel the woofer's
    gear::Product set;
    set.id = "set"; set.category = gear::Category::componentSet; set.brand = "Test"; set.model = "set";
    gear::ComponentSetSpecs cs;
    cs.tweeter.driver = tweeter();
    cs.tweeter.driver->recommendedHpHz = 3500;
    cs.woofer.productId = "mb";
    cs.crossoverHz = 3000;
    set.componentSet = cs;
    rig.add (set);
    rig.inst.drivers[0].productId = "set";   // FL.tweeter
    rig.inst.drivers[2].productId = "set";   // FL.midbass
    const auto plan2 = buildPlan (rig.inst, rig.config, rig.lookup());
    const auto* tw = planFor (plan2, "FL.tweeter");
    const auto* wf = planFor (plan2, "FL.midbass");
    check (tw != nullptr && tw->recommendedHpHz >= 3500.0, "Tweeter channel uses the set's tweeter HP",
           tw != nullptr ? juce::String (tw->recommendedHpHz, 1) : juce::String());
    check (wf != nullptr && wf->driverRmsW == midbass().powerRmsW, "Woofer channel uses the referenced woofer specs");
    const auto sub = gear::componentSubDriver (set, gear::ComponentRole::woofer, rig.lookup());
    check (sub != nullptr && sub->fsHz == midbass().fsHz, "componentSubDriver resolves a referenced product");
}
