#pragma once

#include "../gear/Gear.h"
#include "../tune/SystemConfig.h"
#include <functional>

namespace fa::install
{
/** The physical system the user selected: products + how they're wired. */
struct Installation
{
    juce::String headUnitId, dspId, dacId, audioInterfaceId;
    std::vector<juce::String> micIds;

    struct AmpUse
    {
        juce::String ampId;
        std::vector<juce::String> channelIds;   // OutputChannel ids fed by this amp, in amp-channel order
        bool bridged = false;
        double powerWireLengthM = 5.0;
        juce::String powerWireId;               // product id or empty (then gauge below)
        double powerWireAwg = 4.0;
        juce::String powerWireMaterial = "OFC";
        double groundWireLengthM = 0.5;
    };
    std::vector<AmpUse> amps;

    struct DriverUse
    {
        juce::String channelId;                 // OutputChannel id
        juce::String productId;
        int count = 1;                           // e.g. 2 subs on one channel
        juce::String wiring;                     // "single" | "series" | "parallel" | "dvc-series" | "dvc-parallel"
        double speakerWireLengthM = 4.0;
        double speakerWireAwg = 14.0;
    };
    std::vector<DriverUse> drivers;

    double batteryVoltageEngineOn = 14.2;       // alternator voltage
    double alternatorA = 0, batteryAh = 0;

    /** Acceptable amp-power / driver-RMS ratio (1.0 .. 1.25) before the plan warns that the amp is
        larger than the driver. Gains are ALWAYS set so a driver never gets more than its RMS rating. */
    double ampToDriverPowerHeadroom = 1.25;

    juce::var toVar() const;
    static Installation fromVar (const juce::var& v);
};

/** One manual action the user must do, with the exact value and why. */
struct Instruction
{
    enum class Severity { info, recommended, important, danger };
    juce::String area;            // "Head unit", "Amp 1 (JL XD600/6)", "Wiring", "DSP input", "Mic", ...
    juce::String title;           // "Set gain to 2.1 V"
    juce::String detail;          // how to do it, step by step
    juce::String why;             // the calculation in plain words
    Severity severity = Severity::recommended;
    double value = 0;             // the key number (V, dB, A, AWG, step...)
    juce::String unit;
};

struct WireCheck
{
    juce::String what;            // "Amp 1 power wire"
    double currentA = 0, voltageDropV = 0, voltageAtLoadV = 0, powerLossW = 0;
    double recommendedAwg = 0;
    bool ok = true;
};

struct ChannelPlan
{
    juce::String channelId;
    double loadOhms = 0;                  // final impedance seen by the amp channel
    double ampPowerAtLoadW = 0;
    double driverRmsW = 0;
    double targetVoltageRms = 0;          // for gain setting with a DMM: V = sqrt(P*R)
    double powerRatio = 0;                // amp power / driver rms
    double gainSettingV = 0;              // input sensitivity to set on the amp
    double excursionLimitedHpHz = 0;      // HP needed so Xmax isn't exceeded at full power
    double recommendedHpHz = 0, recommendedLpHz = 0;
    juce::StringArray warnings;
};

struct Plan
{
    std::vector<Instruction> instructions;    // ordered: wiring -> source -> DSP -> amps -> mic
    std::vector<WireCheck> wires;
    std::vector<ChannelPlan> channels;
    double totalCurrentA = 0, recommendedMainFuseA = 0;
    double headUnitMaxCleanVolume = 0;        // step
    double sourceVoltageV = 0;                // signal level reaching the amps
    juce::StringArray warnings;
};

/** Pure calculations from specs (no audio). */
namespace calc
{
    double copperResistancePerMetre (double awg, const juce::String& material);  // ohm/m
    double mm2FromAwg (double awg);
    double awgFromMm2 (double mm2);
    double ampCurrentDraw (double totalRmsW, double efficiencyPercent, double supplyV);
    double voltageForPower (double watts, double ohms);
    double combineLoad (double coilOhms, int coilsPerDriver, int drivers, const juce::String& wiring);
    /** HP frequency below which the driver would exceed Xmax at the given power (sealed/infinite baffle approx). */
    double excursionLimitedHighPass (const gear::DriverSpecs& d, double powerW);

    // ---- additions (see InstallPlanner.cpp / InstallCalc.cpp for the formulas) ----
    double awgDiameterMm (double awg);                                  // ANSI: 0.127 mm * 92^((36-awg)/39)
    double wireAmpacityA (double awg, const juce::String& material);    // conservative in-car fuse guideline
    double standardFuseA (double amps);                                 // next standard fuse size >= amps
    double recommendedAwg (double currentA, double roundTripM, const juce::String& material,
                           double maxDropV = 0.5);                      // thinnest gauge meeting drop + ampacity
    double defaultEfficiencyPercent (const gear::AmpSpecs& amp);        // spec, else D 80 / AB 55 / G-H 65 / ? 60
    /** Rated RMS power per channel (or per bridged pair) at a load; log-log interpolation between rated
        points, constant-voltage extrapolation above, never more than the lowest-load rating below. */
    double ampPowerAtLoad (const gear::AmpSpecs& amp, double loadOhms, bool bridged, juce::String* note = nullptr);
    /** Amp input-sensitivity knob setting (V for full rated power) so a source at sourceV produces targetW. */
    double gainKnobVoltage (double sourceV, double ampRatedW, double targetW);
    /** Last clean volume step: cleanVolumeStep if published, else round(0.8 * maxVolumeStep). */
    int maxCleanVolumeStep (const gear::SourceSpecs& s);
    /** Peak cone excursion (mm) at a frequency with powerW applied, no filter (infinite baffle / closed box,
        constant-voltage drive). Returns 0 when the T/S data is insufficient. */
    double excursionMm (const gear::DriverSpecs& d, double powerW, double freqHz);
    /** Load seen by the amp and how to wire it: "series" | "parallel" | "dvc-series" (coils in series inside
        each driver, drivers in parallel) | "dvc-parallel" (coils in parallel, drivers in series) | "single". */
    juce::String wiringDiagram (int coilsPerDriver, int drivers, const juce::String& wiring, double coilOhms);
}

/** Product lookup by id (lets callers/tests plan without the global GearDatabase). */
using ProductLookup = std::function<const gear::Product* (const juce::String& id)>;

Plan buildPlan (const Installation& inst, const SystemConfig& config, const ProductLookup& lookup);
void applyToConfig (const Plan& plan, const Installation& inst, const ProductLookup& lookup, SystemConfig& config);

/** Builds the full plan from the installation + product specs + current tune config. */
Plan buildPlan (const Installation& inst, const SystemConfig& config, const gear::GearDatabase& db);

/** Applies the plan's safe crossover limits and driver info (Fs, size, power) to the config. */
void applyToConfig (const Plan& plan, const Installation& inst, const gear::GearDatabase& db, SystemConfig& config);
} // namespace fa::install
