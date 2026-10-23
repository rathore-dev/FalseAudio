#include "HardwarePage.h"

namespace fa::ui
{
HardwarePage::HardwarePage (Session& s) : Page (s) {}
HardwarePage::~HardwarePage() = default;

void HardwarePage::paint (juce::Graphics& g)
{
    g.setColour (theme::textDim);
    g.setFont (theme::font (15.0f));
    g.drawText (getTitle() + " - coming soon", getLocalBounds(), juce::Justification::centred);
}

void HardwarePage::resized() {}
} // namespace fa::ui
