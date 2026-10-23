#pragma once

#include "../Page.h"
#include "../AppShell.h"
#include "../components/Widgets.h"
#include "../components/Plots.h"

namespace fa::ui
{
class SystemPage : public Page
{
public:
    explicit SystemPage (Session& s);
    ~SystemPage() override;

    juce::String getTitle() const override    { return "System"; }
    juce::String getSubtitle() const override { return "Speakers, layout and target curve"; }
    icons::Id getIcon() const override        { return icons::Id::car; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SystemPage)
};
} // namespace fa::ui
