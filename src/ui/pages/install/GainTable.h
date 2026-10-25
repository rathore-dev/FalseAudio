#pragma once

#include "InstallKit.h"

namespace fa::ui
{
/** Per-channel gain table: load, amp power, driver RMS, power ratio gauge, DMM target voltage,
    gain setting, excursion-limited HP vs the tune's HP (warning when the tune is lower) and an
    "Apply safe limits to tuning" action. */
class GainTable : public Card
{
public:
    GainTable();

    void setPlan (const std::vector<install::ChannelPlan>& channels, const SystemConfig& config);
    int heightFor (int width) const;

    /** Number of channels whose tune HP is below the excursion-limited HP. */
    int countUnsafeHp() const;

    std::function<void()> onApply;

    void resized() override;

private:
    struct Row
    {
        install::ChannelPlan plan;
        juce::String label;
        juce::Colour roleColour;
        double tuneHpHz = 0;
        bool hasHp = true;
        bool hpUnsafe = false;
    };

    class Table : public juce::Component
    {
    public:
        std::vector<Row> rows;
        int hoverRow = -1;
        int heightFor() const;
        int rowHeight (const Row& r) const;
        void paint (juce::Graphics&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
    };

    Table table;
    FaButton applyBtn { "Apply safe limits to tuning", FaButton::Style::primary, icons::Id::check };
    ikit::Note note;
};
} // namespace fa::ui
