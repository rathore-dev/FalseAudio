#include "AlignmentPage.h"

namespace fa::ui
{
AlignmentPage::AlignmentPage (Session& s) : Page (s) {}
AlignmentPage::~AlignmentPage() = default;

void AlignmentPage::paint (juce::Graphics& g)
{
    g.setColour (theme::textDim);
    g.setFont (theme::font (15.0f));
    g.drawText (getTitle() + " - coming soon", getLocalBounds(), juce::Justification::centred);
}

void AlignmentPage::resized() {}
} // namespace fa::ui
