#include "LivePage.h"

namespace fa::ui
{
LivePage::LivePage (Session& s) : Page (s) {}
LivePage::~LivePage() = default;

void LivePage::paint (juce::Graphics& g)
{
    g.setColour (theme::textDim);
    g.setFont (theme::font (15.0f));
    g.drawText (getTitle() + " - coming soon", getLocalBounds(), juce::Justification::centred);
}

void LivePage::resized() {}
} // namespace fa::ui
