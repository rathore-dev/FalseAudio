#pragma once

#include "../../Page.h"
#include "../../components/Widgets.h"

namespace fa::ui
{
/** Editor card for one crossover filter (high-pass or low-pass): enable, frequency, slope and
    alignment. Options follow the DSP capabilities. */
class FilterEditor : public juce::Component
{
public:
    explicit FilterEditor (bool isHighPass);

    void setCapabilities (const DspCapabilities& caps);
    void setSpec (const FilterSpec& spec, juce::Colour accent);
    FilterSpec getSpec() const;

    std::function<void (const FilterSpec&)> onChange;

    void paint (juce::Graphics& g) override;
    void resized() override;

    static constexpr int idealHeight = 176;

private:
    bool highPass;
    FilterSpec spec;
    juce::Colour accent = theme::accent;
    std::vector<int> slopes { 6, 12, 18, 24, 36, 48 };
    std::vector<Alignment> alignments { Alignment::butterworth, Alignment::linkwitzRiley, Alignment::bessel };

    ToggleSwitch enable { "" };
    ValueField freq;
    SegmentedControl slope;
    SegmentedControl alignment;

    void emit();
    void refreshEnablement();
};
} // namespace fa::ui
