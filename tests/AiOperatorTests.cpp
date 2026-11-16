// Tests for the AI DSP operator: Claude client, tool loop (mock transport), SafetyGuard, map save/load,
// deterministic replay and self-heal through a fake window. No network, no real API key.
#include "TestRegistry.h"
#include "core/ai/ApiKeyStore.h"
#include "core/ai/ClaudeClient.h"
#include "core/ai/DspOperatorAgent.h"
#include "core/ai/OllamaClient.h"
#include "core/ai/SafetyGuard.h"
#include "core/bridge/AiDspController.h"

#include "core/ai/AiProfileStore.h"

#include <atomic>
#include <cstdlib>
#include <deque>
#include <thread>

using fa::test::check;
using namespace fa;

namespace
{
//==============================================================================
struct MockTransport : ClaudeTransport
{
    std::deque<HttpResponse> queue;
    std::vector<juce::String> bodies;
    std::vector<juce::StringPairArray> headers;

    HttpResponse post (const juce::String&, const juce::StringPairArray& h, const juce::String& body, int) override
    {
        bodies.push_back (body);
        headers.push_back (h);
        if (queue.empty())
        {
            HttpResponse r;
            r.networkError = "mock: no response queued";
            return r;
        }
        auto r = queue.front();
        queue.pop_front();
        return r;
    }
};

juce::var obj (std::initializer_list<std::pair<const char*, juce::var>> items)
{
    auto* o = new juce::DynamicObject();
    for (auto& [k, v] : items)
        o->setProperty (k, v);
    return juce::var (o);
}

juce::var arr (std::initializer_list<juce::var> items)
{
    juce::Array<juce::var> a;
    for (auto& i : items)
        a.add (i);
    return juce::var (a);
}

juce::var toolUse (const juce::String& id, const juce::String& name, const juce::var& input)
{
    return obj ({ { "type", "tool_use" }, { "id", id }, { "name", name }, { "input", input } });
}

HttpResponse ok200 (const juce::String& stopReason, const juce::var& content, int in = 1000, int out = 200,
                    const juce::String& refusalCategory = {})
{
    auto m = obj ({ { "id", "msg_1" }, { "type", "message" }, { "role", "assistant" }, { "model", "claude-opus-5-5" },
                    { "content", content }, { "stop_reason", stopReason },
                    { "usage", obj ({ { "input_tokens", in }, { "output_tokens", out },
                                      { "cache_creation_input_tokens", 0 }, { "cache_read_input_tokens", 500 } }) } });
    if (refusalCategory.isNotEmpty())
        m.getDynamicObject()->setProperty ("stop_details", obj ({ { "category", refusalCategory } }));
    HttpResponse r;
    r.status = 200;
    r.body = juce::JSON::toString (m, true);
    return r;
}

HttpResponse err (int status, const juce::String& type, const juce::String& msg, const juce::String& retryAfter = {})
{
    HttpResponse r;
    r.status = status;
    r.retryAfter = retryAfter;
    r.body = juce::JSON::toString (obj ({ { "type", "error" }, { "error", obj ({ { "type", type }, { "message", msg } }) } }), true);
    return r;
}

ClaudeClient makeClient (std::shared_ptr<MockTransport> t, std::vector<int>* sleeps = nullptr)
{
    ClaudeClient::Config cfg;
    cfg.baseBackoffMs = 10;
    return ClaudeClient (cfg, t, [] { return juce::String ("sk-ant-test-0123456789abcdef"); },
                         [sleeps] (int ms) { if (sleeps != nullptr) sleeps->push_back (ms); });
}

//==============================================================================
/** A fake vendor window: a few edits, toggles and buttons at fixed positions. */
struct FakeWindow : WindowBackend
{
    struct El
    {
        juce::String type, name, value;
        bool toggle = false, isToggle = false, isValue = false, isInvoke = false;
        juce::Rectangle<int> bounds;       // screen coordinates
        bool ignoreWrites = false;         // simulates a control that does not take the value
        uia::ElementPath path;
    };

    juce::Rectangle<int> win { 100, 100, 800, 600 };
    std::vector<El> els;
    juce::StringArray log;               // every write, in order
    int screenshots = 0, clicks = 0;
    bool attached = true;
    juce::String title = "Fake DSP Studio - project 1";

    El& add (const juce::String& type, const juce::String& name, const juce::String& value, int x, int y)
    {
        El e;
        e.type = type; e.name = name; e.value = value;
        e.isValue = type == "Edit";
        e.isToggle = type == "CheckBox";
        e.isInvoke = type == "Button";
        e.bounds = { win.getX() + x, win.getY() + y, 80, 22 };
        uia::PathStep st;
        st.controlType = type; st.automationId = name; st.name = name; st.index = 0;
        e.path.steps.push_back (st);
        els.push_back (e);
        return els.back();
    }

    El* byAuto (const juce::String& id)
    {
        for (auto& e : els)
            if (e.name == id) return &e;
        return nullptr;
    }
    El* byPath (const uia::ElementPath& p)
    {
        if (p.steps.size() != 1) return nullptr;
        return byAuto (p.steps[0].automationId);
    }

    bool isAttached() const override { return attached; }
    juce::Rectangle<int> windowBounds() const override { return win; }
    juce::String windowTitle() const override { return title; }

    std::vector<uia::TreeNode> snapshot (int, int) override
    {
        std::vector<uia::TreeNode> out;
        for (auto& e : els)
        {
            uia::TreeNode n;
            n.info.valid = true;
            n.info.controlType = e.type; n.info.name = e.name; n.info.automationId = e.name;
            n.info.value = e.value; n.info.screenBounds = e.bounds; n.info.path = e.path;
            n.info.supportsValue = e.isValue; n.info.supportsToggle = e.isToggle; n.info.supportsInvoke = e.isInvoke;
            out.push_back (n);
        }
        return out;
    }
    bool find (const uia::ElementPath& p, uia::ElementInfo& out) override
    {
        auto* e = byPath (p);
        if (e == nullptr) return false;
        out.valid = true; out.screenBounds = e->bounds; out.path = p; out.controlType = e->type;
        return true;
    }
    bool setText (const uia::ElementPath& p, const juce::String& t, juce::String& er) override
    {
        auto* e = byPath (p);
        if (e == nullptr) { er = "missing"; return false; }
        log.add ("set " + e->name + "=" + t);
        if (! e->ignoreWrites) e->value = t;
        return true;
    }
    bool setRange (const uia::ElementPath& p, double v, juce::String& er) override { return setText (p, juce::String (v), er); }
    bool setToggle (const uia::ElementPath& p, bool on, juce::String& er) override
    {
        auto* e = byPath (p);
        if (e == nullptr) { er = "missing"; return false; }
        log.add ("toggle " + e->name + "=" + juce::String (on ? 1 : 0));
        if (! e->ignoreWrites) e->toggle = on;
        return true;
    }
    bool invoke (const uia::ElementPath& p, juce::String& er) override
    {
        auto* e = byPath (p);
        if (e == nullptr) { er = "missing"; return false; }
        log.add ("invoke " + e->name);
        return true;
    }
    bool selectItem (const uia::ElementPath& p, const juce::String& t, juce::String& er) override { return setText (p, t, er); }
    juce::String readValue (const uia::ElementPath& p) override { auto* e = byPath (p); return e != nullptr ? e->value : juce::String(); }
    bool readToggle (const uia::ElementPath& p, bool& s) override
    {
        auto* e = byPath (p);
        if (e == nullptr || ! e->isToggle) return false;
        s = e->toggle;
        return true;
    }
    bool foreground = true;              // false: the window is not the foreground window, raw input must not be sent
    juce::Rectangle<int> client;         // empty = whole window; otherwise the client area (title bar excluded)
    juce::Rectangle<int> clientBounds() const override { return client.isEmpty() ? win : client; }
    bool clickAt (juce::Point<int>, bool, juce::String& er) override
    {
        if (! foreground) { er = "the DSP window is not the foreground window"; return false; }
        ++clicks; log.add ("click");
        return true;
    }
    bool typeText (const juce::String& t, bool, juce::String& er) override
    {
        if (! foreground) { er = "the DSP window is not the foreground window"; return false; }
        log.add ("type " + t);
        return true;
    }
    bool keyCombo (const juce::String& k, juce::String& er) override
    {
        if (! foreground) { er = "the DSP window is not the foreground window"; return false; }
        log.add ("key " + k);
        return true;
    }
    bool screenshot (int, WindowScreenshot& out) override { ++screenshots; out.base64 = "AAAA"; out.width = 800; out.height = 600; out.scale = 1.0; return true; }

    int writeIndex (const juce::String& entry) const { return log.indexOf (entry); }
};

OutputChannel makeChannel (int output, DriverRole role, const juce::String& id)
{
    OutputChannel ch;
    ch.id = id;
    ch.label = id;
    ch.role = role;
    ch.dspOutput = output;
    return ch;
}

/** Window with controls for outputs 1 (midbass) and 2 (tweeter). Element order fixes ui_tree ids. */
void buildWindow (FakeWindow& w)
{
    for (int c = 1; c <= 2; ++c)
    {
        const int y = 10 + (c - 1) * 200;
        w.add ("Edit", "Gain" + juce::String (c), "0.0", 10, y);
        w.add ("Edit", "Delay" + juce::String (c), "0.00", 10, y + 30);
        w.add ("CheckBox", "Phase" + juce::String (c), "", 10, y + 60);
        w.add ("CheckBox", "Mute" + juce::String (c), "", 10, y + 90);
        w.add ("Edit", "HPFreq" + juce::String (c), "20", 10, y + 120);
        w.add ("Edit", "LPFreq" + juce::String (c), "20000", 10, y + 150);
    }
    w.add ("Edit", "HPType1", "", 200, 130);        // ids 13, 14: high-pass type combos (after the loop so earlier ids stay put)
    w.add ("Edit", "HPType2", "", 200, 330);
    w.add ("Button", "Select1", "", 300, 10);       // ids 15, 16: channel-select buttons (select-then-edit maps)
    w.add ("Button", "Select2", "", 300, 210);
}

ControlBinding uiaBinding (FakeWindow& w, const juce::String& name, ControlBinding::Method m, int decimals = 2)
{
    ControlBinding b;
    b.method = m;
    b.path = w.byAuto (name)->path;
    b.decimals = decimals;
    b.settleMs = 0;
    return b;
}

DspProfile makeProfile (FakeWindow& w)
{
    using M = ControlBinding::Method;
    DspProfile p;
    p.name = "Fake DSP";
    p.aiLearned = true;
    p.windowTitleContains = "Fake DSP Studio";
    p.caps.peqBandsPerChannel = 0;
    for (int c = 0; c < 2; ++c)
    {
        const auto n = juce::String (c + 1);
        p.bindings[DspProfile::keyFor ("gain", c)]     = uiaBinding (w, "Gain" + n, M::uiaText, 1);
        p.bindings[DspProfile::keyFor ("delay", c)]    = uiaBinding (w, "Delay" + n, M::uiaText, 2);
        p.bindings[DspProfile::keyFor ("polarity", c)] = uiaBinding (w, "Phase" + n, M::uiaToggle);
        p.bindings[DspProfile::keyFor ("mute", c)]     = uiaBinding (w, "Mute" + n, M::uiaToggle);
        p.bindings[DspProfile::keyFor ("hp.freq", c)]  = uiaBinding (w, "HPFreq" + n, M::uiaText, 0);
        p.bindings[DspProfile::keyFor ("lp.freq", c)]  = uiaBinding (w, "LPFreq" + n, M::uiaText, 0);
        p.bindings[DspProfile::keyFor ("hp.type", c)]  = uiaBinding (w, "HPType" + n, M::uiaSelect, 0);
    }
    return p;
}

juce::File tempFolder (const juce::String& name)
{
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("fa_ai_tests").getChildFile (name);
    f.deleteRecursively();
    f.createDirectory();
    return f;
}

struct Rig
{
    std::shared_ptr<FakeWindow> win = std::make_shared<FakeWindow>();
    std::shared_ptr<MockTransport> transport = std::make_shared<MockTransport>();
    juce::File folder;
    std::vector<OutputChannel> outputs;
    AiDspController::Options opts;

    explicit Rig (const juce::String& name)
    {
        buildWindow (*win);
        folder = tempFolder (name);
        outputs = { makeChannel (0, DriverRole::midbass, "FL.midbass"), makeChannel (1, DriverRole::tweeter, "FL.tweeter") };
        opts.backend = win;
        opts.transport = transport;
        opts.apiKey = [] { return juce::String ("sk-ant-test-0123456789abcdef"); };
        opts.sleep = [] (int) {};
        opts.outputs = outputs;
        opts.profilesFolder = folder;
        opts.settleMs = 0;
        opts.requireVerifiedMap = false;        // most tests drive the replay with a hand-made map; the verification tests turn it on
        opts.agent.consentGiven = true;
        opts.agent.settleMs = 0;
        opts.client.baseBackoffMs = 1;
    }
};
} // namespace

//==============================================================================
FA_TEST (aiClientRequestShape)
{
    auto t = std::make_shared<MockTransport>();
    t->queue.push_back (ok200 ("end_turn", arr ({ obj ({ { "type", "text" }, { "text", "hi" } }) })));
    auto c = makeClient (t);
    juce::Array<juce::var> tools;
    tools.add (obj ({ { "name", "a" }, { "input_schema", obj ({ { "type", "object" } }) } }));
    tools.add (obj ({ { "name", "b" }, { "input_schema", obj ({ { "type", "object" } }) } }));
    const auto r = c.createMessage ("system text", juce::var (tools),
                                    arr ({ obj ({ { "role", "user" }, { "content", "go" } }) }));
    check (r.ok && r.stopReason == "end_turn", "client: 200 parsed");
    const auto body = juce::JSON::parse (t->bodies[0]);
    check (body["model"].toString() == "claude-opus-5-5", "client: model claude-opus-5-5");
    check (! body.hasProperty ("thinking"), "client: no thinking field (adaptive default, never disabled/budget)");
    check (body["output_config"]["effort"].toString() == "medium", "client: effort set explicitly");
    check (body["tool_choice"]["type"].toString() == "auto", "client: tool_choice auto (forced 400s)");
    check (body["system"][0].hasProperty ("cache_control"), "client: system prompt cached");
    check (body["tools"][1].hasProperty ("cache_control") && ! body["tools"][0].hasProperty ("cache_control"),
           "client: cache breakpoint on last tool only");
    check (body["messages"][body["messages"].size() - 1]["role"].toString() == "user", "client: no assistant prefill");
    check (t->headers[0]["anthropic-version"].isNotEmpty() && t->headers[0]["x-api-key"].isNotEmpty(), "client: headers");
    check (r.usage.cacheReadTokens == 500 && r.costUsd > 0.0, "client: usage and cost tracked", juce::String (r.costUsd, 6));
}

FA_TEST (aiClientRetries)
{
    auto t = std::make_shared<MockTransport>();
    t->queue.push_back (err (429, "rate_limit_error", "slow down", "2"));
    t->queue.push_back (err (529, "overloaded_error", "busy"));
    t->queue.push_back (ok200 ("end_turn", arr ({})));
    std::vector<int> sleeps;
    auto c = makeClient (t, &sleeps);
    auto r = c.createMessage ("s", juce::var(), arr ({ obj ({ { "role", "user" }, { "content", "x" } }) }));
    check (r.ok && r.attempts == 3, "client: 429 and 529 retried then succeeds", juce::String (r.attempts));
    check (sleeps.size() == 2 && sleeps[0] >= 2000, "client: Retry-After honoured", sleeps.empty() ? "" : juce::String (sleeps[0]));

    auto t2 = std::make_shared<MockTransport>();
    t2->queue.push_back (err (400, "invalid_request_error", "bad"));
    t2->queue.push_back (ok200 ("end_turn", arr ({})));
    auto c2 = makeClient (t2);
    r = c2.createMessage ("s", juce::var(), arr ({ obj ({ { "role", "user" }, { "content", "x" } }) }));
    check (! r.ok && r.attempts == 1 && r.error.contains ("bad"), "client: 400 not retried");

    auto t3 = std::make_shared<MockTransport>();
    for (int i = 0; i < 10; ++i)
        t3->queue.push_back (err (500, "api_error", "boom"));
    auto c3 = makeClient (t3);
    r = c3.createMessage ("s", juce::var(), arr ({ obj ({ { "role", "user" }, { "content", "x" } }) }));
    check (! r.ok && r.attempts == 6, "client: gives up after maxRetries", juce::String (r.attempts));

    ClaudeClient nokey (ClaudeClient::Config(), t3, [] { return juce::String(); });
    r = nokey.createMessage ("s", juce::var(), juce::var());
    check (! r.ok && r.error.containsIgnoreCase ("key"), "client: missing key reported");
}

//==============================================================================
FA_TEST (aiSafetyGuardRules)
{
    DspCapabilities caps;
    SafetyGuard::Limits lim;
    lim.maxActions = 5;
    lim.maxUsd = 0.10;
    SafetyGuard g (lim, caps);

    auto mid = makeChannel (0, DriverRole::midbass, "mid");
    auto tw = makeChannel (1, DriverRole::tweeter, "tw");
    tw.driver.fsHz = 1000.0;           // protected HP = 1.6 kHz
    g.setChannels ({ mid, tw });

    // protected tweeter HP
    tw.settings.highPass = FilterSpec::crossover (FilterType::highPass, 500.0);
    juce::StringArray notes;
    check (g.sanitizeChannel (tw, notes) && tw.settings.highPass.freq >= 1600.0, "guard: tweeter HP below protected is raised", juce::String (tw.settings.highPass.freq));
    tw.settings.highPass.enabled = false;
    g.sanitizeChannel (tw, notes);
    check (tw.settings.highPass.enabled && tw.settings.highPass.freq >= 1600.0, "guard: disabled tweeter HP is re-enabled");

    // gain step cap (first write capped, then per-step)
    mid.settings.gainDb = 6.0;
    g.sanitizeChannel (mid, notes);
    check (mid.settings.gainDb == 0.0, "guard: first gain write capped when current gain unknown", juce::String (mid.settings.gainDb));
    g.noteGain (0, -10.0);
    mid.settings.gainDb = 2.0;
    g.sanitizeChannel (mid, notes);
    check (std::abs (mid.settings.gainDb - (-7.0)) < 1e-9, "guard: gain change capped per step", juce::String (mid.settings.gainDb));
    mid.settings.gainDb = -80.0;
    g.noteGain (0, -39.0);
    g.sanitizeChannel (mid, notes);
    check (mid.settings.gainDb >= caps.minChannelGainDb, "guard: gain clamped to DSP range");

    // mute before tweeter crossover / polarity change
    auto tw2 = tw;
    check (g.tweeterNeedsMuteFirst (tw, false, tw2), "guard: tweeter with unknown state needs mute first");
    check (! g.tweeterNeedsMuteFirst (tw, true, tw2), "guard: unchanged tweeter does not need mute");
    tw2.settings.inverted = true;
    check (g.tweeterNeedsMuteFirst (tw, true, tw2), "guard: tweeter polarity change needs mute first");
    auto mid2 = mid;
    mid2.settings.inverted = true;
    check (! g.tweeterNeedsMuteFirst (mid, true, mid2), "guard: non-tweeter does not need mute");

    // AI tool path
    check (! g.checkToolWrite ("hp.freq", 1, 800.0), "guard(tool): tweeter hp.freq below protected refused");
    g.noteMute (1, true);
    check (! g.checkToolWrite ("hp.freq", 1, 800.0), "guard(tool): still refused when muted (below protected HP)");
    check ((bool) g.checkToolWrite ("hp.freq", 1, 2500.0), "guard(tool): tweeter hp.freq above protected allowed when muted");
    g.noteMute (1, false);
    check (! g.checkToolWrite ("hp.freq", 1, 2500.0), "guard(tool): tweeter crossover needs mute first");
    check (! g.checkToolWrite ("polarity", 1, 1.0), "guard(tool): tweeter polarity needs mute first");
    check ((bool) g.checkToolWrite ("hp.freq", 0, 80.0), "guard(tool): midbass hp allowed");
    check (! g.checkToolWrite ("secret.param", 0, 1.0), "guard(tool): undeclared param refused");
    check (! g.checkToolWrite ("gain", 7, -3.0), "guard(tool): unknown output refused");
    check (! g.checkToolWrite ("gain", 0, 5.0), "guard(tool): first gain above cap refused");
    g.noteGain (0, -3.0);
    check ((bool) g.checkToolWrite ("gain", 0, -1.0) && ! g.checkToolWrite ("gain", 0, 6.0), "guard(tool): gain step cap");

    // geometry
    const juce::Rectangle<int> win (100, 100, 800, 600);
    check ((bool) SafetyGuard::checkPointInWindow ({ 10, 10 }, win), "guard: point inside window ok");
    check (! SafetyGuard::checkPointInWindow ({ -1, 10 }, win) && ! SafetyGuard::checkPointInWindow ({ 800, 10 }, win)
           && ! SafetyGuard::checkPointInWindow ({ 10, 600 }, win), "guard: points outside window refused");
    check (! SafetyGuard::checkElementInWindow ({ 2000, 2000, 50, 20 }, win), "guard: element outside window refused");
    check (! SafetyGuard::checkKeyCombo ("alt+F4") && ! SafetyGuard::checkKeyCombo ("ctrl+w") && ! SafetyGuard::checkKeyCombo ("win+r")
           && (bool) SafetyGuard::checkKeyCombo ("ctrl+a") && (bool) SafetyGuard::checkKeyCombo ("Return"), "guard: dangerous key combos refused");

    // budgets and kill switch
    for (int i = 0; i < 5; ++i)
        check ((bool) g.beginAction ("x"), "guard: action within budget");
    check (! g.beginAction ("x"), "guard: action budget enforced");
    g.resetSession();
    ClaudeUsage u;
    u.inputTokens = 100;
    g.addUsage (u, 0.2);
    check (! g.checkBudget(), "guard: dollar budget enforced");
    g.resetSession();
    check ((bool) g.checkBudget(), "guard: reset clears budget");
    g.kill ("test");
    check (! g.beginAction ("x") && g.isKilled(), "guard: kill switch blocks actions");
}

//==============================================================================
FA_TEST (aiAgentToolLoop)
{
    Rig rig ("loop");
    auto& t = *rig.transport;
    const juce::String thinkingSig = "SIG-THINKING-123";

    // Turn 1: thinking block + ui_tree + screenshot
    t.queue.push_back (ok200 ("tool_use", arr ({ obj ({ { "type", "thinking" }, { "thinking", "" }, { "signature", thinkingSig } }),
                                                 toolUse ("tu1", "ui_tree", obj ({})), toolUse ("tu2", "screenshot", obj ({})) })));
    // Turn 2: a legal gain write, an illegal tweeter HP (unmuted + low), a click outside the window, an undeclared param
    t.queue.push_back (ok200 ("tool_use", arr ({
        toolUse ("tu3", "set_value", obj ({ { "element_id", 1 }, { "value", "-3" }, { "param", "gain" }, { "output", 1 } })),
        toolUse ("tu4", "set_value", obj ({ { "element_id", 11 }, { "value", "100" }, { "param", "hp.freq" }, { "output", 2 } })),
        toolUse ("tu5", "click_xy", obj ({ { "x", 5000 }, { "y", 10 }, { "param", "nav" } })),
        toolUse ("tu6", "set_value", obj ({ { "element_id", 1 }, { "value", "1" }, { "param", "banana" }, { "output", 1 } })) })));
    // Turn 3: save_map
    t.queue.push_back (ok200 ("tool_use", arr ({ toolUse ("tu7", "save_map", obj ({
        { "dsp_name", "Fake DSP" }, { "window_title", "Fake DSP Studio" }, { "notes", "gain in dB" },
        { "bindings", arr ({ obj ({ { "key", "gain" }, { "output", 1 }, { "method", "text" }, { "element_id", 1 }, { "decimals", 1 } }),
                             obj ({ { "key", "mute" }, { "output", 2 }, { "method", "toggle" }, { "element_id", 10 } }),
                             obj ({ { "key", "bogus" }, { "method", "text" }, { "element_id", 1 } }) }) } })) })));
    // Turn 4: done
    t.queue.push_back (ok200 ("tool_use", arr ({ toolUse ("tu8", "done", obj ({ { "summary", "mapped" } })) })));

    juce::String e;
    AiDspController c (rig.opts);
    check (c.connect (e), "agent: connect ok", e);
    check (! c.hasMap(), "agent: no map before learning");
    const bool learned = c.learn (e);
    check (learned, "agent: learn succeeds through the mocked loop", e);
    check (t.bodies.size() == 4, "agent: four API turns", juce::String ((int) t.bodies.size()));
    check (t.bodies[1].contains (thinkingSig), "agent: thinking block replayed verbatim in the next request");
    check (t.bodies[1].contains ("tool_result") && t.bodies[1].contains ("tu2"), "agent: tool results returned");
    check (t.bodies[2].contains ("Refused by safety rules") , "agent: unsafe tweeter HP write refused and reported to the model");
    check (rig.win->writeIndex ("set HPFreq2=100") < 0, "agent: refused write never reached the window clock");
    check (rig.win->clicks == 0, "agent: click outside the window never sent");
    check (rig.win->writeIndex ("set Gain1=-3") >= 0, "agent: legal write reached the window");
    check (rig.win->screenshots == 1, "agent: screenshot tool used");
    check (c.hasMap() && c.getProfile().bindings.count ("gain@c0") == 1 && c.getProfile().bindings.count ("mute@c1") == 1,
           "agent: save_map created per-output bindings");
    check (c.getProfile().bindings.count ("bogus") == 0, "agent: unknown binding key rejected");
    check (c.getProfile().aiLearned && c.getProfile().notes == "gain in dB", "agent: profile marked AI learned with notes");
    check (c.getStats().costUsd > 0.0 && c.getStats().learnRuns == 1, "agent: cost tracked", juce::String (c.getStats().costUsd, 5));
    check (AiProfileStore (rig.folder).fileFor ("Fake DSP Studio").existsAsFile(), "agent: map saved to disk in the profile format");
}

FA_TEST (aiAgentRefusalAndErrors)
{
    {
        Rig rig ("refusal");
        rig.transport->queue.push_back (ok200 ("refusal", arr ({ toolUse ("t1", "set_value", obj ({ { "element_id", 1 }, { "value", "-1" }, { "param", "gain" }, { "output", 1 } })) }),
                                               100, 10, "cyber"));
        juce::String e;
        AiDspController c (rig.opts);
        c.connect (e);
        check (! c.learn (e) && e.containsIgnoreCase ("declined"), "agent: refusal stop_reason ends the run", e);
        check (rig.win->log.isEmpty(), "agent: tools of a refused turn are never executed");
    }
    {
        Rig rig ("noconsent");
        rig.opts.agent.consentGiven = false;
        juce::String e;
        AiDspController c (rig.opts);
        c.connect (e);
        check (! c.learn (e) && e.containsIgnoreCase ("consent") && rig.transport->bodies.empty(), "agent: no consent, no API call", e);
    }
    {
        Rig rig ("apierr");
        for (int i = 0; i < 8; ++i)
            rig.transport->queue.push_back (err (429, "rate_limit_error", "limit"));
        juce::String e;
        AiDspController c (rig.opts);
        c.connect (e);
        check (! c.learn (e) && e.contains ("API error"), "agent: persistent 429 surfaces as an error", e);
    }
    {
        Rig rig ("budget");
        rig.opts.limits.maxActions = 2;
        auto& t = *rig.transport;
        for (int i = 0; i < 5; ++i)
            t.queue.push_back (ok200 ("tool_use", arr ({ toolUse ("a" + juce::String (i), "ui_tree", obj ({})) })));
        juce::String e;
        AiDspController c (rig.opts);
        c.connect (e);
        check (! c.learn (e) && e.containsIgnoreCase ("budget"), "agent: action budget stops the loop", e);
    }
}

//==============================================================================
FA_TEST (aiMapSaveLoadAndReplay)
{
    Rig rig ("replay");
    // Pre-learned map on disk (as a previous session would have saved it).
    {
        AiProfileStore store (rig.folder);
        check (store.save (makeProfile (*rig.win), "Fake DSP Studio"), "map: save profile");
        DspProfile back;
        check (store.load ("Fake DSP Studio", back) && back.aiLearned && back.bindings.size() == 14
               && back.bindings.count ("hp.freq@c1") == 1, "map: load round-trips every binding");
    }

    juce::String e;
    AiDspController c (rig.opts);
    check (c.connect (e) && c.hasMap(), "replay: connect loads the saved map by window title", e);

    auto mid = rig.outputs[0];
    mid.settings.gainDb = -4.5;
    mid.settings.delayMs = 1.25;
    mid.settings.inverted = true;
    mid.settings.highPass = FilterSpec::crossover (FilterType::highPass, 60.0);
    mid.settings.lowPass = FilterSpec::crossover (FilterType::lowPass, 3000.0);
    auto tw = rig.outputs[1];
    tw.driver.fsHz = 1000.0;
    tw.settings.highPass = FilterSpec::crossover (FilterType::highPass, 400.0);   // unsafe: below protected 1600 Hz
    tw.settings.gainDb = -3.0;
    tw.settings.inverted = true;

    check (c.applyChannel (mid, e), "replay: midbass applied", e);
    check (c.applyChannel (tw, e), "replay: tweeter applied", e);
    auto& w = *rig.win;
    check (w.byAuto ("Gain1")->value.getDoubleValue() == -4.5 && w.byAuto ("Delay1")->value.getDoubleValue() == 1.25
           && w.byAuto ("Phase1")->toggle && w.byAuto ("HPFreq1")->value.getIntValue() == 60 && w.byAuto ("LPFreq1")->value.getIntValue() == 3000,
           "replay: values reach the right controls");
    check (w.byAuto ("HPFreq2")->value.getIntValue() >= 1600, "replay: tweeter HP clamped to the protected frequency", w.byAuto ("HPFreq2")->value);
    check (c.getLastSafetyNotes().size() > 0, "replay: safety notes reported");
    const int muteOn = w.writeIndex ("toggle Mute2=1"), hpSet = w.log.indexOf ("set HPFreq2=" + w.byAuto ("HPFreq2")->value);
    const int muteOff = w.writeIndex ("toggle Mute2=0");
    check (muteOn >= 0 && hpSet > muteOn && muteOff > hpSet, "replay: tweeter muted before crossover/polarity change, unmuted after");
    check (! w.byAuto ("Mute2")->toggle, "replay: tweeter unmuted at the end");
    check (rig.transport->bodies.empty(), "replay: NO API calls when the map works");
    check (c.getStats().verifiedWrites > 5 && c.getStats().mismatches == 0, "replay: writes were read back", juce::String (c.getStats().verifiedWrites));

    // solo: mutes everything except the listed channel
    SystemConfig cfg;
    cfg.channels = rig.outputs;
    check (c.soloChannels (cfg, { "FL.midbass" }, e), "replay: soloChannels", e);
    check (w.byAuto ("Mute2")->toggle && ! w.byAuto ("Mute1")->toggle, "replay: solo mutes the others, unmutes the soloed one");
    check (rig.transport->bodies.empty(), "replay: solo makes no API calls");

    // kill switch
    c.kill();
    check (! c.setMuted (rig.outputs[0], true, e), "replay: kill switch refuses further writes", e);
    c.resetSafety();
    check (c.setMuted (rig.outputs[0], true, e), "replay: reset re-enables writes", e);
}

FA_TEST (aiSelfHealAndReadback)
{
    // A control disappears (renamed): the AI is called once to repair, the map is updated and saved.
    {
        Rig rig ("heal");
        auto p = makeProfile (*rig.win);
        p.bindings["gain@c0"].path.steps[0].automationId = "OldGainName";
        AiProfileStore (rig.folder).save (p, "Fake DSP Studio");

        auto& t = *rig.transport;
        t.queue.push_back (ok200 ("tool_use", arr ({ toolUse ("h1", "ui_tree", obj ({})) })));
        t.queue.push_back (ok200 ("tool_use", arr ({ toolUse ("h2", "save_map", obj ({
            { "bindings", arr ({ obj ({ { "key", "gain" }, { "output", 1 }, { "method", "text" }, { "element_id", 1 }, { "decimals", 1 } }) }) } })) })));
        t.queue.push_back (ok200 ("tool_use", arr ({ toolUse ("h3", "done", obj ({ { "summary", "repaired gain" } })) })));

        juce::String e;
        AiDspController c (rig.opts);
        c.connect (e);
        auto mid = rig.outputs[0];
        mid.settings.gainDb = -2.0;
        check (c.applyChannel (mid, e), "heal: applyChannel succeeds after the AI repairs the map", e);
        check (c.getStats().heals == 1 && t.bodies.size() == 3, "heal: exactly one AI session", juce::String ((int) t.bodies.size()));
        check (t.bodies[0].contains ("gain@c0"), "heal: the failing key is passed to the AI");
        check (rig.win->byAuto ("Gain1")->value.getDoubleValue() == -2.0, "heal: value written after repair");
        DspProfile back;
        AiProfileStore (rig.folder).load ("Fake DSP Studio", back);
        check (back.bindings["gain@c0"].path.steps[0].automationId == "Gain1", "heal: repaired map saved to disk");
    }
    // Replay-only mode never calls the AI.
    {
        Rig rig ("heal_off");
        auto p = makeProfile (*rig.win);
        p.bindings["gain@c0"].path.steps[0].automationId = "OldGainName";
        AiProfileStore (rig.folder).save (p, "Fake DSP Studio");
        rig.opts.allowLearning = false;
        juce::String e;
        AiDspController c (rig.opts);
        c.connect (e);
        auto mid = rig.outputs[0];
        mid.settings.gainDb = -2.0;
        check (! c.applyChannel (mid, e) && e.contains ("not found"), "heal: missing control reported when learning is off", e);
        check (rig.transport->bodies.empty(), "heal: no API call when learning is off");
    }
    // Read-back mismatch is detected.
    {
        Rig rig ("mismatch");
        AiProfileStore (rig.folder).save (makeProfile (*rig.win), "Fake DSP Studio");
        rig.opts.allowLearning = false;
        rig.win->byAuto ("Gain1")->ignoreWrites = true;
        rig.win->byAuto ("Gain1")->value = "0.0";
        juce::String e;
        AiDspController c (rig.opts);
        c.connect (e);
        auto mid = rig.outputs[0];
        mid.settings.gainDb = -2.0;
        check (! c.applyChannel (mid, e) && e.containsIgnoreCase ("mismatch") && c.getStats().mismatches == 1, "readback: mismatch detected", e);
    }
    // A tweeter cannot be changed without a mute control.
    {
        Rig rig ("nomute");
        auto p = makeProfile (*rig.win);
        p.bindings.erase ("mute@c1");
        AiProfileStore (rig.folder).save (p, "Fake DSP Studio");
        rig.opts.allowLearning = false;
        juce::String e;
        AiDspController c (rig.opts);
        c.connect (e);
        auto tw = rig.outputs[1];
        tw.settings.highPass = FilterSpec::crossover (FilterType::highPass, 4000.0);
        check (! c.applyChannel (tw, e) && e.containsIgnoreCase ("muted"), "safety: tweeter crossover refused without a mute control", e);
        check (rig.win->log.isEmpty() || rig.win->writeIndex ("set HPFreq2=4000") < 0, "safety: nothing written to the tweeter");
    }
}

//==============================================================================
FA_TEST (aiApiKeyStore)
{
    auto folder = tempFolder ("keys");
    ApiKeyStore store (folder);
    const juce::String key = "sk-ant-api03-TESTKEY-0123456789abcdefghij";
    juce::String e;

    check (! store.save ("short", e), "key: junk rejected");
    check (store.save (key, e), "key: saved with DPAPI", e);
    const auto raw = folder.getChildFile ("anthropic_key.bin").loadFileAsString();
    check (store.hasStoredKey() && ! raw.contains ("TESTKEY"), "key: file on disk is not plaintext");
    check (store.resolve() == key || juce::SystemStats::getEnvironmentVariable ("ANTHROPIC_API_KEY", {}).isNotEmpty(), "key: stored key round-trips");
    check (ApiKeyStore::maskedHint (key).contains ("...") && ! ApiKeyStore::maskedHint (key).contains ("TESTKEY"), "key: masked hint hides the key");

#if JUCE_WINDOWS
    const auto previous = juce::SystemStats::getEnvironmentVariable ("ANTHROPIC_API_KEY", {});
    _putenv_s ("ANTHROPIC_API_KEY", "sk-ant-from-environment-0123456789");
    check (store.resolve() == "sk-ant-from-environment-0123456789" && store.getSource() == ApiKeyStore::Source::environment,
           "key: environment variable overrides the stored key");
    _putenv_s ("ANTHROPIC_API_KEY", previous.toRawUTF8());
#endif

    store.clear();
    check (! store.hasStoredKey(), "key: cleared");
    check (! store.hasConsent(), "consent: not given by default");
    store.setConsent (true);
    check (store.hasConsent(), "consent: remembered");
    store.setConsent (false);
    check (! store.hasConsent(), "consent: revocable");
}

//==============================================================================
// Ollama provider (local, free). Mocked HTTP; the live smoke test is gated behind FA_OLLAMA_LIVE=1.
namespace
{
HttpResponse ollama200 (const juce::var& toolCalls, const juce::String& text = {}, int promptTokens = 321, int evalTokens = 45,
                        const juce::String& doneReason = "stop")
{
    auto msg = obj ({ { "role", "assistant" }, { "content", text } });
    if (toolCalls.isArray())
        msg.getDynamicObject()->setProperty ("tool_calls", toolCalls);
    auto body = obj ({ { "model", "qwen3-vl:4b" }, { "message", msg }, { "done", true }, { "done_reason", doneReason },
                       { "prompt_eval_count", promptTokens }, { "eval_count", evalTokens } });
    HttpResponse r;
    r.status = 200;
    r.body = juce::JSON::toString (body, true);
    return r;
}

juce::var ollamaCall (const juce::String& name, const juce::var& args)
{
    return obj ({ { "function", obj ({ { "name", name }, { "arguments", args } }) } });
}

AiDspController::Options ollamaOptions (Rig& rig)
{
    auto o = rig.opts;
    o.provider = LlmProvider::ollama;
    o.ollama.model = "qwen3-vl:4b";
    o.apiKey = [] { return juce::String(); };       // no key needed
    o.agent.consentGiven = true;                    // the user must still allow control, even for a local model
    return o;
}
}

FA_TEST (aiOllamaRequestShape)
{
    auto t = std::make_shared<MockTransport>();
    t->queue.push_back (ollama200 (arr ({ ollamaCall ("ui_tree", obj ({})) })));
    OllamaClient::Config cfg;
    cfg.model = "qwen3-vl:4b";
    cfg.baseUrl = "http://localhost:11434/";
    OllamaClient c (cfg, t);

    juce::Array<juce::var> tools;
    tools.add (obj ({ { "name", "ui_tree" }, { "description", "tree" }, { "input_schema", obj ({ { "type", "object" } }) } }));
    const auto imgBlock = obj ({ { "type", "image" }, { "source", obj ({ { "type", "base64" }, { "media_type", "image/png" }, { "data", "QUJD" } }) } });
    const auto resultContent = arr ({ obj ({ { "type", "text" }, { "text", "shot 10x10" } }), imgBlock });
    const auto toolResult = obj ({ { "type", "tool_result" }, { "tool_use_id", "ollama_1" }, { "content", resultContent } });
    auto messages = arr ({ obj ({ { "role", "user" }, { "content", "go" } }),
                           obj ({ { "role", "assistant" }, { "content", arr ({ toolUse ("ollama_1", "screenshot", obj ({})) }) } }),
                           obj ({ { "role", "user" }, { "content", arr ({ toolResult }) } }) });
    const auto r = c.createMessage ("sys", juce::var (tools), messages);

    check (r.ok && r.stopReason == "tool_use" && r.costUsd == 0.0, "ollama: 200 parsed, stop reason tool_use, cost 0");
    check (t->bodies.size() == 1 && t->headers[0]["x-api-key"].isEmpty(), "ollama: no API key header");
    const auto b = juce::JSON::parse (t->bodies[0]);
    check (b["model"].toString() == "qwen3-vl:4b" && ! (bool) b["stream"], "ollama: model and stream=false");
    check (b["tools"][0]["type"].toString() == "function" && b["tools"][0]["function"]["name"].toString() == "ui_tree"
           && b["tools"][0]["function"]["parameters"]["type"].toString() == "object", "ollama: tools in function format");
    check (b["messages"][0]["role"].toString() == "system" && b["messages"][0]["content"].toString() == "sys", "ollama: system message first");
    check (b["messages"][2]["tool_calls"][0]["function"]["name"].toString() == "screenshot", "ollama: assistant tool_use becomes tool_calls");
    check (b["messages"][3]["role"].toString() == "tool" && b["messages"][3]["tool_name"].toString() == "screenshot"
           && b["messages"][3]["content"].toString() == "shot 10x10", "ollama: tool_result becomes a tool message with tool_name");
    check (b["messages"][4]["role"].toString() == "user" && b["messages"][4]["images"][0].toString() == "QUJD",
           "ollama: screenshot travels as base64 in images of a user message");
    check ((int) b["options"]["num_ctx"] >= 8192, "ollama: context window raised above the default");
    check (OllamaClient::chatUrl ("localhost:11434/") == "http://localhost:11434/api/chat", "ollama: url normalised");
}

FA_TEST (aiOllamaResponseParsing)
{
    LlmResponse r;
    const auto body = ollama200 (arr ({ ollamaCall ("set_value", obj ({ { "element_id", 3 }, { "value", "-2" } })),
                                         ollamaCall ("read_value", juce::var ("{\"element_id\": 4}")),
                                         ollamaCall ("toggle", juce::var ("{oops")) }), "thinking aloud", 700, 90).body;
    check (OllamaClient::parseResponse (body, r), "ollama parse: ok");
    check (r.stopReason == "tool_use" && r.usage.inputTokens == 700 && r.usage.outputTokens == 90 && r.costUsd == 0.0,
           "ollama parse: stop reason, tokens counted, free");
    auto* a = r.content.getArray();
    check (a != nullptr && a->size() == 4 && (*a)[0]["type"].toString() == "text", "ollama parse: text block first");
    check ((*a)[1]["type"].toString() == "tool_use" && (*a)[1]["name"].toString() == "set_value"
           && (int) (*a)[1]["input"]["element_id"] == 3 && (*a)[1]["id"].toString().isNotEmpty(), "ollama parse: object arguments become input");
    check ((int) (*a)[2]["input"]["element_id"] == 4, "ollama parse: JSON-string arguments are parsed");
    check ((*a)[3]["input"].hasProperty ("_malformed"), "ollama parse: unparseable arguments flagged");
    check ((*a)[1]["id"].toString() != (*a)[2]["id"].toString(), "ollama parse: unique tool_use ids");

    LlmResponse len;
    OllamaClient::parseResponse (ollama200 ({}, "cut", 1, 1, "length").body, len);
    check (len.stopReason == "max_tokens", "ollama parse: length -> max_tokens");

    // connection failure and missing model
    auto t = std::make_shared<MockTransport>();
    OllamaClient c ({}, t);
    auto e1 = c.createMessage ("s", arr ({}), arr ({ obj ({ { "role", "user" }, { "content", "x" } }) }));
    check (! e1.ok && e1.error.contains ("Ollama not running"), "ollama: connection failure is a clear error", e1.error);
    HttpResponse nf;
    nf.status = 404;
    nf.body = "{\"error\":\"model not found\"}";
    t->queue.push_back (nf);
    auto e2 = c.createMessage ("s", arr ({}), arr ({ obj ({ { "role", "user" }, { "content", "x" } }) }));
    check (! e2.ok && e2.error.contains ("ollama pull"), "ollama: missing model tells the user to pull it", e2.error);
}

FA_TEST (aiOllamaMalformedArgsRetry)
{
    Rig rig ("ollama_retry");
    auto& t = *rig.transport;
    t.queue.push_back (ollama200 (arr ({ ollamaCall ("ui_tree", juce::var ("{not json")) })));                                // malformed string
    t.queue.push_back (ollama200 (arr ({ ollamaCall ("set_value", obj ({ { "element_id", "1" }, { "value", "-3" } })) })));   // missing param/output
    t.queue.push_back (ollama200 (arr ({ ollamaCall ("ui_tree", obj ({})) })));
    t.queue.push_back (ollama200 (arr ({ ollamaCall ("save_map", obj ({
        { "bindings", arr ({ obj ({ { "key", "gain" }, { "output", 1 }, { "method", "text" }, { "element_id", 1 } }) }) } })) })));
    t.queue.push_back (ollama200 (arr ({ ollamaCall ("done", obj ({ { "summary", "ok" } })) })));

    AiDspController c (ollamaOptions (rig));
    juce::String e;
    check (c.connect (e), "ollama agent: connect", e);
    check (c.makeLlmClient()->providerName() == "ollama", "ollama agent: provider is ollama");
    const bool ok = c.learn (e);
    check (ok, "ollama agent: learns after retries", e);
    check (t.bodies.size() == 5, "ollama agent: five turns", juce::String ((int) t.bodies.size()));
    check (t.bodies.size() > 2 && t.bodies[1].contains ("not valid JSON"), "ollama agent: malformed JSON gets a retry message");
    check (t.bodies.size() > 2 && t.bodies[2].contains ("Invalid arguments for set_value") && t.bodies[2].contains ("param"),
           "ollama agent: missing args get a retry message");
    check (rig.win->writeIndex ("set Gain1=-3") < 0, "ollama agent: invalid call never reached the window");
    check (t.headers[0]["x-api-key"].isEmpty(), "ollama agent: no key sent");
    const auto first = juce::JSON::parse (t.bodies[0]);
    check (first["messages"][0]["content"].toString().length() < DspOperatorAgent::systemPrompt().length() / 2, "ollama agent: short system prompt");
    check (c.getStats().costUsd == 0.0 && c.getStats().usage.total() > 0, "ollama agent: cost 0, tokens counted");
    check (c.hasMap(), "ollama agent: map saved");
}

FA_TEST (aiProviderSwitching)
{
    Rig rig ("provider");
    {
        AiDspController c (rig.opts);                       // default: Claude, unchanged behaviour
        auto cl = c.makeLlmClient();
        check (rig.opts.provider == LlmProvider::claude && cl->providerName() == "claude" && ! cl->isLocal(), "provider: default is claude");
        const auto a = c.effectiveAgentOptions();
        check (! a.compact && a.maxTurns == 80 && a.screenshotMaxEdge == 2000, "provider: claude agent options untouched");
    }
    {
        auto o = ollamaOptions (rig);
        o.ollama.baseUrl = "http://127.0.0.1:9999";
        AiDspController c (o);
        auto cl = c.makeLlmClient();
        check (cl->providerName() == "ollama" && cl->isLocal(), "provider: ollama selected");
        const auto a = c.effectiveAgentOptions();
        check (a.compact && a.consentGiven == o.agent.consentGiven && a.maxTurns <= 30 && a.screenshotMaxEdge <= 768 && a.treeMaxNodes <= 150 && a.maxScreenshots <= 6,
               "provider: ollama gets the small-model profile");
    }
    {
        auto folder = tempFolder ("provider_settings");
        ApiKeyStore s (folder);
        check (s.loadProviderSettings().provider == "claude", "provider settings: default claude");
        ApiKeyStore::ProviderSettings ps;
        ps.provider = "ollama";
        ps.ollamaModel = "qwen3-vl:2b";
        check (s.saveProviderSettings (ps), "provider settings: saved");
        const auto back = ApiKeyStore (folder).loadProviderSettings();
        check (back.provider == "ollama" && back.ollamaModel == "qwen3-vl:2b" && back.ollamaUrl == "http://localhost:11434", "provider settings: round-trip");
    }
}

FA_TEST (aiOllamaLiveSmoke)
{
    if (juce::SystemStats::getEnvironmentVariable ("FA_OLLAMA_LIVE", {}) != "1")
        return;     // optional: needs a running local Ollama with a vision+tools model

    juce::Image img (juce::Image::RGB, 96, 96, true);
    {
        juce::Graphics g (img);
        g.fillAll (juce::Colours::white);
        g.setColour (juce::Colours::red);
        g.fillRect (10, 10, 50, 50);
    }
    juce::MemoryOutputStream png;
    juce::PNGImageFormat().writeImageToStream (img, png);

    OllamaClient::Config cfg;
    const auto model = juce::SystemStats::getEnvironmentVariable ("FA_OLLAMA_MODEL", {});
    if (model.isNotEmpty()) cfg.model = model;
    cfg.numPredict = 2048;
    OllamaClient c (cfg);

    const auto colorProp = obj ({ { "type", "string" }, { "description", "colour name" } });
    const auto schema = obj ({ { "type", "object" }, { "properties", obj ({ { "color", colorProp } }) },
                               { "required", arr ({ juce::var ("color") }) } });
    juce::Array<juce::var> tools;
    tools.add (obj ({ { "name", "report_color" }, { "description", "Report the colour of the square." }, { "input_schema", schema } }));
    const auto source = obj ({ { "type", "base64" }, { "media_type", "image/png" },
                               { "data", juce::Base64::toBase64 (png.getData(), png.getDataSize()) } });
    const auto textBlock = obj ({ { "type", "text" }, { "text", "Look at the image and call report_color with the colour of the square." } });
    const auto imageBlock = obj ({ { "type", "image" }, { "source", source } });
    const auto content = arr ({ textBlock, imageBlock });
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    const auto r = c.createMessage ("You must answer by calling a tool.", juce::var (tools), arr ({ obj ({ { "role", "user" }, { "content", content } }) }));
    const auto secs = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
    std::fprintf (stderr, "[ollama live] ok=%d stop=%s in=%d out=%d %.1fs (%.1f tok/s) err=%s\n", (int) r.ok, r.stopReason.toRawUTF8(),
                  (int) r.usage.inputTokens, (int) r.usage.outputTokens, secs, secs > 0 ? (double) r.usage.outputTokens / secs : 0.0, r.error.toRawUTF8());
    check (r.ok, "ollama live: request succeeded", r.error);
    check (r.stopReason == "tool_use" && r.content[0]["name"].toString() == "report_color", "ollama live: a tool call came back",
           juce::JSON::toString (r.content, true));
}

//==============================================================================
// Safety review fixes (AI path): target classification, tweeter HP, map verification, input confinement.
namespace
{
DspOperatorAgent::VerifyResult verifyOk (const DspOperatorAgent::VerifyRequest&)
{
    DspOperatorAgent::VerifyResult r;
    r.performed = true;
    r.matches = true;
    r.respondedDriver = "expected";
    return r;
}
}

FA_TEST (aiGuardRejectsNanOffAndUnverifiedUnmute)
{
    DspCapabilities caps;
    SafetyGuard g (SafetyGuard::Limits(), caps);
    auto mid = makeChannel (0, DriverRole::midbass, "mid");
    auto tw = makeChannel (1, DriverRole::tweeter, "tw");
    tw.driver.fsHz = 1000.0;           // protected HP = 1.6 kHz
    g.setChannels ({ mid, tw });
    const double nan = std::numeric_limits<double>::quiet_NaN();

    g.noteMute (1, true);
    check (! g.checkToolWrite ("hp.freq", 1, nan) && ! g.checkToolWrite ("gain", 0, nan) && ! g.checkToolWrite ("hp.freq", 1, INFINITY),
           "guard(tool): NaN / inf refused for numeric params");
    check (! g.checkToolWrite ("mute", 1, nan), "guard(tool): NaN refused for toggles");
    check (! g.checkToolWrite ("hp.type", 1, nan, "Off") && ! g.checkToolWrite ("hp.type", 1, nan, "Bypass")
           && ! g.checkToolWrite ("hp.type", 1, nan, "") && (bool) g.checkToolWrite ("hp.type", 1, nan, "LR24"),
           "guard(tool): tweeter hp.type may not be Off/bypass/unspecified");
    check ((bool) g.checkToolWrite ("hp.type", 0, nan, "Off"), "guard(tool): a midbass may switch its high-pass off");

    check ((bool) g.checkToolWrite ("mute", 1, 1.0), "guard(tool): muting a tweeter is always allowed");
    check (! g.checkToolWrite ("mute", 1, 0.0), "guard(tool): tweeter unmute refused without a verified high-pass");
    g.noteHpReadback (1, 800.0);
    check (! g.checkToolWrite ("mute", 1, 0.0), "guard(tool): tweeter unmute refused when the read-back HP is below the floor");
    g.noteHpReadback (1, 2500.0);
    check ((bool) g.checkToolWrite ("mute", 1, 0.0), "guard(tool): tweeter unmute allowed after a read-back at/above the floor");
    g.noteHpReadback (1, nan);
    check (! g.checkToolWrite ("mute", 1, 0.0), "guard(tool): a new hp write invalidates the read-back");
    check ((bool) g.checkToolWrite ("mute", 0, 0.0), "guard(tool): non-tweeters unmute freely");
}

FA_TEST (aiAgentClassifiesWritesByTarget)
{
    Rig rig ("target");
    auto& t = *rig.transport;
    t.queue.push_back (ok200 ("tool_use", arr ({ toolUse ("a1", "ui_tree", obj ({})) })));
    t.queue.push_back (ok200 ("tool_use", arr ({
        // nav label on a value control: refused
        toolUse ("b1", "set_value", obj ({ { "element_id", 11 }, { "value", "300" }, { "param", "nav" }, { "output", 2 } })),
        // clicking the field as nav (focus) is fine, but typing under a nav label is not
        toolUse ("b2", "click_xy", obj ({ { "x", 50 }, { "y", 341 }, { "param", "nav" } })),
        toolUse ("b3", "type_text", obj ({ { "text", "300" }, { "press_enter", true }, { "param", "nav" } })),
        // a real label on a tweeter that is not muted: refused
        toolUse ("b4", "type_text", obj ({ { "text", "300" }, { "param", "hp.freq" }, { "output", 2 } })),
        // arrow keys change the focused value: refused
        toolUse ("b5", "key_combo", obj ({ { "combo", "Up" } })),
        // the mute checkbox is a parameter control, never nav
        toolUse ("b6", "toggle", obj ({ { "element_id", 10 }, { "on", false }, { "param", "nav" }, { "output", 2 } })),
        toolUse ("b7", "toggle", obj ({ { "element_id", 10 }, { "on", true }, { "param", "mute" }, { "output", 2 } })),
        // Off on a tweeter: refused
        toolUse ("b8", "select_item", obj ({ { "element_id", 14 }, { "item", "Off" }, { "param", "hp.type" }, { "output", 2 } })),
        // unmute before the HP is verified: refused
        toolUse ("b9", "toggle", obj ({ { "element_id", 10 }, { "on", false }, { "param", "mute" }, { "output", 2 } })),
        // a control that was identified as mute cannot be re-labelled
        toolUse ("b10", "toggle", obj ({ { "element_id", 10 }, { "on", true }, { "param", "polarity" }, { "output", 2 } })),
        // low HP: refused; safe HP: written and read back; then the unmute is allowed
        toolUse ("b11", "set_value", obj ({ { "element_id", 11 }, { "value", "300" }, { "param", "hp.freq" }, { "output", 2 } })),
        toolUse ("b12", "set_value", obj ({ { "element_id", 11 }, { "value", "9000" }, { "param", "hp.freq" }, { "output", 2 } })),
        toolUse ("b13", "toggle", obj ({ { "element_id", 10 }, { "on", false }, { "param", "mute" }, { "output", 2 } })) })));
    t.queue.push_back (ok200 ("tool_use", arr ({ toolUse ("c1", "fail", obj ({ { "reason", "test over" } })) })));

    juce::String e;
    AiDspController c (rig.opts);
    c.connect (e);
    c.learn (e);
    auto& w = *rig.win;
    check (w.writeIndex ("set HPFreq2=300") < 0, "target: nav label on a value control never wrote it");
    check (w.writeIndex ("type 300") < 0, "target: nav / unmuted-tweeter type_text never typed");
    check (w.clicks == 1 && w.writeIndex ("key Up") < 0, "target: focus click allowed, arrow key refused");
    check (w.writeIndex ("toggle Mute2=1") >= 0, "target: real mute-on write allowed");
    check (w.writeIndex ("set HPType2=Off") < 0, "target: tweeter high-pass Off refused");
    check (w.writeIndex ("set HPFreq2=9000") > w.writeIndex ("toggle Mute2=1") && w.writeIndex ("set HPFreq2=9000") >= 0,
           "target: safe HP written after muting");
    check (w.writeIndex ("toggle Mute2=0") > w.writeIndex ("set HPFreq2=9000"), "target: unmute only after the read-back HP");
    int unmutes = 0;
    for (auto& l : w.log) if (l == "toggle Mute2=0") ++unmutes;
    check (unmutes == 1, "target: exactly one unmute (the unverified one was refused)", juce::String (unmutes));
    check (! w.byAuto ("Phase2")->toggle, "target: relabelled control did not change polarity");
}

FA_TEST (aiClientAreaAndForegroundConfinement)
{
    auto run = [] (const juce::String& name, int y, bool foreground, juce::String& body)
    {
        Rig rig (name);
        rig.win->client = { rig.win->win.getX() + 8, rig.win->win.getY() + 30, 784, 562 };   // title bar excluded
        rig.win->foreground = foreground;
        auto& t = *rig.transport;
        t.queue.push_back (ok200 ("tool_use", arr ({ toolUse ("a", "click_xy", obj ({ { "x", 300 }, { "y", y }, { "param", "nav" } })) })));
        t.queue.push_back (ok200 ("tool_use", arr ({ toolUse ("b", "fail", obj ({ { "reason", "end" } })) })));
        juce::String e;
        AiDspController c (rig.opts);
        c.connect (e);
        c.learn (e);
        body = t.bodies.size() > 1 ? t.bodies[1] : juce::String();
        return rig.win->clicks;
    };
    juce::String body;
    check (run ("client_title", 10, true, body) == 0 && body.contains ("client area"), "input: a click on the title bar is refused", body.substring (0, 300));
    check (run ("client_ok", 100, true, body) == 1, "input: a click in the client area goes through");
    check (run ("client_bg", 100, false, body) == 0 && body.contains ("not sent"), "input: nothing is sent when the window is not in the foreground", body.substring (0, 300));

    check (! SafetyGuard::checkPointInClient ({ 10, 10 }, { 100, 100, 800, 600 }, { 108, 130, 784, 562 })
           && (bool) SafetyGuard::checkPointInClient ({ 50, 60 }, { 100, 100, 800, 600 }, { 108, 130, 784, 562 }),
           "input: checkPointInClient rejects the non-client area");

    // Replay path (saved click-point binding) is confined the same way.
    Rig rig ("client_replay");
    rig.win->client = { rig.win->win.getX() + 8, rig.win->win.getY() + 30, 784, 562 };
    auto p = makeProfile (*rig.win);
    ControlBinding cb;
    cb.method = ControlBinding::Method::clickPoint;
    cb.relX = 0.5; cb.relY = 5.0 / 600.0;          // on the title bar
    cb.settleMs = 0;
    p.bindings[DspProfile::keyFor ("polarity", 0)] = cb;
    AiProfileStore (rig.folder).save (p, "Fake DSP Studio");
    juce::String e;
    AiDspController c (rig.opts);
    c.connect (e);
    auto mid = rig.outputs[0];
    mid.settings.inverted = true;
    check (! c.applyChannel (mid, e) && rig.win->clicks == 0, "input: replay click on the title bar refused", e);
}

FA_TEST (aiReplayAbortsTweeterWithoutHighPassType)
{
    for (int variant = 0; variant < 2; ++variant)
    {
        Rig rig (variant == 0 ? "hptype_unmapped" : "hptype_notinmap");
        auto p = makeProfile (*rig.win);
        if (variant == 0)
            p.bindings.erase ("hp.type@c1");
        else
            p.bindings["hp.type@c1"].valueMap["BW6"] = "Butterworth 6";      // wanted LR24 is not in the map
        AiProfileStore (rig.folder).save (p, "Fake DSP Studio");
        rig.opts.allowLearning = false;
        juce::String e;
        AiDspController c (rig.opts);
        c.connect (e);
        auto tw = rig.outputs[1];
        tw.driver.fsHz = 1000.0;
        tw.settings.highPass = FilterSpec::crossover (FilterType::highPass, 4000.0);
        const bool applied = c.applyChannel (tw, e);
        check (! applied && (e.containsIgnoreCase ("high-pass type") || e.contains ("hp.type")),
               "replay: tweeter with an unmappable high-pass type is refused", e);
        check (! applied, "replay: the channel write is aborted");
        check (rig.win->writeIndex ("set HPFreq2=4000") < 0 && rig.win->writeIndex ("toggle Mute2=0") < 0,
               "replay: no frequency written and no unmute for that tweeter");
        check (! rig.win->byAuto ("Mute2")->toggle || rig.win->writeIndex ("toggle Mute2=1") >= 0, "replay: the tweeter is never left unmuted by this write");
    }
}

FA_TEST (aiProfileFindDoesNotFallBackToAnotherOutput)
{
    DspProfile p;
    p.aiLearned = true;
    ControlBinding b;
    p.bindings["gain"] = b;                                       // generic: no output number
    p.bindings["channel.select@c0"] = b;
    check (p.selectsChannels() && p.find ("gain", 1) != nullptr, "profile: select-then-edit model falls back to the shared control");
    p.bindings.erase ("channel.select@c0");
    check (! p.selectsChannels() && p.find ("gain", 1) == nullptr && p.find ("gain") != nullptr,
           "profile: AI map without a declared select model does not answer a per-output request with a generic binding");
    p.selectThenEdit = true;
    check (p.find ("gain", 1) != nullptr && p.find ("channel.select", 1) == nullptr,
           "profile: declared model uses the shared control, but channel.select stays per output");
    DspProfile classic = p;
    classic.aiLearned = false;
    classic.selectThenEdit = false;
    check (classic.find ("gain", 1) != nullptr, "profile: classic (taught) profiles keep the generic fallback");

    // Controller: shared controls + channel.select for output 1 only -> output 2 is not written.
    Rig rig ("selectmodel");
    DspProfile m;
    m.aiLearned = true;
    m.windowTitleContains = "Fake DSP Studio";
    m.caps.peqBandsPerChannel = 0;
    using M = ControlBinding::Method;
    m.bindings["gain"] = uiaBinding (*rig.win, "Gain1", M::uiaText, 1);
    m.bindings["mute"] = uiaBinding (*rig.win, "Mute1", M::uiaToggle);
    m.bindings["hp.freq"] = uiaBinding (*rig.win, "HPFreq1", M::uiaText, 0);
    m.bindings["hp.type"] = uiaBinding (*rig.win, "HPType1", M::uiaSelect, 0);
    m.bindings["lp.freq"] = uiaBinding (*rig.win, "LPFreq1", M::uiaText, 0);
    m.bindings["channel.select@c0"] = uiaBinding (*rig.win, "Select1", M::uiaInvoke);
    AiProfileStore (rig.folder).save (m, "Fake DSP Studio");
    rig.opts.allowLearning = false;
    juce::String e;
    AiDspController c (rig.opts);
    c.connect (e);
    auto tw = rig.outputs[1];
    tw.settings.highPass = FilterSpec::crossover (FilterType::highPass, 4000.0);
    check (! c.applyChannel (tw, e) && e.contains ("channel.select"), "selectChannel: fails when this output has no channel.select", e);
    check (rig.win->log.isEmpty(), "selectChannel: nothing written to the shared controls", rig.win->log.joinIntoString (","));

    // Output 2 has a select control, but it is an invoke button (no read-back): a tweeter is not touched through it.
    m.bindings["channel.select@c1"] = uiaBinding (*rig.win, "Select2", M::uiaInvoke);
    AiProfileStore (rig.folder).save (m, "Fake DSP Studio");
    AiDspController c2 (rig.opts);
    c2.connect (e);
    check (! c2.applyChannel (tw, e) && e.contains ("read back"), "selectChannel: tweeter edit refused when the selection cannot be read back", e);
    check (rig.win->writeIndex ("set HPFreq1=4000") < 0, "selectChannel: the shared controls were not written for the tweeter");
    auto mid = rig.outputs[0];
    mid.settings.gainDb = -2.0;
    check (c2.applyChannel (mid, e), "selectChannel: a non-tweeter can still be edited through the select model", e);
}

FA_TEST (aiMapNotSavedUnlessLearnedAndVerified)
{
    // 1. A run that saved bindings and then failed must leave nothing behind.
    {
        Rig rig ("persist_fail");
        auto& t = *rig.transport;
        t.queue.push_back (ok200 ("tool_use", arr ({ toolUse ("a", "ui_tree", obj ({})) })));
        t.queue.push_back (ok200 ("tool_use", arr ({ toolUse ("b", "save_map", obj ({ { "window_title", "Fake DSP Studio" },
            { "bindings", arr ({ obj ({ { "key", "gain" }, { "output", 1 }, { "method", "text" }, { "element_id", 1 } }) }) } })) })));
        t.queue.push_back (ok200 ("tool_use", arr ({ toolUse ("c", "fail", obj ({ { "reason", "gave up" } })) })));
        juce::String e;
        AiDspController c (rig.opts);
        c.connect (e);
        check (! c.learn (e), "persist: failed run reports failure", e);
        check (! c.hasMap() && rig.folder.findChildFiles (juce::File::findFiles, false, "ai_*.json").isEmpty(),
               "persist: a failed run neither adopts nor saves its half-learned map");
    }
    // 2. Budget exhausted mid-run: same.
    {
        Rig rig ("persist_budget");
        rig.opts.limits.maxActions = 3;
        auto& t = *rig.transport;
        t.queue.push_back (ok200 ("tool_use", arr ({ toolUse ("a", "ui_tree", obj ({})) })));
        t.queue.push_back (ok200 ("tool_use", arr ({ toolUse ("b", "save_map", obj ({
            { "bindings", arr ({ obj ({ { "key", "gain" }, { "output", 1 }, { "method", "text" }, { "element_id", 1 } }) }) } })) })));
        for (int i = 0; i < 4; ++i)
            t.queue.push_back (ok200 ("tool_use", arr ({ toolUse ("x" + juce::String (i), "ui_tree", obj ({})) })));
        juce::String e;
        AiDspController c (rig.opts);
        c.connect (e);
        check (! c.learn (e) && ! c.hasMap() && rig.folder.findChildFiles (juce::File::findFiles, false, "ai_*.json").isEmpty(),
               "persist: an out-of-budget run saves nothing", e);
    }
    // 3. save_map demands verify_channel for every output it maps.
    {
        Rig rig ("persist_verify");
        rig.opts.agent.verify = [] (const DspOperatorAgent::VerifyRequest& rq)
        {
            auto r = verifyOk (rq);
            r.matches = rq.output == 0;               // output 2 answers from the wrong driver
            r.respondedDriver = r.matches ? "expected" : "other driver";
            return r;
        };
        auto& t = *rig.transport;
        t.queue.push_back (ok200 ("tool_use", arr ({ toolUse ("a", "ui_tree", obj ({})),
            toolUse ("v1", "verify_channel", obj ({ { "output", 1 } })), toolUse ("v2", "verify_channel", obj ({ { "output", 2 } })),
            toolUse ("s", "save_map", obj ({ { "bindings", arr ({
                obj ({ { "key", "gain" }, { "output", 1 }, { "method", "text" }, { "element_id", 1 } }),
                obj ({ { "key", "gain" }, { "output", 2 }, { "method", "text" }, { "element_id", 7 } }),
                obj ({ { "key", "hp.freq" }, { "method", "text" }, { "element_id", 5 } }) }) } })) })));
        t.queue.push_back (ok200 ("tool_use", arr ({ toolUse ("d", "done", obj ({ { "summary", "partial" } })) })));
        juce::String e;
        AiDspController c (rig.opts);
        c.connect (e);
        check (c.learn (e), "persist: run with a partly verified map finishes", e);
        const auto prof = c.getProfile();
        check (prof.bindings.count ("gain@c0") == 1 && prof.bindings.count ("gain@c1") == 0 && prof.bindings.count ("hp.freq") == 0,
               "persist: only bindings of verified outputs are accepted (global binding needs every output verified)");
        check (t.bodies[1].contains ("verify_channel"), "persist: the refusal tells the model to verify first");
        check (! prof.verified && ! c.isMapVerified(), "persist: map with an unverified output is flagged unverified");
        DspProfile onDisk;
        AiProfileStore (rig.folder).loadFor ("Fake DSP Studio - project 1", onDisk);
        check (! onDisk.verified, "persist: the unverified flag is stored on disk");
    }
    // 4. All outputs verified: flagged verified, survives a reload.
    {
        Rig rig ("persist_verified");
        rig.opts.agent.verify = verifyOk;
        auto& t = *rig.transport;
        t.queue.push_back (ok200 ("tool_use", arr ({ toolUse ("a", "ui_tree", obj ({})),
            toolUse ("v1", "verify_channel", obj ({ { "output", 1 } })), toolUse ("v2", "verify_channel", obj ({ { "output", 2 } })),
            toolUse ("s", "save_map", obj ({ { "window_title", "Fake DSP Studio" }, { "bindings", arr ({
                obj ({ { "key", "gain" }, { "output", 1 }, { "method", "text" }, { "element_id", 1 } }),
                obj ({ { "key", "gain" }, { "output", 2 }, { "method", "text" }, { "element_id", 7 } }) }) } })) })));
        t.queue.push_back (ok200 ("tool_use", arr ({ toolUse ("d", "done", obj ({ { "summary", "ok" } })) })));
        juce::String e;
        AiDspController c (rig.opts);
        c.connect (e);
        check (c.learn (e) && c.isMapVerified(), "persist: fully verified run yields a verified map", e);
        AiDspController again (rig.opts);
        again.connect (e);
        check (again.hasMap() && again.isMapVerified(), "persist: verified flag survives a reload");
    }
}

FA_TEST (aiLegacyMapMustBeReverifiedBeforePush)
{
    Rig rig ("legacy");
    rig.opts.requireVerifiedMap = true;
    rig.opts.allowLearning = false;
    rig.opts.agent.verify = verifyOk;
    AiProfileStore (rig.folder).save (makeProfile (*rig.win), "Fake DSP Studio");       // no verified flag: legacy
    juce::String e;
    AiDspController c (rig.opts);
    c.connect (e);
    check (c.hasMap() && ! c.isMapVerified(), "legacy: a map without the flag loads as unverified");
    auto mid = rig.outputs[0];
    mid.settings.gainDb = -2.0;
    check (! c.applyChannel (mid, e) && e.contains ("not been verified"), "legacy: no tune is pushed through an unverified map", e);
    check (rig.win->log.isEmpty(), "legacy: nothing was written");

    check (c.verifyMap (e), "legacy: verifyMap passes when every output's test sound matches", e);
    auto& w = *rig.win;
    const int hp = w.log.indexOf ("set HPFreq2=" + w.byAuto ("HPFreq2")->value);
    check (hp >= 0 && hp < w.writeIndex ("toggle Mute2=0"), "legacy: the tweeter's high-pass is written before it is unmuted for the test");
    check (c.isMapVerified() && c.applyChannel (mid, e), "legacy: after verification the tune is pushed", e);
    AiDspController again (rig.opts);
    again.connect (e);
    check (again.isMapVerified(), "legacy: verification is saved");

    // A mismatching test sound keeps the map unverified.
    Rig rig2 ("legacy2");
    rig2.opts.requireVerifiedMap = true;
    rig2.opts.allowLearning = false;
    rig2.opts.agent.verify = [] (const DspOperatorAgent::VerifyRequest& rq) { auto r = verifyOk (rq); r.matches = false; r.respondedDriver = "wrong"; return r; };
    AiProfileStore (rig2.folder).save (makeProfile (*rig2.win), "Fake DSP Studio");
    AiDspController c2 (rig2.opts);
    c2.connect (e);
    check (! c2.verifyMap (e) && ! c2.isMapVerified() && e.contains ("wrong"), "legacy: a mismatching test sound fails verification", e);
    check (rig2.win->byAuto ("Mute1")->toggle && rig2.win->byAuto ("Mute2")->toggle, "legacy: outputs are left muted after a failed verification");
}

FA_TEST (aiRelearnAndDeleteFindTheSameFileAsLoad)
{
    auto folder = tempFolder ("resolver");
    AiProfileStore store (folder);
    DspProfile p;
    p.aiLearned = true;
    p.name = "Helix";
    p.windowTitleContains = "Fake DSP";                              // chosen by the AI, saved under this fragment
    p.bindings["gain"] = ControlBinding();
    store.save (p, p.windowTitleContains);

    const juce::String title = "Fake DSP Studio - project 1";        // what the window is really called
    DspProfile out;
    check (! store.load (title, out) && ! store.fileFor (title).existsAsFile(), "resolver: exact-title lookup misses an AI-named map (the old bug)");
    check (store.loadFor (title, out) && out.name == "Helix", "resolver: loadFor finds it by fragment");
    check (store.removeFor (title), "resolver: removeFor deletes it");
    check (! store.loadFor (title, out) && store.getFolder().findChildFiles (juce::File::findFiles, false, "ai_*.json").isEmpty(),
           "resolver: after delete there is no map any more (no stale replay)");

    // Controller: connect loads through the same resolver.
    Rig rig ("resolver_ctl");
    DspProfile q = makeProfile (*rig.win);
    q.windowTitleContains = "Fake DSP";
    AiProfileStore (rig.folder).save (q, q.windowTitleContains);
    juce::String e;
    AiDspController c (rig.opts);
    c.connect (e);
    check (c.hasMap(), "resolver: controller loads the fragment-named map");
    AiProfileStore (rig.folder).removeFor (rig.win->title);
    AiDspController c2 (rig.opts);
    c2.connect (e);
    check (! c2.hasMap(), "resolver: after the UI delete the controller sees no map");
}

FA_TEST (aiControllerStateIsThreadSafe)
{
    Rig rig ("threads");
    AiProfileStore (rig.folder).save (makeProfile (*rig.win), "Fake DSP Studio");
    rig.opts.allowLearning = false;
    juce::String e;
    AiDspController c (rig.opts);
    c.connect (e);

    std::atomic<bool> stop { false };
    std::atomic<long> reads { 0 };
    std::thread reader ([&]
    {
        while (! stop)
        {
            const auto st = c.getStats();
            const auto notes = c.getLastSafetyNotes();
            const auto prof = c.getProfile();
            reads += st.writes + notes.size() + (long) prof.bindings.size() + (c.hasMap() ? 1 : 0) + (c.isMapVerified() ? 1 : 0);
        }
    });
    int applied = 0;
    for (int i = 0; i < 40; ++i)
    {
        auto tw = rig.outputs[1];
        tw.driver.fsHz = 1000.0;
        tw.settings.highPass = FilterSpec::crossover (FilterType::highPass, 400.0 + i);
        tw.settings.gainDb = -3.0;
        applied += c.applyChannel (tw, e) ? 1 : 0;
    }
    stop = true;
    reader.join();
    check (applied == 40 && c.getStats().writes > 100 && reads > 0, "threads: stats/notes/map read concurrently with writes without a crash",
           juce::String (applied) + " " + juce::String (c.getStats().writes));
}
