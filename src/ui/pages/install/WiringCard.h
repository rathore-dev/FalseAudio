#pragma once

#include "InstallKit.h"

namespace fa::ui
{
/** Wire checks as a table: current, voltage drop (with a bar), voltage at the load, loss and the
    recommended gauge, each with an OK / Upgrade badge. */
class WiringCard : public Card
{
public:
    WiringCard();

    void setChecks (const std::vector<install::WireCheck>& checks, double supplyV);
    int heightFor (int width) const;

    void resized() override;

private:
    class Table : public juce::Component
    {
    public:
        std::vector<install::WireCheck> checks;
        double supplyV = 14.2;
        int hoverRow = -1;
        void paint (juce::Graphics&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        int heightFor() const;
    };
    Table table;
    ikit::Note legend { "Drop bars are scaled to 10 % of supply. Keep every run under 3 % (0.4 V) - voltage is amplifier headroom." };
};
} // namespace fa::ui
