#pragma once

#include "InstallKit.h"

namespace fa::ui
{
/** Top of the Install page: the headline numbers of the whole system (power, current, main fuse,
    source level, head-unit clean volume), voltage reaching each amp and health chips. */
class SummaryHeader : public juce::Component
{
public:
    SummaryHeader();

    struct Health { int danger = 0, important = 0, wireUpgrades = 0, channelWarnings = 0, stepsDone = 0, stepsTotal = 0; };

    void setPlan (const install::Plan& plan, const install::Installation& inst, const gear::GearDatabase& db,
                  const Health& health);

    /** Height needed for `width` (tiles wrap on narrow windows). */
    int heightFor (int width) const;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Tile { juce::String label, value, unit, foot; juce::Colour tint; icons::Id icon; };
    struct Pill { juce::String text; juce::Colour colour; };

    int tileColumns (int width) const;
    juce::Rectangle<int> tilesArea (int width, int& rows) const;

    std::vector<Tile> tiles;
    std::vector<Pill> ampPills, healthPills;

    static constexpr int tileH = 92, tileMinW = 176, pillH = 26;
};
} // namespace fa::ui
