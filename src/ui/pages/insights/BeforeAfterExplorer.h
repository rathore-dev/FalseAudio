#pragma once

#include "InsightsCommon.h"

namespace fa::ui
{
/** Card with the before / after / target plot. Side selector (Left, Right, L+R, Overlay) and
    toggles for Before, After, Target and the net EQ applied per side. Hovering the plot shows
    exact values (FrequencyPlot readout); the band table can highlight a 1/3-octave band here. */
class BeforeAfterExplorer : public Card
{
public:
    BeforeAfterExplorer();

    void set (const AutoTuneEngine::Result& result, const insights::Insights& in);
    /** Shades one 1/3-octave band (0 clears). */
    void setHighlightBand (double centreHz);

    int idealHeight (int width) const noexcept { return width < 900 ? 400 : 440; }

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void rebuild();

    FrequencyPlot plot;
    SegmentedControl mode { juce::StringArray { "Left", "Right", "L+R", "Overlay" } };
    ToggleSwitch showBefore { "Before" }, showAfter { "After" }, showTarget { "Target" }, showEq { "Net EQ" };

    AutoTuneEngine::Result result;        // measurements stripped
    std::vector<insights::BandRow> bands;
    double reference = 0.0;
    int modeIndex = 0;
    juce::Rectangle<int> statsArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BeforeAfterExplorer)
};
} // namespace fa::ui
