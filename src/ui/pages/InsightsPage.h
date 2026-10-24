#pragma once

#include "../Page.h"
#include "../AppShell.h"
#include "../components/Widgets.h"
#include "../components/Plots.h"
#include "insights/ShimmerPanel.h"

namespace fa::ui
{
/** "What the tune did, frequency by frequency": score strip and judge sheet, before/after
    explorer, 1/3-octave band table with CSV export, per-driver diagnostics cards and the
    distortion overview. Insights are computed on a background thread (shimmer while busy);
    empty state until a tune has completed. */
class InsightsPage : public Page
{
public:
    explicit InsightsPage (Session& s);
    ~InsightsPage() override;

    juce::String getTitle() const override    { return "Insights"; }
    juce::String getSubtitle() const override { return "Before / after, distortion and per-driver diagnostics"; }
    icons::Id getIcon() const override        { return icons::Id::activity; }

    void pageShown() override;
    void sessionChanged() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Content;

    void refresh();
    void startCompute (int stamp);
    void exportCsv();

    juce::Viewport viewport;
    std::unique_ptr<Content> content;
    ShimmerPanel shimmer;
    EmptyState empty { icons::Id::activity, "No insights yet",
                       "Run Auto Tune first - this page then shows what changed in every 1/3-octave band, "
                       "per-driver health and distortion.",
                       "Go to Auto Tune" };

    insights::Insights lastInsights;
    int shownStamp = -1, computingStamp = -1, generation = 0;
    bool haveContent = false, computing = false;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InsightsPage)
};
} // namespace fa::ui
