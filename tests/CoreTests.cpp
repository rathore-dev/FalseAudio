// Headless checks of the measurement and tuning maths on synthetic systems.
// Run: .\build.ps1 -Target FalseAudioTests -Run

#include "core/dsp/Filters.h"
#include "core/dsp/Spectrum.h"
#include "core/dsp/Generators.h"
#include "core/measure/Measurement.h"
#include "core/measure/Calibration.h"
#include "core/tune/AutoEQ.h"
#include "core/tune/CrossoverPlanner.h"
#include "core/tune/SystemSolver.h"
#include "core/sim/VirtualCar.h"
#include "core/sim/VirtualRig.h"
#include "core/tune/AutoTuneEngine.h"
#include "core/tune/TargetLibrary.h"
#include "core/measure/Analysis.h"
#include "core/audio/ListeningTracks.h"
#include "TestRegistry.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <iostream>

using namespace fa;

static int failures = 0;

static void check (bool ok, const juce::String& what, const juce::String& detail = {})
{
    std::cout << (ok ? "  [PASS] " : "  [FAIL] ") << what << (detail.isEmpty() ? "" : "  (" + detail + ")") << "\n";
    if (! ok)
        ++failures;
}

/** Runs x through a chain of biquads in the time domain. */
static std::vector<float> filterSignal (std::vector<float> x, const std::vector<FilterSpec>& chain, double fs)
{
    for (auto& spec : chain)
    {
        for (auto& c : spec.toBiquads (fs))
        {
            double z1 = 0, z2 = 0;
            for (auto& s : x)
            {
                const double in = s;
                const double out = c.b0 * in + z1;
                z1 = c.b1 * in - c.a1 * out + z2;
                z2 = c.b2 * in - c.a2 * out;
                s = (float) out;
            }
        }
    }
    return x;
}

static std::vector<float> delaySignal (const std::vector<float>& x, int samples)
{
    std::vector<float> y (x.size() + (size_t) samples, 0.0f);
    std::copy (x.begin(), x.end(), y.begin() + samples);
    return y;
}

static void testCrossoverSums()
{
    std::cout << "Crossover sums\n";
    const double fs = 48000.0;
    const auto& f = analysisFrequencies();
    auto hp = FilterSpec::crossover (FilterType::highPass, 2000.0);
    auto lp = FilterSpec::crossover (FilterType::lowPass, 2000.0);

    auto sum = hp.response (f, fs) + lp.response (f, fs);
    double maxDev = 0.0;
    for (size_t i = 0; i < f.size(); ++i)
        if (f[i] > 50 && f[i] < 15000)
            maxDev = std::max (maxDev, std::abs (sum.magnitudeDb (i)));
    check (maxDev < 0.2, "LR4 sums flat in correct polarity", "max dev " + juce::String (maxDev, 3) + " dB");

    auto inv = hp.response (f, fs);
    inv.scale (-1.0);
    auto diff = inv + lp.response (f, fs);
    const double notch = dbFromMag (std::abs (diff.interpolate (2000.0)));
    check (notch < -30.0, "LR4 inverted shows deep notch at fc", juce::String (notch, 1) + " dB");

    auto atFc = dbFromMag (std::abs (lp.response (2000.0, fs)));
    check (std::abs (atFc + 6.02) < 0.1, "LR4 is -6 dB at fc", juce::String (atFc, 2) + " dB");

    auto bw = FilterSpec::crossover (FilterType::lowPass, 1000.0, 24, Alignment::butterworth);
    auto atFcBw = dbFromMag (std::abs (bw.response (1000.0, fs)));
    check (std::abs (atFcBw + 3.01) < 0.1, "BW4 is -3 dB at fc", juce::String (atFcBw, 2) + " dB");

    auto bessel = FilterSpec::crossover (FilterType::lowPass, 1000.0, 24, Alignment::bessel);
    auto atFcBes = dbFromMag (std::abs (bessel.response (1000.0, fs)));
    check (std::abs (atFcBes + 3.01) < 0.3, "Bessel4 is -3 dB at fc", juce::String (atFcBes, 2) + " dB");
}

static void testSweepMeasurement()
{
    std::cout << "Sweep -> impulse -> response\n";
    const double fs = 48000.0;
    LogSweep sweep;
    sweep.sampleRate = fs;
    sweep.seconds = 2.0;
    sweep.tailSeconds = 0.5;
    const auto stim = sweep.generate();

    // "Speaker": 2.5 ms of flight time and a +6 dB / Q 2 resonance at 500 Hz.
    const std::vector<FilterSpec> system { FilterSpec::peq (500.0, 2.0, 6.0) };
    const int delaySamples = (int) std::round (0.0025 * fs);
    auto rec = delaySignal (filterSignal (stim, system, fs), delaySamples);
    rec.resize (stim.size());

    Measurement m;
    m.sampleRate = fs;
    m.ir = deconvolve (rec, stim, fs, sweep.fStart, sweep.fEnd);
    m.zeroSample = deconvolutionPreRoll (fs);
    m.analyse (nullptr);

    check (std::abs (m.onsetMs - 2.5) < 0.1, "Arrival time recovered", juce::String (m.onsetMs, 3) + " ms");

    const double at500 = dbFromMag (std::abs (m.response.interpolate (500.0)));
    const double at5k  = dbFromMag (std::abs (m.response.interpolate (5000.0)));
    const double expected = dbFromMag (std::abs (system[0].response (500.0, fs)))
                          - dbFromMag (std::abs (system[0].response (5000.0, fs)));
    check (std::abs ((at500 - at5k) - expected) < 0.3, "Resonance level recovered",
           juce::String (at500 - at5k, 2) + " dB re 5 kHz, expected " + juce::String (expected, 2));
}

static void testArrivalEstimate()
{
    std::cout << "Excess-phase arrival & polarity\n";
    const double fs = 48000.0;
    const auto& f = analysisFrequencies();

    for (bool inverted : { false, true })
    {
        // A midbass behind LR4 80 Hz HP / 400 Hz LP, 3.2 ms late, measured with onset-referenced phase
        auto r = FilterSpec::crossover (FilterType::highPass, 80.0).response (f, fs)
                 * FilterSpec::crossover (FilterType::lowPass, 400.0).response (f, fs);
        if (inverted)
            r.scale (-1.0);
        r.applyDelay (0.0032);

        const auto est = estimateArrival (r, 100.0, 300.0);
        check (std::abs (est.delayMs - 3.2) < 0.15 && est.inverted == inverted,
               juce::String ("Delay/polarity of LR4 band-pass ") + (inverted ? "(inverted)" : "(normal)"),
               juce::String (est.delayMs, 3) + " ms, inverted=" + (est.inverted ? "yes" : "no")
                   + ", conf " + juce::String (est.confidence, 2));
    }
}

static void testAutoEQ()
{
    std::cout << "AutoEQ\n";
    const double fs = 48000.0;
    const auto& f = analysisFrequencies();

    // Measured = target + room-ish peaks/dips
    TargetCurve target = TargetCurve::carReference();
    const auto targetDb = target.levels (f);
    auto measured = FrequencyResponse::unity (f);
    for (size_t i = 0; i < f.size(); ++i)
        measured.values[i] = magFromDb (targetDb[i]);
    measured *= chainResponse ({ FilterSpec::peq (120, 3.0, 8.0),
                                 FilterSpec::peq (500, 2.5, 10.0),
                                 FilterSpec::peq (1600, 8.0, 5.0),
                                 FilterSpec::peq (3500, 1.5, -4.0),
                                 FilterSpec::peq (9000, 1.0, 3.0) }, f, fs);

    AutoEQ::Settings s;
    s.sampleRate = fs;
    s.fLow = 30;
    s.fHigh = 16000;
    s.maxBands = 10;
    const auto res = AutoEQ::solve (measured, targetDb, s);

    check (res.rmsErrorAfterDb < 1.0, "AutoEQ brings response near target",
           juce::String (res.rmsErrorBeforeDb, 2) + " dB -> " + juce::String (res.rmsErrorAfterDb, 2) + " dB with "
               + juce::String ((int) res.filters.size()) + " bands");

    bool limitsOk = true;
    for (auto& fl : res.filters)
        limitsOk = limitsOk && fl.gainDb <= s.maxBoostDb + 1e-6 && fl.gainDb >= s.maxCutDb - 1e-6;
    check (limitsOk, "AutoEQ respects boost/cut limits");
}

static void testCalibrationParse()
{
    std::cout << "Calibration files\n";
    const auto umik = MicCalibration::parse ("\"Sens Factor =-1.378dB, SERNO: 7023456\"\n10.054\t-3.2\n100\t0.1\n1000\t0\n20000\t1.5\n", "UMIK");
    check (umik.isValid() && std::abs (umik.sensitivityDb + 1.378) < 1e-6 && umik.serial == "7023456"
               && std::abs (umik.deviationDb (20000) - 1.5) < 1e-6,
           "UMIK-1 format", "sens " + juce::String (umik.sensitivityDb, 3) + ", serial " + umik.serial);

    const auto csv = MicCalibration::parse ("Freq(Hz),dB,Phase\n20,0.5,2\n1000,0,0\n10000,-1,-5\n", "Generic");
    check (csv.isValid() && csv.hasPhase && std::abs (csv.deviationDb (10000) + 1.0) < 1e-6, "CSV with phase");
}

static void testCrossoverPlanner()
{
    std::cout << "Crossover planner\n";
    auto cfg = SystemConfig::makeLayout (3, true);
    auto decisions = CrossoverPlanner::planDefaults (cfg);
    CrossoverPlanner::apply (cfg, decisions);

    auto* mb  = cfg.find ("FL.midbass");
    auto* tw  = cfg.find ("FL.tweeter");
    auto* sub = cfg.find ("SUB");
    check (mb != nullptr && std::abs (mb->settings.highPass.freq - 80.0) < 1.0, "Midbass HP 80 Hz with sub");
    check (sub != nullptr && std::abs (sub->settings.lowPass.freq - 57.0) < 1.5, "Sub LP half an octave below",
           sub != nullptr ? juce::String (sub->settings.lowPass.freq) + " Hz" : juce::String());
    check (tw != nullptr && tw->settings.highPass.freq >= 1800.0, "Tweeter protected",
           tw != nullptr ? juce::String (tw->settings.highPass.freq) + " Hz" : juce::String());

    for (auto& d : decisions)
        for (auto& r : d.reasons)
            std::cout << "      " << d.channelId << ": " << r << "\n";
}

static double sideErrorDb (const Measurement& m, const TargetCurve& target)
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

static void testEndToEndVirtualCar()
{
    std::cout << "End-to-end auto tune on a virtual car (3-way + sub)\n";
    auto cfg = SystemConfig::makeLayout (3, true);
    CrossoverPlanner::apply (cfg, CrossoverPlanner::planDefaults (cfg));

    VirtualCar car (cfg);
    car.driver ("FR.midrange").wiredInverted = true; // installer mistake to catch

    auto playingAll = [&] (Side side)
    {
        std::vector<std::pair<juce::String, ChannelSettings>> v;
        for (auto& c : cfg.channels)
            if (c.side == side || c.side == Side::mono)
                v.emplace_back (c.id, c.settings);
        return v;
    };

    const double before = sideErrorDb (car.measure (playingAll (Side::left), 0), cfg.target);

    // Measure each driver alone at three mic positions with the protective settings.
    DriverDataMap data;
    for (auto& c : cfg.channels)
    {
        auto& dd = data[c.id];
        dd.measuredWith = c.settings;
        for (int p = 0; p < 3; ++p)
            dd.add (car.measure ({ { c.id, c.settings } }, p));
    }

    SystemSolver solver (cfg, data, {});
    const auto report = solver.solveAll();

    for (auto& n : report.notes)    std::cout << "      " << n << "\n";
    for (auto& w : report.warnings) std::cout << "      ! " << w << "\n";

    check (report.wiringInverted.count ("FR.midrange") && report.wiringInverted.at ("FR.midrange"),
           "Detects the reverse-wired midrange");

    bool othersOk = true;
    for (auto& [id, inv] : report.wiringInverted)
        if (id != "FR.midrange" && inv)
            othersOk = false;
    check (othersOk, "No false polarity alarms");

    // Verify by "measuring" the tuned system, exactly as the app does.
    const double afterL = sideErrorDb (car.measure (playingAll (Side::left), 0), cfg.target);
    const double afterR = sideErrorDb (car.measure (playingAll (Side::right), 0), cfg.target);
    check (afterL < 2.0 && afterL < before, "Left side close to target after tuning",
           juce::String (before, 2) + " dB -> " + juce::String (afterL, 2) + " dB");
    check (afterR < 2.5, "Right side close to target after tuning", juce::String (afterR, 2) + " dB");
    check (report.leftRightMatchDb < 1.5, "Left/right tonal match", juce::String (report.leftRightMatchDb, 2) + " dB");

    // Path-length difference between left and right tweeters is 0.5 m -> ~1.46 ms
    const auto* tl = cfg.find ("FL.tweeter");
    const auto* tr = cfg.find ("FR.tweeter");
    const double dt = tl->settings.delayMs - tr->settings.delayMs;
    check (std::abs (dt - 0.5 / speedOfSound * 1000.0) < 0.15, "Tweeter time alignment",
           juce::String (dt, 3) + " ms (expected " + juce::String (0.5 / speedOfSound * 1000.0, 3) + ")");
}

static void testAutoTuneEngine()
{
    std::cout << "AutoTuneEngine one-click run (virtual rig, 2 mics x 2 positions)\n";
    auto cfg = SystemConfig::makeLayout (3, true);
    VirtualRig rig (cfg, 2);
    rig.setSimulatedSweepMs (0);
    rig.getCar().driver ("FL.tweeter").wiredInverted = true;

    AutoTuneEngine engine;
    engine.onMicPositionChanged = [&rig] (int p) { rig.setMicPosition (p); };

    AutoTuneEngine::Options o;
    o.micPositions = 4;
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    engine.start (cfg, rig, rig, o);
    while (engine.isRunning())
        juce::Thread::sleep (20);

    const auto r = engine.getLastResult();
    std::cout << "      took " << juce::String ((juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0, 2)
              << " s, " << (int) r.measurements.size() << " measurements\n";
    for (auto& w : r.report.warnings)
        std::cout << "      ! " << w << "\n";

    check (r.success, "Pipeline completes", r.error);
    check (r.leftErrorDb < 2.0 && r.rightErrorDb < 2.5, "Verified side errors",
           "L " + juce::String (r.leftErrorDb, 2) + " dB, R " + juce::String (r.rightErrorDb, 2) + " dB");
    check (r.report.wiringInverted.count ("FL.tweeter") && r.report.wiringInverted.at ("FL.tweeter"),
           "Finds the reversed tweeter");
    check (r.score >= 70, "Score", juce::String (r.score) + " / 100");
}

//==============================================================================
static void testTargetLibrary()
{
    std::cout << "Target library (embedded house curves)\n";
    const auto& all = TargetLibrary::builtIn();

    int house = 0;
    bool normalised = true, bandsOk = true;
    for (auto& e : all)
    {
        if (e.family == "FalseAudio")
            continue; // parametric presets keep their own definition (harmanLike tilts from 500 Hz)
        normalised = normalised && std::abs (e.curve.levelAt (1000.0)) < 0.05;
        ++house;
        bandsOk = bandsOk && e.bands.size() == 4 && e.band (DriverRole::subwoofer) != nullptr
                  && e.band (DriverRole::midbass) != nullptr && e.band (DriverRole::midrange) != nullptr
                  && e.band (DriverRole::tweeter) != nullptr && e.extraBands.count ("Rear sub") == 1;
        std::cout << "      " << e.curve.name << " (" << (int) e.curve.points.size() << " pts): " << e.description << "\n";
    }
    check (house == 8, "All 8 house curves load", juce::String (house) + " house + " + juce::String ((int) all.size() - house) + " parametric");
    check (all.size() == 11, "Parametric presets included");
    check (normalised, "Every house curve is 0 dB at 1 kHz");
    check (bandsOk, "Per-band targets present (sub, midbass, midrange, tweeter, rear sub)");

    const auto* af = TargetLibrary::findByName ("Audiofrog");
    check (af != nullptr, "findByName Audiofrog");
    if (af != nullptr)
    {
        // Bands keep the family's reference: where a band dominates it equals the full curve.
        const double tw = af->band (DriverRole::tweeter)->levelAt (10000.0) - af->curve.levelAt (10000.0);
        const double mid = af->band (DriverRole::midrange)->levelAt (1000.0) - af->curve.levelAt (1000.0);
        const double sub = af->band (DriverRole::subwoofer)->levelAt (50.0) - af->band (DriverRole::tweeter)->levelAt (50.0);
        check (std::abs (tw) < 1.0 && std::abs (mid) < 1.0 && sub > 60.0, "Band targets share the full curve's reference",
               "tweeter@10k " + juce::String (tw, 2) + " dB, mid@1k " + juce::String (mid, 2) + " dB re full");
        check (af->curve.levelAt (30.0) > 6.0 && af->curve.levelAt (10000.0) < -2.0, "Audiofrog shape (bass rise, treble tilt)");
    }

    // Import round trip through the user folder.
    auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("FA_W7_TestCurve.txt");
    tmp.replaceWithText ("* test\n20 6\n100 4\n1000 2\n10000 -1\n20000 -3\n");
    TargetLibrary::Entry imported;
    juce::String err;
    const bool ok = TargetLibrary::import (tmp, imported, err);
    bool listed = false;
    for (auto& e : TargetLibrary::userCurves())
        listed = listed || e.curve.name == imported.curve.name;
    check (ok && listed && std::abs (imported.curve.levelAt (1000.0)) < 1e-6 && ! imported.builtIn,
           "Import copies the curve to the user folder", ok ? imported.file.getFullPathName() : err);
    check (TargetLibrary::find (imported.curve.name).has_value(), "find() sees user curves");
    TargetLibrary::removeUserCurve (imported.curve.name);
    tmp.deleteFile();
}

static void testBandTargetLevels()
{
    std::cout << "Per-band targets in level matching\n";
    auto cfg = SystemConfig::makeLayout (3, true);
    CrossoverPlanner::apply (cfg, CrossoverPlanner::planDefaults (cfg));
    VirtualCar car (cfg);

    DriverDataMap data;
    for (auto& c : cfg.channels)
    {
        auto& dd = data[c.id];
        dd.measuredWith = c.settings;
        dd.add (car.measure ({ { c.id, c.settings } }, 0));
    }

    auto run = [&] (const std::map<DriverRole, TargetCurve>& bands)
    {
        auto c = cfg;
        SystemSolver::Options o;
        o.bandTargets = bands;
        SystemSolver s (c, data, o);
        s.matchLevels();
        return c.find ("FL.tweeter")->settings.gainDb - c.find ("FL.midrange")->settings.gainDb;
    };

    TargetCurve quieter;
    quieter.name = "quieter";
    for (auto f : makeLogFrequencies (10.0, 24000.0, 48))
        quieter.points.emplace_back (f, cfg.target.levelAt (f) - 2.0);

    const double plain = run ({});
    const double same = run ({ { DriverRole::tweeter, cfg.target } });
    const double lowered = run ({ { DriverRole::tweeter, quieter } });

    check (std::abs (same - plain) < 0.11, "Band target equal to the full curve changes nothing",
           juce::String (plain, 2) + " vs " + juce::String (same, 2) + " dB");
    check (std::abs ((lowered - plain) + 2.0) < 0.25, "Tweeter band target 2 dB lower -> tweeter 2 dB lower",
           juce::String (lowered - plain, 2) + " dB");
}

//==============================================================================
static void testDistortion()
{
    std::cout << "Harmonic distortion (Farina) on a swept, nonlinear virtual driver\n";
    auto cfg = SystemConfig::makeLayout (3, true);
    VirtualCar car (cfg);
    const juce::String id = "FL.midrange";

    LogSweep sweep;
    sweep.sampleRate = 48000.0;
    sweep.fStart = 20.0;
    sweep.fEnd = 8000.0;           // keeps H2/H3 below Nyquist in the simulation (no aliasing)
    sweep.seconds = 2.0;
    sweep.tailSeconds = 0.5;
    sweep.levelDbFS = -6.0;
    const double A = magFromDb (sweep.levelDbFS);
    const double h2 = 0.2, h3 = 0.16;

    ChannelSettings flat;
    const auto linear = car.measure ({ { id, flat } }, 0);
    const auto linSm = smoothFractionalOctave (linear.response, 6.0);
    auto linDb = [&linSm] (double f) { return dbFromMag (std::abs (linSm.interpolate (f))); };

    // Linear driver: harmonics should be at the noise floor.
    {
        const auto m = car.measureSwept ({ { id, flat } }, 0, sweep);
        const auto d = analysis::harmonicDistortion (m, sweep.seconds, sweep.fStart, sweep.fEnd);
        double worst = -200.0;
        for (size_t i = 0; i < d.freqs.size(); ++i)
            if (d.freqs[i] >= 300.0 && d.freqs[i] <= 2000.0)
                worst = std::max (worst, d.harmonicDb[0][i]);
        check (d.valid && worst < -50.0, "Linear driver shows no distortion", "worst H2 " + juce::String (worst, 1) + " dB");
        check (std::abs (m.onsetMs - linear.onsetMs) < 0.05, "Swept simulation matches the direct one (arrival)",
               juce::String (m.onsetMs, 3) + " vs " + juce::String (linear.onsetMs, 3) + " ms");
    }

    car.driver (id).nonlinearH2 = h2;
    car.driver (id).nonlinearH3 = h3;
    const auto m = car.measureSwept ({ { id, flat } }, 0, sweep);
    const auto d = analysis::harmonicDistortion (m, sweep.seconds, sweep.fStart, sweep.fEnd);
    check (d.valid && d.harmonicDb.size() >= 2, "Distortion analysis valid", juce::String ((int) d.harmonicDb.size()) + " harmonics");
    if (! d.valid || d.harmonicDb.size() < 2)
        return;

    const double compression = 1.0 + 0.75 * h3 * A * A;
    const double c2 = dbFromMag (h2 * A / 2.0 / compression);
    const double c3 = dbFromMag (h3 * A * A / 4.0 / compression);

    for (int k : { 2, 3 })
    {
        double err = 0.0, meas = 0.0, expd = 0.0;
        int n = 0;
        for (size_t i = 0; i < d.freqs.size(); ++i)
        {
            const double f = d.freqs[i];
            if (f < 400.0 || f > 2000.0)
                continue;
            const double expected = (k == 2 ? c2 : c3) + linDb (k * f) - linDb (f);
            err += d.harmonicDb[(size_t) k - 2][i] - expected;
            meas += d.harmonicDb[(size_t) k - 2][i];
            expd += expected;
            ++n;
        }
        err /= std::max (1, n);
        check (n > 10 && std::abs (err) < 2.0, "H" + juce::String (k) + " level recovered (400 Hz - 2 kHz)",
               "measured " + juce::String (meas / std::max (1, n), 1) + " dB, expected " + juce::String (expd / std::max (1, n), 1)
                   + " dB, mean error " + juce::String (err, 2) + " dB");
    }

    double thd1k = 0.0;
    for (size_t i = 0; i < d.freqs.size(); ++i)
        if (d.freqs[i] <= 1000.0)
            thd1k = d.thdPercent[i];
    check (thd1k > 1.0 && thd1k < 20.0, "THD reported in percent", juce::String (thd1k, 2) + " % @ 1 kHz");
}

static void testAdvancedAnalysis()
{
    std::cout << "ETC, waterfall, step response, decay times\n";
    auto cfg = SystemConfig::makeLayout (3, true);
    VirtualCar car (cfg);
    const auto m = car.measure ({ { "FL.midrange", ChannelSettings() } }, 0);

    const auto etc = analysis::energyTimeCurve (m);
    const auto etcPeak = (int) (std::max_element (etc.begin(), etc.end()) - etc.begin());
    check (etc.size() == m.ir.size() && std::abs (etcPeak - m.peakSample) < (int) (0.001 * m.sampleRate),
           "ETC peak at the arrival", juce::String (etcPeak) + " vs IR peak " + juce::String (m.peakSample));
    const int preOnset = std::max (0, m.zeroSample - 100);
    check (etc[(size_t) preOnset] < -60.0f, "ETC quiet before the arrival", juce::String (etc[(size_t) preOnset], 1) + " dB");

    const auto wf = analysis::spectralDecay (m, 300.0, 30, 100.0, 10000.0);
    bool decays = wf.levelDb.size() == 30 && ! wf.freqs.empty();
    double first = 0.0, last = 0.0;
    if (decays)
    {
        for (size_t i = 0; i < wf.freqs.size(); ++i)
        {
            first += wf.levelDb.front()[i];
            last += wf.levelDb.back()[i];
        }
        first /= (double) wf.freqs.size();
        last /= (double) wf.freqs.size();
        decays = last < first - 30.0;
    }
    check (decays, "Waterfall decays", "first slice " + juce::String (first, 1) + " dB, last " + juce::String (last, 1) + " dB");

    const auto step = analysis::stepResponse (m);
    float stepMax = 0.0f;
    for (auto s : step)
        stepMax = std::max (stepMax, std::abs (s));
    check (step.size() == m.ir.size() && std::abs (stepMax - 1.0f) < 1e-4f && std::abs (step.back()) < 0.1f,
           "Step response normalised, band-pass settles to zero", "end " + juce::String (step.back(), 3));

    // Synthetic reverberant decay with a known RT60.
    Measurement room;
    room.sampleRate = 48000.0;
    room.zeroSample = deconvolutionPreRoll (room.sampleRate);
    const double rt60 = 0.4;
    juce::Random rng (99);
    room.ir.assign ((size_t) (room.zeroSample + 2.0 * room.sampleRate), 0.0f);
    for (size_t i = (size_t) room.zeroSample; i < room.ir.size(); ++i)
    {
        const double t = (double) (i - (size_t) room.zeroSample) / room.sampleRate;
        room.ir[i] = (float) ((rng.nextDouble() * 2.0 - 1.0) * std::pow (10.0, -3.0 * t / rt60));
    }
    const auto dts = analysis::decayTimes (room);
    bool rtOk = dts.size() >= 6;
    juce::String detail;
    for (auto& [fc, t] : dts)
    {
        // Octave filters' own ringing and few noise cycles inflate the low bands a little.
        rtOk = rtOk && std::abs (t - rt60) < (fc >= 250.0 ? 0.15 : 0.25) * rt60;
        detail << juce::String (fc, 0) << ":" << juce::String (t, 2) << " ";
    }
    check (rtOk, "T20 decay per octave", detail.trim());
}

//==============================================================================
static double rmsDb (const std::vector<float>& x)
{
    double acc = 0.0;
    for (auto v : x)
        acc += (double) v * v;
    return dbFromPower (acc / (double) std::max<size_t> (1, x.size()));
}

static void testListeningTracks()
{
    std::cout << "Listening tracks\n";
    const double fs = 48000.0;

    const auto full = ListeningTracks::make (ListeningTrack::pinkFull, fs, -20.0);
    check (std::abs (rmsDb (full.left) + 20.0) < 0.1 && full.left == full.right, "Mono pink noise at -20 dBFS RMS, identical L/R",
           juce::String (rmsDb (full.left), 2) + " dBFS");

    const auto c = ListeningTracks::make (ListeningTrack::panCentre, fs, -20.0);
    check (std::abs (rmsDb (c.left) - rmsDb (c.right)) < 0.01 && std::abs (rmsDb (c.left) + 23.01) < 0.1,
           "Centre: equal in L and R, -3 dB each (equal power)",
           "L " + juce::String (rmsDb (c.left), 2) + ", R " + juce::String (rmsDb (c.right), 2) + " dBFS");

    const auto l = ListeningTracks::make (ListeningTrack::panLeft, fs, -20.0);
    bool rightSilent = true;
    for (auto v : l.right)
        rightSilent = rightSilent && v == 0.0f;
    check (rightSilent && rmsDb (l.left) > -20.1, "Left: only the left channel plays");

    const auto lc = ListeningTracks::make (ListeningTrack::panLeftCentre, fs, -20.0);
    const double powerSum = std::pow (10.0, rmsDb (lc.left) / 10.0) + std::pow (10.0, rmsDb (lc.right) / 10.0);
    check (rmsDb (lc.left) > rmsDb (lc.right) + 7.0 && std::abs (dbFromPower (powerSum) + 20.0) < 0.1,
           "Left-centre: louder left, constant total power",
           "L-R " + juce::String (rmsDb (lc.left) - rmsDb (lc.right), 2) + " dB");

    for (auto t : { ListeningTrack::pinkBelow200, ListeningTrack::pink200to1k, ListeningTrack::pink1kto4k, ListeningTrack::pinkAbove4k })
    {
        const auto tr = ListeningTracks::make (t, fs, -20.0);
        const auto [lo, hi] = ListeningTracks::bandLimits (t);
        const int n = (int) tr.left.size();
        const auto bins = realFFT (tr.left.data(), n, n);
        double in = 0.0, out = 0.0;
        for (size_t k = 1; k < bins.size(); ++k)
        {
            const double f = (double) k * fs / n;
            (f >= lo * 0.99 && f <= hi * 1.01 ? in : out) += std::norm (bins[k]);
        }
        const double leak = dbFromPower (out / std::max (in, 1e-30));
        check (leak < -60.0, ListeningTracks::name (t) + " is band-limited", "out-of-band " + juce::String (leak, 1) + " dB");
    }

    const auto sweepTrack = ListeningTracks::make (ListeningTrack::panSweep, fs, -20.0);
    const int seg = (int) (2.0 * fs);
    const std::vector<float> firstL (sweepTrack.left.begin() + seg / 2, sweepTrack.left.begin() + seg);
    const std::vector<float> firstR (sweepTrack.right.begin() + seg / 2, sweepTrack.right.begin() + seg);
    check (sweepTrack.left.size() == (size_t) seg * 8 && rmsDb (firstR) < -100.0 && rmsDb (firstL) > -21.0,
           "Pan sweep starts hard left");

    const auto quiet = ListeningTracks::make (ListeningTrack::noiseCheck, fs);
    check (rmsDb (quiet.left) < -200.0 && rmsDb (quiet.right) < -200.0, "Noise-check track is silent");
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    // FA_TEST_ONLY=<substring>: run only the registered tests whose name contains it (skips the legacy block).
    const auto only = juce::SystemStats::getEnvironmentVariable ("FA_TEST_ONLY", {});
    if (only.isNotEmpty())
    {
        for (auto& t : fa::test::registry())
            if (juce::String (t.name).contains (only)) { std::cout << t.name << std::endl; t.fn(); }
        return fa::test::failureCount() == 0 ? 0 : 1;
    }
    testCrossoverSums();
    testSweepMeasurement();
    testArrivalEstimate();
    testAutoEQ();
    testCalibrationParse();
    testCrossoverPlanner();
    testEndToEndVirtualCar();
    testAutoTuneEngine();
    testTargetLibrary();
    testBandTargetLevels();
    testDistortion();
    testAdvancedAnalysis();
    testListeningTracks();

    failures += fa::test::runRegistered();

    std::cout << "\n" << (failures == 0 ? "ALL TESTS PASSED" : juce::String (failures) + " TEST(S) FAILED") << "\n";
    return failures == 0 ? 0 : 1;
}
