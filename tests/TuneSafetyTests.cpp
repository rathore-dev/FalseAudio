// Found by the first real-hardware Auto Tune (laptop speakers): stacked EQ boosts and an implausible L/R arrival spread.
#include "TestRegistry.h"
#include "core/tune/AutoTuneEngine.h"

using namespace fa;
using fa::test::check;

namespace
{
double netDb (const std::vector<FilterSpec>& peq, double f, double fs)
{
    double db = 0.0;
    for (auto& b : peq)
        if (b.enabled)
            db += 20.0 * std::log10 (std::abs (b.response (f, fs)));
    return db;
}

Measurement impulseMeasurement (double arrivalMs)
{
    Measurement m;
    m.sampleRate = 48000.0;
    m.zeroSample = 4800;
    m.ir.assign (48000, 0.0f);
    m.ir[(size_t) (m.zeroSample + std::lround (arrivalMs * 48.0))] = 1.0f;
    m.analyse (nullptr);
    return m;
}
}

FA_TEST (tuneLimitsTotalStackedBoost)
{
    auto cfg = SystemConfig::makePreset (SystemConfig::Preset::fullRangePair);
    auto ch = cfg.channels.front();
    ch.settings.highPass = FilterSpec::crossover (FilterType::highPass, 188.0, 24);
    // three overlapping +3 dB bands near the HP (each inside the per-band limit, +9 dB net) and a stack mid-band
    ch.settings.peq = { FilterSpec::peq (219.0, 0.75, 3.0), FilterSpec::peq (219.8, 0.74, 3.0), FilterSpec::peq (221.4, 0.83, 2.9),
                        FilterSpec::peq (2152.7, 2.9, 1.8), FilterSpec::peq (2152.7, 2.9, 1.9), FilterSpec::peq (2160.5, 2.9, 3.6),
                        FilterSpec::peq (4000.0, 2.0, -8.0) };
    const double fs = cfg.caps.sampleRate;
    double worstLow = -99.0, worstMid = -99.0;
    for (double f = 188.0; f < 376.0; f *= 1.01) worstLow = std::max (worstLow, netDb (ch.settings.peq, f, fs));
    check (worstLow > cfg.safety.maxLowFreqBoostDb + 2.0, "setup: the stacked bands exceed the low-frequency limit", juce::String (worstLow, 1));

    juce::StringArray notes;
    check (AutoTuneEngine::limitTotalBoost (ch, cfg, &notes), "limiter changed something");
    worstLow = -99.0; worstMid = -99.0;
    for (double f = 20.0; f < 376.0; f *= 1.01) worstLow = std::max (worstLow, netDb (ch.settings.peq, f, fs));
    for (double f = 376.0; f < 20000.0; f *= 1.01) worstMid = std::max (worstMid, netDb (ch.settings.peq, f, fs));
    check (worstLow <= cfg.safety.maxLowFreqBoostDb + 0.1, "net boost near the HP <= low-frequency limit", juce::String (worstLow, 2) + " dB");
    check (worstMid <= cfg.safety.maxBoostDb + 0.1, "net boost elsewhere <= maxBoostDb", juce::String (worstMid, 2) + " dB");
    check (ch.settings.peq.size() < 7, "near-identical bands merged", juce::String ((int) ch.settings.peq.size()));
    bool cutKept = false;
    for (auto& b : ch.settings.peq) cutKept = cutKept || (b.gainDb < -7.5 && std::abs (b.freq - 4000.0) < 1.0);
    check (cutKept, "cuts are left alone");
    check (notes.size() > 0, "a note explains it");

    // already within limits: untouched
    auto ok = cfg.channels.back();
    ok.settings.peq = { FilterSpec::peq (1000.0, 2.0, 3.0), FilterSpec::peq (3000.0, 2.0, -4.0) };
    const auto before = ok.settings.peq;
    check (! AutoTuneEngine::limitTotalBoost (ok, cfg), "compliant EQ is not modified");
    check (ok.settings.peq.size() == before.size() && ok.settings.peq[0].gainDb == before[0].gainDb, "gains unchanged");
}

FA_TEST (solverRejectsImplausibleLeftRightArrivalSpread)
{
    auto run = [] (double leftMs, double rightMs, SystemConfig& cfg, SystemSolver::Report& rep)
    {
        cfg = SystemConfig::makePreset (SystemConfig::Preset::fullRangePair);
        DriverDataMap data;
        data["FL.fullrange"].add (impulseMeasurement (leftMs));
        data["FR.fullrange"].add (impulseMeasurement (rightMs));
        SystemSolver solver (cfg, data, {});
        solver.alignTime();
        rep = solver.getReport();
    };
    SystemConfig cfg; SystemSolver::Report rep;

    run (59.0, 59.3, cfg, rep);                       // laptop-like: 0.3 ms apart
    check (std::abs (cfg.find ("FL.fullrange")->settings.delayMs - 0.3) < 0.1 && cfg.find ("FR.fullrange")->settings.delayMs == 0.0,
           "plausible spread is aligned", juce::String (cfg.find ("FL.fullrange")->settings.delayMs, 2) + " ms");
    check (rep.warnings.isEmpty(), "no warning for a plausible spread");

    run (42.8, 60.7, cfg, rep);                       // one sweep was bad: 18 ms apart
    check (cfg.find ("FL.fullrange")->settings.delayMs < 0.05 && cfg.find ("FR.fullrange")->settings.delayMs < 0.05,
           "implausible spread: no huge delay written", juce::String (cfg.find ("FL.fullrange")->settings.delayMs, 2) + " / "
           + juce::String (cfg.find ("FR.fullrange")->settings.delayMs, 2));
    check (rep.warnings.size() == 1 && rep.warnings[0].contains ("not physically plausible"), "it is reported as a warning");
}
