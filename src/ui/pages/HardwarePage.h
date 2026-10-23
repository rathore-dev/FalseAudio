#pragma once

#include "../Page.h"
#include "../AppShell.h"
#include "../components/Widgets.h"
#include "../components/Plots.h"

namespace fa::ui
{
class HardwarePage : public Page
{
public:
    explicit HardwarePage (Session& s);
    ~HardwarePage() override;

    juce::String getTitle() const override    { return "Hardware"; }
    juce::String getSubtitle() const override { return "Audio interface, routing and microphones"; }
    icons::Id getIcon() const override        { return icons::Id::plug; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HardwarePage)
};
} // namespace fa::ui
