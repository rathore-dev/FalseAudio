#pragma once

#include "InsightsCommon.h"

namespace fa::ui
{
/** Every driver's THD vs frequency on one plot (dB re fundamental, with 0.1 / 1 / 10 % reference
    lines) and a ranked "worst offenders" panel with severity bars. */
class DistortionOverview : public Card
{
public:
    DistortionOverview();

    void set (const insights::Insights& in, const SystemConfig& cfg);
    int idealHeight (int width) const noexcept;

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    struct Rank
    {
        juce::String label;
        juce::Colour colour;
        double peakPercent = 0, peakHz = 0, at100 = 0;
    };

    bool stacked (int width) const noexcept { return width < 820; }

    FrequencyPlot plot;
    std::vector<Rank> ranks;
    juce::Rectangle<int> rankArea;
    int flagged = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DistortionOverview)
};
} // namespace fa::ui
