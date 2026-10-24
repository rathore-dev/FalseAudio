#pragma once

#include "StageTimeline.h"

namespace fa::ui
{
/** Shown when a run has finished: animated score, key metrics, before/after/target plot with
    L / R / L+R toggle, findings, "what changed" and the next actions. Also renders the
    failed / cancelled state with the error and a retry button. */
class TuneResultView : public juce::Component
{
public:
    using Stage = AutoTuneEngine::Stage;

    explicit TuneResultView (Session& session);
    ~TuneResultView() override;

    std::function<void()> onViewReport;
    std::function<void()> onRunAgain;

    void showResult (const AutoTuneEngine::Result& result);
    void showFailure (const juce::String& error, bool cancelled, Stage stoppedAt, double seconds);

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    class Content;
    class ScorePanel;
    class FailurePanel;

    Session& session;
    juce::Viewport viewport;
    std::unique_ptr<Content> content;
    std::unique_ptr<FailurePanel> failure;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TuneResultView)
};
} // namespace fa::ui
