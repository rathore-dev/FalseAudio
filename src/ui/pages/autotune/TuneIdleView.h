#pragma once

#include "TuneOptionsPanel.h"

namespace fa::ui
{
/** The hero screen: headline, one-line promise, giant Start button, summary chips of what will
    be tuned, the 8-step "what happens" list and an expandable Options panel. */
class TuneIdleView : public juce::Component,
                     private juce::Timer
{
public:
    explicit TuneIdleView (Session& session);
    ~TuneIdleView() override;

    std::function<void()> onStart;
    std::function<void()> onViewReport;      // "View last results" (shown when a result exists)
    std::function<void()> onHeightChanged;   // idealHeight() changed (options panel animating)

    /** Rebuilds chips and texts from the session. */
    void refresh();

    /** Total height needed for `width` (the page puts this view in a viewport). */
    int idealHeight (int width) const;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void setOptionsOpen (bool open);
    int heroBlockHeight (int width) const;
    bool twoColumn (int width) const { return width >= 880; }

    Session& session;
    FaButton startButton { "Start Auto Tune", FaButton::Style::primary, icons::Id::bolt };
    FaButton optionsButton { "Options", FaButton::Style::ghost, icons::Id::chevronRight };
    FaButton lastReportButton { "View last results", FaButton::Style::ghost, icons::Id::document };
    juce::String lastLine;                   // "Last tune scored 74 / 100", empty when no result
    TuneOptionsPanel options;
    std::vector<std::unique_ptr<Chip>> chips;

    juce::String demoNote;
    bool optionsOpen = false;
    float openAmount = 0.0f;
    juce::Rectangle<int> heroArea, stepsArea, chipsArea, noteArea, optionsHeader;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TuneIdleView)
};
} // namespace fa::ui
