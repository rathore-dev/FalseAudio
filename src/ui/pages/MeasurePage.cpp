#include "MeasurePage.h"

namespace fa::ui
{
MeasurePage::MeasurePage (Session& s) : Page (s) {}
MeasurePage::~MeasurePage() = default;

void MeasurePage::paint (juce::Graphics& g)
{
    g.setColour (theme::textDim);
    g.setFont (theme::font (15.0f));
    g.drawText (getTitle() + " - coming soon", getLocalBounds(), juce::Justification::centred);
}

void MeasurePage::resized() {}
} // namespace fa::ui
