#pragma once

#include "../../Page.h"
#include "../../components/Widgets.h"

namespace fa::ui
{
/** Vertical list of output channels with a role-coloured stripe, label, side and a one-line
    summary of the filters. Click to select. */
class ChannelList : public juce::Component
{
public:
    ChannelList();
    ~ChannelList() override;

    void setChannels (const SystemConfig& config, const juce::String& selectedId);
    std::function<void (const juce::String&)> onSelect;

    /** Shorter rows so more channels fit in short windows. */
    void setCompact (bool c);

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    class Row;
    juce::Viewport viewport;
    juce::Component content;
    std::vector<std::unique_ptr<Row>> rows;
    int count = 0;
    int rowH = 58;

    void layoutRows();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelList)
};
} // namespace fa::ui
