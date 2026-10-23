#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace fa
{
/** Hosts another application's top-level window (the DSP vendor's PC software) inside a JUCE
    component, so the DSP tool appears in the middle of FalseAudio. Windows only; on other
    platforms attach() fails gracefully.

    The foreign window is re-parented as a child, stripped of its caption/borders and kept
    sized to this component. detach() restores it exactly as it was. */
class DspWindowHost : public juce::Component,
                      private juce::Timer
{
public:
    struct WindowInfo
    {
        void* handle = nullptr;
        juce::String title;
        juce::String processName;
        juce::uint32 processId = 0;
    };

    DspWindowHost();
    ~DspWindowHost() override;

    /** Visible top-level windows of other processes (candidate DSP apps). */
    static std::vector<WindowInfo> listWindows();

    bool attach (const WindowInfo& window, juce::String& error);
    void detach();
    bool isAttached() const noexcept { return hosted != nullptr; }
    WindowInfo getAttached() const   { return info; }

    /** Launches an executable and attaches to its main window when it appears (up to 20 s). */
    bool launchAndAttach (const juce::File& exe, juce::String& error);

    /** Called when the hosted window closes or attaching finishes. */
    std::function<void()> onStateChanged;

    void paint (juce::Graphics&) override;
    void resized() override;
    void parentHierarchyChanged() override;

private:
    void timerCallback() override;
    void reposition();

    void* hosted = nullptr;
    WindowInfo info;
    long long savedStyle = 0, savedExStyle = 0;
    void* savedParent = nullptr;
    int savedX = 0, savedY = 0, savedW = 0, savedH = 0;

    juce::uint32 pendingProcessId = 0;
    juce::uint32 pendingSince = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DspWindowHost)
};
} // namespace fa
