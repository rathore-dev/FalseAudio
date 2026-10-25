#pragma once

#include "../../Page.h"
#include "../../components/Widgets.h"

namespace fa::ui
{
/** Scrollable list of measurements: colour chip, name, channel, mic position, SNR, time,
    visibility eye, delete. Paints its own card with a header and a "Clear all" action. */
class MeasurementLibrary : public juce::Component
{
public:
    struct Item
    {
        juce::String id, name, channel;
        int micPosition = 0;
        double snrDb = 0.0;
        juce::Time time;
        juce::Colour colour = theme::accent;
        bool visible = true;
        bool fromTune = false;
        bool clipped = false;
    };

    MeasurementLibrary();
    ~MeasurementLibrary() override;

    void setItems (const std::vector<Item>& items, const juce::String& selectedId);

    std::function<void (const juce::String&)> onSelect;
    std::function<void (const juce::String&)> onDelete;
    std::function<void (const juce::String&, bool)> onVisibilityChanged;
    std::function<void()> onClearAll;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    class Row;
    juce::Viewport viewport;
    juce::Component content;
    std::vector<std::unique_ptr<Row>> rows;
    FaButton clearButton { "Clear", FaButton::Style::ghost, icons::Id::trash };
    int count = 0;

    void layoutRows();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeasurementLibrary)
};
} // namespace fa::ui
