#pragma once

#include "TabbedPage.h"
#include "SystemPage.h"
#include "GearPage.h"

namespace fa::ui
{
/** Car: everything that describes the vehicle, as two sub-tabs - Layout (speakers, presets,
    channels, topology, target) and Equipment (the products fitted, from the gear database or
    your own). */
class CarPage : public TabbedPage
{
public:
    explicit CarPage (Session& s) : TabbedPage (s, "tab.car")
    {
        addTab<SystemPage> ("layout", "Layout", 108);
        addTab<GearPage> ("equipment", "Equipment", 120);
        finishTabs();
    }

    juce::String getTitle() const override { return "Car"; }
    icons::Id getIcon() const override     { return icons::Id::car; }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CarPage)
};
} // namespace fa::ui
