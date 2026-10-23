#include "ReportPage.h"

namespace fa::ui
{
ReportPage::ReportPage (Session& s) : Page (s) {}
ReportPage::~ReportPage() = default;

void ReportPage::paint (juce::Graphics& g)
{
    g.setColour (theme::textDim);
    g.setFont (theme::font (15.0f));
    g.drawText (getTitle() + " - coming soon", getLocalBounds(), juce::Justification::centred);
}

void ReportPage::resized() {}
} // namespace fa::ui
