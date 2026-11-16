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
    static constexpr int compactHeight = 156;
    /** Tighter header and gaps for short windows (see compactHeight). */
    void setCompact (bool c);
    int headerH() const noexcept { return compact ? 28 : 40; }
    int gapH() const noexcept { return compact ? 16 : 18; }

private:
    bool highPass;
    bool compact = false;
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
