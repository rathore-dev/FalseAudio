// Setup guide (mic placement, interface steps) and tune insights.

#include "TestRegistry.h"
#include "core/guide/SetupGuide.h"
#include "core/tune/insights/TuneInsights.h"
#include "core/sim/VirtualRig.h"

using namespace fa;
using fa::test::check;

namespace
{
gear::Product makeMic (const juce::String& model, const juce::String& type, bool phantom, bool calFile)
{
    gear::Product p;
    p.category = gear::Category::measurementMic;
    p.brand = "Test";
    p.model = model;
    gear::MicSpecs s;
    s.type = type;
    s.needsPhantom = phantom;
    s.calFileAvailable = calFile;
    p.mic = s;
    return p;
}

const guide::Step* findStep (const std::vector<guide::Step>& steps, const juce::String& id)
{
    for (auto& s : steps)
        if (s.id == id)
            return &s;
    return nullptr;
}
} // namespace

FA_TEST (setupGuideMicPlacement)
{
    auto cfg = SystemConfig::makeLayout (2, false);
    const auto umik = makeMic ("UMIK-1", "USB", false, true);
    const auto xlrNoCal = makeMic ("Generic", "XLR", true, false);

    bool counts = true, symmetric = true, close = true, labelled = true;
    for (int n = 1; n <= 9; ++n)
    {
        const auto p = guide::micPlacement (cfg, n, &umik);
        counts = counts && (int) p.positions.size() == n;
        double sx = 0.0;
        for (auto& pos : p.positions)
        {
            sx += pos.xCm;
            close = close && std::sqrt (pos.xCm * pos.xCm + pos.yCm * pos.yCm + pos.zCm * pos.zCm) <= 20.0;
            labelled = labelled && pos.label.isNotEmpty();
        }
        symmetric = symmetric && std::abs (sx) < 1e-9;
    }
    check (counts, "Mic placement gives the requested number of positions (1..9)");
    check (symmetric, "Positions are left/right symmetric about the head centre");
    check (close && labelled, "Positions labelled and within 20 cm of the head centre");
    check (guide::micPlacement (cfg, 20, nullptr).positions.size() == 9, "Position count clamps to 9");

    const auto u = guide::micPlacement (cfg, 3, &umik);
    check (u.orientation.containsIgnoreCase ("roof") && u.orientation.contains ("90"), "UMIK: roof, 90 degree file", u.orientation);
    const auto x = guide::micPlacement (cfg, 3, &xlrNoCal);
    check (x.orientation.containsIgnoreCase ("forward"), "XLR without 90 degree file: forward fallback", x.orientation);

    cfg.listeningSeat = 1;
    const auto pass = guide::micPlacement (cfg, 1, &umik);
    check (pass.instructions.joinIntoString (" ").containsIgnoreCase ("passenger"), "Seat appears in the instructions");
}

FA_TEST (setupGuideInterfaceSteps)
{
    AudioRouting routing;
    routing.micInputs = { 0 };

    // Generic: no specs at all.
    const auto generic = guide::interfaceSetup (nullptr, nullptr, routing);
    bool allIds = true;
    for (auto* id : { "driver", "samplerate", "buffer", "phantom", "gain", "monitor", "output", "loopback", "windows" })
        allIds = allIds && findStep (generic, id) != nullptr;
    check (allIds && generic.size() == 9, "Generic interface setup has every step", juce::String ((int) generic.size()) + " steps");
    check (findStep (generic, "loopback")->status == guide::Step::Status::skipped, "No loopback configured -> skipped");

    // Specific interface without phantom power and a condenser XLR mic.
    gear::Product iface;
    iface.category = gear::Category::audioInterface;
    iface.brand = "Test";
    iface.model = "Box 2x2";
    gear::InterfaceSpecs is;
    is.inputs = 2; is.outputs = 4; is.micPreamps = 2;
    is.phantomPower = false; is.asio = true; is.maxGainDb = 56.0;
    is.sampleRates = { "44100", "48000", "96000" };
    iface.audioInterface = is;
    const auto xlr = makeMic ("Cond", "XLR", true, true);

    routing.loopbackOutput = 2;
    routing.loopbackInput = 1;
    const auto specific = guide::interfaceSetup (&iface, &xlr, routing);
    const auto* ph = findStep (specific, "phantom");
    check (ph != nullptr && ph->status == guide::Step::Status::warning, "Missing phantom power flagged for a condenser mic");
    const auto* gain = findStep (specific, "gain");
    check (gain != nullptr && gain->bullets.joinIntoString (" ").contains ("28 dB"), "Gain starting point from the interface's max gain");
    const auto* lb = findStep (specific, "loopback");
    check (lb != nullptr && lb->detail.contains ("Output 3") && lb->detail.contains ("Input 2") && lb->status != guide::Step::Status::failed,
           "Exact loopback routing", lb != nullptr ? lb->detail : juce::String());
    check (findStep (specific, "driver")->detail.contains ("ASIO"), "ASIO mentioned when the interface has it");
    check (findStep (specific, "samplerate")->status != guide::Step::Status::warning, "48 kHz supported -> no warning");

    routing.loopbackInput = 0;   // clashes with the mic
    const auto clash = guide::interfaceSetup (&iface, &xlr, routing);
    check (findStep (clash, "loopback")->status == guide::Step::Status::failed, "Loopback on the mic input is an error");

    const auto umik = makeMic ("UMIK-1", "USB", false, true);
    const auto usb = guide::interfaceSetup (&iface, &umik, routing);
    check (findStep (usb, "phantom")->detail.contains ("OFF"), "USB mic: phantom not needed");
}

FA_TEST (tuneInsightsFromAutoTune)
{
    auto cfg = SystemConfig::makeLayout (3, true);
    VirtualRig rig (cfg, 2);
    rig.setSimulatedSweepMs (0);
    rig.getCar().driver ("FL.tweeter").wiredInverted = true;

    AutoTuneEngine engine;
    engine.onMicPositionChanged = [&rig] (int p) { rig.setMicPosition (p); };
    AutoTuneEngine::Options o;
    o.micPositions = 2;
    engine.start (cfg, rig, rig, o);
    while (engine.isRunning())
        juce::Thread::sleep (20);
    const auto r = engine.getLastResult();
    check (r.success, "Auto-tune ran for insights", r.error);
    if (! r.success)
        return;

    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    const auto in = insights::compute (r, o.sweepSeconds, 10.0, 24000.0);
    const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;

    check (in.bands.size() == 31 && in.bands.front().freqHz == 20.0 && in.bands.back().freqHz == 20000.0,
           "31 third-octave bands", juce::String ((int) in.bands.size()));
    double worst = 0.0;
    for (auto& b : in.bands)
        if (b.freqHz >= 100.0 && b.freqHz <= 10000.0)
            worst = std::max (worst, std::abs (b.leftAfterDb - b.targetDb));
    check (worst < 6.0, "Left after follows the target per band", "worst " + juce::String (worst, 1) + " dB");

    double eqSpan = 0.0;
    for (auto& b : in.bands)
        eqSpan = std::max (eqSpan, std::abs (b.eqAppliedLeftDb));
    check (eqSpan > 0.1 && eqSpan < 40.0, "Net EQ per side computed", "max |EQ| " + juce::String (eqSpan, 1) + " dB");

    check (in.drivers.size() == r.tunedConfig.channels.size(), "Diagnostics for every driver",
           juce::String ((int) in.drivers.size()) + " of " + juce::String ((int) r.tunedConfig.channels.size()));

    const insights::DriverDiagnostics* tw = nullptr;
    const insights::DriverDiagnostics* sub = nullptr;
    for (auto& d : in.drivers)
    {
        if (d.channelId == "FL.tweeter") tw = &d;
        if (d.channelId.containsIgnoreCase ("sub")) sub = &d;
    }
    check (tw != nullptr && tw->polarityReversed && tw->findings.joinIntoString (" ").containsIgnoreCase ("polarity"),
           "Reversed tweeter reported in plain language");
    check (tw != nullptr && tw->usableLowHz > 500.0 && tw->usableHighHz > 8000.0, "Tweeter usable band",
           tw != nullptr ? juce::String (tw->usableLowHz, 0) + " - " + juce::String (tw->usableHighHz, 0) + " Hz" : juce::String());
    check (sub != nullptr && sub->usableHighHz < 500.0, "Sub usable band",
           sub != nullptr ? juce::String (sub->usableLowHz, 0) + " - " + juce::String (sub->usableHighHz, 0) + " Hz" : juce::String());
    check (tw != nullptr && tw->snrDb > 20.0 && tw->arrivalMs > 0.0, "SNR and arrival filled",
           tw != nullptr ? juce::String (tw->snrDb, 0) + " dB, " + juce::String (tw->arrivalMs, 2) + " ms" : juce::String());

    auto inRange = [] (double s) { return s >= 0.0 && s <= 100.0; };
    check (inRange (in.tonalBalanceScore) && inRange (in.imagingScore) && inRange (in.cleanlinessScore) && in.tonalBalanceScore > 50.0,
           "Scores in range", "tonal " + juce::String (in.tonalBalanceScore, 0) + ", imaging " + juce::String (in.imagingScore, 0)
                                  + ", clean " + juce::String (in.cleanlinessScore, 0));
    check (! in.summary.isEmpty(), "Summary written", in.summary[0]);

    juce::StringArray lines;
    lines.addLines (insights::bandsToCsv (in).trimEnd());
    check (lines.size() == (int) in.bands.size() + 1 && lines[0].startsWith ("Frequency"), "CSV export",
           juce::String (lines.size()) + " lines");

    int nf = 0;
    for (auto& d : in.drivers)
        for (auto& f : d.findings)
            if (nf++ < 6)
                std::cout << "      - " << f << "\n";
    std::cout << "      insights computed in " << juce::String (ms, 0) << " ms\n";
}

FA_TEST (tonalScoreUsesUsableBand)
{
    using P = SystemConfig::Preset;
    for (auto p : { P::active3WaySub, P::passive2WaySub, P::passiveFront, P::fullRangePair, P::singleDriver })
    {
        auto cfg = SystemConfig::makePreset (p);
        VirtualRig rig (cfg, 2);
        rig.setSimulatedSweepMs (0);
        AutoTuneEngine engine;
        engine.onMicPositionChanged = [&rig] (int pos) { rig.setMicPosition (pos); };
        AutoTuneEngine::Options o;
        o.micPositions = 2;
        engine.start (cfg, rig, rig, o);
        while (engine.isRunning())
            juce::Thread::sleep (20);
        const auto r = engine.getLastResult();
        const auto name = SystemConfig::presetName (p);
        check (r.success, "Auto-tune ran: " + name, r.error);
        if (! r.success)
            continue;
        const auto in = insights::compute (r, o.sweepSeconds, 10.0, 24000.0);

        // Reference: the old full-range (40 Hz..16 kHz) score from the same bands.
        auto rms = [&] (double lo, bool left)
        {
            double sum = 0.0, mean = 0.0; int n = 0;
            for (auto& b : in.bands)
                if (b.freqHz >= lo && b.freqHz <= 16000.0) { mean += (left ? b.leftAfterDb : b.rightAfterDb) - b.targetDb; ++n; }
            if (n == 0) return 0.0;
            mean /= n;
            for (auto& b : in.bands)
                if (b.freqHz >= lo && b.freqHz <= 16000.0) { const double e = (left ? b.leftAfterDb : b.rightAfterDb) - b.targetDb - mean; sum += e * e; }
            return std::sqrt (sum / n);
        };
        const double oldScore = juce::jlimit (0.0, 100.0, 100.0 - 6.0 * (rms (40.0, true) + (r.rightAfter.isEmpty() ? rms (40.0, true) : rms (40.0, false))));
        const double sideErr = 0.5 * (r.leftErrorDb + r.rightErrorDb);
        const juce::String detail = name + ": tonal " + juce::String (in.tonalBalanceScore, 1) + ", old " + juce::String (oldScore, 1)
                                  + ", side err " + juce::String (sideErr, 1) + " dB";

        if (cfg.hasSubwoofer())
            check (std::abs (in.tonalBalanceScore - oldScore) <= 2.0, "Sub preset keeps its tonal score", detail);
        else
        {
            check (in.tonalBalanceScore >= oldScore - 0.01, "No-sub tonal score not worse than full-range", detail);
            if (sideErr < 3.0)
                check (in.tonalBalanceScore >= 60.0, "No-sub tonal consistent with side error", detail);
            check (in.summary.joinIntoString (" ").contains ("isn't scored"), "No-sub note present", name);
        }
    }
}
