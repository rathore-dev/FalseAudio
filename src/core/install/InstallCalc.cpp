// Pure physics / rule-of-thumb calculations used by the install planner.
// Everything here works from published specs only (no audio).

#include "InstallPlanner.h"
#include "../Types.h"

namespace fa::install::calc
{
namespace
{
    constexpr double copperResistivity = 1.724e-8;   // ohm*m, annealed copper @ 20 degC (IACS 100 %)
    constexpr double ccaFactor         = 1.6;        // CCA (copper-clad aluminium) ~ 1.55..1.65x copper
    constexpr double airDensity        = 1.204;      // kg/m^3 @ 20 degC
    constexpr double pRef              = 20.0e-6;    // Pa, 0 dB SPL

    bool isCca (const juce::String& material) { return material.containsIgnoreCase ("CCA") || material.containsIgnoreCase ("alu"); }

    /** Standard car-audio gauges from thin to thick (0 = 1/0, -1 = 2/0, -2 = 3/0, -3 = 4/0). */
    const double standardGauges[] = { 18, 16, 14, 12, 10, 8, 6, 4, 2, 1, 0, -1, -2, -3 };

    /** Driver model reduced to: peak cone acceleration in the mass-controlled band (F/Mms), ws and Qts.
        Cone displacement for voltage drive (electrical damping folded into Qts) is
            X(w) = (F/Mms) / | ws^2 - w^2 + j w ws / Qts |
        which tends to F/Kms below resonance and to F/(Mms w^2) above it. */
    struct ExcursionModel
    {
        double accelPeak = 0, ws = 0, qts = 0;
        bool valid = false;

        double peakMetres (double f) const
        {
            const double w = 2.0 * pi * f;
            const double re = ws * ws - w * w, im = w * ws / qts;
            return accelPeak / std::sqrt (re * re + im * im);
        }
    };

    ExcursionModel makeModel (const gear::DriverSpecs& d, double powerW)
    {
        ExcursionModel m;
        // T/S measured on ONE coil of a DVC ("single"): with both coils driven Qes halves (Bl^2 / Re doubles).
        const bool oneCoilTs = d.voiceCoils > 1 && d.tsWiring.equalsIgnoreCase ("single");
        const double qes = d.qes > 0 ? (oneCoilTs ? 0.5 * d.qes : d.qes) : 0.0;
        double qts = d.qts > 0 ? d.qts : (qes > 0 && d.qms > 0 ? qes * d.qms / (qes + d.qms) : 0.0);
        if (oneCoilTs && qes > 0 && d.qms > 0)
            qts = qes * d.qms / (qes + d.qms);
        if (d.fsHz <= 0 || qts <= 0 || powerW <= 0)
            return m;

        // Power ratings refer to the nominal impedance: V = sqrt(P * Znom). Xmax is a peak value,
        // so the sine's peak voltage (sqrt 2 * Vrms) is used.
        // DVC: reOhms is ONE coil's DC resistance (schema), so work per coil: Zc = coilOhms. In the car both coils
        // are always driven (series or parallel); either way the force per sqrt(watt) is sqrt 2 x one coil's.
        const bool dvc   = d.voiceCoils > 1;
        const double zNom = dvc && d.coilOhms > 0 ? d.coilOhms
                          : (d.nominalOhms > 0 ? d.nominalOhms : (d.coilOhms > 0 ? d.coilOhms : 4.0));
        const double re   = d.reOhms > 0 ? d.reOhms : 0.85 * zNom;   // typical Re/Znom when Re is unpublished
        // Re can never exceed the nominal impedance (Re ~ 0.7..0.95 Znom). If it does, Re/Bl were published for
        // a different coil configuration than the nominal impedance (e.g. DVC: nominal per coil, Re for coils in
        // series). Re-derive the nominal for the T/S configuration so the drive voltage is not under-estimated.
        const double zDrive = std::max (zNom, re / 0.85);
        const double sd   = d.sdCm2 * 1.0e-4;
        const double ws   = 2.0 * pi * d.fsHz;

        const double vRms = std::sqrt (powerW * zDrive);
        const double vPk  = std::sqrt (2.0) * vRms;

        double accel = 0;
        if (d.bl > 0 && d.mmsGrams > 0)
        {
            // 1) Motor: F = Bl * V / Re  ->  a = Bl V / (Re Mms)
            if (dvc)
            {
                // One coil's Bl: published Bl is for coils in series (2 x Blc), in parallel / one coil (Blc).
                // Unknown wiring -> assume Bl is one coil's (the larger, conservative force estimate).
                const double blc = d.tsWiring.equalsIgnoreCase ("series") ? 0.5 * d.bl : d.bl;
                accel = std::sqrt ((double) d.voiceCoils) * blc * vPk / (re * d.mmsGrams * 1.0e-3);
            }
            else
                accel = d.bl * vPk / (re * d.mmsGrams * 1.0e-3);
        }
        else if (sd > 0 && d.sensitivityDb > 0)
        {
            // 2) Sensitivity (half-space, 1 m): a piston radiating into 2*pi gives p = rho * Sd * a / (2 pi r)
            //    in its mass-controlled band, so a = 2 pi r p / (rho Sd).
            const double vRef = d.sensitivityRef.contains ("2.83") ? 2.83 : std::sqrt (zNom);  // "1W/1m" -> 1 W into Znom
            const double spl  = d.sensitivityDb + 20.0 * std::log10 (vRms / vRef);
            const double pRms = pRef * std::pow (10.0, spl / 20.0);
            accel = std::sqrt (2.0) * 2.0 * pi * 1.0 * pRms / (airDensity * sd);
        }
        else if (sd > 0 && d.vasLitres > 0 && qes > 0)
        {
            // 3) From Vas & Qes: Kms = rho c^2 Sd^2 / Vas, Mms = Kms / ws^2, Bl^2 = ws Mms Re / Qes.
            const double kms = airDensity * speedOfSound * speedOfSound * sd * sd / (d.vasLitres * 1.0e-3);
            const double mms = kms / (ws * ws);
            const double bl  = std::sqrt (ws * mms * re / qes);
            accel = bl * vPk / (re * mms);
        }

        if (accel <= 0)
            return m;
        m.accelPeak = accel; m.ws = ws; m.qts = qts; m.valid = true;
        return m;
    }

    /** |H| of an even-order Linkwitz-Riley high pass (LR4: 24 dB/oct): x^2n / (1 + x^2n), x = f/fc. */
    double lrHighPassMag (double f, double fc, int order = 4)
    {
        const double x = std::pow (f / fc, (double) order);
        return x / (1.0 + x);
    }
}

//==============================================================================
double awgDiameterMm (double awg)
{
    // ANSI/ASTM B258: d = 0.127 mm * 92^((36 - n) / 39); 1/0 = 0, 2/0 = -1, ...
    return 0.127 * std::pow (92.0, (36.0 - awg) / 39.0);
}

double mm2FromAwg (double awg)
{
    const double d = awgDiameterMm (awg);
    return pi * d * d / 4.0;
}

double awgFromMm2 (double mm2)
{
    if (mm2 <= 0)
        return 0;
    const double d = std::sqrt (4.0 * mm2 / pi);
    return 36.0 - 39.0 * std::log (d / 0.127) / std::log (92.0);
}

double copperResistancePerMetre (double awg, const juce::String& material)
{
    // R/m = rho / A, solid-equivalent copper area (stranded wire of the same gauge has ~ the same copper area).
    const double r = copperResistivity / (mm2FromAwg (awg) * 1.0e-6);
    return isCca (material) ? r * ccaFactor : r;
}

double wireAmpacityA (double awg, const juce::String& material)
{
    // Conservative car-audio fuse guideline for OFC power wire (bundled, in-cabin runs, 105 degC insulation).
    // The fuse at the battery must not exceed this: the fuse protects the WIRE.
    struct Row { double awg, amps; };
    static const Row rows[] = { { 18, 10 }, { 16, 15 }, { 14, 20 }, { 12, 30 }, { 10, 50 }, { 8, 80 },
                                { 6, 100 }, { 4, 150 }, { 2, 200 }, { 1, 225 }, { 0, 250 }, { -1, 300 },
                                { -2, 350 }, { -3, 400 } };
    // Thinner than 18 AWG: scale the 18 AWG rating by copper area (never more than 18 AWG's rating).
    double amps = awg > rows[0].awg ? rows[0].amps * mm2FromAwg (awg) / mm2FromAwg (rows[0].awg) : rows[0].amps;
    for (auto& r : rows)
        if (awg <= r.awg + 1.0e-9)
            amps = r.amps;   // thicker (smaller number) -> larger rating
    return isCca (material) ? amps * 0.7 : amps;   // CCA: higher resistance -> more heat for the same current
}

double standardFuseA (double amps)
{
    static const double sizes[] = { 1, 2, 3, 5, 7.5, 10, 15, 20, 25, 30, 35, 40, 50, 60, 70, 80, 100, 125, 150,
                                    175, 200, 225, 250, 300, 350, 400, 500 };
    for (auto s : sizes)
        if (s >= amps - 1.0e-9)
            return s;
    return sizes[std::size (sizes) - 1];
}

double recommendedAwg (double currentA, double roundTripM, const juce::String& material, double maxDropV)
{
    for (auto g : standardGauges)
    {
        const double drop = currentA * copperResistancePerMetre (g, material) * roundTripM;
        // The fuse actually installed is 1.25 x I rounded UP to a standard size, and that fuse must not exceed the
        // wire's rating (the fuse protects the wire) - so compare the rounded fuse, not 1.25 x I.
        if (drop <= maxDropV && wireAmpacityA (g, material) >= standardFuseA (1.25 * currentA) - 1.0e-9)
            return g;
    }
    return standardGauges[std::size (standardGauges) - 1];
}

double ampCurrentDraw (double totalRmsW, double efficiencyPercent, double supplyV)
{
    // Input power = output / efficiency; I = P_in / V_supply (continuous sine at full rated output = worst case).
    if (supplyV <= 0)
        return 0;
    const double eff = juce::jlimit (0.2, 0.98, (efficiencyPercent > 0 ? efficiencyPercent : 60.0) / 100.0);
    return totalRmsW / eff / supplyV;
}

double ampSupplyCurrent (double outputW, double efficiencyPercent, double supplyV, double wireOhms,
                         double* voltageAtAmp, bool* collapsed)
{
    // The amp draws constant input power Pin = Pout / eff, so I = Pin / (Vs - I R)  ->  R I^2 - Vs I + Pin = 0.
    // Physical (low-current) root: I = (Vs - sqrt(Vs^2 - 4 R Pin)) / (2 R). No real root means the wire cannot
    // deliver Pin at all (the supply would collapse below Vs / 2): report the maximum-transfer point instead.
    if (collapsed != nullptr) *collapsed = false;
    if (supplyV <= 0)
    {
        if (voltageAtAmp != nullptr) *voltageAtAmp = 0;
        return 0;
    }
    const double pin = ampCurrentDraw (outputW, efficiencyPercent, 1.0);   // W (I at 1 V == watts)
    double i = 0;
    if (wireOhms <= 1.0e-12)
        i = pin / supplyV;
    else
    {
        const double disc = supplyV * supplyV - 4.0 * wireOhms * pin;
        if (disc >= 0)
            i = (supplyV - std::sqrt (disc)) / (2.0 * wireOhms);
        else
        {
            i = supplyV / (2.0 * wireOhms);
            if (collapsed != nullptr) *collapsed = true;
        }
    }
    if (voltageAtAmp != nullptr) *voltageAtAmp = supplyV - i * wireOhms;
    return i;
}

double defaultEfficiencyPercent (const gear::AmpSpecs& amp)
{
    if (amp.efficiencyPercent > 0)
        return amp.efficiencyPercent;
    const auto c = amp.amplifierClass.toUpperCase();
    if (c.contains ("AB"))                    return 55.0;
    if (c.contains ("G") || c.contains ("H")) return 65.0;
    if (c.contains ("D"))                     return 80.0;
    return 60.0;   // unknown: assume the less efficient case so the wire and fuse come out safe
}

double voltageForPower (double watts, double ohms)
{
    // P = V^2 / R  ->  V = sqrt(P * R)  (RMS)
    return watts > 0 && ohms > 0 ? std::sqrt (watts * ohms) : 0.0;
}

double gainKnobVoltage (double sourceV, double ampRatedW, double targetW)
{
    // A sensitivity knob marked "x V" means x V in -> rated power out. We want sourceV -> targetW, i.e. the
    // amp's voltage gain G = sqrt(targetW R) / sourceV, so the knob reads sqrt(ratedW R) / G
    //   = sourceV * sqrt(ratedW / targetW).
    if (sourceV <= 0 || ampRatedW <= 0 || targetW <= 0)
        return sourceV;
    return sourceV * std::sqrt (ampRatedW / targetW);
}

int maxCleanVolumeStep (const gear::SourceSpecs& s)
{
    if (s.cleanVolumeStep > 0)
        return s.cleanVolumeStep;
    if (s.maxVolumeStep > 0)
        return (int) std::round (0.8 * s.maxVolumeStep);   // most head units clip in the top 15..25 % of the scale
    return 0;
}

double ampPowerAtLoad (const gear::AmpSpecs& amp, double loadOhms, bool bridged, juce::String* note)
{
    if (loadOhms <= 0)
        return 0;

    std::vector<std::pair<double, double>> pts;   // (ohms, watts)
    for (auto& [key, watts] : amp.rmsPerChannelW)
    {
        auto k = key.toLowerCase().removeCharacters (" _-");
        const bool isBridged = k.startsWith ("bridged") || k.startsWith ("bridge");
        if (isBridged != bridged)
            continue;
        if (isBridged)
            k = k.replace ("bridged", "").replace ("bridge", "");
        const double ohms = k.upToFirstOccurrenceOf ("ohm", false, true).getDoubleValue();
        if (ohms > 0 && watts > 0)
            pts.push_back ({ ohms, watts });
    }

    if (pts.empty() && bridged)
    {
        // A bridged pair driving R puts R/2 on each channel's half: P_bridged(R) ~ 2 x P_channel(R/2).
        const double p = 2.0 * ampPowerAtLoad (amp, loadOhms / 2.0, false);
        if (note != nullptr && p > 0)
            *note = "bridged power estimated as 2 x the per-channel rating at half the load";
        return p;
    }
    if (pts.empty())
        return 0;

    std::sort (pts.begin(), pts.end());
    for (auto& p : pts)
        if (std::abs (p.first - loadOhms) / loadOhms < 0.02)
            return p.second;

    if (loadOhms > pts.back().first)
    {
        // Above the highest rated load the amp is voltage limited: P = V^2 / R.
        if (note != nullptr)
            *note = "extrapolated (constant output voltage) above the highest rated load";
        return pts.back().second * pts.back().first / loadOhms;
    }
    if (loadOhms < pts.front().first)
    {
        if (note != nullptr)
            *note = "below the lowest rated load - using that rating (no more power is assumed)";
        return pts.front().second;
    }
    for (size_t i = 1; i < pts.size(); ++i)
    {
        if (loadOhms < pts[i].first)
        {
            auto& a = pts[i - 1];
            auto& b = pts[i];
            const double t = std::log (loadOhms / a.first) / std::log (b.first / a.first);
            if (note != nullptr)
                *note = "interpolated between the " + juce::String (a.first, 1) + " and " + juce::String (b.first, 1) + " ohm ratings";
            return std::exp (std::log (a.second) + t * (std::log (b.second) - std::log (a.second)));
        }
    }
    return pts.back().second;
}

double combineLoad (double coilOhms, int coilsPerDriver, int drivers, const juce::String& wiring)
{
    // Series adds resistances, n equal resistances in parallel give R / n.
    const int coils = std::max (1, coilsPerDriver);
    const int n     = std::max (1, drivers);
    const auto w    = wiring.trim().toLowerCase();

    bool coilsSeries = true, driversSeries = true;   // "single"/unknown: highest impedance (safest for the amp)
    if (w == "parallel")          { coilsSeries = false; driversSeries = false; }
    else if (w == "series")       { coilsSeries = true;  driversSeries = true; }
    else if (w == "dvc-series")   { coilsSeries = true;  driversSeries = false; }
    else if (w == "dvc-parallel") { coilsSeries = false; driversSeries = true; }

    const double perDriver = coilsSeries ? coilOhms * coils : coilOhms / coils;
    return driversSeries ? perDriver * n : perDriver / n;
}

juce::String wiringDiagram (int coilsPerDriver, int drivers, const juce::String& wiring, double coilOhms)
{
    const int coils = std::max (1, coilsPerDriver);
    const int n     = std::max (1, drivers);
    const auto w    = wiring.trim().toLowerCase();
    const bool coilsSeries   = ! (w == "parallel" || w == "dvc-parallel");
    const bool driversSeries = ! (w == "parallel" || w == "dvc-series");
    const double perDriver   = coilsSeries ? coilOhms * coils : coilOhms / coils;
    const double total       = combineLoad (coilOhms, coils, n, wiring);

    juce::String s;
    if (coils > 1)
    {
        if (coilsSeries)
            s << "On each driver connect coil 1 (-) to coil 2 (+)" << (coils > 2 ? ", and so on through every coil" : "")
              << " (coils in SERIES: " << juce::String (coilOhms, 2) << " x " << coils << " = " << juce::String (perDriver, 2)
              << " ohm). Coil 1 (+) is the driver's +, the last coil (-) is the driver's -.\n";
        else
            s << "On each driver join all coil (+) terminals together and all coil (-) terminals together (coils in PARALLEL: "
              << juce::String (coilOhms, 2) << " / " << coils << " = " << juce::String (perDriver, 2) << " ohm).\n";
    }

    if (n == 1)
        s << "Amp (+) -> driver (+), amp (-) -> driver (-).";
    else if (driversSeries)
    {
        s << "Drivers in SERIES: amp (+) -> driver 1 (+)";
        for (int i = 1; i < n; ++i)
            s << ", driver " << i << " (-) -> driver " << (i + 1) << " (+)";
        s << ", driver " << n << " (-) -> amp (-).";
    }
    else
        s << "Drivers in PARALLEL: amp (+) -> every driver's (+), amp (-) -> every driver's (-).";

    s << "\nFinal load at the amp: " << juce::String (total, 2) << " ohm. Check with a DMM across the amp end of the wires "
      << "(expect roughly 0.8-0.9 x this, the DC resistance Re) before connecting.";
    return s;
}

//==============================================================================
double excursionMm (const gear::DriverSpecs& d, double powerW, double freqHz)
{
    const auto m = makeModel (d, powerW);
    return m.valid && freqHz > 0 ? m.peakMetres (freqHz) * 1000.0 : 0.0;
}

double excursionLimitedHighPass (const gear::DriverSpecs& d, double powerW)
{
    // Assumptions (documented for the user):
    //  - infinite baffle / closed box with the box stiffness ignored (conservative for sealed boxes; NOT valid
    //    below the tuning of a ported box, where excursion rises much faster);
    //  - continuous sine at the full applied power (music has 6..12 dB crest factor, so this is a worst case);
    //  - the DSP high pass is a 24 dB/oct Linkwitz-Riley (the process document's slope).
    // Result: the lowest LR4 corner frequency for which the filtered peak excursion stays <= Xmax at every
    // frequency, i.e. the frequency below which Xmax would be exceeded. 0 = never exceeded / unknown data.
    if (d.xmaxMm <= 0)
        return 0.0;
    const auto m = makeModel (d, powerW);
    if (! m.valid)
        return 0.0;

    const double xmax = d.xmaxMm * 1.0e-3;
    constexpr int perOct = 48;
    std::vector<double> freqs, x;
    for (double f = 5.0; f <= 20000.0; f *= std::pow (2.0, 1.0 / perOct))
    {
        freqs.push_back (f);
        x.push_back (m.peakMetres (f));
    }

    if (*std::max_element (x.begin(), x.end()) <= xmax)
        return 0.0;

    for (double fc : freqs)
    {
        double worst = 0;
        for (size_t i = 0; i < freqs.size(); ++i)
            worst = std::max (worst, x[i] * lrHighPassMag (freqs[i], fc));
        if (worst <= xmax)
            return fc;
    }
    return 20000.0;
}
} // namespace fa::install::calc
