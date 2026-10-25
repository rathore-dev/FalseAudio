#pragma once

#include "InsightsCommon.h"

namespace fa::ui
{
/** Skeleton of the Insights layout with a travelling shimmer, shown while the insights are
    computed on a background thread. */
class ShimmerPanel : public juce::Component,
                     private juce::Timer
{
public:
    ShimmerPanel() { setInterceptsMouseClicks (false, false); }

    void visibilityChanged() override
    {
        if (isVisible()) startTimerHz (60);
        else stopTimer();
    }

    void paint (juce::Graphics& g) override
    {
        auto a = getLocalBounds();
        std::vector<juce::Rectangle<int>> blocks;

        auto strip = a.removeFromTop (236);
        blocks.push_back (strip.removeFromRight (std::min (380, strip.getWidth() / 3)));
        strip.removeFromRight (insightsui::gap);
        blocks.push_back (strip);
        a.removeFromTop (insightsui::gap);
        blocks.push_back (a.removeFromTop (440));
        a.removeFromTop (insightsui::gap);
        if (a.getHeight() > 60)
            blocks.push_back (a);

        for (const auto& b : blocks)
            theme::fillCard (g, b.toFloat());

        // travelling highlight
        const float w = (float) getWidth();
        const float x = -400.0f + phase * (w + 800.0f);
        juce::ColourGradient grad (juce::Colours::transparentWhite, x, 0.0f,
                                   juce::Colours::transparentWhite, x + 400.0f, 0.0f, false);
        grad.addColour (0.5, juce::Colours::white.withAlpha (0.045f));
        for (const auto& b : blocks)
        {
            juce::Graphics::ScopedSaveState ss (g);
            juce::Path p;
            p.addRoundedRectangle (b.toFloat(), theme::radius);
            g.reduceClipRegion (p);
            g.setGradientFill (grad);
            g.fillRect (b);
        }

        auto msg = blocks.size() > 2 ? blocks[2] : getLocalBounds();
        auto icon = msg.withSizeKeepingCentre (28, 28).translated (0, -22);
        icons::draw (g, icons::Id::activity, icon.toFloat(), theme::accent.withAlpha (0.6f + 0.4f * std::sin (phase * juce::MathConstants<float>::twoPi)));
        g.setColour (theme::textDim);
        g.setFont (theme::fontMedium (14.0f));
        g.drawText ("Analysing the tune, frequency by frequency...", msg.withSizeKeepingCentre (msg.getWidth(), 24).translated (0, 14),
                    juce::Justification::centred, true);
    }

private:
    void timerCallback() override
    {
        phase += 1.0f / 90.0f;
        if (phase > 1.0f)
            phase -= 1.0f;
        repaint();
    }

    float phase = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShimmerPanel)
};
} // namespace fa::ui
