#pragma once

#include "../AppShell.h"
#include "../components/Widgets.h"

namespace fa::ui
{
/** Top bar: current page title + subtitle on the left; on the right the Auto Tune status
    (while running), audio-device and DSP status chips and the global Demo/Live mode pill.
    A hairline progress bar runs along the bottom edge while Auto Tune is working. */
class HeaderBar : public juce::Component
{
public:
    explicit HeaderBar (Session& session);
    ~HeaderBar() override;

    void setPage (const juce::String& title, const juce::String& subtitle);
    /** Re-reads mode, audio device and DSP state from the session. */
    void refresh();
    /** text empty = not tuning; progress < 0 = indeterminate. */
    void setTuneStatus (const juce::String& text, double progress);

    std::function<void (Session::Mode)> onModeChange;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    Session& session;
    juce::String title, subtitle;
    SegmentedControl modePill { { "Demo", "Live" } };
    Chip audioChip, dspChip, tuneChip;
    ProgressBarFa tuneBar;
    bool tuning = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HeaderBar)
};
} // namespace fa::ui
