#pragma once

#include "../../Page.h"
#include "../../components/Widgets.h"

namespace fa::ui
{
/** Pro-meter style SPL readout: big tabular number, weighting selector (A/C/Z), max/min hold,
    and an input peak meter so clipping is visible at a glance. */
class SplMeter : public juce::Component
{
public:
    SplMeter();

    /** Feed a new broadband level (dB SPL when calibrated, otherwise dBFS). */
    void setLevel (double db, bool calibrated);
    void setInputPeak (float dbfs);
    void setIdle (const juce::String& reason);          // no live audio: show dashes + reason
    void resetHold();

    int getWeighting() const noexcept { return weighting.getSelected(); }   // 0 A, 1 C, 2 Z
    std::function<void (int)> onWeightingChanged;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    SegmentedControl weighting { { "A", "C", "Z" } };
    FaButton resetButton { "Reset", FaButton::Style::ghost, icons::Id::refresh };
    LevelMeter inputMeter;

    double level = -200.0, shown = -200.0, maxHold = -200.0, minHold = 200.0;
    bool calibrated = false;
    juce::String idleReason;
    float inputPeak = -90.0f;
};
} // namespace fa::ui
