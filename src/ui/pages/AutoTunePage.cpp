#include "AutoTunePage.h"

namespace fa::ui
{
AutoTunePage::AutoTunePage (Session& s) : Page (s) {}
AutoTunePage::~AutoTunePage() = default;

void AutoTunePage::paint (juce::Graphics& g)
{
    g.setColour (theme::textDim);
    g.setFont (theme::font (15.0f));
    g.drawText (getTitle() + " - coming soon", getLocalBounds(), juce::Justification::centred);
}

void AutoTunePage::resized() {}
} // namespace fa::ui
