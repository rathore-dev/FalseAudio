#pragma once

#include "../Page.h"
#include "../AppShell.h"
#include "../components/Widgets.h"
#include "../components/Plots.h"

namespace fa::ui
{
class AutoTunePage : public Page
{
public:
    explicit AutoTunePage (Session& s);
    ~AutoTunePage() override;

    juce::String getTitle() const override    { return "Auto Tune"; }
    juce::String getSubtitle() const override { return "One click: measure, align, cross over, equalise, verify"; }
    icons::Id getIcon() const override        { return icons::Id::bolt; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutoTunePage)
};
} // namespace fa::ui
