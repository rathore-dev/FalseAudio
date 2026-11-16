#include "DspOperatorAgent.h"
#include <cmath>

namespace fa
{
namespace
{
    juce::var makeObj (std::initializer_list<std::pair<const char*, juce::var>> items)
    {
        auto* o = new juce::DynamicObject();
        for (auto& [k, v] : items)
            o->setProperty (k, v);
        return juce::var (o);
    }

    juce::var prop (const char* type, const char* description)
    {
        return makeObj ({ { "type", type }, { "description", description } });
    }

    juce::var tool (const char* name, const char* description, std::initializer_list<std::pair<const char*, juce::var>> props,
                    std::initializer_list<const char*> required)
    {
        auto* p = new juce::DynamicObject();
        for (auto& [k, v] : props)
            p->setProperty (k, v);
        juce::Array<juce::var> req;
        for (auto* r : required)
            req.add (r);
        auto schema = makeObj ({ { "type", "object" }, { "properties", juce::var (p) }, { "required", req } });
        return makeObj ({ { "name", name }, { "description", description }, { "input_schema", schema } });
    }

    double numberFrom (const juce::String& text)
    {
        const auto t = text.trim().replaceCharacter (',', '.');
        if (t.isEmpty())
            return std::numeric_limits<double>::quiet_NaN();
        const auto c = t[0];
        if (! (juce::CharacterFunctions::isDigit (c) || c == '-' || c == '+' || c == '.'))
            return std::numeric_limits<double>::quiet_NaN();
        return t.getDoubleValue();
    }

    bool hasInt (const juce::var& v, const char* name) { return v.hasProperty (name) && ! v[name].isVoid(); }
}

//==============================================================================
juce::String DspOperatorAgent::systemPrompt()
{
    return R"(You are the DSP operator inside FalseAudio, a car-audio auto-tuning application. You work a vendor's DSP tuning software (Helix PC-Tool, Audison bit, Mosconi, miniDSP, Alpine, Dirac, DSP-8 and so on) the way a careful installer would, through a few tools. You only ever see and control the one attached window.

GOAL
Work out which controls of the window set, for every DSP output the task lists: gain/level, delay, polarity (phase invert), mute, high-pass and low-pass (frequency, filter type, slope), and the parametric EQ bands (frequency, gain, Q; select-band controls if the UI has one editor for many bands), plus a channel-select control if the software edits one output at a time, plus a store/save button if there is one. Then record what you learned with save_map. Afterwards the app drives the window by itself from that map without calling you, so the map must be exact and reusable: prefer UI Automation controls (element ids from ui_tree) and fall back to window coordinates (click_xy) only for custom-drawn controls.

HOW TO WORK
1. Start with ui_tree and screenshot. Read labels, units and value formats (dB, ms, cm, Hz, "LR 24", "Linkwitz-Riley 24 dB/oct"). Notice how outputs are named and numbered.
2. Identify controls by their labels, positions and by trying them. Probe only with harmless values: change one value a little, read it back (read_value), restore it. Never guess which output a control drives when a mistake could damage speakers: confirm with verify_channel, which makes the app play a test tone/sweep on the physical output and reports which driver and band responded. Use it at least once per output (and again whenever you are unsure the channel-select control really switches outputs).
3. Every set_value / toggle / select_item / click_xy / type_text must declare what it changes with param (gain, delay, polarity, mute, hp.freq, hp.type, lp.freq, lp.type, peq.freq, peq.gain, peq.q, or nav for navigation, save for store buttons) and the 1-based output number. The app enforces safety rules in code and will refuse values that break them: a tweeter's high-pass may never be below its protected frequency; tweeter crossover and polarity changes need the channel muted first (toggle mute on, change, toggle mute off); gain changes are limited per step; actions are limited to the attached window; there is a budget of actions and cost. A refusal is final for that value: do not try to work around it, adapt.
4. Element ids from ui_tree are valid until the next ui_tree call. Call ui_tree again after anything that changes the screen (tab, channel, band selection).
5. Call save_map as soon as you are sure of a group of bindings (you may call it several times; later bindings overwrite earlier ones with the same key). Binding keys: channel.select, gain, delay, polarity, mute, hp.freq, hp.type, lp.freq, lp.type, peq.select, peq.freq, peq.gain, peq.q, save. Give output (1-based) for controls that exist per output, band (1-based) for per-band controls. If one editor serves all outputs after a channel.select, give that binding once without output and a channel.select binding per output. Describe value units: delay unit (ms, cm, m, in, samples), decimals, scale/offset if the UI shows a different quantity, value_map for type selections (key like LR24, LR12, BW12, BW24, BS12, BS24, OFF -> exact text in the UI), invert_toggle if a checkbox is on for normal polarity.
6. Finish with done (summary of what is mapped and what could not be found) or fail (reason). Do not stop with plain text: always end with done or fail.

Be economical: every screenshot and every call costs money. Think before acting, batch independent reads, do not repeat identical calls.)";
}

juce::String DspOperatorAgent::compactSystemPrompt()
{
    return R"(You operate a DSP tuning program window for FalseAudio through tools. Goal: find which control sets each output's gain, delay, polarity, mute, high-pass, low-pass and PEQ, then call save_map and finish with done.

Rules:
- Call exactly ONE tool per turn. Arguments must be valid JSON that matches the tool schema. Output numbers are 1-based.
- Start with ui_tree. Take a screenshot only when the tree is not enough.
- Every set_value, toggle, select_item, invoke, click_xy and type_text needs param (gain, delay, polarity, mute, hp.freq, hp.type, lp.freq, lp.type, peq.freq, peq.gain, peq.q, nav or save) and the output number.
- Probe only with small harmless changes, read_value, then restore. Use verify_channel to confirm which output a control drives.
- The app refuses unsafe values (tweeter high-pass too low, no mute before tweeter changes, big gain steps). A refusal is final: adapt.
- save_map binding keys: channel.select, gain, delay, polarity, mute, hp.freq, hp.type, lp.freq, lp.type, peq.select, peq.freq, peq.gain, peq.q, save. Use element_id from the latest ui_tree.
- End with done or fail. Never answer with plain text only.)";
}

juce::var DspOperatorAgent::toolDefinitions()
{
    const auto paramProp = prop ("string", "What this control changes: nav, save, gain, delay, polarity, mute, hp.freq, hp.type, lp.freq, lp.type, peq.freq, peq.gain or peq.q.");
    const auto outProp = prop ("integer", "1-based DSP output number the control belongs to (omit only for nav/save).");
    juce::Array<juce::var> t;
    t.add (tool ("ui_tree", "Pruned UI Automation tree of the attached window: element id, type, name, value, bounds (view pixels) and supported actions. Ids stay valid until the next ui_tree call.", {}, {}));
    t.add (tool ("screenshot", "Screenshot of the attached window only (downscaled if large). Coordinates for click_xy are pixels of this image.", {}, {}));
    t.add (tool ("set_value", "Type/set a value into a control found via ui_tree (value pattern or range pattern).",
                 { { "element_id", prop ("integer", "id from ui_tree") }, { "value", prop ("string", "text to enter, e.g. \"-3.5\"") },
                   { "param", paramProp }, { "output", outProp } }, { "element_id", "value", "param", "output" }));
    t.add (tool ("invoke", "Press a button / tab / list item. Use only for navigation (param nav), selecting an output or band (nav), or a store button (save); set mute with toggle.",
                 { { "element_id", prop ("integer", "id from ui_tree") }, { "param", paramProp }, { "output", outProp } }, { "element_id" }));
    t.add (tool ("select_item", "Choose an entry of a combo box or list by its visible text.",
                 { { "element_id", prop ("integer", "id from ui_tree") }, { "item", prop ("string", "visible text of the entry") },
                   { "param", paramProp }, { "output", outProp } }, { "element_id", "item", "param", "output" }));
    t.add (tool ("toggle", "Set a checkbox / toggle button to on or off (mute: on = muted; polarity: on = inverted as the UI shows it).",
                 { { "element_id", prop ("integer", "id from ui_tree") }, { "on", prop ("boolean", "desired state") },
                   { "param", paramProp }, { "output", outProp } }, { "element_id", "on", "param", "output" }));
    t.add (tool ("read_value", "Read the current value (or toggle state) of a control.",
                 { { "element_id", prop ("integer", "id from ui_tree") } }, { "element_id" }));
    t.add (tool ("click_xy", "Click at a point of the screenshot (custom-drawn controls without UI Automation).",
                 { { "x", prop ("integer", "x in screenshot pixels") }, { "y", prop ("integer", "y in screenshot pixels") },
                   { "double_click", prop ("boolean", "double click (to edit a numeric field)") },
                   { "on", prop ("boolean", "only when this click presses a mute/polarity button: the state it should end in") },
                   { "param", paramProp }, { "output", outProp } }, { "x", "y", "param" }));
    t.add (tool ("type_text", "Type text into the focused control (after click_xy), optionally pressing Enter.",
                 { { "text", prop ("string", "text to type") }, { "press_enter", prop ("boolean", "press Enter afterwards") },
                   { "param", paramProp }, { "output", outProp } }, { "text", "param" }));
    t.add (tool ("key_combo", "Press a key combination inside the window, e.g. \"ctrl+a\", \"Return\", \"Tab\", \"F2\".",
                 { { "combo", prop ("string", "keys joined with +") } }, { "combo" }));
    t.add (tool ("verify_channel", "Ask the app to play a test tone or sweep on a DSP output and report which physical driver and band responded. Use it to confirm which output a control drives. The channel under test must be unmuted and others muted by you first if the window needs it.",
                 { { "output", prop ("integer", "1-based DSP output to test") }, { "mode", prop ("string", "tone or sweep") } }, { "output" }));
    t.add (tool ("save_map", "Record learned controls. Each binding: key, optional output/band (1-based), method (text, range, toggle, invoke, select, click_point, click_and_type), element_id (UIA methods) or x,y (click methods), unit, decimals, scale, offset, invert_toggle, value_map, settle_ms, description.",
                 { { "dsp_name", prop ("string", "name for this DSP software/profile") }, { "window_title", prop ("string", "part of the window title identifying this software") },
                   { "process_name", prop ("string", "executable name if known") }, { "notes", prop ("string", "value formats, units and quirks worth remembering") },
                   { "bindings", makeObj ({ { "type", "array" }, { "description", "list of binding objects" }, { "items", makeObj ({ { "type", "object" } }) } }) } },
                 { "bindings" }));
    t.add (tool ("done", "Finish successfully.", { { "summary", prop ("string", "what is mapped, what is missing") } }, { "summary" }));
    t.add (tool ("fail", "Give up.", { { "reason", prop ("string", "why the map could not be learned") } }, { "reason" }));
    return t;
}

//==============================================================================
DspOperatorAgent::DspOperatorAgent (LlmClient& c, WindowBackend& w, SafetyGuard& g, Options o)
    : client (c), window (w), guard (g), opts (std::move (o))
{
}

void DspOperatorAgent::report (const juce::String& stage)
{
    if (opts.progress == nullptr)
        return;
    Progress p;
    p.stage = stage;
    p.turn = turn;
    p.actions = guard.actionsUsed();
    p.bindings = (int) profile.bindings.size();
    p.usd = guard.usdSpent();
    p.tokens = guard.tokensUsed();
    opts.progress (p);
}

DspOperatorAgent::Result DspOperatorAgent::learn (const std::vector<OutputChannel>& outputs, const DspCapabilities& caps,
                                                  const DspProfile& existing, const juce::StringArray& focusKeys)
{
    Result res;
    profile = existing;
    profile.caps = caps;
    outputsInUse = outputs;
    ids.clear();
    screenshotsTaken = 0;
    cancelled = false;
    turn = 0;
    elementRoles.clear();
    navSeq = 0;
    clickedRole = {};
    verifiedOutputs.clear();
    touchedOutputs.clear();
    priorVerified.clear();
    for (int o : existing.verifiedOutputs) priorVerified.insert (o);
    profile.verified = false;
    updateVerifiedState();

    auto finish = [&] (bool ok, const juce::String& msg)
    {
        res.success = ok;
        res.message = msg;
        res.profile = profile;
        res.turns = turn;
        res.actions = guard.actionsUsed();
        say ((ok ? "Finished: " : "Stopped: ") + msg);
        return res;
    };

    if (! opts.consentGiven)
        return finish (false, "Consent to send the DSP window to the Anthropic API has not been given.");
    if (! window.isAttached())
        return finish (false, "No DSP window is attached.");

    guard.setChannels (outputs);
    const auto wb = window.windowBounds();
    viewScale = wb.isEmpty() ? 1.0 : std::min (1.0, (double) opts.screenshotMaxEdge / (double) std::max (wb.getWidth(), wb.getHeight()));

    juce::String task;
    task << "Learn the control map of the attached DSP software window"
         << (opts.vendorHint.isNotEmpty() ? " (" + opts.vendorHint + ")" : juce::String()) << ".\n\nOutputs to map (1-based numbers as in the DSP software):\n";
    for (auto& ch : outputs)
        task << "- Output " << (ch.dspOutput + 1) << ": " << ch.label << " (" << toString (ch.role) << ")"
             << (ch.role == DriverRole::tweeter ? " - TWEETER: protected, mute before crossover/polarity changes" : "") << "\n";
    task << "\nDSP capabilities: " << caps.peqBandsPerChannel << " PEQ bands per output, gain "
         << caps.minChannelGainDb << ".." << caps.maxChannelGainDb << " dB, max delay " << caps.maxDelayMs << " ms.\n";
    task << "Window size: " << wb.getWidth() << "x" << wb.getHeight() << " px, view scale " << juce::String (viewScale, 3) << ".\n";
    if (profile.bindings.size() > 0)
        task << "Already known bindings: " << (int) profile.bindings.size() << " (only repair/extend).\n";
    if (focusKeys.size() > 0)
        task << "These bindings failed or no longer read back correctly and must be re-learned: " << focusKeys.joinIntoString (", ") << ".\n";

    juce::Array<juce::var> messages;
    messages.add (makeObj ({ { "role", "user" }, { "content", task } }));

    const auto tools = toolDefinitions();
    const auto system = opts.compact ? compactSystemPrompt() : systemPrompt();
    int nudges = 0;

    for (turn = 1; turn <= opts.maxTurns; ++turn)
    {
        if (cancelled || guard.isKilled())
        {
            res.cancelled = true;
            return finish (false, guard.isKilled() ? "Stopped: " + guard.killReason() : juce::String ("Cancelled."));
        }
        if (auto b = guard.checkBudget(); ! b)
            return finish (false, b.reason);

        report (client.isLocal() ? "Waiting for the local model" : "Waiting for Claude");
        const auto r = client.createMessage (system, tools, juce::var (messages));
        res.usage += r.usage;
        res.costUsd += r.costUsd;
        guard.addUsage (r.usage, r.costUsd);

        if (! r.ok)
            return finish (false, "API error: " + r.error);
        if (r.isRefusal())   // never run tools from a refused turn
            return finish (false, "Claude declined this request (refusal" + (r.refusalCategory.isNotEmpty() ? ": " + r.refusalCategory : juce::String()) + ").");
        if (r.stopReason == "max_tokens")
            return finish (false, "Claude's reply was cut off (max_tokens); raise max tokens or lower effort.");

        messages.add (makeObj ({ { "role", "assistant" }, { "content", r.content } }));   // verbatim, thinking blocks included

        std::vector<juce::var> uses;
        if (auto* arr = r.content.getArray())
            for (auto& b : *arr)
                if (b.getProperty ("type", {}).toString() == "tool_use")
                    uses.push_back (b);

        if (uses.empty())
        {
            if (++nudges > 2)
                return finish (false, "Claude stopped without calling done or fail.");
            messages.add (makeObj ({ { "role", "user" }, { "content", "Continue using the tools, and finish with done or fail." } }));
            continue;
        }

        juce::Array<juce::var> results;
        bool finished = false, success = false;
        juce::String finalText;
        for (auto& u : uses)
        {
            const auto name = u.getProperty ("name", {}).toString();
            const auto id = u.getProperty ("id", {}).toString();
            Outcome o;
            if (finished)
            {
                o.isError = true;
                o.text = "Not executed: the session already finished.";
            }
            else
            {
                report ("Running " + name);
                auto input = u.getProperty ("input", {});
                const auto bad = validateArgs (name, input);
                if (bad.isNotEmpty())
                {
                    o.isError = true;
                    o.text = bad;
                }
                else
                    o = runTool (name, input);
                say ("[" + name + "] " + o.text.upToFirstOccurrenceOf ("\n", false, false).substring (0, 160));
                if (o.finished) { finished = true; success = o.success; finalText = o.text; }
            }

            juce::Array<juce::var> content;
            content.add (makeObj ({ { "type", "text" }, { "text", o.text } }));
            if (o.hasImage)
                content.add (makeObj ({ { "type", "image" },
                                        { "source", makeObj ({ { "type", "base64" }, { "media_type", o.image.mediaType }, { "data", o.image.base64 } }) } }));
            auto block = makeObj ({ { "type", "tool_result" }, { "tool_use_id", id }, { "content", content } });
            if (o.isError)
                block.getDynamicObject()->setProperty ("is_error", true);
            results.add (block);
        }
        messages.add (makeObj ({ { "role", "user" }, { "content", results } }));

        if (finished)
            return finish (success, finalText);
    }
    return finish (false, "Turn limit reached.");
}

//==============================================================================
juce::String DspOperatorAgent::validateArgs (const juce::String& name, juce::var& in) const
{
    if (in.isObject() && in.hasProperty ("_malformed"))
        return "Your arguments for " + name + " were not valid JSON. Call " + name + " again with a JSON object that matches the schema. You sent: "
               + in["_malformed"].toString().substring (0, 120);
    if (! opts.compact)
        return {};

    const auto defs = toolDefinitions();
    juce::var def;
    juce::StringArray names;
    if (auto* arr = defs.getArray())
        for (auto& d : *arr)
        {
            names.add (d.getProperty ("name", {}).toString());
            if (names[names.size() - 1] == name) def = d;
        }
    if (! def.isObject())
        return "Unknown tool \"" + name + "\". Available tools: " + names.joinIntoString (", ") + ".";
    if (! in.isObject())
        in = juce::var (new juce::DynamicObject());

    const auto schema = def.getProperty ("input_schema", {});
    const auto props = schema.getProperty ("properties", {});
    juce::StringArray problems;
    if (auto* req = schema.getProperty ("required", {}).getArray())
        for (auto& r : *req)
        {
            const auto key = r.toString();
            if (! in.hasProperty (key) || in.getProperty (key, {}).isVoid())
                problems.add ("missing \"" + key + "\" (" + props.getProperty (key, {}).getProperty ("type", {}).toString() + ")");
        }
    if (auto* po = props.getDynamicObject())
        for (auto& nv : po->getProperties())
        {
            const auto key = nv.name.toString();
            if (! in.hasProperty (key) || in.getProperty (key, {}).isVoid())
                continue;
            const auto type = nv.value.getProperty ("type", {}).toString();
            const auto val = in.getProperty (key, {});
            auto* obj = in.getDynamicObject();
            if (type == "integer")
            {
                const auto txt = val.toString().trim();
                if (val.isString() && txt.isNotEmpty() && txt.containsOnly ("-0123456789"))
                    obj->setProperty (key, txt.getIntValue());
                else if (val.isDouble() && (double) val == std::floor ((double) val))
                    obj->setProperty (key, (int) (double) val);
                else if (! (val.isInt() || val.isInt64()))
                    problems.add ("\"" + key + "\" must be an integer");
            }
            else if (type == "boolean")
            {
                if (val.isString() && (val.toString().equalsIgnoreCase ("true") || val.toString().equalsIgnoreCase ("false")))
                    obj->setProperty (key, val.toString().equalsIgnoreCase ("true"));
                else if (! val.isBool())
                    problems.add ("\"" + key + "\" must be true or false");
            }
            else if (type == "string")
            {
                if (val.isInt() || val.isInt64() || val.isDouble())
                    obj->setProperty (key, val.toString());
                else if (! val.isString())
                    problems.add ("\"" + key + "\" must be a string");
            }
        }
    if (problems.isEmpty())
        return {};
    return "Invalid arguments for " + name + ": " + problems.joinIntoString ("; ") + ". Call " + name + " again with corrected JSON arguments.";
}

DspOperatorAgent::Outcome DspOperatorAgent::runTool (const juce::String& name, const juce::var& in)
{
    Outcome o;
    if (name == "done" || name == "fail")
    {
        o.finished = true;
        if (name == "done")
        {
            if (profile.bindings.empty())
            {
                o.finished = false;
                o.isError = true;
                o.text = "Nothing saved yet: call save_map with the bindings first, then done.";
                return o;
            }
            o.success = true;
            o.text = in.getProperty ("summary", "done").toString();
        }
        else
            o.text = in.getProperty ("reason", "failed").toString();
        return o;
    }

    if (auto v = guard.beginAction (name); ! v)
    {
        o.isError = true;
        o.finished = true;      // budget or kill switch: stop the whole session
        o.text = v.reason;
        return o;
    }
    if (! window.isAttached())
    {
        o.isError = true;
        o.finished = true;
        o.text = "The DSP window is gone.";
        return o;
    }

    if (name == "ui_tree")        return toolUiTree();
    if (name == "screenshot")     return toolScreenshot();
    if (name == "set_value" || name == "toggle" || name == "select_item" || name == "invoke") return toolWrite (name, in);
    if (name == "read_value")     return toolReadValue (in);
    if (name == "click_xy")       return toolClick (in);
    if (name == "type_text")      return toolType (in);
    if (name == "key_combo")      return toolKey (in);
    if (name == "verify_channel") return toolVerify (in);
    if (name == "save_map")       return toolSaveMap (in);

    o.isError = true;
    o.text = "Unknown tool \"" + name + "\".";
    return o;
}

DspOperatorAgent::Outcome DspOperatorAgent::toolUiTree()
{
    Outcome o;
    const auto wb = window.windowBounds();
    const auto nodes = window.snapshot (opts.treeMaxNodes, opts.treeMaxDepth);
    ids.clear();
    juce::String t;
    int shown = 0;
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        const auto& n = nodes[i].info;
        if (n.screenBounds.isEmpty())
            continue;
        if (opts.compact && n.name.isEmpty() && n.value.isEmpty()
            && ! (n.supportsRange || n.supportsValue || n.supportsToggle || n.supportsInvoke || n.supportsSelection))
            continue;     // pure layout containers: noise for a small model
        const int id = (int) i + 1;
        ids[id] = n;
        const auto b = n.screenBounds.translated (-wb.getX(), -wb.getY());
        t << juce::String::repeatedString ("  ", nodes[i].depth) << "[" << id << "] " << n.controlType;
        if (n.name.isNotEmpty())         t << " \"" << n.name.substring (0, 60) << "\"";
        if (n.value.isNotEmpty())        t << " value=\"" << n.value.substring (0, 60) << "\"";
        if (n.automationId.isNotEmpty() && ! opts.compact) t << " autoid=" << n.automationId.substring (0, 40);
        t << " @" << juce::roundToInt (b.getX() * viewScale) << "," << juce::roundToInt (b.getY() * viewScale);
        if (! opts.compact)
            t << " " << juce::roundToInt (b.getWidth() * viewScale) << "x" << juce::roundToInt (b.getHeight() * viewScale);
        juce::StringArray f;
        if (n.supportsRange)     f.add ("range " + juce::String (n.rangeMin) + ".." + juce::String (n.rangeMax));
        if (n.supportsValue)     f.add ("value");
        if (n.supportsToggle)    f.add ("toggle");
        if (n.supportsInvoke)    f.add ("invoke");
        if (n.supportsSelection) f.add ("select");
        if (n.supportsExpand)    f.add ("expand");
        if (f.size() > 0)        t << " {" << f.joinIntoString (",") << "}";
        t << "\n";
        ++shown;
    }
    if ((int) nodes.size() >= opts.treeMaxNodes)
        t << "(tree truncated at " << opts.treeMaxNodes << " nodes)\n";
    if (shown == 0)
        t = "No UI Automation elements found: the window is probably custom-drawn. Use screenshot and click_xy/type_text.";
    o.text = t;
    return o;
}

DspOperatorAgent::Outcome DspOperatorAgent::toolScreenshot()
{
    Outcome o;
    if (screenshotsTaken >= opts.maxScreenshots)
    {
        o.isError = true;
        o.text = "Screenshot budget used up (" + juce::String (opts.maxScreenshots) + "). Rely on ui_tree and read_value.";
        return o;
    }
    if (! window.screenshot (opts.screenshotMaxEdge, o.image))
    {
        o.isError = true;
        o.text = "Could not capture the window (is it minimised?).";
        return o;
    }
    ++screenshotsTaken;
    viewScale = o.image.scale;
    o.hasImage = true;
    o.text = "Window screenshot " + juce::String (o.image.width) + "x" + juce::String (o.image.height) + " px. click_xy uses pixels of this image.";
    return o;
}

bool DspOperatorAgent::resolve (const juce::var& in, uia::ElementInfo& info, juce::String& error)
{
    const int id = (int) in.getProperty ("element_id", 0);
    auto it = ids.find (id);
    if (it == ids.end())
    {
        error = "Unknown element_id " + juce::String (id) + ". Call ui_tree and use an id from the latest result.";
        return false;
    }
    uia::ElementInfo live;
    if (! window.find (it->second.path, live))
    {
        error = "Element " + juce::String (id) + " no longer exists (UI changed). Call ui_tree again.";
        return false;
    }
    if (auto v = SafetyGuard::checkElementInWindow (live.screenBounds, window.windowBounds()); ! v)
    {
        error = v.reason;
        return false;
    }
    info = it->second;
    return true;
}

SafetyGuard::Verdict DspOperatorAgent::declared (const juce::var& in, juce::String& param, int& output0)
{
    param = in.getProperty ("param", {}).toString().trim();
    output0 = hasInt (in, "output") ? (int) in.getProperty ("output", 0) - 1 : -1;
    if (param.isEmpty())
        return SafetyGuard::Verdict::deny ("Declare param (what this control changes).");
    if (! isNav (param) && output0 < 0)
        return SafetyGuard::Verdict::deny ("Declare output (1-based) for param " + param + ".");
    return SafetyGuard::Verdict::allow();
}

bool DspOperatorAgent::knownRole (const uia::ElementInfo& info, Role& out) const
{
    const auto key = info.path.describe();
    auto it = elementRoles.find (key);
    if (it != elementRoles.end())
    {
        out = it->second;
        return true;
    }
    for (auto& kv : profile.bindings)          // controls already saved in the map
        if (kv.second.path.steps.size() > 0 && kv.second.path.describe() == key)
        {
            const auto p = kv.first.upToFirstOccurrenceOf ("@", false, false);
            out.param = (p == "channel.select" || p == "peq.select" || p == "save") ? juce::String ("nav") : p;
            out.output = kv.first.contains ("@c") ? kv.first.fromFirstOccurrenceOf ("@c", false, false).getIntValue() : -1;
            out.navSeq = -1;
            return true;
        }
    return false;
}

void DspOperatorAgent::rememberRole (const uia::ElementInfo& info, const juce::String& param, int out0)
{
    Role r;
    r.param = isNav (param) ? juce::String ("nav") : param;
    r.output = isNav (param) ? -1 : out0;
    r.navSeq = navSeq;
    elementRoles[info.path.describe()] = r;
    if (isNav (param))
        ++navSeq;
}

const uia::ElementInfo* DspOperatorAgent::elementAtScreenPoint (juce::Point<int> screen) const
{
    const uia::ElementInfo* best = nullptr;
    long long bestArea = 0;
    for (auto& kv : ids)
    {
        const auto& b = kv.second.screenBounds;
        if (b.isEmpty() || ! b.contains (screen))
            continue;
        const long long area = (long long) b.getWidth() * b.getHeight();
        if (best == nullptr || area < bestArea) { best = &kv.second; bestArea = area; }
    }
    return best;
}

SafetyGuard::Verdict DspOperatorAgent::checkTarget (const juce::String& tool, const juce::String& param, int out0, double value,
                                                    const juce::String& text, const uia::ElementInfo* info)
{
    using V = SafetyGuard::Verdict;
    const bool nav = isNav (param);

    // The label is the model's claim; the target control decides.
    if (info != nullptr)
    {
        Role known;
        if (knownRole (*info, known))
        {
            const bool knownNav = isNav (known.param);
            if (nav != knownNav || (! nav && known.param != param))
                return V::deny ("That control was identified as \"" + known.param + "\""
                                + (known.output >= 0 ? " of output " + juce::String (known.output + 1) : juce::String())
                                + ", not \"" + param + "\". A control cannot change role.");
            if (! nav && known.output >= 0 && known.output != out0 && known.navSeq == navSeq)
                return V::deny ("That control was used for output " + juce::String (known.output + 1) + " and you now say output "
                                + juce::String (out0 + 1) + ". Select the other output first (param nav) if one editor serves all outputs.");
        }
        else if (nav)
        {
            const juce::String ident = info->name + " " + info->automationId;
            const bool valueLike = info->supportsRange || (info->supportsValue && ! info->supportsSelection && tool != "select_item");
            if (tool == "set_value" || tool == "type_text")
                return V::deny ("A control that takes a value is never navigation; declare what it changes (gain, hp.freq ...).");
            if (valueLike && tool != "click_xy")
                return V::deny ("That control holds a value, so it is not navigation: declare what it changes.");
            if (SafetyGuard::looksLikeParameterControl (ident) && ! (info->supportsValue && tool == "click_xy"))
                return V::deny ("\"" + info->name + "\" looks like an audio parameter control, not navigation: declare what it changes.");
        }
    }
    else if (nav && (tool == "set_value" || tool == "type_text"))
        return V::deny ("Free-form text goes into a parameter: declare what it changes (gain, hp.freq ...) and the output; nav/save are not allowed here.");

    if (nav)
        return V::allow();
    return guard.checkToolWrite (param, out0, value, text);
}

DspOperatorAgent::Outcome DspOperatorAgent::toolWrite (const juce::String& name, const juce::var& in)
{
    Outcome o;
    juce::var inp = in;
    if (name == "invoke" && ! inp.hasProperty ("param"))
        inp.getDynamicObject()->setProperty ("param", "nav");

    double value = std::numeric_limits<double>::quiet_NaN();
    if (name == "set_value")   value = numberFrom (inp.getProperty ("value", {}).toString());
    if (name == "toggle")      value = (bool) inp.getProperty ("on", false) ? 1.0 : 0.0;
    const auto itemText = name == "select_item" ? inp.getProperty ("item", {}).toString()
                        : name == "set_value"   ? inp.getProperty ("value", {}).toString() : juce::String();

    juce::String param;
    int out0 = -1;
    if (auto v = declared (inp, param, out0); ! v)
    {
        o.isError = true;
        o.text = "Refused by safety rules: " + v.reason;
        return o;
    }

    uia::ElementInfo info;
    juce::String err;
    if (! resolve (inp, info, err))
    {
        o.isError = true;
        o.text = err;
        return o;
    }
    if (auto v = checkTarget (name, param, out0, value, itemText, &info); ! v)
    {
        o.isError = true;
        o.text = "Refused by safety rules: " + v.reason;
        return o;
    }
    const bool tweeterOut = ! isNav (param) && out0 >= 0 && guard.isTweeter (out0);
    if (tweeterOut && (param == "hp.type" || param == "hp.freq"))
        guard.noteHpReadback (out0, std::numeric_limits<double>::quiet_NaN());     // must be read back again after this write

    bool ok = false;
    if (name == "set_value")
    {
        const auto text = inp.getProperty ("value", {}).toString();
        if (info.supportsRange && ! info.supportsValue && std::isfinite (value))
            ok = window.setRange (info.path, value, err);
        else
            ok = window.setText (info.path, text, err);
    }
    else if (name == "toggle")      ok = window.setToggle (info.path, value > 0.5, err);
    else if (name == "select_item") ok = window.selectItem (info.path, inp.getProperty ("item", {}).toString(), err);
    else                            ok = window.invoke (info.path, err);

    if (! ok)
    {
        o.isError = true;
        o.text = "Action failed: " + err;
        return o;
    }
    if (opts.settleMs > 0)
        juce::Thread::sleep (opts.settleMs);

    rememberRole (info, param, out0);
    if (param == "mute" && name == "toggle")
        guard.noteMute (out0, value > 0.5);
    if (param == "gain" && std::isfinite (value))
        guard.noteGain (out0, value);
    if (tweeterOut && param == "hp.freq" && name == "set_value")
        guard.noteHpReadback (out0, numberFrom (window.readValue (info.path)));

    o.text = "OK.";
    bool st = false;
    if (name == "toggle" && window.readToggle (info.path, st))
        o.text += " State now " + juce::String (st ? "on" : "off") + (st != (value > 0.5) ? " (DIFFERS from requested)." : ".");
    else if (name == "set_value" || name == "select_item")
        o.text += " Read back: \"" + window.readValue (info.path) + "\".";
    return o;
}

DspOperatorAgent::Outcome DspOperatorAgent::toolReadValue (const juce::var& in)
{
    Outcome o;
    uia::ElementInfo info;
    juce::String err;
    if (! resolve (in, info, err))
    {
        o.isError = true;
        o.text = err;
        return o;
    }
    bool st = false;
    const auto read = window.readValue (info.path);
    o.text = "value=\"" + read + "\"";
    if (window.readToggle (info.path, st))
        o.text += " toggle=" + juce::String (st ? "on" : "off");
    Role role;
    if (knownRole (info, role) && role.param == "hp.freq" && role.output >= 0 && guard.isTweeter (role.output))
        guard.noteHpReadback (role.output, numberFrom (read));
    return o;
}

DspOperatorAgent::Outcome DspOperatorAgent::toolClick (const juce::var& in)
{
    Outcome o;
    juce::String param;
    int out0 = -1;
    if (auto v = declared (in, param, out0); ! v)
    {
        o.isError = true;
        o.text = "Refused by safety rules: " + v.reason;
        return o;
    }
    const juce::Point<int> real (juce::roundToInt ((int) in.getProperty ("x", 0) / viewScale),
                                 juce::roundToInt ((int) in.getProperty ("y", 0) / viewScale));
    const auto wb = window.windowBounds();
    if (auto v = SafetyGuard::checkPointInClient (real, wb, window.clientBounds()); ! v)
    {
        o.isError = true;
        o.text = "Refused: " + v.reason;
        return o;
    }

    // What is under the point decides what this click is, not the label.
    const juce::Point<int> screen (wb.getX() + real.x, wb.getY() + real.y);
    const auto* under = elementAtScreenPoint (screen);
    double value = std::numeric_limits<double>::quiet_NaN();
    if (in.hasProperty ("on") && ! in["on"].isVoid())
        value = (bool) in.getProperty ("on", false) ? 1.0 : 0.0;
    if (auto v = checkTarget ("click_xy", param, out0, value, {}, under); ! v)
    {
        o.isError = true;
        o.text = "Refused by safety rules: " + v.reason;
        return o;
    }
    if (under == nullptr && isNav (param))      // custom-drawn: a saved non-nav click point at this spot is not navigation
        for (auto& kv : profile.bindings)
        {
            const auto& cb = kv.second;
            if ((cb.method == ControlBinding::Method::clickPoint || cb.method == ControlBinding::Method::clickAndType)
                && std::abs (cb.relX * wb.getWidth() - real.x) < 10 && std::abs (cb.relY * wb.getHeight() - real.y) < 10)
            {
                const auto p = kv.first.upToFirstOccurrenceOf ("@", false, false);
                if (p != "channel.select" && p != "peq.select" && p != "save")
                {
                    o.isError = true;
                    o.text = "Refused by safety rules: that point is the saved control \"" + p + "\", not navigation: declare what it changes.";
                    return o;
                }
            }
        }

    juce::String err;
    if (! window.clickAt (real, (bool) in.getProperty ("double_click", false), err))
    {
        o.isError = true;
        o.text = "Click not sent: " + err;
        return o;
    }
    clickedRole = {};
    if (under != nullptr)
        knownRole (*under, clickedRole);
    if (! isNav (param))
    {
        if (param == "mute" && std::isfinite (value))
            guard.noteMute (out0, value > 0.5);
        if (guard.isTweeter (out0) && (param == "hp.freq" || param == "hp.type"))
            guard.noteHpReadback (out0, std::numeric_limits<double>::quiet_NaN());
    }
    else
        ++navSeq;
    if (opts.settleMs > 0)
        juce::Thread::sleep (opts.settleMs);
    o.text = "Clicked.";
    return o;
}

DspOperatorAgent::Outcome DspOperatorAgent::toolType (const juce::var& in)
{
    Outcome o;
    const auto text = in.getProperty ("text", {}).toString();
    juce::String param;
    int out0 = -1;
    if (auto v = declared (in, param, out0); ! v)
    {
        o.isError = true;
        o.text = "Refused by safety rules: " + v.reason;
        return o;
    }
    const double value = numberFrom (text);
    if (auto v = checkTarget ("type_text", param, out0, value, text, nullptr); ! v)
    {
        o.isError = true;
        o.text = "Refused by safety rules: " + v.reason;
        return o;
    }
    if (clickedRole.param.isNotEmpty() && (isNav (clickedRole.param) || clickedRole.param != param
                                           || (clickedRole.output >= 0 && clickedRole.output != out0)))
    {
        o.isError = true;
        o.text = "Refused by safety rules: the control you clicked is \"" + clickedRole.param + "\", not \"" + param + "\".";
        return o;
    }
    if (guard.isTweeter (out0) && (param == "hp.freq" || param == "hp.type"))
        guard.noteHpReadback (out0, std::numeric_limits<double>::quiet_NaN());   // typed blind: cannot be read back from here

    juce::String err;
    if (! window.typeText (text, (bool) in.getProperty ("press_enter", false), err))
    {
        o.isError = true;
        o.text = "Text not sent: " + err;
        return o;
    }
    if (param == "gain" && std::isfinite (value))
        guard.noteGain (out0, value);
    if (opts.settleMs > 0)
        juce::Thread::sleep (opts.settleMs);
    o.text = "Typed.";
    return o;
}

DspOperatorAgent::Outcome DspOperatorAgent::toolKey (const juce::var& in)
{
    Outcome o;
    const auto combo = in.getProperty ("combo", {}).toString();
    if (auto v = SafetyGuard::checkKeyCombo (combo); ! v)
    {
        o.isError = true;
        o.text = "Refused: " + v.reason;
        return o;
    }
    // Arrow / Home / End / Page keys, typed characters, Delete ... change the value of whatever has focus: that is a
    // parameter write the guard cannot classify. Only navigation / commit keys are allowed here.
    const auto norm = combo.toLowerCase().removeCharacters (" ");
    static const char* const allowed[] = { "tab", "shift+tab", "escape", "esc", "return", "enter", "ctrl+a", "f2" };
    bool permitted = false;
    for (auto* a : allowed)
        if (norm == a) permitted = true;
    if (! permitted)
    {
        o.isError = true;
        o.text = "Refused by safety rules: key \"" + combo + "\" can change the value of the focused control. Only Tab, Escape, Return, F2 and ctrl+a "
                 "are allowed; enter values with set_value, or click_xy + type_text with param and output.";
        return o;
    }
    juce::String err;
    if (! window.keyCombo (combo, err))
    {
        o.isError = true;
        o.text = err;
        return o;
    }
    o.text = "Pressed " + combo + ".";
    return o;
}

DspOperatorAgent::Outcome DspOperatorAgent::toolVerify (const juce::var& in)
{
    Outcome o;
    if (opts.verify == nullptr)
    {
        o.text = "verify_channel is not available in this session; rely on labels and be conservative.";
        return o;
    }
    VerifyRequest rq;
    rq.output = (int) in.getProperty ("output", 1) - 1;
    rq.mode = in.getProperty ("mode", "tone").toString();
    const auto r = opts.verify (rq);
    if (r.performed && r.matches)
        verifiedOutputs.insert (rq.output);
    else if (r.performed)
        verifiedOutputs.erase (rq.output);
    updateVerifiedState();
    if (! r.performed)
        o.text = "Test not performed: " + r.detail;
    else
        o.text = juce::String (r.matches ? "The test sound came from the expected output. " : "MISMATCH: ")
                 + "Responded driver: " + r.respondedDriver + (r.respondedBand.isNotEmpty() ? ", band: " + r.respondedBand : juce::String())
                 + (r.detail.isNotEmpty() ? ". " + r.detail : juce::String());
    return o;
}

void DspOperatorAgent::updateVerifiedState()
{
    std::set<int> good = verifiedOutputs;
    for (int o : priorVerified)
        if (touchedOutputs.count (o) == 0)
            good.insert (o);
    profile.verifiedOutputs.assign (good.begin(), good.end());
    bool all = ! outputsInUse.empty();
    for (auto& ch : outputsInUse)
        all = all && good.count (ch.dspOutput) > 0;
    profile.verified = all;
}

DspOperatorAgent::Outcome DspOperatorAgent::toolSaveMap (const juce::var& in)
{
    Outcome o;
    using M = ControlBinding::Method;
    static const std::map<juce::String, M> methods {
        { "text", M::uiaText }, { "range", M::uiaRange }, { "toggle", M::uiaToggle }, { "invoke", M::uiaInvoke },
        { "select", M::uiaSelect }, { "click_point", M::clickPoint }, { "click_and_type", M::clickAndType } };

    juce::StringArray keys;
    for (auto& s : teachSlots())
        keys.add (s.key);

    const auto wb = window.windowBounds();
    int accepted = 0;
    juce::StringArray problems;

    if (auto* arr = in.getProperty ("bindings", {}).getArray())
    {
        for (auto& b : *arr)
        {
            const auto key = b.getProperty ("key", {}).toString();
            const auto mname = b.getProperty ("method", {}).toString();
            if (! keys.contains (key)) { problems.add ("unknown key \"" + key + "\""); continue; }
            auto mit = methods.find (mname);
            if (mit == methods.end()) { problems.add (key + ": unknown method \"" + mname + "\""); continue; }

            // Every output a binding drives must have passed verify_channel first (when the app can run that test).
            const int chOut = hasInt (b, "output") ? (int) b.getProperty ("output", 1) - 1 : -1;
            {
                std::vector<int> touched;
                if (key != "save")
                {
                    if (chOut >= 0) touched.push_back (chOut);
                    else for (auto& oc : outputsInUse) touched.push_back (oc.dspOutput);
                }
                juce::StringArray unverified;
                for (int t : touched)
                {
                    touchedOutputs.insert (t);
                    if (opts.verify != nullptr && verifiedOutputs.count (t) == 0)
                        unverified.add (juce::String (t + 1));
                }
                if (unverified.size() > 0)
                {
                    problems.add (key + ": output " + unverified.joinIntoString (", ") + " has not passed verify_channel (call verify_channel for it first; a tweeter's role must be confirmed before it is mapped)");
                    continue;
                }
            }

            ControlBinding cb;
            cb.method = mit->second;
            const bool click = cb.method == M::clickPoint || cb.method == M::clickAndType;
            if (click)
            {
                if (wb.isEmpty() || ! hasInt (b, "x") || ! hasInt (b, "y")) { problems.add (key + ": x,y required"); continue; }
                const double rx = (double) (int) b.getProperty ("x", 0) / viewScale / wb.getWidth();
                const double ry = (double) (int) b.getProperty ("y", 0) / viewScale / wb.getHeight();
                if (rx < 0 || rx >= 1 || ry < 0 || ry >= 1) { problems.add (key + ": point outside the window"); continue; }
                cb.relX = rx;
                cb.relY = ry;
            }
            else
            {
                auto it = ids.find ((int) b.getProperty ("element_id", 0));
                if (it == ids.end()) { problems.add (key + ": unknown element_id (call ui_tree again before save_map)"); continue; }
                cb.path = it->second.path;
                cb.capturedDescription = it->second.controlType + " \"" + it->second.name + "\"";
            }
            cb.unit = b.getProperty ("unit", {}).toString();
            cb.decimals = hasInt (b, "decimals") ? (int) b.getProperty ("decimals", 2) : 2;
            cb.scale = hasInt (b, "scale") ? (double) b.getProperty ("scale", 1.0) : 1.0;
            cb.offset = (double) b.getProperty ("offset", 0.0);
            cb.invertToggle = (bool) b.getProperty ("invert_toggle", false);
            cb.settleMs = hasInt (b, "settle_ms") ? (int) b.getProperty ("settle_ms", 60) : 60;
            if (click && b.getProperty ("description", {}).toString().isNotEmpty())
                cb.capturedDescription = b.getProperty ("description", {}).toString();
            if (auto* vm = b.getProperty ("value_map", {}).getDynamicObject())
                for (auto& p : vm->getProperties())
                    cb.valueMap[p.name.toString()] = p.value.toString();

            const int ch = hasInt (b, "output") ? (int) b.getProperty ("output", 1) - 1 : -1;
            const int band = hasInt (b, "band") ? (int) b.getProperty ("band", 1) - 1 : -1;
            profile.bindings[DspProfile::keyFor (key, ch, band)] = cb;
            ++accepted;
        }
    }

    updateVerifiedState();
    profile.aiLearned = true;
    profile.learnedAt = juce::Time::getCurrentTime().toISO8601 (true);
    if (in.getProperty ("dsp_name", {}).toString().isNotEmpty())           profile.name = in.getProperty ("dsp_name", {}).toString();
    if (in.getProperty ("window_title", {}).toString().isNotEmpty())       profile.windowTitleContains = in.getProperty ("window_title", {}).toString();
    if (in.getProperty ("process_name", {}).toString().isNotEmpty())       profile.processName = in.getProperty ("process_name", {}).toString();
    if (in.getProperty ("notes", {}).toString().isNotEmpty())              profile.notes = in.getProperty ("notes", {}).toString();

    o.text = "Saved " + juce::String (accepted) + " binding(s); total " + juce::String ((int) profile.bindings.size()) + ".";
    if (problems.size() > 0)
    {
        o.isError = accepted == 0;
        o.text += " Rejected: " + problems.joinIntoString ("; ") + ".";
    }
    return o;
}
} // namespace fa
