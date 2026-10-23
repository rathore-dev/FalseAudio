#pragma once

#include "DspController.h"
#include "DspProfile.h"
#include <juce_events/juce_events.h>

namespace fa
{
/** Drives a vendor's DSP software through Windows UI Automation using a taught DspProfile.
    Parameters that have no binding fall back to a manual instruction (hybrid mode), so any DSP
    works and every taught control is automatic. */
class UIAutomationController : public DspController
{
public:
    using WindowProvider = std::function<void*()>;

    UIAutomationController (DspProfile profile, WindowProvider window, ManualDspController::AskUser fallback);

    const DspProfile& getProfile() const noexcept { return profile; }

    juce::String getName() const override { return profile.name; }
    bool isAutomatic() const override     { return true; }
    DspCapabilities getCapabilities() const override { return profile.caps; }
    bool connect (juce::String& error) override;
    bool isConnected() const override;

    bool applyChannel (const OutputChannel& channel, juce::String& error) override;
    bool setMuted (const OutputChannel& channel, bool muted, juce::String& error) override;
    bool beginBatch (juce::String&) override;
    bool endBatch (juce::String& error) override;

    /** Performs one binding with a value; used by the Teach wizard's "Test" button too. */
    bool perform (const ControlBinding& b, const juce::String& textValue, double numericValue, bool toggleValue,
                  juce::String& error);

    /** Converts a delay in ms to the binding's unit. */
    static double convertDelay (double ms, const juce::String& unit, double sampleRate);

private:
    DspProfile profile;
    WindowProvider windowProvider;
    ManualDspController::AskUser askUser;
    uia::Automation automation;
    void* window = nullptr;
    bool batching = false;
    juce::StringArray manualLines;
    int selectedChannel = -1;

    bool selectChannel (int channel, juce::String& error);
    bool setNumeric (const juce::String& key, int channel, int band, double value, const juce::String& label, juce::String& error);
    bool setToggle (const juce::String& key, int channel, bool on, const juce::String& label, juce::String& error);
    bool setCrossover (const juce::String& prefix, int channel, const FilterSpec& f, const juce::String& label, juce::String& error);
    bool flushManual (juce::String& error);
};

/** Captures one control for the Teach wizard: the user clicks a control inside the DSP window
    and a ControlBinding is built from what UI Automation reports (or a click position for
    custom-drawn controls). Polls the mouse on the message thread. */
class TeachSession : private juce::Timer
{
public:
    using Callback = std::function<void (const ControlBinding&, const uia::ElementInfo&)>;

    explicit TeachSession (void* dspWindow);
    ~TeachSession() override;

    void capture (TeachSlot::Kind kind, Callback onCaptured);
    void cancel();
    bool isCapturing() const noexcept { return callback != nullptr; }

    /** Info about the control currently under the mouse (for a live hover readout). */
    uia::ElementInfo hoverInfo() const;

private:
    void timerCallback() override;

    void* window;
    uia::Automation automation;
    TeachSlot::Kind kind = TeachSlot::Kind::numeric;
    Callback callback;
    bool wasDown = false;
    juce::Point<int> downPos;
};
} // namespace fa
