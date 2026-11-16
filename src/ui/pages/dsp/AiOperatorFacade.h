#pragma once

#include <JuceHeader.h>

/** UI-side seam between the AI operator panel and whatever runs the AI (src/core/ai + AiDspController).
    The panel only talks to this interface. Every callback is invoked on the MESSAGE THREAD, so a real
    binding must marshal worker-thread events with MessageManager::callAsync. */
namespace fa::ui::dsp
{
class AiOperatorFacade
{
public:
    virtual ~AiOperatorFacade() = default;

    enum class State { idle, running, done, stopped, error };
    enum class Step  { learn = 0, map, verify, apply, readback };
    enum class StepState { pending, active, done, failed };
    static constexpr int numSteps = 5;

    struct LogEntry
    {
        juce::String time;      // "12:03:41"
        juce::String kind;      // "see" | "click" | "type" | "read" | "think" | "safety" | "error"
        juce::String what;      // what the AI did ("Clicked Out 3 gain field")
        juce::String why;       // why ("to set -3.5 dB from the tune")
    };

    struct Cost
    {
        int calls = 0;
        int tokensIn = 0, tokensOut = 0;
        double usd = 0.0;
        double budgetUsd = 0.0;
    };

    struct MapOutput
    {
        juce::String output;        // "Out 1"
        juce::String driver;        // "Front L tweeter" or "(unassigned)"
        juce::StringArray found;    // e.g. Gain, Delay, Polarity, HP, LP, Slope, PEQ, Mute
        juce::StringArray missing;
    };
    struct MapSummary
    {
        bool exists = false;
        juce::String software;      // window / product the map belongs to
        juce::String learnedOn;     // "2026-11-16"
        std::vector<MapOutput> outputs;
    };

    struct SoftwareInfo
    {
        juce::String vendor;        // "miniDSP"
        juce::String productName;   // "Device Console"
        juce::String downloadUrl;
        bool installed = false;
    };

    struct ModelOption { juce::String id, label, note; };

    // ---- API key (stored encrypted; never read back in clear text)
    virtual bool hasKey() const = 0;
    virtual juce::String keyHint() const = 0;                       // "sk-ant-...wxyz" or ""
    virtual bool saveKey (const juce::String& key, juce::String& error) = 0;
    virtual void testKey (std::function<void (bool ok, juce::String message)> done) = 0;   // async
    virtual void removeKey() = 0;

    // ---- provider: "claude" (default, needs the API key) or "ollama" (local, free, no key, nothing leaves the PC).
    // Consent to control the window is required for BOTH providers.
    virtual juce::String getProvider() const { return "claude"; }
    virtual void setProvider (const juce::String&) {}
    virtual juce::String getOllamaUrl() const { return "http://localhost:11434"; }
    virtual juce::String getOllamaModel() const { return "qwen3-vl:4b"; }
    virtual void setOllama (const juce::String& /*url*/, const juce::String& /*model*/) {}
    virtual void testOllama (std::function<void (bool ok, juce::String message)> done) { if (done) done (false, "Not available."); }   // async GET /api/tags

    // ---- settings
    virtual std::vector<ModelOption> models() const = 0;            // first = default
    virtual void setModel (const juce::String& id) = 0;
    virtual juce::String getModel() const = 0;
    virtual void setBudgetUsd (double usd) = 0;
    virtual double getBudgetUsd() const = 0;

    // ---- target window and software
    virtual void setTarget (const juce::String& processName, const juce::String& windowTitle) = 0;   // empty = none
    virtual SoftwareInfo softwareInfo() const = 0;                  // vendor of the attached window ("" vendor = unknown)
    /** All known vendor tools with official download pages; when non-empty the panel offers them in a menu. */
    virtual std::vector<SoftwareInfo> vendorList() const { return {}; }
    virtual bool hasVendorList() const { return false; }           // cheap; true when vendorList() is offered

    // ---- consent (per window; must be true before start())
    virtual void setConsent (bool allowed) = 0;
    virtual bool hasConsent() const = 0;

    // ---- run
    virtual bool start (bool forceRelearn, juce::String& error) = 0;   // false + error if no key/consent/target
    virtual void stop() = 0;                                           // kill switch: immediate, idempotent
    virtual State getState() const = 0;

    // ---- learned map
    virtual MapSummary getMap() const = 0;
    virtual void deleteMap() = 0;

    // ---- events (set by the panel, called on the message thread)
    std::function<void (Step, StepState)> onStep;
    std::function<void (const LogEntry&)> onLog;
    std::function<void (const Cost&)> onCost;
    std::function<void (State, const juce::String& message)> onState;
    std::function<void (const juce::Image&)> onThumbnail;       // small snapshot of the vendor window (<= 480 px wide)
    std::function<void()> onMapChanged;                         // call getMap() again
};

/** Timer-driven simulation: a full run with fake log, cost, thumbnail and map. Used in Demo mode. */
std::unique_ptr<AiOperatorFacade> createDemoAiOperator();
} // namespace fa::ui::dsp
