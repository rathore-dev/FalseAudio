// Guarded DSP decorator, safety rules, QA-hook default, and the gear data validator.

#include "TestRegistry.h"
#include "core/bridge/GuardedDspController.h"
#include "core/bridge/SafetyRules.h"
#include "core/gear/GearValidator.h"

using namespace fa;
using fa::test::check;

namespace
{
/** Records every write and every mute call. */
struct Recorder : DspController
{
    std::vector<OutputChannel> writes;
    std::vector<std::pair<int, bool>> mutes;
    bool automatic = true;

    juce::String getName() const override { return "rec"; }
    bool isAutomatic() const override { return automatic; }
    DspCapabilities getCapabilities() const override { return {}; }
    bool connect (juce::String&) override { return true; }
    bool isConnected() const override { return true; }
    bool applyChannel (const OutputChannel& c, juce::String&) override { writes.push_back (c); return true; }
    bool setMuted (const OutputChannel& c, bool m, juce::String&) override { mutes.push_back ({ c.dspOutput, m }); return true; }
};

OutputChannel tweeterCh (double fs, double hp, double gain)
{
    OutputChannel c;
    c.id = "FL.tweeter"; c.label = "FL tweeter"; c.role = DriverRole::tweeter; c.kind = ChannelKind::activeDriver;
    c.dspOutput = 0; c.driver.fsHz = fs;
    c.settings.highPass = FilterSpec::crossover (FilterType::highPass, hp);
    c.settings.gainDb = gain;
    return c;
}
} // namespace

FA_TEST (safetyMinTweeterHp)
{
    check (safety::minTweeterHpHz (400.0) == 1200.0, "Tweeter floor: Fs 400 -> 1200 Hz");
    check (safety::minTweeterHpHz (800.0) == 1600.0, "Tweeter floor: Fs 800 -> 2 x Fs = 1600 Hz");
    check (safety::minTweeterHpHz (0.0) >= 1200.0, "Tweeter floor: unknown Fs is at least 1.2 kHz");
}

FA_TEST (guardedDspController)
{
    {
        Recorder rec;
        GuardedDspController g (rec);
        juce::String err;
        check (g.applyChannel (tweeterCh (500.0, 800.0, 4.0), err), "guard: write passes through");
        const auto& w = rec.writes.back();
        check (w.settings.highPass.freq >= 1200.0, "guard: tweeter HP raised to the protected floor", juce::String (w.settings.highPass.freq, 0));
        check (w.settings.gainDb <= 0.0, "guard: gain above 0 dB clamped to 0 dB", juce::String (w.settings.gainDb, 1));
        check (! g.takeNotes().isEmpty(), "guard: clamps are reported");
    }
    {
        Recorder rec;
        GuardedDspController g (rec);
        g.setTweeterHpOverride (true);
        juce::String err;
        g.applyChannel (tweeterCh (500.0, 800.0, -3.0), err);
        check (rec.writes.back().settings.highPass.freq == 800.0, "guard: explicit user override keeps the HP");
    }
    {
        Recorder rec;
        GuardedDspController g (rec);
        juce::String err;
        g.applyChannel (tweeterCh (500.0, 2500.0, -20.0), err);
        const size_t before = rec.writes.size();
        g.applyChannel (tweeterCh (500.0, 2500.0, 0.0), err);       // +20 dB jump
        const double step = safety::kMaxGainStepDb;
        bool stepsOk = rec.writes.size() - before >= 7;
        double prev = -20.0;
        for (size_t i = before; i < rec.writes.size(); ++i)
        {
            stepsOk = stepsOk && rec.writes[i].settings.gainDb - prev <= step + 1e-9;
            prev = rec.writes[i].settings.gainDb;
        }
        check (stepsOk && rec.writes.back().settings.gainDb == 0.0, "guard: +20 dB rise is walked in <= max-step writes");
    }
    {
        Recorder rec;
        GuardedDspController g (rec);
        juce::String err;
        g.applyChannel (tweeterCh (500.0, 2500.0, -6.0), err);
        rec.mutes.clear();
        g.applyChannel (tweeterCh (500.0, 3500.0, -6.0), err);      // crossover change
        check (rec.mutes.size() == 2 && rec.mutes[0].second && ! rec.mutes[1].second, "guard: tweeter muted around a crossover change");
        rec.mutes.clear();
        auto pol = tweeterCh (500.0, 3500.0, -6.0);
        pol.settings.inverted = true;
        g.applyChannel (pol, err);
        check (rec.mutes.size() == 2, "guard: tweeter muted around a polarity change");
        rec.mutes.clear();
        g.applyChannel (pol, err);
        check (rec.mutes.empty(), "guard: no mute when nothing changed");
    }
    {
        Recorder rec;
        rec.automatic = false;                                      // manual DSP: no mute prompts
        GuardedDspController g (rec);
        juce::String err;
        g.applyChannel (tweeterCh (500.0, 2500.0, -6.0), err);
        g.applyChannel (tweeterCh (500.0, 3500.0, -6.0), err);
        check (rec.mutes.empty(), "guard: manual controllers get no mute instructions");
    }
}

FA_TEST (qaHookDefaultsOff)
{
    // The app target is configured with FA_QA_HOOKS=OFF unless build.ps1 -QaHooks is used.
    juce::File root;
    for (auto d = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory(); ! d.isRoot(); d = d.getParentDirectory())
        if (d.getChildFile ("CMakeLists.txt").existsAsFile()) { root = d; break; }
    const auto cm = root.getChildFile ("CMakeLists.txt").loadFileAsString();
    check (cm.contains ("option(FA_QA_HOOKS") && cm.contains ("OFF)"), "QA hook: CMake option FA_QA_HOOKS exists and defaults OFF");
}

FA_TEST (gearValidatorRules)
{
    using namespace fa::gear;
    auto parse = [] (const char* json) { return juce::JSON::parse (juce::String (json)); };
    std::vector<GearIssue> out;
    validateGearProduct (parse (R"({"id":"bad","category":"driverMidbass","driver":{"nominalOhms":0,"fsHz":-5,"qts":2.0,"sensitivityDb":120}})"), "t.json", out);
    int errors = 0, warnings = 0;
    for (auto& i : out)
        (i.level == GearIssue::Level::error ? errors : warnings)++;
    check (errors == 2, "validator: 0 ohm and negative Fs are hard errors", juce::String (errors));
    check (warnings >= 2, "validator: Qts > 1.5 and sensitivity 120 dB are warnings", juce::String (warnings));

    out.clear();
    validateGearProduct (parse (R"({"id":"mono","category":"ampMono","amp":{"channels":1,"rmsPerChannelW":{"1ohm":8000}}})"), "t.json", out);
    check (out.empty(), "validator: 8000 W mono amp is fine");
    validateGearProduct (parse (R"({"id":"multi","category":"ampMulti","amp":{"channels":4,"rmsPerChannelW":{"4ohm":8000}}})"), "t.json", out);
    check (out.size() == 1 && out[0].level == GearIssue::Level::warning, "validator: 8000 W/channel on a 4-ch amp is a warning");
}

FA_TEST (gearDataValidation)
{
    juce::File gear;
    for (auto d = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory(); ! d.isRoot(); d = d.getParentDirectory())
        if (d.getChildFile ("data").getChildFile ("gear").isDirectory()) { gear = d.getChildFile ("data").getChildFile ("gear"); break; }
    check (gear.isDirectory(), "gear data folder found");
    if (! gear.isDirectory())
        return;

    std::vector<fa::gear::GearIssue> issues;
    const int n = fa::gear::validateGearFolder (gear, issues);
    const auto report = fa::gear::formatGearReport (issues, n);
    const auto reportFile = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getSiblingFile ("gear_validation_report.txt");
    reportFile.replaceWithText (report);
    std::cout << "  gear validation report: " << reportFile.getFullPathName() << "\n";

    int errors = 0;
    juce::String first;
    for (auto& i : issues)
        if (i.level == fa::gear::GearIssue::Level::error)
        {
            ++errors;
            if (first.isEmpty())
                first = i.file + " " + i.productId + " " + i.field;
        }
    check (n > 1000, "gear validation scanned the database", juce::String (n) + " products");
    check (errors == 0, "gear data has no hard errors (0 ohm, <= 0 Fs/Qts/RMS ...); warnings are in the report", first);
}
