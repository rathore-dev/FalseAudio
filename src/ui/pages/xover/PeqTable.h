#pragma once

#include "../../Page.h"
#include "../../components/Widgets.h"

namespace fa::ui
{
/** Parametric EQ band table: one row per band with frequency, gain, Q, type, enable and
    delete; header with band count and an "Add band" button. Values honour the DSP limits. */
class PeqTable : public juce::Component
{
public:
    PeqTable();
    ~PeqTable() override;

    void setBands (const std::vector<FilterSpec>& bands, const DspCapabilities& caps, juce::Colour accent, int selectedIndex);
    void setSelected (int index);
    int getSelected() const noexcept { return selected; }

    std::function<void (int, const FilterSpec&)> onBandChanged;
    std::function<void (int)> onBandDeleted;
    std::function<void (int)> onBandSelected;
    std::function<void()> onAddBand;
    std::function<void()> onResetAll;

    void paint (juce::Graphics& g) override;
    void resized() override;

    static constexpr int rowHeight = 44;

private:
    class Row;
    juce::Viewport viewport;
    juce::Component content;
    std::vector<std::unique_ptr<Row>> rows;
    FaButton addButton { "Add band", FaButton::Style::primary, icons::Id::plus };
    FaButton resetButton { "Reset EQ", FaButton::Style::ghost, icons::Id::refresh };
    DspCapabilities caps;
    juce::Colour accent = theme::accent;
    int selected = -1;
    int count = 0;

    void layoutRows();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PeqTable)
};
} // namespace fa::ui
