#pragma once

#include "../Page.h"
#include "../AppShell.h"
#include "../components/Widgets.h"
#include "../components/Plots.h"

namespace fa::ui
{
class LivePage : public Page
{
public:
    explicit LivePage (Session& s);
    ~LivePage() override;

    juce::String getTitle() const override    { return "Live"; }
    juce::String getSubtitle() const override { return "Real-time analyzer and transfer function"; }
    icons::Id getIcon() const override        { return icons::Id::activity; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LivePage)
};
} // namespace fa::ui
