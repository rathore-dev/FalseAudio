#pragma once

#include "DspController.h"
#include "DspProfile.h"
#include "../ai/AiProfileStore.h"
#include "../ai/ApiKeyStore.h"
#include "../ai/ClaudeClient.h"
#include "../ai/LlmClient.h"
#include "../ai/OllamaClient.h"
#include "../ai/DspOperatorAgent.h"
#include "../ai/SafetyGuard.h"
#include "../ai/WindowBackend.h"
#include <memory>
#include <mutex>
#include <set>

namespace fa
{
/** DspController that drives ANY vendor DSP software without a human teaching it.

    - connect(): attaches the window and loads a previously learned map (AiProfileStore).
    - learn(): lets Claude (DspOperatorAgent) work out the controls and saves the map. Blocking,
      costs API money - call it from a worker thread, after the user consented.
    - applyChannel / setMuted / soloChannels / batch: deterministic - they replay the saved map
      through the window with NO API calls. Every value passes SafetyGuard first, every write is read
      back and compared. The AI is called again only when a control cannot be found or a read-back is
      far off (self-heal, at most Options::maxHeals times per session).

    Safety (SafetyGuard, in code): protected tweeter HP, per-step gain cap, tweeters are muted before
    crossover/polarity changes (refused if no mute control is mapped), window-bounds, budgets, kill().

    Usage by the UI: build Options, construct, connect(); if (! hasMap()) learn() on a thread; kill()
    stops everything immediately; getStats() feeds a cost/progress display. */
class AiDspController : public DspController
{
public:
    struct Options
    {
        std::shared_ptr<WindowBackend> backend;            // tests inject a fake; null = real UIA on windowProvider
        std::function<void*()> windowProvider;             // HWND of the attached DSP software
        std::vector<OutputChannel> outputs;                // the system's outputs (what has to be mapped)
        DspCapabilities caps;

        LlmProvider provider = LlmProvider::claude;        // claude (default) | ollama (local, free, no key)
        ClaudeClient::Config client;                       // model claude-opus-5-5, effort
        OllamaClient::Config ollama;                       // used when provider == ollama
        std::shared_ptr<ClaudeTransport> transport;        // null = WinHTTP; tests inject recorded responses
        ClaudeClient::ApiKeyProvider apiKey;               // null = ApiKeyStore().resolve()
        ClaudeClient::SleepFn sleep;                       // tests: no-op

        SafetyGuard::Limits limits;
        DspOperatorAgent::Options agent;                   // consentGiven, verify, log, progress ...
        juce::File profilesFolder;                         // empty = %APPDATA%\FalseAudio\profiles
        int maxHeals = 2;                                  // AI re-learns allowed per session
        int settleMs = 60;                                 // after each UI write, before read-back
        bool allowLearning = true;                         // false: never call the AI (replay only)
        bool requireVerifiedMap = true;                    // applyChannel refuses a map that did not pass verify_channel for every output
    };

    struct Stats
    {
        ClaudeUsage usage;
        double costUsd = 0.0;
        int learnRuns = 0, writes = 0, verifiedWrites = 0, unverifiedWrites = 0, mismatches = 0, heals = 0;
    };

    explicit AiDspController (Options options);

    juce::String getName() const override;
    bool isAutomatic() const override { return true; }
    DspCapabilities getCapabilities() const override { std::lock_guard<std::mutex> l (stateMutex); return ! profile.bindings.empty() ? profile.caps : opts.caps; }
    bool connect (juce::String& error) override;
    bool isConnected() const override { return connected.load(); }

    bool applyChannel (const OutputChannel& channel, juce::String& error) override;
    bool setMuted (const OutputChannel& channel, bool muted, juce::String& error) override;
    bool soloChannels (const SystemConfig& cfg, const juce::StringArray& ids, juce::String& error) override;
    bool beginBatch (juce::String&) override { batching = true; return true; }
    bool endBatch (juce::String& error) override;

    /** True when a learned map is loaded. */
    bool hasMap() const { std::lock_guard<std::mutex> l (stateMutex); return ! profile.bindings.empty(); }
    /** Diagnostics of the most recent learn() run, successful or not (a failed run is never adopted or saved). */
    struct LastRun { DspProfile profile; int turns = 0, actions = 0; bool success = false; juce::String message; };
    LastRun getLastRun() const { std::lock_guard<std::mutex> l (stateMutex); return lastRun; }
    /** Snapshot copy (the worker may be learning). */
    DspProfile getProfile() const { std::lock_guard<std::mutex> l (stateMutex); return profile; }
    /** True when the loaded/learned map passed verify_channel for every output. Legacy and unverified maps are false. */
    bool isMapVerified() const;
    /** Re-verifies a loaded map before a tune is pushed through it: every output is muted, then (tweeter: protected HP written and
        read back first) unmuted alone while the app plays a test sound that must come from the expected driver. Blocking, worker thread. */
    bool verifyMap (juce::String& error);

    /** Builds the LLM client for Options::provider (Claude with the key provider, or Ollama). */
    std::unique_ptr<LlmClient> makeLlmClient() const;
    /** Agent options as actually used: for a local model it applies the small-model profile
        (compact prompt/tree, fewer turns and screenshots, harder downscale, consent implied: nothing leaves the PC). */
    DspOperatorAgent::Options effectiveAgentOptions() const;

    /** Lets the AI learn (or extend) the map and saves it. Blocking. */
    bool learn (juce::String& error);

    /** Kill switch: stops the agent and refuses every further write until resetSafety(). */
    void kill (const juce::String& why = "Stopped by user");
    void resetSafety();
    SafetyGuard& getGuard() noexcept { return *guard; }
    Stats getStats() const { std::lock_guard<std::mutex> l (stateMutex); return stats; }
    /** Messages about values the guard changed during the last applyChannel. */
    juce::StringArray getLastSafetyNotes() const { std::lock_guard<std::mutex> l (stateMutex); return lastNotes; }

private:
    enum class Write { ok, notBound, failed };

    Options opts;
    std::shared_ptr<WindowBackend> backend;
    std::unique_ptr<SafetyGuard> guard;
    AiProfileStore store;
    DspProfile profile;
    juce::String windowTitle;
    std::atomic<bool> connected { false };
    bool batching = false;
    bool lastWriteVerified = false;                       // the last performBinding had a real read-back
    bool selectConfirmed = true;                          // select-then-edit model: the channel selection was read back
    int selectedChannel = -1;
    mutable std::mutex stateMutex;                        // guards stats, lastNotes and the profile (UI thread reads them)
    Stats stats;
    juce::StringArray lastNotes;
    LastRun lastRun;
    std::map<int, OutputChannel> lastApplied;
    std::set<juce::String> badKeys;                       // keys that failed in the current call
    std::atomic<DspOperatorAgent*> activeAgent { nullptr };

    void log (const juce::String& s) const { if (opts.agent.log != nullptr) opts.agent.log (s); }
    bool selectChannel (int channel, juce::String& error);
    bool tweeterEditAllowed (int channel, juce::String& error) const;
    Write writeNumeric (const juce::String& key, int channel, int band, double value, bool allowMissingIfNeutral,
                        double neutral, juce::String& error);
    Write writeToggle (const juce::String& key, int channel, bool on, bool neutral, juce::String& error);
    Write writeCrossover (const juce::String& prefix, int channel, const FilterSpec& f, juce::String& error);
    bool performBinding (const ControlBinding& b, const juce::String& key, const juce::String& text, double numeric,
                         bool toggleValue, juce::String& error);
    bool applyOnce (const OutputChannel& ch, juce::String& error);
    bool setMutedOnce (int output, bool muted, juce::String& error);
    bool tryHeal (juce::String& error);
    template <typename F> void withStats (F&& f) { std::lock_guard<std::mutex> l (stateMutex); f (stats); }
    bool saveMap();
    bool loadMap();
};
} // namespace fa
