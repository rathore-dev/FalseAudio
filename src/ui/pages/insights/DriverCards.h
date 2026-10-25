#pragma once

#include "InsightsCommon.h"

namespace fa::ui
{
/** Diagnostics card for one output channel: role-colour stripe, usable band, arrival, polarity
    status, SNR and peak THD, a THD-vs-frequency mini plot, resonance pills and plain-language
    findings with icons. */
class DriverCard : public juce::Component
{
public:
    DriverCard();

    void set (const insights::DriverDiagnostics& d, const SystemConfig& cfg);
    int idealHeight (int width) const;

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    int findingsHeight (int width) const;

    insights::DriverDiagnostics diag;
    juce::String title, subtitle;
    juce::Colour colour = theme::accent;
    FrequencyPlot thdPlot;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DriverCard)
};

/** Responsive grid of DriverCards (1-4 columns). */
class DriverCardGrid : public juce::Component
{
public:
    DriverCardGrid() = default;

    void set (const insights::Insights& in, const SystemConfig& cfg);
    int idealHeight (int width) const;
    int numCards() const noexcept { return (int) cards.size(); }

    void resized() override;

private:
    int columnsFor (int width) const noexcept { return juce::jlimit (1, 4, width / 380); }
    std::vector<std::unique_ptr<DriverCard>> cards;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DriverCardGrid)
};
} // namespace fa::ui
