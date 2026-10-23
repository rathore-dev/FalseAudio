#pragma once

#include "../Page.h"
#include "../AppShell.h"
#include "../components/Widgets.h"
#include "../components/Plots.h"

namespace fa::ui
{
class CrossoverEqPage : public Page
{
public:
    explicit CrossoverEqPage (Session& s);
    ~CrossoverEqPage() override;

    juce::String getTitle() const override    { return "Crossovers & EQ"; }
    juce::String getSubtitle() const override { return "Filters and parametric EQ"; }
    icons::Id getIcon() const override        { return icons::Id::filter; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CrossoverEqPage)
};
} // namespace fa::ui
