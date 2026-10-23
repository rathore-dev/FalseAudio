#pragma once

#include "../Page.h"
#include "../AppShell.h"
#include "../components/Widgets.h"
#include "../components/Plots.h"

namespace fa::ui
{
class ReportPage : public Page
{
public:
    explicit ReportPage (Session& s);
    ~ReportPage() override;

    juce::String getTitle() const override    { return "Report"; }
    juce::String getSubtitle() const override { return "Results of the last tune"; }
    icons::Id getIcon() const override        { return icons::Id::document; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReportPage)
};
} // namespace fa::ui
