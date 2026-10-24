#pragma once

#include "../system/PageKit.h"

namespace fa::ui
{
/** Friendly banner at the top of the Hardware page: explains Demo mode (simulated car and mics)
    and offers a one-click switch to Live, or the reverse. */
class ModeBanner : public juce::Component
{
public:
    static constexpr int height = 72;

    ModeBanner()
    {
        addAndMakeVisible (button);
        button.onClick = [this] { if (onSwitch) onSwitch (! demo); };
        setDemo (true);
    }

    void setDemo (bool isDemo)
    {
        demo = isDemo;
        button.setStyle (demo ? FaButton::Style::primary : FaButton::Style::secondary);
        button.setButtonText (demo ? "Switch to Live" : "Back to Demo");
        button.setIcon (demo ? icons::Id::plug : icons::Id::car);
        repaint();
    }

    std::function<void (bool wantLive)> onSwitch;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        const auto tint = demo ? theme::accent : theme::good;
        g.setColour (tint.withAlpha (0.08f));
        g.fillRoundedRectangle (r, theme::radius);
        g.setGradientFill (juce::ColourGradient (tint.withAlpha (0.16f), r.getX(), r.getY(),
                                                 tint.withAlpha (0.0f), r.getX() + r.getWidth() * 0.6f, r.getY(), false));
        g.fillRoundedRectangle (r, theme::radius);
        g.setColour (tint.withAlpha (0.45f));
        g.drawRoundedRectangle (r, theme::radius, 1.0f);

        auto b = getLocalBounds().reduced (kit::gap2, 0);
        auto iconArea = b.removeFromLeft (40).toFloat().withSizeKeepingCentre (40.0f, 40.0f);
        g.setColour (tint.withAlpha (0.15f));
        g.fillEllipse (iconArea);
        icons::draw (g, demo ? icons::Id::car : icons::Id::bolt, iconArea.reduced (10.0f), tint);
        b.removeFromLeft (kit::gap2);
        b.removeFromRight (button.getWidth() + kit::gap2);

        g.setColour (theme::text);
        g.setFont (theme::fontMedium (14.0f));
        g.drawText (demo ? "Demo mode - hardware is simulated" : "Live mode - using your audio interface",
                    b.removeFromTop (height / 2).withTrimmedTop (14), juce::Justification::centredLeft, true);
        g.setColour (theme::textDim);
        g.setFont (theme::font (12.5f));
        g.drawText (demo ? "A virtual car, DSP and microphones stand in for the real thing so you can explore every page. "
                           "Settings below still apply to your interface when you go Live."
                         : "Sweeps play through the outputs below and are captured from the selected microphones.",
                    b.withTrimmedBottom (12), juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        button.setBounds (getLocalBounds().reduced (kit::gap2, 0).removeFromRight (150).withSizeKeepingCentre (150, kit::btnH));
    }

private:
    FaButton button { "Switch to Live", FaButton::Style::primary, icons::Id::plug };
    bool demo = true;
};
} // namespace fa::ui
