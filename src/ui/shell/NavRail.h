#pragma once

#include "../AppShell.h"
#include "../theme/Animation.h"

namespace fa::ui
{
/** Left navigation rail: brand mark, grouped icon+label buttons (one per page) with a gliding
    selection pill, and a version footer. Items are grouped under small section labels
    (TUNE / SETUP / EXPERT); when the window is short the rail compacts to icon-only buttons
    with the group separators kept as hairlines, so everything always fits at 720 px. */
class NavRail : public juce::Component
{
public:
    struct Item
    {
        PageId id;
        juce::String label;
        icons::Id icon;
        juce::String section;     // first item of each new section starts a group
        juce::String tooltip;
    };

    NavRail();
    ~NavRail() override;

    void setItems (std::vector<Item> items);
    void setSelected (PageId id, bool animate = true);
    PageId getSelected() const noexcept { return selectedId; }

    std::function<void (PageId)> onSelect;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class NavButton;

    struct Layout
    {
        int itemHeight = theme::navItemHeight;
        int sectionHeight = 22;
        bool compact = false;
        std::vector<int> itemY;          // top of each item
        std::vector<int> sectionY;       // top of each section label (same index as items; -1 if none)
    };

    Layout computeLayout() const;
    juce::Rectangle<float> pillBounds (int index) const;
    int indexOf (PageId id) const;

    std::vector<std::unique_ptr<NavButton>> buttons;
    std::vector<Item> items;
    Layout layout;
    PageId selectedId = PageId::autoTune;
    anim::Value indicator { *this, 0.0f, 14.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NavRail)
};
} // namespace fa::ui
