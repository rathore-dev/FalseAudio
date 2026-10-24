// Turns the selected products + wiring into an ordered list of precise manual settings:
// wiring -> head unit -> DSP input / gain structure -> amp gains & switches -> speaker wiring -> mic -> safety.
//
// Gain structure follows the usual installer method (and the Audiofrog process):
//   target terminal voltage V = sqrt(P * R), P = min(amp power at the real load, driver RMS),
//   set with a 0 dBFS tone at the source's clean maximum, speakers disconnected, DMM on AC volts.
//   The tune itself runs polarity -> delay -> LR4 24 dB/oct crossovers -> EQ, sub LP half an octave
//   below the midbass HP, tweeter HP >= 1.6..2 x Fs, and a noise check at the end.

#include "InstallPlanner.h"
#include "../tune/CrossoverPlanner.h"
#include "../Types.h"

namespace fa::install
{
namespace
{
    juce::String num (double v, int dp = 1) { return juce::String (v, dp); }

    juce::String awgName (double awg)
    {
        if (awg <= 0.0)
            return juce::String ((int) std::round (1.0 - awg)) + "/0 AWG";
        return juce::String ((int) std::round (awg)) + " AWG";
    }

    DriverRole roleFromCategory (gear::Category c)
    {
        switch (c)
        {
            case gear::Category::subwoofer:      return DriverRole::subwoofer;
            case gear::Category::driverTweeter:  return DriverRole::tweeter;
            case gear::Category::driverMidrange: return DriverRole::midrange;
            case gear::Category::driverMidbass:  return DriverRole::midbass;
            default:                             return DriverRole::fullrange;
        }
    }

    Instruction make (const juce::String& area, const juce::String& title, const juce::String& detail,
                      const juce::String& why, Instruction::Severity sev, double value = 0, const juce::String& unit = {})
    {
        Instruction i;
        i.area = area; i.title = title; i.detail = detail; i.why = why; i.severity = sev; i.value = value; i.unit = unit;
        return i;
    }

    juce::var stringsToVar (const std::vector<juce::String>& v)
    {
        juce::Array<juce::var> a;
        for (auto& s : v)
            a.add (s);
        return a;
    }

    std::vector<juce::String> stringsFromVar (const juce::var& v)
    {
        std::vector<juce::String> out;
        if (auto* a = v.getArray())
            for (auto& s : *a)
                out.push_back (s.toString());
        return out;
    }

    /** Where a DSP output channel is amplified. */
    struct AmpSlot
    {
        int ampIndex = -1, slot = 0;
        bool bridged = false;
    };

    AmpSlot findAmpSlot (const Installation& inst, const juce::String& channelId)
    {
        for (size_t a = 0; a < inst.amps.size(); ++a)
            for (size_t k = 0; k < inst.amps[a].channelIds.size(); ++k)
                if (inst.amps[a].channelIds[k] == channelId)
                    return { (int) a, (int) k, inst.amps[a].bridged };
        return {};
    }

    juce::String ampChannelLabel (int slot, bool bridged)
    {
        return bridged ? "Ch " + juce::String (2 * slot + 1) + "+" + juce::String (2 * slot + 2) + " (bridged)"
                       : "Ch " + juce::String (slot + 1);
    }

    double toneFor (DriverRole r) { return r == DriverRole::subwoofer ? 50.0 : 400.0; }

    /** Specs the excursion model still needs (empty = it can run). Mirrors calc::excursionMm's three paths. */
    juce::StringArray missingForExcursion (const gear::DriverSpecs& d)
    {
        juce::StringArray m;
        if (d.xmaxMm <= 0) m.add ("Xmax");
        if (d.fsHz <= 0)   m.add ("Fs");
        if (d.qts <= 0 && ! (d.qes > 0 && d.qms > 0)) m.add ("Qts");
        const bool motor = d.bl > 0 && d.mmsGrams > 0;
        const bool sens  = d.sdCm2 > 0 && d.sensitivityDb > 0;
        const bool vas   = d.sdCm2 > 0 && d.vasLitres > 0 && d.qes > 0;
        if (! (motor || sens || vas))
            m.add ("Sd + sensitivity (or Bl + Mms)");
        return m;
    }
}

//==============================================================================
juce::var Installation::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("headUnit", headUnitId);
    o->setProperty ("dsp", dspId);
    o->setProperty ("dac", dacId);
    o->setProperty ("audioInterface", audioInterfaceId);
    o->setProperty ("mics", stringsToVar (micIds));

    juce::Array<juce::var> a;
    for (auto& u : amps)
    {
        auto* x = new juce::DynamicObject();
        x->setProperty ("amp", u.ampId);
        x->setProperty ("channels", stringsToVar (u.channelIds));
        x->setProperty ("bridged", u.bridged);
        x->setProperty ("powerWireLength", u.powerWireLengthM);
        x->setProperty ("powerWire", u.powerWireId);
        x->setProperty ("powerWireAwg", u.powerWireAwg);
        x->setProperty ("powerWireMaterial", u.powerWireMaterial);
        x->setProperty ("groundWireLength", u.groundWireLengthM);
        a.add (juce::var (x));
    }
    o->setProperty ("amps", a);

    juce::Array<juce::var> d;
    for (auto& u : drivers)
    {
        auto* x = new juce::DynamicObject();
        x->setProperty ("channel", u.channelId);
        x->setProperty ("product", u.productId);
        x->setProperty ("count", u.count);
        x->setProperty ("wiring", u.wiring);
        x->setProperty ("speakerWireLength", u.speakerWireLengthM);
        x->setProperty ("speakerWireAwg", u.speakerWireAwg);
        d.add (juce::var (x));
    }
    o->setProperty ("drivers", d);

    o->setProperty ("batteryVoltage", batteryVoltageEngineOn);
    o->setProperty ("alternatorA", alternatorA);
    o->setProperty ("batteryAh", batteryAh);
    o->setProperty ("alternator", alternatorId);
    o->setProperty ("battery", batteryId);
    o->setProperty ("headroom", ampToDriverPowerHeadroom);
    return juce::var (o);
}

Installation Installation::fromVar (const juce::var& v)
{
    Installation i;
    i.headUnitId       = v.getProperty ("headUnit", {}).toString();
    i.dspId            = v.getProperty ("dsp", {}).toString();
    i.dacId            = v.getProperty ("dac", {}).toString();
    i.audioInterfaceId = v.getProperty ("audioInterface", {}).toString();
    i.micIds           = stringsFromVar (v.getProperty ("mics", {}));

    if (auto* a = v.getProperty ("amps", {}).getArray())
        for (auto& x : *a)
        {
            AmpUse u;
            u.ampId             = x.getProperty ("amp", {}).toString();
            u.channelIds        = stringsFromVar (x.getProperty ("channels", {}));
            u.bridged           = x.getProperty ("bridged", false);
            u.powerWireLengthM  = x.getProperty ("powerWireLength", 5.0);
            u.powerWireId       = x.getProperty ("powerWire", {}).toString();
            u.powerWireAwg      = x.getProperty ("powerWireAwg", 4.0);
            u.powerWireMaterial = x.getProperty ("powerWireMaterial", "OFC").toString();
            u.groundWireLengthM = x.getProperty ("groundWireLength", 0.5);
            i.amps.push_back (u);
        }

    if (auto* d = v.getProperty ("drivers", {}).getArray())
        for (auto& x : *d)
        {
            DriverUse u;
            u.channelId          = x.getProperty ("channel", {}).toString();
            u.productId          = x.getProperty ("product", {}).toString();
            u.count              = x.getProperty ("count", 1);
            u.wiring             = x.getProperty ("wiring", {}).toString();
            u.speakerWireLengthM = x.getProperty ("speakerWireLength", 4.0);
            u.speakerWireAwg     = x.getProperty ("speakerWireAwg", 14.0);
            i.drivers.push_back (u);
        }

    i.batteryVoltageEngineOn   = v.getProperty ("batteryVoltage", 14.2);
    i.alternatorA              = v.getProperty ("alternatorA", 0.0);
    i.batteryAh                = v.getProperty ("batteryAh", 0.0);
    i.alternatorId             = v.getProperty ("alternator", {}).toString();
    i.batteryId                = v.getProperty ("battery", {}).toString();
    i.ampToDriverPowerHeadroom = v.getProperty ("headroom", 1.25);
    return i;
}

//==============================================================================
Plan buildPlan (const Installation& inst, const SystemConfig& config, const ProductLookup& lookup)
{
    using Sev = Instruction::Severity;
    Plan plan;

    auto find = [&] (const juce::String& id) -> const gear::Product*
    {
        return id.isNotEmpty() && lookup ? lookup (id) : nullptr;
    };

    const auto* hu  = find (inst.headUnitId);
    const auto* dsp = find (inst.dspId);
    const auto* dac = find (inst.dacId);
    const bool hasDsp = inst.dspId.isNotEmpty();
    const gear::SourceSpecs* huSrc  = hu  != nullptr && hu->source  ? &*hu->source  : nullptr;
    const gear::SourceSpecs* dspSrc = dsp != nullptr && dsp->source ? &*dsp->source : nullptr;
    const gear::SourceSpecs* dacSrc = dac != nullptr && dac->source ? &*dac->source : nullptr;

    std::vector<Instruction> wiring, head, dspIns, ampIns, speakers, mics, missing, safety;

    // Every calculation that can't run because a spec is missing becomes an info instruction instead.
    juce::StringArray missingSeen;
    auto needSpecs = [&] (const juce::String& product, const juce::String& fields, const juce::String& purpose)
    {
        const auto key = product + "|" + fields + "|" + purpose;
        if (missingSeen.contains (key))
            return;
        missingSeen.add (key);
        missing.push_back (make ("Missing specs", "Add " + fields + " for " + product + " to " + purpose,
                                 "Edit the product in the equipment database (manufacturer manual / spec sheet). "
                                 "Until then this value is not calculated" + juce::String (" or is shown as a labelled estimate."),
                                 "FalseAudio never invents specs for safety-critical settings.", Instruction::Severity::info));
    };

    //==========================================================================
    // Head unit: clean volume and pre-out level
    const bool huHasPreouts = huSrc != nullptr && (huSrc->preoutVoltageV > 0 || huSrc->preoutChannels > 0);
    double huV = 0;
    if (huSrc != nullptr)
    {
        plan.headUnitMaxCleanVolume = calc::maxCleanVolumeStep (*huSrc);
        huV = huSrc->preoutVoltageV;
        if (plan.headUnitMaxCleanVolume <= 0)
            needSpecs (hu->displayName(), "maxVolumeStep (or cleanVolumeStep)", "calculate the maximum clean volume step");
        if (huV <= 0 && huSrc->preoutChannels > 0)
            needSpecs (hu->displayName(), "pre-out voltage", "know the signal level it delivers");
    }
    else if (inst.headUnitId.isNotEmpty())
        needSpecs (hu != nullptr ? hu->displayName() : inst.headUnitId, "the head unit's source specs", "calculate its clean volume and pre-out level");

    //==========================================================================
    // Signal level reaching the amps (0 dBFS at the source's clean maximum)
    double sourceV = 0;
    juce::String sourceName;
    if (hasDsp)
    {
        sourceName = dsp != nullptr ? dsp->displayName() : juce::String ("DSP");
        sourceV = dspSrc != nullptr ? dspSrc->preoutVoltageV : 0.0;
    }
    else if (dacSrc != nullptr)
    {
        sourceName = dac->displayName();
        sourceV = dacSrc->preoutVoltageV;
    }
    else if (hu != nullptr)
    {
        sourceName = hu->displayName();
        sourceV = huV;
    }
    if (sourceV <= 0)
    {
        sourceV = 2.0;
        needSpecs (sourceName.isEmpty() ? juce::String ("the source") : sourceName, "output (pre-out) voltage",
                   "calculate exact amp gain-knob settings (2.0 V used as an ESTIMATE)");
        plan.warnings.add ("Output voltage of " + (sourceName.isEmpty() ? juce::String ("the source") : sourceName)
                           + " is unknown - 2.0 V assumed. Measure it (0 dB tone, DMM on the RCA) and enter it for exact gain settings.");
    }
    plan.sourceVoltageV = sourceV;

    //==========================================================================
    // Per channel: load, power, gain target, excursion-limited HP
    struct ChanCtx { const Installation::DriverUse* use; const gear::DriverSpecs* spec; DriverRole role; AmpSlot slot;
                     const gear::Product* prod; double perDriverW; };
    std::vector<ChanCtx> ctx;

    for (auto& du : inst.drivers)
    {
        ChannelPlan cp;
        cp.channelId = du.channelId;
        const auto* ch   = config.find (du.channelId);
        const auto* prod = find (du.productId);
        const gear::DriverSpecs* ds = prod != nullptr && prod->driver ? &*prod->driver : nullptr;
        const DriverRole role = ch != nullptr ? ch->role : (prod != nullptr ? roleFromCategory (prod->category) : DriverRole::fullrange);
        const int count = std::max (1, du.count);
        const auto slot = findAmpSlot (inst, du.channelId);
        const auto* ampProd = slot.ampIndex >= 0 ? find (inst.amps[(size_t) slot.ampIndex].ampId) : nullptr;
        // Ratings of the amp channel group this channel is on (e.g. the sub channel of a 5-channel amp has its
        // own power / minimum load). Bridged slot k uses output channels 2k / 2k+1.
        std::optional<gear::AmpSpecs> groupAmp;
        if (ampProd != nullptr && ampProd->amp)
            groupAmp = gear::ampSpecsForChannel (*ampProd->amp, slot.bridged ? 2 * slot.slot : slot.slot);
        const gear::AmpSpecs* amp = groupAmp ? &*groupAmp : nullptr;

        double coilOhms = 4.0;
        int coils = 1;
        if (ds != nullptr)
        {
            coils = std::max (1, ds->voiceCoils);
            coilOhms = ds->coilOhms > 0 ? ds->coilOhms : (ds->nominalOhms > 0 ? ds->nominalOhms : 0.0);
        }
        const juce::String model = prod != nullptr ? prod->displayName() : (du.productId.isNotEmpty() ? du.productId : du.channelId);
        if (ds == nullptr)
            needSpecs (model, "the driver's specs (impedance, RMS power, Fs, Xmax...)", "calculate load, gain and safe high-pass for "
                       + (ch != nullptr ? ch->label : du.channelId));
        if (coilOhms <= 0)
        {
            coilOhms = 4.0;
            cp.warnings.add ("Driver impedance unknown - 4 ohm ESTIMATE used.");
            if (ds != nullptr)
                needSpecs (model, "nominal impedance (and coilOhms / voiceCoils for DVC)", "calculate the exact load");
        }
        if (ds != nullptr && ds->powerRmsW <= 0)
            needSpecs (model, "RMS power rating", "limit the amp gain to what the driver can take");
        if (ds != nullptr)
            if (auto miss = missingForExcursion (*ds); ! miss.isEmpty())
                needSpecs (model, miss.joinIntoString (", "), "compute the excursion-safe high-pass");
        if (ds != nullptr && ds->fsHz <= 0 && role != DriverRole::subwoofer)
            needSpecs (model, "Fs", "set the Fs-based high-pass floor (role default used until then)");
        if ((count > 1 || coils > 1) && (du.wiring.isEmpty() || du.wiring == "single"))
            cp.warnings.add ("Wiring not specified for " + juce::String (count) + " driver(s) / " + juce::String (coils)
                             + " coil(s): calculated as all-series (highest impedance).");

        cp.loadOhms   = calc::combineLoad (coilOhms, coils, count, du.wiring);
        cp.driverRmsW = ds != nullptr ? ds->powerRmsW * count : 0.0;

        juce::String powerNote;
        if (amp != nullptr)
        {
            cp.ampPowerAtLoadW = calc::ampPowerAtLoad (*amp, cp.loadOhms, slot.bridged, &powerNote);
            const double minOhms = amp->minStableOhms * (slot.bridged ? 2.0 : 1.0);
            if (minOhms > 0 && cp.loadOhms < minOhms - 1.0e-6)
                cp.warnings.add ("DANGER: " + num (cp.loadOhms, 2) + " ohm is below the amp's minimum stable load of "
                                 + num (minOhms, 2) + " ohm" + (slot.bridged ? " (bridged = 2 x per-channel minimum)" : "")
                                 + ". Rewire for a higher impedance or the amp will overheat / protect.");
            if (powerNote.isNotEmpty())
                cp.warnings.add ("Amp power " + powerNote + " (estimate).");
            if (cp.ampPowerAtLoadW <= 0)
                needSpecs (ampProd->displayName(), juce::String (slot.bridged ? "bridged " : "") + "RMS power per channel (4/2/1 ohm)",
                           "compute the power at this load and the gain voltage");
            if (amp->minStableOhms <= 0)
                needSpecs (ampProd->displayName(), "minimum stable impedance", "verify the " + num (cp.loadOhms, 2) + " ohm load is safe");
        }
        else if (slot.ampIndex >= 0)
            needSpecs (ampProd != nullptr ? ampProd->displayName() : inst.amps[(size_t) slot.ampIndex].ampId, "the amplifier's specs",
                       "compute power, gain and current draw");

        double targetW = 0;
        if (cp.ampPowerAtLoadW > 0 && cp.driverRmsW > 0) targetW = std::min (cp.ampPowerAtLoadW, cp.driverRmsW);
        else if (cp.ampPowerAtLoadW > 0)               { targetW = cp.ampPowerAtLoadW; cp.warnings.add ("Driver RMS rating unknown - gain target uses the amp's full power."); }
        else                                             targetW = cp.driverRmsW;

        cp.targetVoltageRms = calc::voltageForPower (targetW, cp.loadOhms);
        cp.powerRatio = cp.driverRmsW > 0 ? cp.ampPowerAtLoadW / cp.driverRmsW : 0.0;
        if (cp.powerRatio > juce::jlimit (1.0, 1.25, inst.ampToDriverPowerHeadroom))
            cp.warnings.add ("Amp can deliver " + num (cp.ampPowerAtLoadW, 0) + " W but the driver(s) are rated "
                             + num (cp.driverRmsW, 0) + " W RMS: the gain is set for " + num (cp.driverRmsW, 0)
                             + " W - do not turn it higher.");

        // Recommended HP = max(role/Fs safety rule, excursion limit at the applied power, manufacturer)
        const double perDriverW = targetW / count;
        OutputChannel tmp;
        tmp.role = role;
        if (ds != nullptr)
        {
            tmp.driver.sizeInches = ds->sizeInches;
            tmp.driver.fsHz = ds->fsHz;
            cp.excursionLimitedHpHz = calc::excursionLimitedHighPass (*ds, perDriverW);
        }
        double hp = CrossoverPlanner::safeHighPassHz (tmp, config.safety);
        if (role == DriverRole::tweeter && ds != nullptr && ds->fsHz > 0 && cp.excursionLimitedHpHz <= 0)
            hp = std::max (hp, 2.0 * ds->fsHz);   // no Xmax data: use the conservative end of 1.6..2 x Fs
        hp = std::max (hp, cp.excursionLimitedHpHz);
        if (ds != nullptr)
        {
            hp = std::max (hp, ds->recommendedHpHz);
            cp.recommendedLpHz = ds->recommendedLpHz;
        }
        cp.recommendedHpHz = hp;
        if (role == DriverRole::subwoofer)
            cp.warnings.add ("Excursion limit assumes a sealed box / infinite baffle. In a ported box set the subsonic "
                             "filter to ~0.7-0.8 x the port tuning frequency.");

        plan.channels.push_back (cp);
        ctx.push_back ({ &du, ds, role, slot, prod, perDriverW });
    }

    // Sub LP half an octave below the lowest front way's HP (process rule; 80 Hz front HP with a sub).
    {
        double frontHp = 0;
        for (auto want : { DriverRole::midbass, DriverRole::fullrange, DriverRole::midrange })
        {
            for (size_t i = 0; i < ctx.size(); ++i)
                if (ctx[i].role == want)
                    frontHp = std::max (frontHp, plan.channels[i].recommendedHpHz);
            if (frontHp > 0)
                break;
        }
        frontHp = std::max (frontHp, 80.0);
        for (size_t i = 0; i < ctx.size(); ++i)
            if (ctx[i].role == DriverRole::subwoofer)
            {
                auto& cp = plan.channels[i];
                const double lp = frontHp / std::sqrt (2.0);
                cp.recommendedLpHz = cp.recommendedLpHz > 0 ? std::min (cp.recommendedLpHz, lp) : lp;
                if (cp.recommendedHpHz > 0 && cp.recommendedHpHz >= cp.recommendedLpHz)
                    cp.warnings.add ("The safe high-pass (" + num (cp.recommendedHpHz, 0) + " Hz) is at or above the sub's low-pass ("
                                     + num (cp.recommendedLpHz, 0) + " Hz): this sub cannot take the gain-target power here. "
                                     "Lower the sub channel's gain/power or use a sub with more Xmax.");
            }
    }

    // Gain knob settings (now that the source voltage is known)
    for (size_t i = 0; i < ctx.size(); ++i)
    {
        auto& cp = plan.channels[i];
        const double targetW = cp.targetVoltageRms * cp.targetVoltageRms / std::max (cp.loadOhms, 1.0e-6);
        cp.gainSettingV = calc::gainKnobVoltage (sourceV, cp.ampPowerAtLoadW > 0 ? cp.ampPowerAtLoadW : targetW, targetW);
    }

    //==========================================================================
    // Power wiring
    const double supplyV = inst.batteryVoltageEngineOn > 0 ? inst.batteryVoltageEngineOn : 14.2;
    double longestRun = 0;
    for (size_t a = 0; a < inst.amps.size(); ++a)
    {
        const auto& use = inst.amps[a];
        const auto* ap = find (use.ampId);
        const gear::AmpSpecs* amp = ap != nullptr && ap->amp ? &*ap->amp : nullptr;
        const juce::String ampName = "Amp " + juce::String ((int) a + 1) + (ap != nullptr ? " (" + ap->displayName() + ")" : juce::String());
        const bool internal = hasDsp && use.ampId == inst.dspId;

        // Output power = each used channel's rated power at its real load (its own channel group's ratings).
        // Channels without a driver entry count at their group's 4 ohm rating; with no usable channel data at all
        // the amp's worst-case total (every channel at its highest rating) is used.
        double outW = 0;
        for (size_t k = 0; k < use.channelIds.size(); ++k)
        {
            bool found = false;
            for (auto& cp : plan.channels)
                if (cp.channelId == use.channelIds[k]) { outW += cp.ampPowerAtLoadW; found = true; break; }
            if (! found && amp != nullptr)
                outW += calc::ampPowerAtLoad (gear::ampSpecsForChannel (*amp, use.bridged ? 2 * (int) k : (int) k), 4.0, use.bridged);
        }
        if (outW <= 0 && amp != nullptr)
            outW = gear::ampTotalMaxRmsW (*amp);
        const double eff = amp != nullptr ? calc::defaultEfficiencyPercent (*amp) : 60.0;
        const bool effEstimated = amp == nullptr || amp->efficiencyPercent <= 0;
        bool currentFromFuse = false;
        if (outW <= 0)
        {
            if (amp != nullptr && amp->fuseRatingA > 0)
                currentFromFuse = true;   // the amp's own fuse bounds its draw: usable as a labelled estimate
            else
            {
                if (! internal)
                    needSpecs (ap != nullptr ? ap->displayName() : use.ampId, "RMS power ratings (or fuse rating)",
                               "size the power wire and fuse");
                continue;
            }
        }

        // Wire: product spec first, then the gauge typed in.
        double awg = use.powerWireAwg;
        juce::String material = use.powerWireMaterial.isNotEmpty() ? use.powerWireMaterial : juce::String ("OFC");
        double ohmsPerM = 0, ampacity = 0;
        if (const auto* wp = find (use.powerWireId); wp != nullptr && wp->wire)
        {
            if (wp->wire->awg != 0 || wp->wire->mm2 <= 0) awg = wp->wire->awg;
            else                                           awg = calc::awgFromMm2 (wp->wire->mm2);
            if (wp->wire->material.isNotEmpty()) material = wp->wire->material;
            ohmsPerM = wp->wire->ohmsPerMetre;
            ampacity = wp->wire->ampacityA;
        }
        if (ohmsPerM <= 0) ohmsPerM = calc::copperResistancePerMetre (awg, material);
        if (ampacity <= 0) ampacity = calc::wireAmpacityA (awg, material);

        // Round trip = power run + ground run (ground assumed the same gauge; chassis path neglected).
        const double roundTrip = std::max (0.0, use.powerWireLengthM) + std::max (0.0, use.groundWireLengthM);
        const double rWire = ohmsPerM * roundTrip;
        longestRun = std::max (longestRun, roundTrip);

        // Class-D/AB input power is ~constant for a given output, so I rises as V at the amp sags: solved exactly
        // (a fixed-point iteration diverges / goes negative when the wire is far too thin).
        double vAmp = supplyV, current = 0;
        bool collapsed = false;
        if (currentFromFuse)
        {
            current = amp->fuseRatingA;
            vAmp = supplyV - current * rWire;
        }
        else
            current = calc::ampSupplyCurrent (outW, eff, supplyV, rWire, &vAmp, &collapsed);

        WireCheck wc;
        wc.what = ampName + " power wire";
        wc.currentA = current;
        wc.voltageDropV = current * rWire;
        wc.voltageAtLoadV = vAmp;
        wc.powerLossW = current * current * rWire;
        wc.recommendedAwg = calc::recommendedAwg (current, roundTrip, material);
        // Battery fuse: the manual's recommended external fuse when published, else 1.25 x the full-power draw rounded
        // up to a standard size. Either way it protects the WIRE, so the wire must be rated for it.
        const bool manualFuse = amp != nullptr && amp->recommendedFuseA > 0;
        const double fuse = manualFuse ? amp->recommendedFuseA : calc::standardFuseA (1.25 * current);
        const bool fuseTooBig = ! manualFuse && 1.25 * current > 500.0;   // beyond the largest single standard fuse
        wc.ok = ! collapsed && ! fuseTooBig && wc.voltageDropV <= 0.5 && fuse <= ampacity;
        while (wc.recommendedAwg > -3.0 + 1.0e-9 && calc::wireAmpacityA (wc.recommendedAwg, material) < fuse - 1.0e-9)
        {
            // thicker until the wire can carry the fuse actually used (e.g. the manual's)
            double next = -3.0;
            for (double g : { 16.0, 14.0, 12.0, 10.0, 8.0, 6.0, 4.0, 2.0, 1.0, 0.0, -1.0, -2.0, -3.0 })
                if (g < wc.recommendedAwg - 1.0e-9) { next = g; break; }
            wc.recommendedAwg = next;
        }
        if (! wc.ok && wc.recommendedAwg >= awg - 1.0e-9)
        {
            // Published wire data (resistance / ampacity) can fail where the generic gauge passes: never tell the
            // user to "use" the gauge that just failed - go at least one standard size thicker.
            for (double g : { 16.0, 14.0, 12.0, 10.0, 8.0, 6.0, 4.0, 2.0, 1.0, 0.0, -1.0, -2.0, -3.0 })
                if (g < awg - 1.0e-9)
                { wc.recommendedAwg = g; break; }
        }
        const bool noSingleRun = fuseTooBig
                                 || fuse > calc::wireAmpacityA (wc.recommendedAwg, material) + 1.0e-9
                                 || current * calc::copperResistancePerMetre (wc.recommendedAwg, material) * roundTrip > 0.5 + 1.0e-9;
        if (collapsed)
            plan.warnings.add ("DANGER: " + ampName + " cannot get " + num (outW, 0) + " W through " + awgName (awg) + " " + material
                               + " over " + num (roundTrip, 1) + " m - the supply would collapse (amp shuts down / wire overheats). Use much thicker wire.");
        if (noSingleRun)
            plan.warnings.add (ampName + " draws about " + num (current, 0) + " A at full power: no single " + material
                               + " run up to 4/0 AWG meets the 0.5 V drop and fuse rules. Use two parallel runs (each fused at the battery) "
                               "or shorten the run, and keep gains below full power.");
        plan.wires.push_back (wc);
        plan.totalCurrentA += current;

        const juce::String why = (currentFromFuse ? "ESTIMATE: power ratings unknown, the amp's " + num (amp->fuseRatingA, 0)
                                                        + " A fuse rating is used as its maximum draw. "
                                                      : num (outW, 0) + " W RMS out / " + num (eff, 0) + " % efficiency"
                                                        + (effEstimated ? " (class estimate)" : "") + " / " + num (vAmp, 1)
                                                        + " V = " + num (current, 1) + " A. ")
                               + awgName (awg) + " " + material + " = "
                               + juce::String (ohmsPerM * 1000.0, 3) + " mohm/m x " + num (roundTrip, 1)
                               + " m (power + ground) = " + juce::String (rWire * 1000.0, 1) + " mohm -> drop "
                               + juce::String (wc.voltageDropV, 2) + " V, " + num (wc.powerLossW, 0) + " W heat in the wire.";
        if (internal && current < 1.0e-6)
            continue;
        if (wc.ok)
            wiring.push_back (make ("Wiring", ampName + ": " + awgName (awg) + " " + material + " power wire is OK",
                                    "Expect about " + num (vAmp, 1) + " V at the amp's +12V terminal at full output with the engine running "
                                    "(measure B+ to GND with a DMM during loud playback). Ground: same gauge, "
                                    + num (use.groundWireLengthM, 1) + " m or shorter, to bare scraped metal on the chassis.",
                                    why, Sev::recommended, awg, "AWG"));
        else
            wiring.push_back (make ("Wiring", ampName + ": use " + awgName (wc.recommendedAwg) + " power wire (not " + awgName (awg) + ")",
                                    "Your " + awgName (awg) + " " + material + " run drops " + juce::String (wc.voltageDropV, 2)
                                    + " V (target <= 0.5 V) and/or cannot carry a " + num (fuse, 0) + " A fuse (rated ~"
                                    + num (ampacity, 0) + " A). Use " + awgName (wc.recommendedAwg) + " " + material
                                    + " for both the power and ground wires.",
                                    why, fuse > ampacity ? Sev::danger : Sev::important, wc.recommendedAwg, "AWG"));

        const double runFuse = fuse;   // if this exceeds the wire's rating the instruction above says to use thicker wire
        const bool fuseNeedsThickerWire = fuse > ampacity;
        const double fuseWireAwg = fuseNeedsThickerWire ? wc.recommendedAwg : awg;
        const double fuseWireAmpacity = fuseNeedsThickerWire ? calc::wireAmpacityA (wc.recommendedAwg, material) : ampacity;
        wiring.push_back (make ("Wiring", ampName + ": fuse " + num (runFuse, 0) + " A within 45 cm (18 in) of the battery"
                                    + (fuseNeedsThickerWire ? " - ONLY on " + awgName (wc.recommendedAwg) + " wire" : juce::String()),
                                "Install the fuse holder on the power wire no more than 45 cm from the battery + terminal. "
                                "Leave the fuse OUT until all wiring is finished."
                                + (amp != nullptr && amp->fuseRatingA > 0
                                       ? " The amp's own fuse(s) total " + num (amp->fuseRatingA, 0) + " A - replace only with the same rating."
                                       : juce::String()),
                                (manualFuse ? "Fuse = the " + num (fuse, 0) + " A external fuse the amp's manual recommends (full-power draw ~"
                                              + num (current, 1) + " A)"
                                            : "Fuse = 1.25 x " + num (current, 1) + " A rounded up to a standard size")
                                + "; it protects the WIRE ("
                                + awgName (fuseWireAwg) + " " + material + " ~ " + num (fuseWireAmpacity, 0) + " A), so it must never exceed the wire's rating."
                                + (fuseNeedsThickerWire ? " NEVER put this fuse on your " + awgName (awg) + " wire (rated ~" + num (ampacity, 0)
                                                              + " A): a fuse larger than the wire's rating lets the wire overheat before it blows."
                                                        : juce::String()),
                                fuse > ampacity ? Sev::danger : Sev::important, runFuse, "A"));
    }

    plan.recommendedMainFuseA = plan.totalCurrentA > 0 ? calc::standardFuseA (1.25 * plan.totalCurrentA) : 0.0;
    if (inst.amps.size() > 1 && plan.totalCurrentA > 0)
    {
        const double mainAwg = calc::recommendedAwg (plan.totalCurrentA, longestRun, "OFC");
        wiring.push_back (make ("Wiring", "Shared main wire: " + awgName (mainAwg) + " OFC with a " + num (plan.recommendedMainFuseA, 0) + " A main fuse",
                                "Only if the amps share one wire from the battery to a distribution block: run " + awgName (mainAwg)
                                + " OFC, main fuse at the battery " + num (plan.recommendedMainFuseA, 0)
                                + " A, and fuse each branch at the block with the per-amp values above.",
                                "Total draw " + num (plan.totalCurrentA, 1) + " A x 1.25 = " + num (1.25 * plan.totalCurrentA, 0)
                                + " A -> next standard fuse; gauge chosen for <= 0.5 V drop over " + num (longestRun, 1) + " m.",
                                Sev::important, plan.recommendedMainFuseA, "A"));
    }
    // Charging system: typed values win, else the selected alternator / battery products. A hot rating (what the
    // alternator really makes once warm) is preferred over the headline cold rating.
    double alternatorA = inst.alternatorA, alternatorIdleA = 0, batteryAh = inst.batteryAh;
    if (const auto* alt = find (inst.alternatorId); alt != nullptr && alt->alternator)
    {
        if (alternatorA <= 0)
            alternatorA = alt->alternator->hotRatedA > 0 ? alt->alternator->hotRatedA : alt->alternator->ratedA;
        alternatorIdleA = alt->alternator->idleA;
    }
    if (const auto* bat = find (inst.batteryId); bat != nullptr && bat->battery && batteryAh <= 0)
        batteryAh = bat->battery->ah;

    if (plan.totalCurrentA > 0)
    {
        const double musicA = plan.totalCurrentA / 8.0;   // music averages ~1/8 of rated power (-9 dB)
        if (alternatorIdleA > 0 && musicA + 50.0 > alternatorIdleA)
            plan.warnings.add ("At idle the alternator makes only ~" + num (alternatorIdleA, 0) + " A: loud music (~" + num (musicA, 0)
                               + " A average) plus ~50 A for the car will drain the battery while idling.");
        if (batteryAh > 0)
            wiring.push_back (make ("Wiring", "Engine-off listening: about " + num (0.5 * batteryAh / std::max (musicA, 1.0), 1) + " h at loud levels",
                                    "With the engine off the amps run from the " + num (batteryAh, 0) + " Ah battery. Do not discharge a "
                                    "starting battery below ~50 % or the car may not start.",
                                    "50 % of " + num (batteryAh, 0) + " Ah / ~" + num (musicA, 0) + " A average music draw (1/8 of the "
                                    + num (plan.totalCurrentA, 0) + " A full-power draw).", Sev::info));
        if (alternatorA > 0 && plan.totalCurrentA + 50.0 > alternatorA)
            plan.warnings.add ("Amps can draw " + num (plan.totalCurrentA, 0) + " A at full power; with ~50 A for the car itself that exceeds the "
                               + num (alternatorA, 0) + " A alternator. Do the 'Big 3' upgrade and/or a higher-output alternator, "
                               "or keep listening levels moderate (music averages ~1/8 of rated power).");
        else if (plan.totalCurrentA > 100.0)
            wiring.push_back (make ("Wiring", "Consider the 'Big 3' upgrade",
                                    "Upgrade alternator+ to battery+, battery- to chassis, and engine block to chassis to the same gauge as your main power wire.",
                                    "Full-power draw is " + num (plan.totalCurrentA, 0) + " A; stock charging wires cause voltage sag at that current.",
                                    Sev::recommended));
    }
    if (! inst.amps.empty())
        wiring.push_back (make ("Wiring", "Route RCA and remote wires away from the power wire",
                                "Run RCAs down the opposite side of the car from the power wire (cross it only at 90 degrees). "
                                "Connect the amp remote-on lead to the head unit's / DSP's remote output.",
                                "Parallel runs couple alternator whine and amp switching noise into the signal.", Sev::recommended));

    //==========================================================================
    // Head unit
    if (huSrc != nullptr || inst.headUnitId.isNotEmpty())
    {
        const juce::String area = "Head unit" + (hu != nullptr ? " (" + hu->displayName() + ")" : juce::String());
        if (huSrc != nullptr && plan.headUnitMaxCleanVolume > 0)
        {
            const bool known = huSrc->cleanVolumeStep > 0;
            head.push_back (make (area, "Maximum clean volume: step " + juce::String ((int) plan.headUnitMaxCleanVolume)
                                          + (huSrc->maxVolumeStep > 0 ? " of " + juce::String (huSrc->maxVolumeStep) : juce::String()),
                                  "Use this step for every gain-setting measurement and never go above it while listening. "
                                  "Verify: play a 0 dB 1 kHz tone, measure the RCA pre-out (or watch it on a scope) and step up "
                                  "until the voltage stops rising or the sine flattens; the step before that is the real maximum.",
                                  known ? juce::String ("Published clean-volume step for this head unit.")
                                        : "Not published: most head units reach full pre-out voltage and start clipping in the top 15-25 % "
                                          "of the scale, so ~80 % of " + juce::String (huSrc->maxVolumeStep) + " steps is used.",
                                  Sev::important, plan.headUnitMaxCleanVolume, "step"));
        }
        if (huSrc != nullptr && huV > 0)
            head.push_back (make (area, "Pre-out level at that step: about " + num (huV, 1) + " V",
                                  "This is the signal reaching " + juce::String (hasDsp ? "the DSP input." : "the amplifiers."),
                                  "Rated pre-out voltage of the head unit (0 dBFS content at the clean maximum).",
                                  Sev::info, huV, "V"));
        if (huSrc != nullptr && ! huHasPreouts)
            head.push_back (make (area, "No RCA pre-outs: use speaker-level (high-level) inputs",
                                  "Connect the head unit's speaker outputs to the " + juce::String (hasDsp ? "DSP's" : "amp's")
                                  + " high-level inputs (enable auto-turn-on / signal sense if there is no remote wire).",
                                  "Without pre-outs the speaker outputs are the only line to the next stage.", Sev::important));

        if (hasDsp)
        {
            head.push_back (make (area, "EQ flat, loudness OFF, bass boost OFF",
                                  "Set every EQ band to 0 / preset 'Flat' or 'Off', loudness OFF, bass boost / bass enhancer / "
                                  "'sound' presets OFF, subwoofer level 0 (or off if the sub is fed from the DSP).",
                                  "The DSP does all EQ; head-unit processing changes with volume and breaks the tune.", Sev::important));
            head.push_back (make (area, "Time alignment OFF, crossovers / HPF / LPF OFF (full range)",
                                  "Disable listening-position / time-alignment, and set any head-unit high-pass or low-pass to 'Through' / 'Off'.",
                                  "The DSP handles delays and crossovers; doing it twice causes phase errors and holes in the response.",
                                  Sev::important));
            head.push_back (make (area, "Balance and fader centred",
                                  "Balance 0, fader 0.", "Any offset changes the level the DSP receives on each input.", Sev::recommended));
        }
        else
        {
            head.push_back (make (area, "Bass boost OFF, loudness OFF, EQ flat",
                                  "Start from a flat head unit. Bass boost/loudness add up to +12 dB at low volume and clip the amps.",
                                  "Gain setting assumes a flat 0 dBFS signal.", Sev::important));
            if (huSrc != nullptr && huSrc->crossovers)
                head.push_back (make (area, "Use the head unit's crossovers",
                                      "Front/rear HPF and sub LPF in the head unit: see the per-channel frequencies under 'Crossover limits'.",
                                      "No DSP is installed.", Sev::recommended));
        }
    }

    //==========================================================================
    // DSP input & gain structure
    if (hasDsp)
    {
        const juce::String area = "DSP" + (dsp != nullptr ? " (" + dsp->displayName() + ")" : juce::String());
        const double dspMaxIn = dspSrc != nullptr ? dspSrc->maxInputVoltageV : 0.0;

        if (huSrc != nullptr)
            dspIns.push_back (make (area, huHasPreouts ? "Input: RCA (low-level) from the head unit pre-outs" : "Input: high-level (speaker) inputs",
                                    huHasPreouts ? "Use the low-level/RCA input mode in the DSP software."
                                                 : "Use the high-level input mode (and its higher input-sensitivity range).",
                                    "Matches the head unit's output type.", Sev::important));

        if (huV > 0 && dspMaxIn <= 0 && dsp != nullptr)
            needSpecs (dsp->displayName(), "max input voltage", "match the head unit's level to the DSP input");
        if (huV > 0 && dspMaxIn > 0)
        {
            const double db = 20.0 * std::log10 (huV / dspMaxIn);
            if (db > 0.5)
                dspIns.push_back (make (area, "Input level: attenuate by " + num (db, 1) + " dB",
                                        "Set the DSP input sensitivity/gain to its high-voltage (lowest gain) range, or reduce the input "
                                        "level by " + num (db, 1) + " dB, so the head unit at its clean maximum does not clip the DSP input.",
                                        "Head unit " + num (huV, 1) + " V > DSP input clip point " + num (dspMaxIn, 1) + " V.",
                                        Sev::important, -db, "dB"));
            else if (db < -6.0)
                dspIns.push_back (make (area, "Input level: raise by up to " + num (-db, 1) + " dB",
                                        "Increase the DSP input gain (or choose its low-voltage range) until the input meter peaks just "
                                        "below clip with a 0 dB track at the head unit's clean maximum.",
                                        "Head unit " + num (huV, 1) + " V is well below the DSP's " + num (dspMaxIn, 1)
                                        + " V input range; using the full range improves signal-to-noise.",
                                        Sev::recommended, -db, "dB"));
            else
                dspIns.push_back (make (area, "Input level: matched",
                                        "Leave the DSP input gain at 0 dB; confirm the input meter peaks just below clip.",
                                        "Head unit " + num (huV, 1) + " V vs DSP input " + num (dspMaxIn, 1) + " V.", Sev::info));
        }

        dspIns.push_back (make (area, "Master volume / output gains at 0 dB for gain setting",
                                "Set the DSP master to its maximum (0 dB), every output channel gain to 0 dB and EQ flat while you set amp gains. "
                                "FalseAudio then only CUTS channel levels during the tune.",
                                "Amp gains are referenced to the DSP's full output of " + num (sourceV, 1) + " V.",
                                Sev::important, sourceV, "V"));
        dspIns.push_back (make (area, "Tuning order (done by FalseAudio)",
                                "1) polarity of every driver, 2) delays (time alignment), 3) Linkwitz-Riley 24 dB/oct crossovers "
                                "(sub LP half an octave below the midbass HP, tweeter HP >= 1.6-2 x Fs), 4) EQ, 5) noise check.",
                                "Audiofrog DSP tuning process.", Sev::info));
    }

    // Crossover limits per channel (DSP or, without one, head unit / amp filters)
    for (size_t i = 0; i < ctx.size(); ++i)
    {
        const auto& cp = plan.channels[i];
        const auto* ch = config.find (cp.channelId);
        const juce::String label = ch != nullptr ? ch->label : cp.channelId;
        const auto* ds = ctx[i].spec;
        juce::String why;
        if (ctx[i].role == DriverRole::tweeter && ds != nullptr && ds->fsHz > 0)
            why << "Tweeter Fs " << num (ds->fsHz, 0) << " Hz -> HP at least 1.6-2 x Fs. ";
        else if (ds != nullptr && ds->fsHz > 0)
            why << "Fs " << num (ds->fsHz, 0) << " Hz. ";
        if (cp.excursionLimitedHpHz > 0)
            why << "Xmax " << num (ds->xmaxMm, 2) << " mm would be exceeded below ~" << num (cp.excursionLimitedHpHz, 0)
                << " Hz (LR4) at " << num (ctx[i].perDriverW, 0) << " W per driver (sealed/infinite-baffle model, continuous sine). ";
        if (ds != nullptr && ds->recommendedHpHz > 0)
            why << "Manufacturer recommends >= " << num (ds->recommendedHpHz, 0) << " Hz. ";

        if (why.isEmpty())
            why = "ESTIMATE: role-based safety floor only - add the driver's Fs / Xmax for a calculated limit.";

        if (ctx[i].role == DriverRole::subwoofer)
            dspIns.push_back (make ("Crossover limits", label + ": subsonic HP " + num (cp.recommendedHpHz, 0) + " Hz, LP about "
                                        + num (cp.recommendedLpHz, 0) + " Hz",
                                    "LR4 24 dB/oct. The LP sits half an octave below the front stage's lowest HP so the sub and midbass blend.",
                                    why, Sev::important, cp.recommendedHpHz, "Hz"));
        else
            dspIns.push_back (make ("Crossover limits", label + ": never high-pass below " + num (cp.recommendedHpHz, 0) + " Hz",
                                    "LR4 24 dB/oct. FalseAudio will not place this channel's HP lower than this value."
                                    + (cp.recommendedLpHz > 0 ? " Manufacturer LP: " + num (cp.recommendedLpHz, 0) + " Hz." : juce::String()),
                                    why, ctx[i].role == DriverRole::tweeter ? Sev::danger : Sev::important, cp.recommendedHpHz, "Hz"));
    }

    //==========================================================================
    // Amplifiers
    for (size_t a = 0; a < inst.amps.size(); ++a)
    {
        const auto& use = inst.amps[a];
        const auto* ap = find (use.ampId);
        const gear::AmpSpecs* amp = ap != nullptr && ap->amp ? &*ap->amp : nullptr;
        const juce::String area = "Amp " + juce::String ((int) a + 1) + (ap != nullptr ? " (" + ap->displayName() + ")" : juce::String());
        const bool internal = hasDsp && use.ampId == inst.dspId;

        if (! internal)
        {
            if (hasDsp)
                ampIns.push_back (make (area, "Crossover switches OFF (full range)",
                                        "Set every HPF/LPF/BPF switch to OFF / FULL / ALL-PASS. If a filter cannot be disabled, set HPF to "
                                        "its lowest and LPF to its highest frequency.",
                                        "The DSP does the crossovers; a second filter changes slope and phase.", Sev::important));
            if (amp == nullptr || amp->hasBassBoost)
                ampIns.push_back (make (area, "Bass boost 0 dB",
                                        "Turn bass boost / bass EQ fully off (0 dB). Also any remote bass knob to its 0 dB / centre-detent position.",
                                        "Bass boost adds up to " + num (amp != nullptr && amp->bassBoostMaxDb > 0 ? amp->bassBoostMaxDb : 12.0, 0)
                                        + " dB at ~45 Hz after the gain is set - it clips the amp and over-excurts the driver.",
                                        Sev::important, 0, "dB"));
            if (amp != nullptr && amp->hasSubsonic)
                ampIns.push_back (make (area, hasDsp ? "Subsonic filter: lowest / OFF" : "Subsonic filter ON",
                                        hasDsp ? "The DSP high-passes the sub; set the amp's subsonic to its lowest frequency or OFF."
                                               : "Set the subsonic to the sub's recommended HP (see 'Crossover limits').",
                                        "Avoid stacking two high-pass filters.", Sev::recommended));
            if (! hasDsp && amp != nullptr)
                ampIns.push_back (make (area, "Use the amp's filters (no DSP)",
                                        "Set HPF on speaker channels and LPF on sub channels to the values under 'Crossover limits'."
                                        + juce::String (amp->hasHpf ? "" : " This amp has no HPF: tweeters need their passive crossover."),
                                        "Without a DSP the amp filters are the drivers' only protection.", Sev::important));
            ampIns.push_back (make (area, "Input mode: low-level (RCA)" + juce::String (use.bridged ? ", BRIDGED output" : ""),
                                    use.bridged ? "Feed the input pair as the manual says for bridged mode (usually the left input, or both "
                                                  "inputs with the mono switch) and connect the speaker to the left (+) and right (-) terminals."
                                                : "Use the RCA inputs from " + sourceName + ".",
                                    use.bridged ? (amp != nullptr && amp->minStableOhms > 0
                                                       ? "Bridged load must be at least " + num (2.0 * amp->minStableOhms, 1) + " ohm (2 x per-channel minimum)."
                                                       : juce::String ("Bridged load must be at least 2 x the per-channel minimum load (usually 4 ohm) - check the manual."))
                                                : juce::String ("Low-level inputs give the best noise performance."),
                                    use.bridged ? Sev::important : Sev::recommended));
        }

        for (size_t k = 0; k < use.channelIds.size(); ++k)
        {
            const auto& id = use.channelIds[k];
            size_t idx = ctx.size();
            for (size_t i = 0; i < ctx.size(); ++i)
                if (plan.channels[i].channelId == id) { idx = i; break; }
            if (idx == ctx.size())
                continue;

            const auto& cp = plan.channels[idx];
            const auto* ch = config.find (id);
            if (cp.targetVoltageRms <= 0)
                continue;   // neither amp power nor driver RMS known: reported under "Missing specs"
            if (! internal && amp != nullptr && amp->inputSensitivityMinV <= 0 && amp->inputSensitivityMaxV <= 0)
                needSpecs (ap->displayName(), "input sensitivity range", "check the gain knob can reach the target voltage");
            const juce::String chLabel = ampChannelLabel ((int) k, use.bridged) + " - " + (ch != nullptr ? ch->label : id);
            const double targetW = cp.targetVoltageRms * cp.targetVoltageRms / std::max (cp.loadOhms, 1.0e-6);
            const double tone = toneFor (ctx[idx].role);

            juce::String why;
            why << "V = sqrt(P x R) = sqrt(" << num (targetW, 0) << " W x " << num (cp.loadOhms, 2) << " ohm) = "
                << num (cp.targetVoltageRms, 1) << " V. P = min(amp " << num (cp.ampPowerAtLoadW, 0) << " W at "
                << num (cp.loadOhms, 2) << " ohm, driver RMS " << num (cp.driverRmsW, 0) << " W).";

            if (internal)
            {
                ampIns.push_back (make (area, chLabel + ": limit output to " + num (cp.targetVoltageRms, 1) + " V",
                                        "Amplified inside the DSP (no gain knob). Speakers disconnected, 0 dB " + num (tone, 0)
                                        + " Hz tone, master at 0 dB: lower this channel's output level in the DSP software until the "
                                        "DMM (AC volts) on its speaker terminals reads " + num (cp.targetVoltageRms, 1) + " V, never more.",
                                        why, Sev::important, cp.targetVoltageRms, "V"));
                continue;
            }

            juce::String knob;
            auto sev = Sev::important;
            if (amp != nullptr && amp->inputSensitivityMaxV > 0 && cp.gainSettingV > amp->inputSensitivityMaxV)
            {
                const double over = 20.0 * std::log10 (cp.gainSettingV / amp->inputSensitivityMaxV);
                knob << "Even at minimum gain (" << num (amp->inputSensitivityMaxV, 1) << " V) the amp is too loud for this driver: "
                     << "set the knob to minimum and reduce this channel's " << (hasDsp ? "DSP output" : "source level") << " by "
                     << num (over, 1) << " dB, then fine-tune with the DMM. ";
                sev = Sev::danger;
            }
            else if (amp != nullptr && amp->inputSensitivityMinV > 0 && cp.gainSettingV < amp->inputSensitivityMinV)
                knob << "The knob only goes to " << num (amp->inputSensitivityMinV, 2) << " V (max gain): at that setting this channel "
                     << "reaches less than full power - that is safe. ";
            else
                knob << "Input-sensitivity knob around " << num (cp.gainSettingV, 2) << " V (knob markings are approximate - the DMM reading decides). ";

            juce::String detail;
            detail << knob << "\nDMM method:\n"
                   << "1. Disconnect the speakers from this channel. All gains at minimum, filters OFF, bass boost 0 dB.\n"
                   << "2. " << (hasDsp ? "DSP master 0 dB, channel gain 0 dB, EQ flat, and this channel's filters bypassed (safe: no speaker connected). "
                                       : "Head-unit EQ flat. ")
                   << "Head unit at step " << juce::String ((int) plan.headUnitMaxCleanVolume) << " (clean maximum).\n"
                   << "3. Play a 0 dB " << num (tone, 0) << " Hz sine (use <= 400 Hz unless your meter is true-RMS at higher frequencies).\n"
                   << "4. DMM on AC volts across this channel's speaker terminals; turn the gain up slowly until it reads "
                   << num (cp.targetVoltageRms, 1) << " V. Stop - do not go higher.\n"
                   << "5. Stop the tone. " << (hasDsp ? "RE-ENABLE this channel's DSP high-pass / low-pass filters BEFORE reconnecting the "
                                                        "speaker (a tweeter or midrange on a full-range signal can be destroyed in seconds). "
                                                      : "")
                   << "Then reconnect the speaker.\n"
                   << "Alternative: oscilloscope on the terminals - raise gain until the sine just starts to flatten, back off slightly, "
                   << "and never exceed " << num (cp.targetVoltageRms, 1) << " V; or let FalseAudio's mic distortion check confirm "
                   << "no clipping at the clean maximum.";
            if (amp != nullptr && amp->ratedVoltageV > supplyV + 0.05)
                detail << "\nNote: the power ratings are at " << num (amp->ratedVoltageV, 1) << " V; at your " << num (supplyV, 1)
                       << " V an unregulated amp makes up to " << num (100.0 * (1.0 - std::pow (supplyV / amp->ratedVoltageV, 2.0)), 0)
                       << " % less. If the sine flattens (clips) before " << num (cp.targetVoltageRms, 1)
                       << " V, stop at the last clean reading - never set the gain into clipping.";
            if (! cp.warnings.isEmpty())
                detail << "\nNote: " << cp.warnings.joinIntoString ("\nNote: ");

            ampIns.push_back (make (area, chLabel + ": set gain to " + num (cp.targetVoltageRms, 1) + " V AC ("
                                        + num (targetW, 0) + " W @ " + num (cp.loadOhms, 2) + " ohm)",
                                    detail, why, sev, cp.targetVoltageRms, "V"));
        }
    }

    //==========================================================================
    // Speaker wiring
    for (size_t i = 0; i < ctx.size(); ++i)
    {
        const auto& du = *ctx[i].use;
        const auto& cp = plan.channels[i];
        const auto* ds = ctx[i].spec;
        const auto* ch = config.find (du.channelId);
        const juce::String label = ch != nullptr ? ch->label : du.channelId;
        const int coils = ds != nullptr ? std::max (1, ds->voiceCoils) : 1;
        const double coilOhms = ds != nullptr ? (ds->coilOhms > 0 ? ds->coilOhms : (ds->nominalOhms > 0 ? ds->nominalOhms : 4.0)) : 4.0;

        if (std::max (1, du.count) > 1 || coils > 1)
            speakers.push_back (make ("Speaker wiring", label + ": wire for " + num (cp.loadOhms, 2) + " ohm",
                                      calc::wiringDiagram (coils, du.count, du.wiring, coilOhms),
                                      "Series adds impedances; N equal impedances in parallel give R / N.",
                                      Sev::important, cp.loadOhms, "ohm"));

        // Speaker wire: round trip (+ and -) resistance in series with the load.
        const double rw = calc::copperResistancePerMetre (du.speakerWireAwg, "OFC") * 2.0 * std::max (0.0, du.speakerWireLengthM);
        if (cp.loadOhms > 0)
        {
            WireCheck wc;
            wc.what = label + " speaker wire";
            const double v = cp.targetVoltageRms;
            wc.currentA = v / (cp.loadOhms + rw);
            wc.voltageDropV = wc.currentA * rw;
            wc.voltageAtLoadV = v - wc.voltageDropV;
            wc.powerLossW = wc.currentA * wc.currentA * rw;
            const double lossDb = -20.0 * std::log10 (cp.loadOhms / (cp.loadOhms + rw));
            // Recommend the thinnest gauge losing <= 0.5 dB (keeps damping factor reasonable too).
            wc.recommendedAwg = 8.0;   // thickest offered if nothing thinner meets 0.5 dB
            for (double g : { 18.0, 16.0, 14.0, 12.0, 10.0, 8.0 })
            {
                const double r = calc::copperResistancePerMetre (g, "OFC") * 2.0 * du.speakerWireLengthM;
                if (-20.0 * std::log10 (cp.loadOhms / (cp.loadOhms + r)) <= 0.5) { wc.recommendedAwg = g; break; }
            }
            wc.ok = lossDb <= 0.5;
            plan.wires.push_back (wc);
            if (! wc.ok)
                speakers.push_back (make ("Speaker wiring", label + ": use " + awgName (wc.recommendedAwg) + " speaker wire",
                                          num (du.speakerWireLengthM, 1) + " m of " + awgName (du.speakerWireAwg) + " loses "
                                          + num (lossDb, 2) + " dB into " + num (cp.loadOhms, 2) + " ohm.",
                                          "Wire " + juce::String (rw * 1000.0, 0) + " mohm (both conductors) in series with the load.",
                                          Sev::recommended, wc.recommendedAwg, "AWG"));
        }
    }
    if (! ctx.empty())
        speakers.push_back (make ("Speaker wiring", "Keep polarity consistent: (+) to (+) everywhere",
                                  "Use the striped/marked conductor for (+) on every run. FalseAudio checks polarity acoustically as the first tuning step.",
                                  "A reversed driver cancels its neighbour around the crossover.", Sev::recommended));

    //==========================================================================
    // Measurement mics
    for (auto& id : inst.micIds)
    {
        const auto* mp = find (id);
        if (mp == nullptr || ! mp->mic)
            continue;
        const auto& m = *mp->mic;
        juce::String detail = m.type.equalsIgnoreCase ("USB") ? juce::String ("Plug it straight into the laptop's USB port.")
                                                              : juce::String ("Connect it with an XLR cable to the audio interface.");
        if (m.needsPhantom)
            detail << " Turn ON 48 V phantom power for its input; set the preamp gain so the sweep peaks around -12 dBFS.";
        if (m.calFileAvailable)
            detail << " Load its calibration file" << (m.calFileUrl.isNotEmpty() ? " (" + m.calFileUrl + ")" : juce::String()) << " in FalseAudio.";
        mics.push_back (make ("Mic", mp->displayName(), detail, "Measurement accuracy depends on the right gain and calibration.", Sev::recommended));
    }

    //==========================================================================
    // Safety
    for (auto& cp : plan.channels)
        for (auto& w : cp.warnings)
            if (w.startsWith ("DANGER"))
                plan.warnings.add (cp.channelId + ": " + w);
    for (auto& w : plan.warnings)
        safety.push_back (make ("Safety", w.upToFirstOccurrenceOf (".", false, false), w, {}, w.contains ("DANGER") ? Sev::danger : Sev::important));

    safety.push_back (make ("Safety", "Disconnect the battery negative before wiring",
                            "Remove the battery (-) terminal before running power wire; install the fuse last.",
                            "An unfused short of a power wire to the chassis can start a fire.", Sev::danger));
    safety.push_back (make ("Safety", "Gains are not volume controls",
                            "Never turn amp gains above the voltages set above, never re-enable bass boost, and never exceed head unit step "
                            + juce::String ((int) plan.headUnitMaxCleanVolume) + ".",
                            "Above them the amp clips or the driver gets more than its RMS rating.", Sev::important));
    safety.push_back (make ("Safety", "Noise check at the end",
                            "After tuning, mute the source / volume 0 with everything on and listen at the seat. If hiss is audible, "
                            "turn ALL amp gains down by the same amount (never just one channel), then let FalseAudio re-match levels.",
                            "Audiofrog process: the last step is a noise check; equal gain reductions keep the tuned balance.",
                            Sev::recommended));

    for (auto* section : { &wiring, &head, &dspIns, &ampIns, &speakers, &mics, &missing, &safety })
        plan.instructions.insert (plan.instructions.end(), section->begin(), section->end());
    return plan;
}

//==============================================================================
void applyToConfig (const Plan& plan, const Installation& inst, const ProductLookup& lookup, SystemConfig& config)
{
    auto find = [&] (const juce::String& id) -> const gear::Product*
    {
        return id.isNotEmpty() && lookup ? lookup (id) : nullptr;
    };

    if (const auto* dsp = find (inst.dspId); dsp != nullptr && dsp->source)
    {
        const auto& s = *dsp->source;
        if (s.peqBandsPerChannel > 0) config.caps.peqBandsPerChannel = s.peqBandsPerChannel;
        if (s.maxDelayMs > 0)         config.caps.maxDelayMs = s.maxDelayMs;
        if (s.sampleRateHz > 0)       config.caps.sampleRate = s.sampleRateHz;
    }

    double minMidbassHp = 0, minMidrangeHp = 0, minTweeterHp = 0, minTweeterFactor = 0;
    auto keepMin = [] (double& v, double x) { if (x > 0) v = v > 0 ? std::min (v, x) : x; };

    for (auto& du : inst.drivers)
    {
        auto* ch = config.find (du.channelId);
        if (ch == nullptr)
            continue;
        const auto* p = find (du.productId);
        if (p != nullptr && p->driver)
        {
            const auto& d = *p->driver;
            const int n = std::max (1, du.count);
            ch->driver.model      = p->displayName() + (n > 1 ? " x" + juce::String (n) : juce::String());
            ch->driver.sizeInches = d.sizeInches;
            ch->driver.fsHz       = d.fsHz;
            ch->driver.powerRmsW  = d.powerRmsW * n;
            ch->driver.xmaxMm     = d.xmaxMm;
        }

        for (auto& cp : plan.channels)
        {
            if (cp.channelId != du.channelId || cp.recommendedHpHz <= 0)
                continue;
            // Only ever RAISE the high pass to the safe value; snap up onto the DSP's grid.
            auto& hp = ch->settings.highPass;
            if (hp.freq < cp.recommendedHpHz)
            {
                double f = CrossoverPlanner::snapFrequency (cp.recommendedHpHz, config.caps);
                for (int guard = 0; f < cp.recommendedHpHz - 1.0e-6 && guard < 100; ++guard)
                    f = config.caps.freqStepHz > 0 ? f + config.caps.freqStepHz
                                                   : CrossoverPlanner::snapFrequency (f * std::pow (2.0, 1.0 / 24.0), config.caps) + (config.caps.freqStepHz == 0 ? 1.0 : 0.0);
                hp.freq = f;
            }
            switch (ch->role)
            {
                case DriverRole::midbass:  keepMin (minMidbassHp, cp.recommendedHpHz); break;
                case DriverRole::midrange: keepMin (minMidrangeHp, cp.recommendedHpHz); break;
                case DriverRole::tweeter:
                    keepMin (minTweeterHp, cp.recommendedHpHz);
                    if (ch->driver.fsHz > 0) keepMin (minTweeterFactor, cp.recommendedHpHz / ch->driver.fsHz);
                    break;
                default: break;
            }
        }
    }

    // Global floors the crossover planner uses, raised (never lowered) to the smallest per-role safe value.
    auto& s = config.safety;
    if (minMidbassHp > 0)     s.minMidbassHpHz     = std::max (s.minMidbassHpHz, minMidbassHp);
    if (minMidrangeHp > 0)    s.minMidrangeHpHz    = std::max (s.minMidrangeHpHz, minMidrangeHp);
    if (minTweeterHp > 0)     s.minTweeterHpHz     = std::max (s.minTweeterHpHz, minTweeterHp);
    if (minTweeterFactor > 0) s.tweeterMinHpFactor = std::max (s.tweeterMinHpFactor, std::min (minTweeterFactor, 4.0));
}
} // namespace fa::install
