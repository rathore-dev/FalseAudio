#pragma once

#include <juce_graphics/juce_graphics.h>
#include "ClaudeClient.h"
#include "../tune/SystemConfig.h"
#include <atomic>
#include <map>
#include <mutex>

namespace fa
{
/** Safety enforced in code, between the AI (or a stored map) and the vendor window. Nothing the
    model says can switch these rules off. Thread-safe: kill() may be called from the UI thread
    while the agent runs on a worker.

    Rules:
      - a tweeter's high-pass is never below the planner's protected HP (CrossoverPlanner::safeHighPassHz)
      - gain changes are capped per step (and a first write with unknown prior value is capped low)
      - tweeter crossover / polarity changes require the channel to be muted first
      - actions must stay inside the attached window; dangerous key combos are refused
      - per-session budgets for actions, tokens and dollars
      - kill switch                                                                            */
class SafetyGuard
{
public:
    struct Limits
    {
        int maxActions = 600;                 // UI actions + tool calls per session
        double maxUsd = 5.0;                  // API spend per session
        juce::int64 maxTokens = 3000000;      // all token kinds per session
        double maxGainStepDb = 3.0;           // largest allowed gain change per write
        double maxFirstGainDb = 0.0;          // cap for a first write when the previous gain is unknown
        double minFreqHz = 10.0, maxFreqHz = 24000.0;
        SafetyLimits planner;                 // same limits the CrossoverPlanner uses
    };

    struct Verdict
    {
        bool ok = true;
        juce::String reason;
        explicit operator bool() const noexcept { return ok; }
        static Verdict allow() { return {}; }
        static Verdict deny (const juce::String& why) { Verdict v; v.ok = false; v.reason = why; return v; }
    };

    SafetyGuard (Limits limits, DspCapabilities caps);

    // ---- kill switch & budgets ---------------------------------------------------------------
    void kill (const juce::String& why = "Stopped by user");
    bool isKilled() const noexcept { return killed.load(); }
    juce::String killReason() const;
    void resetSession();                       // clears kill flag, counters and learned state

    /** Call before every UI action / tool call. Counts it and checks kill switch and budgets. */
    Verdict beginAction (const juce::String& what);
    void addUsage (const ClaudeUsage& u, double usd);
    Verdict checkBudget() const;
    int actionsUsed() const noexcept { return actions.load(); }
    double usdSpent() const;
    juce::int64 tokensUsed() const;
    const Limits& getLimits() const noexcept { return limits; }
    const DspCapabilities& getCaps() const noexcept { return caps; }

    // ---- geometry ----------------------------------------------------------------------------
    /** Window-relative point (0,0 = top-left of the window). */
    static Verdict checkPointInWindow (juce::Point<int> rel, juce::Rectangle<int> windowScreenBounds);
    /** An element found by UIA must lie (at least by its centre) inside the attached window. */
    static Verdict checkElementInWindow (juce::Rectangle<int> elementScreenBounds, juce::Rectangle<int> windowScreenBounds);
    /** Window-relative point that must be in the CLIENT area: not the title bar, borders or system buttons.
        `clientScreenBounds` is the client rectangle in screen coordinates (empty = use the whole window). */
    static Verdict checkPointInClient (juce::Point<int> rel, juce::Rectangle<int> windowScreenBounds, juce::Rectangle<int> clientScreenBounds);
    /** Refuses alt+f4, ctrl+w/q, win-key combos, alt+tab, ctrl+alt+del. */
    static Verdict checkKeyCombo (const juce::String& combo);

    // ---- channel knowledge -------------------------------------------------------------------
    /** Registers the outputs (role, protected HP) the AI may touch. */
    void setChannels (const std::vector<OutputChannel>& channels);
    double protectedHpHz (const OutputChannel& ch) const;
    void noteMute (int output, bool muted);
    bool isMuted (int output) const;
    void noteGain (int output, double gainDb);        // known current value (e.g. read back)
    bool knownGain (int output, double& gainDb) const;
    /** Read-back of a tweeter's high-pass frequency (Hz; NaN forgets it). Unmuting a tweeter needs a read-back >= protected HP. */
    void noteHpReadback (int output, double hz);
    bool hpVerified (int output) const;
    bool isTweeter (int output) const;
    /** Free text (item / typed text) that switches a filter off: "Off", "Bypass", "None", "Flat", "Full range"... */
    static bool isOffFilterText (const juce::String& text);
    /** True when a control's name suggests it changes an audio parameter (so it can never be a "nav" control). */
    static bool looksLikeParameterControl (const juce::String& nameOrId);

    // ---- value rules (deterministic path) ----------------------------------------------------
    /** Clamps `next` in place: protected tweeter HP, gain step/range, delay, PEQ and crossover
        ranges. Every change is described in `notes`. Returns true if anything was changed. */
    bool sanitizeChannel (OutputChannel& next, juce::StringArray& notes);

    /** Intermediate gain values (excluding the target) that walk from the known current gain to
        `target` in steps of at most maxGainStepDb. Empty when the gain is unknown or within one step. */
    std::vector<double> gainSteps (int output, double target) const;

    /** True when a tweeter crossover or polarity change must be preceded by muting the channel. */
    bool tweeterNeedsMuteFirst (const OutputChannel& prevApplied, bool havePrev, const OutputChannel& next) const;

    // ---- value rules (AI tool path) ----------------------------------------------------------
    /** Validates one write requested by the AI. `param` is one of: nav, save (no audio value), gain, delay, polarity, mute,
        hp.freq, hp.type, lp.freq, lp.type, peq.freq, peq.gain, peq.q. `value` is the numeric
        value (0/1 for toggles); pass NaN only for type selections (hp.type / lp.type), whose chosen entry goes in `text`.
        Numeric and toggle params refuse NaN/inf. A tweeter's hp.type may never be Off/bypass, and a tweeter may only be
        unmuted (mute value 0) after its high-pass was read back at or above the protected frequency.
        "nav" / "save" are allowed here: the CALLER must have checked that the target control really is navigation. */
    Verdict checkToolWrite (const juce::String& param, int output, double value, const juce::String& text = {});

    static bool isKnownParam (const juce::String& param);

private:
    struct ChannelInfo { DriverRole role = DriverRole::midbass; double protectedHp = 0.0; juce::String label; };

    Limits limits;
    DspCapabilities caps;
    mutable std::mutex m;
    std::atomic<bool> killed { false };
    std::atomic<int> actions { 0 };
    juce::String killWhy;
    double usd = 0.0;
    juce::int64 tokens = 0;
    std::map<int, ChannelInfo> channels;
    std::map<int, bool> muted;
    std::map<int, double> gains;
    std::map<int, double> hpReadback;
};
} // namespace fa
