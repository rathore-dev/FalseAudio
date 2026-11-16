#pragma once

#include "TabbedPage.h"
#include "MeasurePage.h"
#include "LivePage.h"
#include "AlignmentPage.h"
#include "CrossoverEqPage.h"
#include "../components/DspSend.h"

namespace fa::ui
{
/** Expert: the manual tools Auto Tune normally drives for you, as sub-tabs - Measure,
    Live (RTA), Alignment and Crossovers & EQ. One "Send to DSP" for the whole area pushes
    the current config (delays, gains, polarity, crossovers, EQ) to the DSP. */
class ExpertPage : public TabbedPage
{
public:
    explicit ExpertPage (Session& s) : TabbedPage (s, "tab.expert")
    {
        addTab<MeasurePage> ("measure", "Measure", 104);
        addTab<LivePage> ("live", "Live RTA", 104);
        addTab<AlignmentPage> ("alignment", "Alignment", 112);
        crossover = &addTab<CrossoverEqPage> ("crossover", "Crossovers & EQ", 148);
        finishTabs();

        sendButton.setComponentID ("expert.send");
        sendButton.setTooltip ("Write the current delay, gain, polarity, crossovers and EQ of every channel to the DSP");
        sendButton.onClick = [this]
        {
            crossover->flushPendingEdits();
            sendConfigToDsp (session, shell);
        };
        addAndMakeVisible (sendButton);

        hint.setText ("Manual tools. Auto Tune sets all of this for you.", juce::dontSendNotification);
        hint.setFont (theme::font (12.0f));
        hint.setColour (juce::Label::textColourId, theme::textMuted);
        hint.setJustificationType (juce::Justification::centredRight);
        hint.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (hint);
    }

    juce::String getTitle() const override { return "Expert"; }
    icons::Id getIcon() const override     { return icons::Id::flask; }

private:
    void layoutBarExtras (juce::Rectangle<int> area) override
    {
        sendButton.setBounds (area.removeFromRight (132));
        area.removeFromRight (theme::pad);
        hint.setVisible (area.getWidth() >= 280);
        hint.setBounds (area);
    }

    CrossoverEqPage* crossover = nullptr;
    FaButton sendButton { "Send to DSP", FaButton::Style::primary, icons::Id::bolt };
    juce::Label hint;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExpertPage)
};
} // namespace fa::ui
