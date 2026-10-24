#pragma once

#include "StageTimeline.h"

namespace fa::ui
{
/** Shown while the engine runs: the stage stepper, the live engine message, elapsed time, a
    Cancel button and a frequency plot that fills with every driver measurement as it arrives. */
class TuneRunningView : public juce::Component,
                        private juce::Timer
{
public:
    using Stage = AutoTuneEngine::Stage;

    explicit TuneRunningView (Session& session);
    ~TuneRunningView() override;

    std::function<void()> onCancel;

    void begin();                                  // resets everything and starts the clock
    void setStage (Stage s);
    void setProgress (Stage s, double fraction, const juce::String& message);
    void addMeasurement (const Measurement& m);
    void setOutcome (Stage finalStage);            // freezes the clock
    double getElapsedSeconds() const;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    Session& session;
    StageTimeline timeline;
    FrequencyPlot plot;
    FaButton cancelButton { "Cancel", FaButton::Style::danger, icons::Id::stop };

    juce::String message = "Starting";
    Stage stage = Stage::idle;
    double startMs = 0.0, frozenSeconds = -1.0;
    int measurementCount = 0;
    bool haveReference = false;
    double referenceDb = 0.0;
    double overallFraction = 0.0;

    juce::Rectangle<int> timelineCard, statusRow, plotCard;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TuneRunningView)
};
} // namespace fa::ui
