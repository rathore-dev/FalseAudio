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

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    testCrossoverSums();
    testSweepMeasurement();
    testArrivalEstimate();
    testAutoEQ();
    testCalibrationParse();
    testCrossoverPlanner();
    testEndToEndVirtualCar();
    testAutoTuneEngine();

    std::cout << "\n" << (failures == 0 ? "ALL TESTS PASSED" : juce::String (failures) + " TEST(S) FAILED") << "\n";
    return failures == 0 ? 0 : 1;
}
