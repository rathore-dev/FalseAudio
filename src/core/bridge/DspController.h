#pragma once

#include "../tune/SystemConfig.h"
#include <functional>

namespace fa
{
/** The single interface the auto-tuner uses to drive a DSP. Implementations:
     - VirtualRig            : the simulated car (demo mode, tests)
     - ManualDspController   : any DSP; the user enters values shown by the app, then confirms
     - UIAutomationController: drives the vendor's own PC software through Windows UI Automation
    All calls may block (they run on the auto-tune thread, never the message thread). */
class DspController
{
public:
    virtual ~DspController() = default;

    virtual juce::String getName() const = 0;

    /** True if the controller changes the DSP by itself (no user action needed). */
    virtual bool isAutomatic() const = 0;

    virtual DspCapabilities getCapabilities() const = 0;

    virtual bool connect (juce::String& error) = 0;
    virtual bool isConnected() const = 0;

    /** Writes every setting of one channel (gain, delay, polarity, crossovers, PEQ, mute). */
    virtual bool applyChannel (const OutputChannel& channel, juce::String& error) = 0;

    /** Mutes/unmutes a channel (used to measure drivers one at a time). */
    virtual bool setMuted (const OutputChannel& channel, bool muted, juce::String& error) = 0;

    /** Optional grouping: implementations may defer work until endBatch(). */
    virtual bool beginBatch (juce::String&) { return true; }
    virtual bool endBatch (juce::String&)   { return true; }

    /** Applies every channel of a config (batched). */
    bool applyAll (const SystemConfig& cfg, juce::String& error);

    /** Mutes everything except the listed channel ids. */
    virtual bool soloChannels (const SystemConfig& cfg, const juce::StringArray& ids, juce::String& error);
};

/** For DSPs without automation: every change becomes a human-readable instruction list that is
    handed to `askUser`, which blocks until the user confirms (true) or cancels (false). */
class ManualDspController : public DspController
{
public:
    using AskUser = std::function<bool (const juce::String& title, const juce::StringArray& steps)>;

    ManualDspController (DspCapabilities caps, AskUser askUser);

    juce::String getName() const override        { return "Manual (guided)"; }
    bool isAutomatic() const override            { return false; }
    DspCapabilities getCapabilities() const override { return caps; }
    bool connect (juce::String&) override        { return true; }
    bool isConnected() const override            { return true; }

    bool applyChannel (const OutputChannel& channel, juce::String& error) override;
    bool setMuted (const OutputChannel& channel, bool muted, juce::String& error) override;
    bool soloChannels (const SystemConfig& cfg, const juce::StringArray& ids, juce::String& error) override;
    bool beginBatch (juce::String&) override;
    bool endBatch (juce::String& error) override;

    /** Formats one channel's settings exactly as they should be typed into a DSP.
        A graphic EQ (caps->isGraphic(), or - without caps - a peq list that is exactly a
        standard graphic layout) is printed as one "31-band GEQ: 20 Hz 0.0, 25 Hz -1.5, ..." line. */
    static juce::StringArray describeChannel (const OutputChannel& ch, const DspCapabilities* caps = nullptr);

private:
    DspCapabilities caps;
    AskUser askUser;
    bool batching = false;
    juce::StringArray pending;
    juce::String pendingTitle;

    bool flush (const juce::String& title, juce::String& error);
};
} // namespace fa
