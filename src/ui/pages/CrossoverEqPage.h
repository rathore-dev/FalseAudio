#pragma once

#include "../Page.h"
#include "../AppShell.h"
#include "../components/Widgets.h"
#include "../components/Plots.h"
#include "xover/ChannelList.h"
#include "xover/FilterEditor.h"
#include "xover/PeqTable.h"
#include "xover/GraphicBands.h"

namespace fa::ui
{
/** Crossovers and parametric EQ for every output channel: electrical response plot with
    draggable EQ nodes, HP/LP editors, PEQ band table, Reset EQ and Copy L->R. */
class CrossoverEqPage : public Page,
                        private juce::Timer
{
public:
    explicit CrossoverEqPage (Session& s);
    ~CrossoverEqPage() override;

    juce::String getTitle() const override    { return "Crossovers & EQ"; }
    juce::String getSubtitle() const override { return "Filters and parametric EQ"; }
    icons::Id getIcon() const override        { return icons::Id::filter; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void pageShown() override;
    void sessionChanged() override;

private:
    // ---- left column ----
    ChannelList channelList;
    FilterEditor highPassEditor { true };
    FilterEditor lowPassEditor { false };

    // ---- main ----
    FrequencyPlot plot;
    ToggleSwitch showAllToggle { "All channels" };
    ToggleSwitch predictedToggle { "Predicted acoustic" };
    ToggleSwitch sumToggle { "Side sum" };
    FaButton sendButton { "Send to DSP", FaButton::Style::primary, icons::Id::bolt };
    FaButton copyButton { juce::String (juce::CharPointer_UTF8 ("Copy L \xE2\x86\x92 R")), FaButton::Style::secondary, icons::Id::copy };
    PeqTable peqTable;
    GraphicBands graphicBands;
    bool isGraphic() const { return session.getConfig().caps.isGraphic(); }

    juce::String selectedId;
    int selectedBand = -1;
    juce::Rectangle<int> plotCardArea;

    // throttled write-back while dragging nodes
    bool pendingWrite = false;
    bool applyingEdit = false;
    OutputChannel pendingChannel;

    void timerCallback() override;

    const OutputChannel* selectedChannel() const;
    void selectChannel (const juce::String& id);
    void refreshAll();
    void refreshEditors();
    void rebuildPlot();
    void rebuildNodes();

    void commit (const OutputChannel& ch, bool immediate);
    void modifySelected (const std::function<void (ChannelSettings&)>& fn, bool immediate = true);
    void addBand (double freq, double gainDb);
    void copyLeftToRight();

    juce::Colour colourFor (const OutputChannel& ch) const;
    FilterSpec clampBand (FilterSpec b) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CrossoverEqPage)
};
} // namespace fa::ui
