#pragma once

#include "TuneCommon.h"
#include <array>

namespace fa::ui
{
/** Horizontal 8-step pipeline stepper (preflight ... report). Completed steps fill with the
    accent, the active one pulses with a glow and a per-stage progress arc; failed/cancelled
    runs mark the stage where they stopped. */
class StageTimeline : public juce::Component,
                      private juce::Timer
{
public:
    using Stage = AutoTuneEngine::Stage;

    StageTimeline();
    ~StageTimeline() override;

    void reset();
    void setStage (Stage s);
    void setProgress (Stage s, double fraction);
    void setOutcome (Stage finalStage);          // done / failed / cancelled
    Stage getStage() const noexcept { return current; }

    static int stageIndex (Stage s);             // 0..7 for work stages, -1 otherwise
    static juce::String shortDescription (Stage s);

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    Stage current = Stage::idle;
    Stage outcome = Stage::idle;
    std::array<double, AutoTuneEngine::numWorkStages> progress {};
    std::array<float, AutoTuneEngine::numWorkStages> shownFill {};
    float pulse = 0.0f;
    float lineFill = 0.0f;
};
} // namespace fa::ui
