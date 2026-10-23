#include "CrossoverEqPage.h"

namespace fa::ui
{
CrossoverEqPage::CrossoverEqPage (Session& s) : Page (s) {}
CrossoverEqPage::~CrossoverEqPage() = default;

void CrossoverEqPage::paint (juce::Graphics& g)
{
    g.setColour (theme::textDim);
    g.setFont (theme::font (15.0f));
    g.drawText (getTitle() + " - coming soon", getLocalBounds(), juce::Justification::centred);
}

void CrossoverEqPage::resized() {}
} // namespace fa::ui
