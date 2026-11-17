// AI operator against FakeDspConsole (a fake vendor DSP window) with a real local model.
//
//   - the pure parts (state file round-trip, fake verify_channel, expected-value oracle, comparison) always run;
//   - the live run is gated: FA_FAKEDSP_LIVE=1. It launches tools/fakedsp/FakeDspConsole.exe, attaches the AI operator
//     through the real UIA WindowBackend, learns the map with Ollama (qwen3-vl:4b), pushes a computed demo tune through the
//     learned map (GuardedDspController + SafetyGuard) and compares FakeDspConsole's final state per parameter.
//
// Env: FA_FAKEDSP_LIVE=1, FA_FAKEDSP_OUT=<dir> (log, map, results), FA_FAKEDSP_EXE=<path>, FA_FAKEDSP_CUSTOM=1 (custom-drawn
//      variant), FA_FAKEDSP_UNMUTED=1 (factory state: all outputs unmuted; default muted), FA_FAKEDSP_MODE=run|dump|smoke, FA_OLLAMA_MODEL.
// NO AUDIO is ever played: the app has no audio, "verify_channel" is answered from the fake's mute state.
#include "TestRegistry.h"
#include "FakeDspState.h"
#include "core/ai/SafetyGuard.h"
#include "core/bridge/AiDspController.h"
#include "core/bridge/GuardedDspController.h"
#include "core/sim/VirtualRig.h"
#include "core/tune/AutoTuneEngine.h"

#include <atomic>
#include <cstdlib>
#include <thread>

#if JUCE_WINDOWS
 #define NOMINMAX
 #include <windows.h>
#endif

using fa::test::check;
using namespace fa;

namespace
{
struct ParamResult
{
    int output = 0;                 // 1-based
    juce::String param, expected, actual, note;
    bool ok = false;
};

std::unique_ptr<SafetyGuard> makeOracleGuard (const SystemConfig& cfg)
{
    auto g = std::make_unique<SafetyGuard> (SafetyGuard::Limits {}, cfg.caps);
    g->setChannels (cfg.channels);
    return g;
}

/** What the guarded controller is allowed to leave in the DSP for this channel (gain <= 0, protected HP, ranges). */
OutputChannel expectedChannel (const SystemConfig& cfg, const OutputChannel& in)
{
    auto g = makeOracleGuard (cfg);
    OutputChannel ch = in;
    ch.settings.gainDb = std::min (ch.settings.gainDb, 0.0);
    g->noteGain (ch.dspOutput, ch.settings.gainDb);       // already walked there: no step cap in the oracle
    juce::StringArray notes;
    g->sanitizeChannel (ch, notes);
    return ch;
}

bool nearRel (double a, double b, double rel, double abs) { return std::abs (a - b) <= std::max (abs, rel * std::abs (b)); }

juce::String filterText (const FilterSpec& f) { return f.enabled ? crossoverKey (f) + " @" + juce::String (f.freq, 0) + " Hz" : juce::String ("Off"); }

/** Per-parameter comparison of the DSP state against what the guarded controller should have written. */
std::vector<ParamResult> compareState (const SystemConfig& cfg, const fakedsp::State& st)
{
    std::vector<ParamResult> res;
    auto add = [&] (int out, const juce::String& param, const juce::String& exp, const juce::String& act, bool ok, const juce::String& note = {})
    {
        res.push_back ({ out, param, exp, act, note, ok });
    };
    const auto choices = fakedsp::filterChoices();
    auto supported = [&] (const FilterSpec& f)
    {
        if (! f.enabled) return true;
        const auto k = crossoverKey (f);
        for (int i = 1; i < choices.size(); ++i)
        {
            fakedsp::Filter ff; ff.choice = i;
            if (ff.key() == k) return true;
        }
        return false;
    };

    for (auto& in : cfg.channels)
    {
        const auto ch = expectedChannel (cfg, in);
        const auto& s = ch.settings;
        const int n = in.dspOutput;
        if (n < 0 || n >= fakedsp::numOutputs)
            continue;
        const auto& o = st.out[n];
        const int o1 = n + 1;

        add (o1, "gain", juce::String (s.gainDb, 1), juce::String (o.gain, 1), std::abs (o.gain - s.gainDb) <= 0.06);
        add (o1, "delay", juce::String (s.delayMs, 2), juce::String (o.delayMs, 2), std::abs (o.delayMs - s.delayMs) <= 0.011);
        add (o1, "polarity", s.inverted ? "inverted" : "normal", o.inverted ? "inverted" : "normal", o.inverted == s.inverted);
        add (o1, "mute", "unmuted", o.muted ? "muted" : "unmuted", ! o.muted);

        auto cmpFilter = [&] (const juce::String& name, const FilterSpec& f, const fakedsp::Filter& got)
        {
            const bool sup = supported (f);
            const juce::String expKey = f.enabled ? (sup ? crossoverKey (f) : juce::String ("(unsupported "
                                                  + crossoverKey (f) + ")")) : juce::String();
            const bool typeOk = f.enabled ? (sup && got.key() == crossoverKey (f)) : got.choice == 0;
            add (o1, name + ".type", f.enabled ? crossoverKey (f) : "Off", got.choice == 0 ? "Off" : got.key(), typeOk,
                 sup ? juce::String() : "slope not offered by the fake device");
            if (f.enabled)
                add (o1, name + ".freq", juce::String (f.freq, 0), juce::String (got.freq, 0), nearRel (got.freq, f.freq, 0.02, 1.0));
            juce::ignoreUnused (expKey);
        };
        if (ch.hasHighPass() || ch.role == DriverRole::subwoofer)
            cmpFilter ("hp", s.highPass, o.hp);
        if (ch.hasLowPass())
            cmpFilter ("lp", s.lowPass, o.lp);

        for (int b = 0; b < fakedsp::numPeqBands; ++b)
        {
            const bool used = b < (int) s.peq.size();
            if (! used)
            {
                add (o1, "peq" + juce::String (b + 1) + ".gain", "0.0", juce::String (o.peq[b].gain, 1), std::abs (o.peq[b].gain) <= 0.06);
                continue;
            }
            const auto& f = s.peq[(size_t) b];
            add (o1, "peq" + juce::String (b + 1) + ".freq", juce::String (f.freq, 0), juce::String (o.peq[b].freq, 0), nearRel (o.peq[b].freq, f.freq, 0.02, 1.0));
            add (o1, "peq" + juce::String (b + 1) + ".gain", juce::String (f.gainDb, 1), juce::String (o.peq[b].gain, 1), std::abs (o.peq[b].gain - f.gainDb) <= 0.06);
            add (o1, "peq" + juce::String (b + 1) + ".q", juce::String (f.q, 2), juce::String (o.peq[b].q, 2), nearRel (o.peq[b].q, f.q, 0.03, 0.011));
        }
    }
    return res;
}

/** The harness side of verify_channel: which output "plays" is decided from the fake DSP's mutes only. */
DspOperatorAgent::VerifyResult fakeVerify (const SystemConfig& cfg, const fakedsp::State& st, int out0)
{
    DspOperatorAgent::VerifyResult r;
    r.performed = true;
    std::vector<int> audible;
    for (int i = 0; i < fakedsp::numOutputs; ++i)
        if (! st.out[i].muted)
            audible.push_back (i);

    auto describe = [&] (int o) -> juce::String
    {
        for (auto& c : cfg.channels)
            if (c.dspOutput == o)
                return c.label + " (" + toString (c.role) + ")";
        return "nothing connected";
    };
    if (audible.empty())
    {
        r.detail = "Silence: every output is muted.";
        return r;
    }
    if (audible.size() > 1)
    {
        juce::StringArray names;
        for (int o : audible) names.add (describe (o));
        r.respondedDriver = "several drivers at once";
        r.detail = juce::String ((int) audible.size()) + " outputs are playing (" + names.joinIntoString (", ")
                   + "). Mute all but the output under test and test again.";
        return r;
    }
    r.respondedDriver = describe (audible[0]);
    for (auto& c : cfg.channels)
        if (c.dspOutput == audible[0])
            r.respondedBand = c.role == DriverRole::tweeter ? "high" : c.role == DriverRole::subwoofer ? "low" : "mid";
    r.matches = audible[0] == out0;
    return r;
}

//==============================================================================
juce::File repoRoot()
{
    auto f = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    for (int i = 0; i < 8 && f.exists(); ++i, f = f.getParentDirectory())
        if (f.getChildFile ("CMakeLists.txt").existsAsFile())
            return f;
    return {};
}

juce::File findFakeExe()
{
    const auto env = juce::SystemStats::getEnvironmentVariable ("FA_FAKEDSP_EXE", {});
    if (env.isNotEmpty())
        return juce::File (env);
    auto f = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();   // ...\FalseAudioTests_artefacts\Release
    for (int i = 0; i < 4; ++i, f = f.getParentDirectory())
    {
        auto c = f.getChildFile ("FakeDspConsole_artefacts/Release/FakeDspConsole.exe");
        if (c.existsAsFile())
            return c;
    }
    return {};
}

bool ollamaUp()
{
    juce::URL url ("http://localhost:11434/api/tags");
    if (auto in = url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress).withConnectionTimeoutMs (2000)))
        return in->readEntireStreamAsString().contains ("models");
    return false;
}

bool ollamaHasModel (const juce::String& model)
{
    juce::URL url ("http://localhost:11434/api/tags");
    if (auto in = url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress).withConnectionTimeoutMs (3000)))
        return in->readEntireStreamAsString().contains ("\"" + model + "\"");
    return false;
}

#if JUCE_WINDOWS
HWND findFakeWindow (int waitMs)
{
    const auto t0 = juce::Time::getMillisecondCounter();
    while ((int) (juce::Time::getMillisecondCounter() - t0) < waitMs)
    {
        if (auto h = FindWindowW (nullptr, juce::String (fakedsp::windowTitle).toWideCharPointer()))
            if (IsWindowVisible (h))
                return h;
        juce::Thread::sleep (200);
    }
    return nullptr;
}
#endif

struct LiveLog
{
    juce::File file;
    std::mutex m;
    double t0 = juce::Time::getMillisecondCounterHiRes();
    void line (const juce::String& s)
    {
        std::lock_guard<std::mutex> l (m);
        const auto stamped = "[" + juce::String ((juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0, 1) + "s] " + s;
        file.appendText (stamped + "\n");
        std::fprintf (stderr, "%s\n", stamped.toRawUTF8());
    }
};

juce::String dumpTree (WindowBackend& wb, int maxNodes, int maxDepth)
{
    juce::String t;
    const auto b = wb.windowBounds();
    const auto nodes = wb.snapshot (maxNodes, maxDepth);
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        const auto& n = nodes[i].info;
        t << juce::String::repeatedString ("  ", nodes[i].depth) << "[" << (int) i + 1 << "] " << n.controlType << " \"" << n.name << "\" value=\"" << n.value
          << "\" @" << n.screenBounds.getX() - b.getX() << "," << n.screenBounds.getY() - b.getY()
          << (n.supportsRange ? " range" : "") << (n.supportsValue ? " value" : "") << (n.supportsToggle ? " toggle" : "")
          << (n.supportsInvoke ? " invoke" : "") << (n.supportsSelection ? " select" : "") << (n.supportsExpand ? " expand" : "") << "\n";
    }
    return t;
}
} // namespace

//==============================================================================
FA_TEST (fakeDspStateAndOracle)
{
    // State file round-trip.
    fakedsp::State s;
    s.selected = 3;
    s.out[2].gain = -4.5;
    s.out[2].hp.choice = 6;   // Linkwitz-Riley 24
    s.out[2].hp.freq = 3500;
    s.out[2].peq[1].gain = 2.5;
    const auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("fakedsp_state_test.json");
    check (s.save (f), "fakedsp: state file written");
    fakedsp::State back;
    check (fakedsp::State::load (f, back) && back.selected == 3 && back.out[2].gain == -4.5 && back.out[2].hp.freq == 3500
           && back.out[2].peq[1].gain == 2.5, "fakedsp: state file round-trips");
    f.deleteFile();
    fakedsp::Filter lr; lr.choice = 6;
    check (lr.key() == "LR24", "fakedsp: filter key", lr.key());

    // verify_channel is decided by the mutes only.
    auto cfg = SystemConfig::makeLayout (3, true);
    fakedsp::State st;
    for (auto& o : st.out) o.muted = true;
    st.out[2].muted = false;
    auto v = fakeVerify (cfg, st, 2);
    check (v.performed && v.matches && v.respondedDriver.isNotEmpty(), "fakedsp verify: the single unmuted output is reported", v.respondedDriver);
    check (! fakeVerify (cfg, st, 3).matches, "fakedsp verify: a different claimed output does not match");
    st.out[3].muted = false;
    v = fakeVerify (cfg, st, 2);
    check (! v.matches && v.detail.contains ("2 outputs"), "fakedsp verify: two outputs playing is a mismatch", v.detail);
    for (auto& o : st.out) o.muted = true;
    check (! fakeVerify (cfg, st, 2).matches, "fakedsp verify: all muted is silence");

    // Oracle: tweeter HP is raised to the protected value, gain capped at 0 dB; comparison is per parameter.
    SystemConfig tuned = cfg;
    for (auto& c : tuned.channels)
    {
        c.settings.gainDb = 3.0;
        c.settings.highPass = FilterSpec::crossover (FilterType::highPass, c.role == DriverRole::tweeter ? 500.0 : 80.0, 24, Alignment::linkwitzRiley);
    }
    const auto exp = expectedChannel (tuned, *tuned.find ("FL.tweeter"));
    check (exp.settings.gainDb == 0.0 && exp.settings.highPass.freq > 1000.0, "fakedsp oracle: tweeter HP raised, gain capped",
           juce::String (exp.settings.highPass.freq));
    const auto cmp = compareState (tuned, fakedsp::State());
    int bad = 0;
    for (auto& r : cmp) bad += r.ok ? 0 : 1;
    check (! cmp.empty() && bad > 0, "fakedsp compare: the factory state is not the tune", juce::String (bad) + "/" + juce::String ((int) cmp.size()));
}

//==============================================================================
FA_TEST (fakeDspLive)
{
    if (juce::SystemStats::getEnvironmentVariable ("FA_FAKEDSP_LIVE", {}) != "1")
        return;
#if JUCE_WINDOWS
    const auto mode = juce::SystemStats::getEnvironmentVariable ("FA_FAKEDSP_MODE", "run");
    auto outDir = juce::File (juce::SystemStats::getEnvironmentVariable ("FA_FAKEDSP_OUT", {}));
    if (outDir == juce::File())
        outDir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("fakedsp_run");
    outDir.createDirectory();
    LiveLog log { outDir.getChildFile ("log.txt") };
    log.file.deleteFile();
    const auto stateFile = outDir.getChildFile ("fake_state.json");
    stateFile.deleteFile();

    if (mode == "body")
    {
        // Writes the exact first request the agent sends to Ollama (for debugging with curl). No window, no model call.
        OllamaClient oc ((OllamaClient::Config()));
        juce::Array<juce::var> msgs;
        auto* m = new juce::DynamicObject();
        m->setProperty ("role", "user");
        juce::String task = "Learn the control map of the attached DSP software window.\n\nOutputs to map (1-based numbers as in the DSP software):\n";
        for (auto& ch : SystemConfig::makeLayout (3, true).channels)
            task << "- Output " << (ch.dspOutput + 1) << ": " << ch.label << " (" << toString (ch.role) << ")"
                 << (ch.role == DriverRole::tweeter ? " - TWEETER: protected, mute before crossover/polarity changes" : "") << "\n";
        task << "\nDSP capabilities: 5 PEQ bands per output, gain -40..12 dB, max delay 20 ms.\nWindow size: 544x483 px, view scale 1.000.\n";
        m->setProperty ("content", task);
        msgs.add (juce::var (m));
        outDir.getChildFile ("body.json").replaceWithText (oc.buildRequestBody (DspOperatorAgent::compactSystemPrompt(), DspOperatorAgent::toolDefinitions(), juce::var (msgs)));
        return;
    }

    // Keep the PC awake while the (slow) local model works: a suspended machine silently ruins the timing and the run.
    SetThreadExecutionState (ES_CONTINUOUS | ES_SYSTEM_REQUIRED);

    // ---- 1. Ollama ----
    juce::String model = juce::SystemStats::getEnvironmentVariable ("FA_OLLAMA_MODEL", "qwen3-vl:4b");
    if (mode == "run")
    {
        if (! ollamaUp())
        {
            const auto exe = juce::File (juce::SystemStats::getEnvironmentVariable ("LOCALAPPDATA", {})).getChildFile ("Programs/Ollama/ollama.exe");
            log.line ("Ollama is not running; starting " + exe.getFullPathName() + " serve");
            static juce::ChildProcess serve;
            serve.start (juce::StringArray { exe.getFullPathName(), "serve" });
            for (int i = 0; i < 40 && ! ollamaUp(); ++i)
                juce::Thread::sleep (500);
        }
        check (ollamaUp(), "live: Ollama answers on localhost:11434");
        check (ollamaHasModel (model), "live: model " + model + " is installed");
        if (! ollamaUp() || ! ollamaHasModel (model))
            return;
    }

    // ---- 2. launch FakeDspConsole ----
    const auto exe = findFakeExe();
    check (exe.existsAsFile(), "live: FakeDspConsole.exe found", exe.getFullPathName());
    if (! exe.existsAsFile())
        return;
    while (auto h = FindWindowW (nullptr, juce::String (fakedsp::windowTitle).toWideCharPointer()))   // never attach to a stale instance
        PostMessageW (h, WM_CLOSE, 0, 0), juce::Thread::sleep (500);
    juce::StringArray cmd { exe.getFullPathName(), "--state=" + stateFile.getFullPathName() };
    const bool custom = juce::SystemStats::getEnvironmentVariable ("FA_FAKEDSP_CUSTOM", {}) == "1";
    if (custom) cmd.add ("--custom");
    // Safe factory state (every output muted) by default: a tweeter that is live with no high-pass at power-up is the unit's
    // state, not the AI's doing. FA_FAKEDSP_UNMUTED=1 starts every output unmuted.
    const bool startUnmuted = juce::SystemStats::getEnvironmentVariable ("FA_FAKEDSP_UNMUTED", {}) == "1";
    if (! startUnmuted) cmd.add ("--muted");
    juce::ChildProcess app;
    check (app.start (cmd), "live: FakeDspConsole started");
    HWND hwnd = findFakeWindow (15000);
    check (hwnd != nullptr, "live: FakeDspConsole window found");
    if (hwnd == nullptr)
        return;
    struct Closer { juce::ChildProcess& p; HWND h; ~Closer() { PostMessageW (h, WM_CLOSE, 0, 0); juce::Thread::sleep (700); p.kill(); } } closer { app, hwnd };
    juce::Thread::sleep (800);
    uia::Automation::bringToFront (hwnd);
    log.line (juce::String ("Window up: ") + (custom ? "custom-drawn variant" : "standard variant"));

    auto backend = makeUiaWindowBackend ([hwnd] () -> void* { return hwnd; });
    {
        const auto wb = backend->windowBounds();
        log.line ("hwnd=" + juce::String::toHexString ((juce::int64) (intptr_t) hwnd) + " visible=" + juce::String ((int) IsWindowVisible (hwnd)) + " iconic=" + juce::String ((int) IsIconic (hwnd))
                  + " bounds=" + wb.toString() + " foreground=" + juce::String ((int) uia::Automation::isForeground (hwnd)));
    }
    if (mode == "dump")
    {
        const auto t = dumpTree (*backend, 400, 14);
        outDir.getChildFile ("tree.txt").replaceWithText (t);
        WindowScreenshot shot;
        if (backend->screenshot (1200, shot))
        {
            juce::MemoryOutputStream mos;
            juce::Base64::convertFromBase64 (mos, shot.base64);
            outDir.getChildFile ("shot.png").replaceWithData (mos.getData(), mos.getDataSize());
        }
        log.line ("tree:\n" + t);
        return;
    }
    if (mode == "smoke")
    {
        // Drives the window by hand through the same backend calls the learned map uses (no AI).
        const auto nodes = backend->snapshot (400, 14);
        auto findNode = [&] (const juce::String& type, const juce::String& name, int nth = 0) -> const uia::TreeNode*
        {
            int k = 0;
            for (auto& n : nodes)
                if (n.info.controlType == type && n.info.name == name && k++ == nth) return &n;
            return nullptr;
        };
        juce::String e;
        auto report = [&] (const char* what, bool ok) { log.line (juce::String (what) + (ok ? ": ok" : ": FAILED ") + e); e.clear(); };
        if (auto* n = findNode ("Slider", "Gain")) { report ("gain setRange -3.5", backend->setRange (n->info.path, -3.5, e)); log.line ("  read: " + backend->readValue (n->info.path)); } else log.line ("no Slider Gain");
        if (auto* n = findNode ("Edit", "Dly")) { report ("delay setText 1.25", backend->setText (n->info.path, "1.25", e)); log.line ("  read: " + backend->readValue (n->info.path)); } else log.line ("no Edit Dly");
        if (auto* n = findNode ("Button", "MUTE")) { report ("mute toggle on", backend->setToggle (n->info.path, true, e)); bool st = false; backend->readToggle (n->info.path, st); log.line (juce::String ("  toggle=") + (st ? "on" : "off")); } else log.line ("no Button MUTE");
        if (auto* n = findNode ("RadioButton", "OUT 3")) report ("select OUT 3 (invoke)", backend->invoke (n->info.path, e)); else log.line ("no Button OUT 3");
        if (auto* n = findNode ("ComboBox", "Type")) { report ("hp type select", backend->selectItem (n->info.path, "Linkwitz-Riley 24 dB/oct", e)); log.line ("  read: " + backend->readValue (n->info.path)); } else log.line ("no ComboBox Type");
        juce::Thread::sleep (500);
        fakedsp::State st;
        fakedsp::State::load (stateFile, st);
        log.line ("state: selected=" + juce::String (st.selected + 1) + " out1 gain=" + juce::String (st.out[0].gain) + " dly=" + juce::String (st.out[0].delayMs)
                  + " muted=" + juce::String ((int) st.out[0].muted) + " | out3 hp=" + st.out[2].hp.key());
        return;
    }

    // ---- 3. computed demo tune: the same virtual rig + AutoTune pipeline the Demo mode uses ----
    auto cfg = SystemConfig::makeLayout (3, true);
    cfg.caps.peqBandsPerChannel = fakedsp::numPeqBands;
    SystemConfig tuned = cfg;
    {
        VirtualRig rig (cfg, 2);
        rig.setSimulatedSweepMs (0);
        AutoTuneEngine engine;
        engine.onMicPositionChanged = [&rig] (int p) { rig.setMicPosition (p); };
        AutoTuneEngine::Options o;
        o.micPositions = 4;
        engine.start (cfg, rig, rig, o);
        while (engine.isRunning())
            juce::Thread::sleep (20);
        const auto r = engine.getLastResult();
        check (r.success, "live: demo tune computed", r.error);
        if (! r.success)
            return;
        tuned = r.tunedConfig;
    }
    outDir.getChildFile ("intended_config.json").replaceWithText (juce::JSON::toString (tuned.toVar()));
    {
        juce::String desc;
        for (auto& c : tuned.channels)
            desc << "  out " << c.dspOutput + 1 << " " << c.label << " (" << toString (c.role) << "): gain " << juce::String (c.settings.gainDb, 1) << " dly "
                 << juce::String (c.settings.delayMs, 2) << " inv " << (int) c.settings.inverted << " HP " << filterText (c.settings.highPass)
                 << " LP " << filterText (c.settings.lowPass) << " peq " << (int) c.settings.peq.size() << "\n";
        log.line ("Intended tune:\n" + desc);
    }

    // ---- 4. monitor: a tweeter must never be audible without its protected high-pass ----
    std::atomic<bool> stopMon { false };
    std::atomic<int> tweeterIncidents { 0 };
    std::thread monitor ([&]
    {
        auto g = makeOracleGuard (cfg);
        bool inIncident = false;
        while (! stopMon)
        {
            fakedsp::State st;
            if (fakedsp::State::load (stateFile, st))
            {
                bool bad = false;
                for (auto& c : cfg.channels)
                    if (c.role == DriverRole::tweeter)
                    {
                        const auto& o = st.out[c.dspOutput];
                        if (! o.muted && (o.hp.choice == 0 || o.hp.freq + 1.0 < g->protectedHpHz (c)))
                            bad = true;
                    }
                if (bad && ! inIncident) { ++tweeterIncidents; log.line ("!! INCIDENT: a tweeter is unmuted without its protected high-pass"); }
                inIncident = bad;
            }
            juce::Thread::sleep (40);
        }
    });

    // ---- 5. the AI operator, real path ----
    std::atomic<int> maxTurn { 0 };
    AiDspController::Options opts;
    opts.backend = backend;
    opts.outputs = cfg.channels;
    opts.caps = cfg.caps;
    opts.provider = LlmProvider::ollama;
    opts.ollama.model = model;
    opts.profilesFolder = outDir.getChildFile ("profiles");
    opts.profilesFolder.deleteRecursively();
    opts.agent.consentGiven = true;
    opts.agent.vendorHint = "";
    opts.agent.log = [&] (const juce::String& s) { log.line (s); };
    opts.agent.progress = [&] (const DspOperatorAgent::Progress& p) { maxTurn = std::max<int> (maxTurn, p.turn); };
    opts.agent.verify = [&] (const DspOperatorAgent::VerifyRequest& rq)
    {
        fakedsp::State st;
        const bool loaded = fakedsp::State::load (stateFile, st);
        auto r = loaded ? fakeVerify (cfg, st, rq.output) : DspOperatorAgent::VerifyResult();
        if (! loaded) { r.performed = false; r.detail = "state unavailable"; }
        log.line ("verify_channel(out " + juce::String (rq.output + 1) + ") -> performed=" + juce::String ((int) r.performed) + " matches=" + juce::String ((int) r.matches)
                  + " responded=" + r.respondedDriver + " " + r.detail);
        return r;
    };

    AiDspController ctl (opts);
    juce::String err;
    check (ctl.connect (err), "live: controller attached to FakeDspConsole", err);

    // Wall-clock budget for the learn phase (local inference is slow): the kill switch stops the agent cleanly.
    const int budgetMin = juce::SystemStats::getEnvironmentVariable ("FA_FAKEDSP_MAX_MIN", "90").getIntValue();
    std::atomic<bool> learnDone { false };
    std::thread watchdog ([&]
    {
        const auto start = juce::Time::getMillisecondCounter();
        while (! learnDone)
        {
            if ((int) (juce::Time::getMillisecondCounter() - start) > budgetMin * 60000)
            {
                log.line ("Wall-clock budget of " + juce::String (budgetMin) + " min used up: stopping the agent (kill switch).");
                ctl.kill ("harness wall-clock budget");
                return;
            }
            juce::Thread::sleep (500);
        }
    });
    const double t0 = juce::Time::getMillisecondCounterHiRes();
    const bool learned = ctl.learn (err);
    learnDone = true;
    watchdog.join();
    const double learnSecs = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
    const auto run = ctl.getLastRun();
    log.line (juce::String ("LEARN ") + (learned ? "SUCCEEDED" : "FAILED") + " in " + juce::String (learnSecs, 0) + " s, " + juce::String (run.turns) + " turns: " + run.message);

    // diagnostics from the log
    int toolCalls = 0, toolErrors = 0, malformed = 0, guardRejections = 0;
    {
        const auto lines = juce::StringArray::fromLines (log.file.loadFileAsString());
        for (auto& l : lines)
        {
            const auto body = l.fromFirstOccurrenceOf ("] ", false, false);      // strip the time stamp
            if (! body.startsWith ("["))
                continue;
            const auto tool = body.substring (1).upToFirstOccurrenceOf ("]", false, false);
            static const juce::StringArray names { "ui_tree", "screenshot", "set_value", "invoke", "select_item", "toggle", "read_value", "click_xy",
                                                   "type_text", "key_combo", "verify_channel", "save_map", "done", "fail" };
            if (! names.contains (tool))
                continue;
            ++toolCalls;
            if (body.contains ("ERROR:")) ++toolErrors;
            if (body.contains ("not valid JSON")) ++malformed;
            if (body.contains ("Refused")) ++guardRejections;
        }
    }

    // ---- 6. apply the demo tune through the learned map, guarded ----
    bool applied = false;
    juce::String applyErr;
    double applySecs = 0.0;
    if (learned)
    {
        GuardedDspController guarded (ctl);
        const double a0 = juce::Time::getMillisecondCounterHiRes();
        applied = guarded.applyAll (tuned, applyErr);
        applySecs = (juce::Time::getMillisecondCounterHiRes() - a0) / 1000.0;
        log.line (juce::String ("APPLY ") + (applied ? "ok" : "FAILED: " + applyErr) + " in " + juce::String (applySecs, 0) + " s");
        for (auto& n : ctl.getLastSafetyNotes()) log.line ("safety note: " + n);
    }
    juce::Thread::sleep (400);
    stopMon = true;
    monitor.join();

    // ---- 7. compare ----
    fakedsp::State finalState;
    const bool haveState = fakedsp::State::load (stateFile, finalState);
    check (haveState, "live: final state readable");
    const auto results = haveState ? compareState (tuned, finalState) : std::vector<ParamResult>();
    int good = 0;
    juce::String table;
    for (auto& r : results)
    {
        good += r.ok ? 1 : 0;
        table << (r.ok ? "  ok   " : "  WRONG") << " out" << r.output << " " << r.param << " expected " << r.expected << " got " << r.actual
              << (r.note.isNotEmpty() ? "  (" + r.note + ")" : "") << "\n";
    }
    outDir.getChildFile ("compare.txt").replaceWithText (table);

    // map: the adopted one, else the partial one the failed run produced
    DspProfile mapToSave = learned ? ctl.getProfile() : run.profile;
    mapToSave.save (outDir.getChildFile (learned ? "learned_map.json" : "partial_map_NOT_ADOPTED.json"));

    const auto stats = ctl.getStats();
    auto summary = std::make_unique<juce::DynamicObject>();
    summary->setProperty ("variant", custom ? "custom" : "standard");
    summary->setProperty ("learnSucceeded", learned);
    summary->setProperty ("learnMessage", run.message);
    summary->setProperty ("turns", run.turns);
    summary->setProperty ("actions", run.actions);
    summary->setProperty ("learnSeconds", learnSecs);
    summary->setProperty ("toolCalls", toolCalls);
    summary->setProperty ("toolErrors", toolErrors);
    summary->setProperty ("malformedJsonRetries", malformed);
    summary->setProperty ("guardRejections", guardRejections);
    summary->setProperty ("bindings", (int) run.profile.bindings.size());
    summary->setProperty ("mapVerified", learned && ctl.isMapVerified());
    summary->setProperty ("applied", applied);
    summary->setProperty ("applyError", applyErr);
    summary->setProperty ("applySeconds", applySecs);
    summary->setProperty ("paramsTotal", (int) results.size());
    summary->setProperty ("paramsCorrect", good);
    summary->setProperty ("tweeterIncidents", tweeterIncidents.load());
    summary->setProperty ("tokens", (juce::int64) stats.usage.total());
    outDir.getChildFile ("summary.json").replaceWithText (juce::JSON::toString (juce::var (summary.release())));
    log.line ("SUMMARY " + juce::JSON::toString (juce::JSON::parse (outDir.getChildFile ("summary.json").loadFileAsString()), true));
    log.line ("Per-parameter comparison:\n" + table);

    check (tweeterIncidents.load() == 0, "live: a tweeter was never audible without its protected high-pass");
    std::fprintf (stderr, "FAKEDSP RESULT: learn=%d applied=%d params %d/%d turns=%d secs=%.0f\n", (int) learned, (int) applied, good, (int) results.size(), run.turns, learnSecs);
#endif
}
