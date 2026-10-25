#pragma once

#include "InsightsCommon.h"

namespace fa::ui
{
/** 1/3-octave band grid (20 Hz - 20 kHz): target, before, after, delta and EQ applied for left
    and right, plus the L+R sum. Deltas are heat-coloured (green on target, red far off), EQ cells
    carry a mini bar, headers sort on click, hovering a row reports its centre frequency so the
    explorer can shade that band. */
class BandTable : public Card
{
public:
    BandTable();

    void set (const insights::Insights& in);

    std::function<void (double centreHz)> onBandHover;   // 0 when the mouse leaves the grid
    std::function<void()> onExport;

    int idealHeight (int width) const;

    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    enum Col { freq, target, lBefore, lAfter, lDelta, lEq, rBefore, rAfter, rDelta, rEq, sum, numCols };

    double cellValue (const insights::BandRow& r, int col) const;
    double sortValue (const insights::BandRow& r, int col) const;
    void resort();
    juce::Rectangle<int> gridArea() const;
    juce::Rectangle<int> columnRect (juce::Rectangle<int> rowRect, int col) const;
    int rowAt (juce::Point<int> p) const;
    int columnAt (juce::Point<int> p) const;

    std::vector<insights::BandRow> rows;
    std::vector<int> order;
    int sortCol = freq;
    bool sortAscending = true;
    int hoverRow = -1, hoverCol = -1;
    bool hoverHeader = false;

    int withinL = 0, withinR = 0;
    double worstL = 0, worstLHz = 0, worstR = 0, worstRHz = 0;

    FaButton exportButton { "Export CSV", FaButton::Style::secondary, icons::Id::download };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandTable)
};
} // namespace fa::ui
