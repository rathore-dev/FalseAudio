#pragma once

#include "../Page.h"
#include "../AppShell.h"
#include "../components/Widgets.h"
#include "../components/Plots.h"
#include "autotune/TuneIdleView.h"
#include "autotune/TuneRunningView.h"
#include "autotune/TuneResultView.h"

namespace fa::ui
{
/** The hero/home screen: one button that tunes the car. Idle -> running (stage timeline, live
    measurements) -> done (score, before/after, findings) or failed/cancelled (error + retry). */
class AutoTunePage : public Page,
                     private AutoTuneEngine::Listener
{
public:
    explicit AutoTunePage (Session& s);
    ~AutoTunePage() override;

    juce::String getTitle() const override    { return "Auto Tune"; }
    juce::String getSubtitle() const override { return "One click: measure, align, cross over, equalise, verify"; }
    icons::Id getIcon() const override        { return icons::Id::bolt; }

    void pageShown() override;
    void sessionChanged() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    enum class State { idle, running, done, failed };

    void autoTuneStageChanged (AutoTuneEngine::Stage) override;
    void autoTuneProgress (AutoTuneEngine::Stage, double, const juce::String&) override;
    void autoTuneMeasurement (const Measurement&) override;
    void autoTuneFinished (const AutoTuneEngine::Result&) override;

    void setState (State s);
    void startTune();
    void cancelTune();
    juce::Rectangle<int> contentBounds() const { return getLocalBounds().reduced (theme::padLg, theme::pad); }

    State state = State::idle;
    AutoTuneEngine::Stage lastStage = AutoTuneEngine::Stage::idle;

    juce::Viewport idleViewport;
    TuneIdleView idleView;
    TuneRunningView runningView;
    TuneResultView resultView;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutoTunePage)
};
} // namespace fa::ui
