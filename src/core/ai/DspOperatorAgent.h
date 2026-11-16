#pragma once

#include "ClaudeClient.h"
#include "LlmClient.h"
#include <juce_graphics/juce_graphics.h>
#include "SafetyGuard.h"
#include "WindowBackend.h"
#include "../bridge/DspProfile.h"
#include <atomic>

namespace fa
{
/** The AI operator: an agent loop in which Claude looks at a vendor DSP window (UIA tree +
    screenshots) and works out which control is which output's gain, delay, polarity, HP/LP, PEQ
    band and mute. What it learns is returned as a DspProfile (the existing profile format) so the
    next run applies it deterministically without any API call.

    The model only ever sees the attached window. Every value it writes passes SafetyGuard first.
    Output numbers in all tool calls are 1-based (as printed on DSP software). Coordinates are in
    "view pixels": real window pixels times viewScale (<= 1), the same space as screenshots.

    Tools offered: ui_tree, screenshot, set_value, invoke, select_item, toggle, read_value,
    click_xy, type_text, key_combo, verify_channel, save_map, done, fail. (The Anthropic computer
    toolset is not used: its screenshot captures the whole display and it has no window scoping,
    so custom window-relative tools are used instead.)

    Blocking: run learn() on a worker thread; cancel() / SafetyGuard::kill() may be called from any thread. */
class DspOperatorAgent
{
public:
    struct VerifyRequest { int output = 0; juce::String mode = "tone"; };   // output is 0-based here
    struct VerifyResult
    {
        bool performed = false;           // false: the app could not play a test (no audio / not live)
        bool matches = false;             // the claimed output is the one that responded
        juce::String respondedDriver;     // e.g. "Front Left Tweeter"
        juce::String respondedBand;       // e.g. "high (4-20 kHz)"
        juce::String detail;
    };
    /** The app solo-plays a tone/sweep on the physical output and reports what responded. */
    using VerifyFn = std::function<VerifyResult (const VerifyRequest&)>;
    using LogFn = std::function<void (const juce::String&)>;

    struct Progress
    {
        juce::String stage;               // human readable current step
        int turn = 0, actions = 0, bindings = 0;
        double usd = 0.0;
        juce::int64 tokens = 0;
    };
    using ProgressFn = std::function<void (const Progress&)>;

    struct Options
    {
        bool consentGiven = false;        // the user agreed that screenshots/UI text go to Anthropic
        int maxTurns = 80;
        int maxScreenshots = 25;
        int screenshotMaxEdge = 2000;     // API limit is 2576 px on the long edge
        int treeMaxNodes = 500, treeMaxDepth = 14;
        int settleMs = 80;                // wait after a UI write before reading it back
        juce::String vendorHint;          // e.g. "Helix PC-Tool", optional
        LogFn log;
        ProgressFn progress;
        VerifyFn verify;                  // may be empty: verify_channel then reports "not available"
        bool compact = false;             // small local model: short prompt, pruned tree, strict tool-argument validation
    };

    struct Result
    {
        bool success = false;
        bool cancelled = false;
        juce::String message;             // summary from done(), or the failure reason
        DspProfile profile;               // bindings learned (merged over the input profile)
        ClaudeUsage usage;
        double costUsd = 0.0;
        int turns = 0, actions = 0;
    };

    DspOperatorAgent (LlmClient& client, WindowBackend& window, SafetyGuard& guard, Options options);

    /** Learns (or repairs) the control map. `existing` is kept and extended. `focusKeys` (optional)
        lists binding keys (DspProfile::keyFor style) that failed and must be re-learned. */
    Result learn (const std::vector<OutputChannel>& outputs, const DspCapabilities& caps,
                  const DspProfile& existing, const juce::StringArray& focusKeys = {});

    void cancel() noexcept { cancelled = true; }

    static juce::String systemPrompt();
    static juce::String compactSystemPrompt();     // for small local models
    static juce::var toolDefinitions();

private:
    struct Outcome
    {
        juce::String text;
        bool isError = false;
        bool finished = false, success = false;
        bool hasImage = false;
        WindowScreenshot image;
    };

    Outcome runTool (const juce::String& name, const juce::var& input);
    /** Checks (and gently coerces) tool arguments against the tool schema. Returns "" when fine, else a retry message. */
    juce::String validateArgs (const juce::String& name, juce::var& input) const;
    Outcome toolUiTree();
    Outcome toolScreenshot();
    Outcome toolWrite (const juce::String& name, const juce::var& in);
    Outcome toolReadValue (const juce::var& in);
    Outcome toolClick (const juce::var& in);
    Outcome toolType (const juce::var& in);
    Outcome toolKey (const juce::var& in);
    Outcome toolVerify (const juce::var& in);
    Outcome toolSaveMap (const juce::var& in);

    bool resolve (const juce::var& in, uia::ElementInfo& info, juce::String& error);
    SafetyGuard::Verdict declared (const juce::var& in, double value, juce::String& param, int& output0);
    void say (const juce::String& s) const { if (opts.log != nullptr) opts.log (s); }
    void report (const juce::String& stage);

    LlmClient& client;
    WindowBackend& window;
    SafetyGuard& guard;
    Options opts;
    std::atomic<bool> cancelled { false };

    // per-run state
    std::map<int, uia::ElementInfo> ids;
    double viewScale = 1.0;
    int screenshotsTaken = 0;
    DspProfile profile;
    std::vector<OutputChannel> outputsInUse;
    int turn = 0;
};
} // namespace fa
