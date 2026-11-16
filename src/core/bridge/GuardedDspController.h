#pragma once

#include "DspController.h"
#include "SafetyRules.h"
#include <atomic>
#include <functional>
#include <map>
#include <mutex>

namespace fa
{
/** Safety decorator around ANY DspController (virtual, manual, UIA, native, AI). Session routes every
    write through one. Enforced on each applyChannel:
      - tweeter HP >= max(planner protected HP, safety::minTweeterHpHz(Fs)) unless the user confirmed an override
      - output gain <= 0 dB
      - upward gain steps <= maxGainStepDb (walked in several writes; reductions are immediate)
      - on automatic controllers, a tweeter is muted before a crossover/polarity change and restored after. */
class GuardedDspController : public DspController
{
public:
    struct Options
    {
        double maxGainDb = safety::kMaxOutputGainDb;
        double maxGainStepDb = safety::kMaxGainStepDb;
        SafetyLimits planner;
    };

    explicit GuardedDspController (DspController& inner, Options o = {});

    DspController& getInner() const noexcept { return wrapped; }

    /** True only after the user explicitly confirmed a tweeter HP below the safe floor. */
    void setTweeterHpOverride (bool confirmedByUser) { overrideHp.store (confirmedByUser); }
    bool getTweeterHpOverride() const noexcept { return overrideHp.load(); }

    /** Descriptions of every clamp made since the last call (also passed to onNote). */
    juce::StringArray takeNotes();
    std::function<void (const juce::String&)> onNote;

    /** Pure rule (HP floor, gain ceiling): clamps `ch` in place; returns true when something changed. */
    bool sanitize (OutputChannel& ch, juce::StringArray& notes) const;

    juce::String getName() const override              { return wrapped.getName(); }
    bool isAutomatic() const override                  { return wrapped.isAutomatic(); }
    DspCapabilities getCapabilities() const override   { return wrapped.getCapabilities(); }
    bool connect (juce::String& e) override            { return wrapped.connect (e); }
    bool isConnected() const override                  { return wrapped.isConnected(); }
    bool applyChannel (const OutputChannel& channel, juce::String& error) override;
    bool setMuted (const OutputChannel& channel, bool muted, juce::String& error) override { return wrapped.setMuted (channel, muted, error); }
    bool beginBatch (juce::String& e) override         { return wrapped.beginBatch (e); }
    bool endBatch (juce::String& e) override           { return wrapped.endBatch (e); }
    bool soloChannels (const SystemConfig& cfg, const juce::StringArray& ids, juce::String& e) override { return wrapped.soloChannels (cfg, ids, e); }

private:
    struct Last { double gainDb = 0; double hpHz = 0; int hpType = 0; bool inverted = false; };
    DspController& wrapped;
    Options opt;
    std::atomic<bool> overrideHp { false };
    mutable std::mutex m;
    std::map<int, Last> last;
    juce::StringArray notes;
    void note (const juce::String& s);
};
} // namespace fa
