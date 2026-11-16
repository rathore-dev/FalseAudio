#pragma once

#include "../Page.h"
#include "../AppShell.h"
#include "../components/Widgets.h"
#include "../components/Plots.h"
#include "insights/ShimmerPanel.h"
#include "report/ReportDocument.h"

namespace fa::ui
{
/** Results: everything about the last tune in one scroll. Score strip and judge sheet,
    before/after explorer, 1/3-octave band table (CSV export), per-driver diagnostics and
    distortion, then the tune sheet (final DSP settings, findings, tuner notes). The toolbar
    exports the tune (text sheet, REW, Equalizer APO, JSON preset), saves / opens a session
    and imports a preset. Insights are computed on a background thread (shimmer while busy);
    empty state until a tune has completed. */
class ResultsPage : public Page
{
public:
    explicit ResultsPage (Session& s);
    ~ResultsPage() override;

    juce::String getTitle() const override    { return "Results"; }
    juce::String getSubtitle() const override { return "Score, before / after, diagnostics, final settings and exports"; }
    icons::Id getIcon() const override        { return icons::Id::activity; }

    void pageShown() override;
    void sessionChanged() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Analysis;
    class Body;
    enum class Export { text, rew, apo, json, session };

    void refresh();
    void startCompute (int stamp);
    void exportCsv();
    void exportAs (Export kind);
    void openSession();
    void importPreset();
    void writeAndToast (const juce::File& f, const juce::String& content, const juce::String& what);
    juce::String defaultBaseName() const;

    juce::Viewport viewport;
    std::unique_ptr<Analysis> analysis;
    ReportDocument document;
    std::unique_ptr<Body> body;
    ShimmerPanel shimmer;
    EmptyState empty { icons::Id::activity, "No results yet",
                       "Run Auto Tune first. This page then shows the score, what changed in every band, "
                       "per-driver health, and every DSP setting ready to export.",
                       "Go to Tune" };

    FaButton textButton    { "Text sheet", FaButton::Style::secondary, icons::Id::copy };
    FaButton rewButton     { "REW filters", FaButton::Style::secondary, icons::Id::download };
    FaButton apoButton     { "Equalizer APO", FaButton::Style::secondary, icons::Id::download };
    FaButton jsonButton    { "JSON preset", FaButton::Style::secondary, icons::Id::download };
    FaButton sessionButton { "Save session", FaButton::Style::primary, icons::Id::save };
    FaButton openButton    { "Open session", FaButton::Style::secondary, icons::Id::open };
    FaButton importButton  { "Import preset", FaButton::Style::secondary, icons::Id::download };

    insights::Insights lastInsights;
    int shownStamp = -1, computingStamp = -1, generation = 0;
    bool haveContent = false, computing = false, hasResult = false;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ResultsPage)
};
} // namespace fa::ui
