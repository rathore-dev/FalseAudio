#pragma once

#include "../../Page.h"
#include "core/measure/Analysis.h"

namespace fa::ui
{
/** Pseudo-3D cumulative spectral decay: time slices drawn as stacked, offset curves that
    recede into the plot, each filled to occlude the ones behind and tinted from the accent
    (t = 0) towards a cool violet as the energy decays. Hover shows the frequency under the
    cursor on the front slice. */
class WaterfallView : public juce::Component
{
public:
    WaterfallView() { setMouseCursor (juce::MouseCursor::CrosshairCursor); }

    void setData (const analysis::Waterfall& wf, juce::Colour baseColour, const juce::String& caption);
    void clear();
    bool hasData() const noexcept { return ! data.levelDb.empty(); }

    void paint (juce::Graphics& g) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    analysis::Waterfall data;
    juce::Colour colour = theme::accent;
    juce::String caption;
    double maxDb = 0.0;
    juce::Point<float> hover { -1.0f, -1.0f };

    struct Geometry
    {
        juce::Rectangle<float> frame;  // whole usable area
        float x0 = 0, width = 0;       // back slice x-range
        float yBack = 0;               // back baseline
        float dx = 0, dy = 0;          // total depth offset (front slice)
        float curveHeight = 0;         // pixel height of the dB range
        double fLow = 20.0, fHigh = 20000.0, dbLow = -60.0, dbHigh = 0.0;

        float xFor (double f, float u) const
        {
            const double t = std::log (f / fLow) / std::log (fHigh / fLow);
            return x0 + (float) t * width + u * dx;
        }
        float baseFor (float u) const { return yBack + u * dy; }
        float yFor (double db, float u) const
        {
            const double t = juce::jlimit (0.0, 1.0, (db - dbLow) / (dbHigh - dbLow));
            return baseFor (u) - (float) t * curveHeight;
        }
    };
    Geometry geometry() const;
};
} // namespace fa::ui
