#pragma once

#include "../Page.h"
#include "../AppShell.h"
#include "../components/Widgets.h"
#include "../components/Plots.h"

namespace fa::ui
{
class AlignmentPage : public Page
{
public:
    explicit AlignmentPage (Session& s);
    ~AlignmentPage() override;

    juce::String getTitle() const override    { return "Alignment"; }
    juce::String getSubtitle() const override { return "Delay, polarity and level per channel"; }
    icons::Id getIcon() const override        { return icons::Id::clock; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AlignmentPage)
};
} // namespace fa::ui
