#include "DspPage.h"

namespace fa::ui
{
DspPage::DspPage (Session& s) : Page (s) {}
DspPage::~DspPage() = default;

void DspPage::paint (juce::Graphics& g)
{
    g.setColour (theme::textDim);
    g.setFont (theme::font (15.0f));
    g.drawText (getTitle() + " - coming soon", getLocalBounds(), juce::Justification::centred);
}

void DspPage::resized() {}
} // namespace fa::ui
