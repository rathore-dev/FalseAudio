#pragma once

#include "../../Page.h"
#include "../../components/Widgets.h"

namespace fa::ui
{
/** Slim informational banner with an action button, shown when live audio is unavailable. */
class DemoBanner : public juce::Component
{
public:
    DemoBanner (const juce::String& message, const juce::String& actionText, icons::Id icon = icons::Id::info)
        : text (message), iconId (icon), action (actionText, FaButton::Style::secondary, icons::Id::bolt)
    {
        addAndMakeVisible (action);
        action.onClick = [this] { if (onAction) onAction(); };
    }

    std::function<void()> onAction;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        theme::fillCard (g, r, theme::accent.withAlpha (0.07f), theme::accent.withAlpha (0.35f), theme::radiusSm);
        auto inner = getLocalBounds().reduced (14, 0);
        icons::draw (g, iconId, inner.removeFromLeft (18).withSizeKeepingCentre (16, 16).toFloat(), theme::accent);
        inner.removeFromLeft (10);
        inner.removeFromRight (action.getWidth() + 10);
        g.setColour (theme::text);
        g.setFont (theme::font (12.5f));
        g.drawFittedText (text, inner, juce::Justification::centredLeft, 1);
    }

    void resized() override
    {
        action.setBounds (getLocalBounds().reduced (8, 6).removeFromRight (160));
    }

private:
    juce::String text;
    icons::Id iconId;
    FaButton action;
};
} // namespace fa::ui
