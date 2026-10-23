#pragma once

#include "../Page.h"
#include "../AppShell.h"
#include "../components/Widgets.h"
#include "../components/Plots.h"

namespace fa::ui
{
class DspPage : public Page
{
public:
    explicit DspPage (Session& s);
    ~DspPage() override;

    juce::String getTitle() const override    { return "DSP"; }
    juce::String getSubtitle() const override { return "Control your DSP software"; }
    icons::Id getIcon() const override        { return icons::Id::chip; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DspPage)
};
} // namespace fa::ui
