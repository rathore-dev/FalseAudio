#pragma once

#include "InstallKit.h"

namespace fa::ui
{
/** Step-by-step checklist of every manual setting, grouped by area (Wiring -> Head unit -> DSP ->
    Amps -> Speakers -> Safety). Rows expand to show the steps and the "why" maths; tick boxes are
    persisted in a small JSON file next to the app settings. */
class ChecklistCard : public Card
{
public:
    explicit ChecklistCard (juce::File tickFile);

    void setInstructions (const std::vector<install::Instruction>& list);

    int heightFor (int width) const;
    int countDone() const;
    int countTotal() const { return (int) rows.size(); }
    /** Number of unticked rows with the given severity. */
    int countOpen (ikit::Severity s) const;

    /** Called when expanding/collapsing changes the height, or a tick changes the progress. */
    std::function<void()> onLayoutChanged, onProgressChanged;

    /** Plain-text version (used by Copy all / Export). */
    juce::String asText (bool markdown) const;

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    struct Row
    {
        install::Instruction ins;
        ikit::Area area = ikit::Area::other;
        juce::String key;
        bool expanded = false;
    };

    class View : public juce::Component
    {
    public:
        explicit View (ChecklistCard& o) : owner (o) {}
        void paint (juce::Graphics&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
    private:
        ChecklistCard& owner;
        int hoverRow = -1, hoverGroup = -1;
        bool hoverTick = false;
    };

    /** Layout entry: either a group header (row < 0) or an instruction row. */
    struct Item { int group = -1, row = -1; juce::Rectangle<int> bounds; int detailH = 0, whyH = 0; };

    void rebuildLayout (int width) const;
    int viewHeight (int width) const;
    static juce::Rectangle<int> tickBoxFor (juce::Rectangle<int> rowBounds);
    void exportChecklist();
    void updateProgressText();

    std::vector<Row> rows;
    std::vector<ikit::Area> groupsInOrder;
    std::set<int> collapsedGroups;
    ikit::TickStore ticks;

    mutable std::vector<Item> items;
    mutable int layoutWidth = -1;

    View view { *this };
    FaButton copyBtn { "Copy all", FaButton::Style::ghost, icons::Id::copy };
    FaButton exportBtn { "Export", FaButton::Style::secondary, icons::Id::download };
    FaButton resetBtn { "Reset ticks", FaButton::Style::ghost, icons::Id::refresh };
    std::unique_ptr<juce::FileChooser> chooser;

    static constexpr int groupH = 44, rowCollapsedH = 60;

    friend class View;
};
} // namespace fa::ui
