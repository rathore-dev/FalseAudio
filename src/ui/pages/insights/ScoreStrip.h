#pragma once

#include "InsightsCommon.h"

namespace fa::ui
{
/** Top strip of the Insights page: overall tune score with verdict, the three insight rings
    (Tonal, Imaging, Cleanliness) and a "Judge sheet" of category scores with bars.

    The judge sheet shows insight-derived rows by default; setJudgeRows() replaces them, so the
    advanced::JudgeScore categories can be wired in without touching the layout. */
class ScoreStrip : public juce::Component
{
public:
    struct JudgeRow
    {
        juce::String name;
        double score = 0.0;          // 0..100
        juce::String detail;         // e.g. "1.2 dB RMS"
    };

    ScoreStrip();

    void set (const AutoTuneEngine::Result& result, const insights::Insights& in);
    void setJudgeRows (std::vector<JudgeRow> rows, juce::StringArray comments = {});

    int idealHeight (int width) const;

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    bool stacked (int width) const noexcept { return width < 1040; }
    int judgeWidth (int width) const noexcept;
    int judgeHeight (int width) const;

    ScoreRing overall, tonal, imaging, clean;
    std::vector<JudgeRow> rows;
    juce::StringArray comments;
    double judgeTotal = -1.0;            // < 0: no simulated judge result (insight rows shown instead)

    int overallScore = 0;
    juce::String verdict, summary;
    juce::Rectangle<int> ringsArea, judgeArea, textArea;
    int dividerX = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScoreStrip)
};
} // namespace fa::ui
