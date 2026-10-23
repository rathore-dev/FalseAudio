#pragma once

#include "../Page.h"
#include "../AppShell.h"
#include "../components/Widgets.h"
#include "../components/Plots.h"

namespace fa::ui
{
class MeasurePage : public Page
{
public:
    explicit MeasurePage (Session& s);
    ~MeasurePage() override;

    juce::String getTitle() const override    { return "Measure"; }
    juce::String getSubtitle() const override { return "Sweep measurements and analysis"; }
    icons::Id getIcon() const override        { return icons::Id::waveform; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeasurePage)
};
} // namespace fa::ui
