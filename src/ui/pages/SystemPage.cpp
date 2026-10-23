#include "SystemPage.h"

namespace fa::ui
{
SystemPage::SystemPage (Session& s) : Page (s) {}
SystemPage::~SystemPage() = default;

void SystemPage::paint (juce::Graphics& g)
{
    g.setColour (theme::textDim);
    g.setFont (theme::font (15.0f));
    g.drawText (getTitle() + " - coming soon", getLocalBounds(), juce::Justification::centred);
}

void SystemPage::resized() {}
} // namespace fa::ui
