#pragma once

#include "Plots.h"

namespace fa::ui
{
/** Visual test harness: every plot type with demo data from a VirtualCar measurement of a
    3-way + sub layout. Not a Page - drop it into any window or snapshot it offscreen. */
class PlotGallery : public juce::Component
{
public:
    explicit PlotGallery (bool animate = true);
    ~PlotGallery() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Test helper: feeds a synthetic mouseMove to whatever plot lies under `pos` (gallery
        coordinates) so offscreen snapshots can show crosshairs / tooltips. */
    void previewHover (juce::Point<float> pos);

private:
    struct Card { juce::String title, subtitle; juce::Component* content = nullptr; juce::Rectangle<int> bounds; };
    std::vector<Card> cards;

    FrequencyPlot magnitude, phase, coherence, rta, eq, groupDelay;
    TimePlot impulse, etc;
    ScoreRing ringA, ringB, ringC;
    CarView car;

    SystemConfig config;
    std::vector<FrequencyPlot::Node> eqNodes;
    FrequencyResponse eqBase;

    void buildDemoData (bool animate);
    void updateEqCurve();
};

/** Renders the gallery offscreen (no window needed) and writes a PNG - a quick visual
    regression check for the plot components. */
void writePlotGallerySnapshot (const juce::File& file, int width = 1600, int height = 1100);
} // namespace fa::ui
