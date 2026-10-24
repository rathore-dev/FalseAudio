#pragma once

#include "../autotune/TuneCommon.h"

namespace fa::ui
{
/** Per-channel final settings table: role stripe, output, channel, gain, delay (ms + cm),
    polarity, high-pass, low-pass and every EQ band. Rows grow with the number of bands. */
class SettingsTable : public juce::Component
{
public:
    void setConfig (const SystemConfig& cfg);
    int getIdealHeight (int width) const;
    void paint (juce::Graphics&) override;

private:
    SystemConfig config;
    int rowHeight (const OutputChannel& ch) const;
    bool compact (int width) const { return width < 860; }
};

/** The printable report body, laid out as a document: header, metrics, before/after plots,
    settings table, findings, notes, footer. Lives inside the ReportPage's viewport. */
class ReportDocument : public juce::Component
{
public:
    explicit ReportDocument (Session& session);

    void setResult (const AutoTuneEngine::Result& result);
    int idealHeight (int width) const;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Layout
    {
        juce::Rectangle<int> sheet, header, ring, metrics, judgeTitle, judge, plotsTitle, plotL, plotR, plotSum, tableTitle, table,
                             findingsTitle, findings, notesTitle, notes, footer;
        int total = 0;
    };
    Layout computeLayout (int width) const;
    void drawSectionTitle (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title, const juce::String& aside = {}) const;

    Session& session;
    AutoTuneEngine::Result result;
    juce::Time completedAt;

    ScoreRing ring;
    MetricTile leftErr  { "Left error", "dB" };
    MetricTile rightErr { "Right error", "dB" };
    MetricTile match    { "L / R match", "dB" };
    MetricTile duration { "Duration", "" };
    MetricTile sweeps   { "Measurements", "" };
    FrequencyPlot plotL, plotR, plotSum;
    JudgePanel judgePanel;
    SettingsTable table;
    FindingsList findings;
    NotesView notes;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReportDocument)
};
} // namespace fa::ui
