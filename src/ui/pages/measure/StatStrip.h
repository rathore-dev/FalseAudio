#pragma once

#include "../../Page.h"

namespace fa::ui
{
/** A horizontal row of "stat tiles" (label on top, tabular value + unit below) used for
    measurement details and live readouts. Paints itself as a low card. */
class StatStrip : public juce::Component
{
public:
    struct Stat
    {
        juce::String label;
        juce::String value;
        juce::String unit;
        juce::Colour colour = theme::text;
        bool chip = false;      // draw the value as a coloured status chip instead of a number
    };

    StatStrip() = default;

    void setTitle (const juce::String& t, juce::Colour swatch = juce::Colours::transparentBlack);
    void setStats (std::vector<Stat> s);
    void paint (juce::Graphics& g) override;

private:
    juce::String title;
    juce::Colour swatch = juce::Colours::transparentBlack;
    std::vector<Stat> stats;
};
} // namespace fa::ui
