#include "BandTable.h"

namespace fa::ui
{
namespace
{
    constexpr int groupH  = 20;
    constexpr int headerH = 28;
    constexpr int rowH    = 26;
    constexpr int footerH = 30;
    constexpr int freqColW = 88;

    const char* const columnNames[] = { "Freq", "Target", "Before", "After", "Delta", "EQ", "Before", "After", "Delta", "EQ", "L+R" };
}

BandTable::BandTable()
    : Card ("Band table", "1/3 octave, 20 Hz - 20 kHz  |  click a header to sort, hover a row to see it in the explorer")
{
    addAndMakeVisible (exportButton);
    exportButton.onClick = [this] { if (onExport) onExport(); };
}

void BandTable::set (const insights::Insights& in)
{
    rows.clear();
    for (const auto& b : in.bands)
        if (b.freqHz >= 19.0 && b.freqHz <= 20100.0)
            rows.push_back (b);

    withinL = withinR = 0;
    worstL = worstR = worstLHz = worstRHz = 0.0;
    for (const auto& r : rows)
    {
        const double dl = r.leftAfterDb - r.targetDb, dr = r.rightAfterDb - r.targetDb;
        if (std::abs (dl) <= 1.0) ++withinL;
        if (std::abs (dr) <= 1.0) ++withinR;
        if (std::abs (dl) > std::abs (worstL)) { worstL = dl; worstLHz = r.freqHz; }
        if (std::abs (dr) > std::abs (worstR)) { worstR = dr; worstRHz = r.freqHz; }
    }

    hoverRow = hoverCol = -1;
    resort();
    repaint();
}

double BandTable::cellValue (const insights::BandRow& r, int col) const
{
    switch (col)
    {
        case freq:    return r.freqHz;
        case target:  return r.targetDb;
        case lBefore: return r.leftBeforeDb;
        case lAfter:  return r.leftAfterDb;
        case lDelta:  return r.leftAfterDb - r.targetDb;
        case lEq:     return r.eqAppliedLeftDb;
        case rBefore: return r.rightBeforeDb;
        case rAfter:  return r.rightAfterDb;
        case rDelta:  return r.rightAfterDb - r.targetDb;
        case rEq:     return r.eqAppliedRightDb;
        case sum:     return r.sumAfterDb;
        default:      return 0.0;
    }
}

double BandTable::sortValue (const insights::BandRow& r, int col) const
{
    const double v = cellValue (r, col);
    return (col == lDelta || col == rDelta || col == lEq || col == rEq) ? std::abs (v) : v;
}

void BandTable::resort()
{
    order.resize (rows.size());
    for (size_t i = 0; i < order.size(); ++i)
        order[i] = (int) i;
    std::stable_sort (order.begin(), order.end(), [this] (int a, int b)
    {
        const double va = sortValue (rows[(size_t) a], sortCol), vb = sortValue (rows[(size_t) b], sortCol);
        return sortAscending ? va < vb : va > vb;
    });
}

int BandTable::idealHeight (int) const
{
    return insightsui::cardHeaderH + groupH + headerH + (int) rows.size() * rowH + footerH + theme::pad;
}

juce::Rectangle<int> BandTable::gridArea() const
{
    auto a = getContentArea();
    a.removeFromBottom (footerH);
    return a;
}

juce::Rectangle<int> BandTable::columnRect (juce::Rectangle<int> rowRect, int col) const
{
    const int flexW = (rowRect.getWidth() - freqColW) / (numCols - 1);
    if (col == freq)
        return rowRect.withWidth (freqColW);
    return { rowRect.getX() + freqColW + (col - 1) * flexW, rowRect.getY(), flexW, rowRect.getHeight() };
}

int BandTable::rowAt (juce::Point<int> p) const
{
    auto g = gridArea();
    g.removeFromTop (groupH + headerH);
    if (! g.contains (p))
        return -1;
    const int i = (p.y - g.getY()) / rowH;
    return i >= 0 && i < (int) order.size() ? i : -1;
}

int BandTable::columnAt (juce::Point<int> p) const
{
    auto g = gridArea();
    if (! g.contains (p))
        return -1;
    if (p.x < g.getX() + freqColW)
        return freq;
    const int flexW = (g.getWidth() - freqColW) / (numCols - 1);
    const int c = 1 + (p.x - g.getX() - freqColW) / std::max (1, flexW);
    return c < numCols ? c : -1;
}

void BandTable::resized()
{
    auto action = getHeaderActionArea();
    const int w = std::min (138, action.getWidth());
    exportButton.setBounds (action.removeFromRight (w).withSizeKeepingCentre (w, 30));
}

void BandTable::mouseMove (const juce::MouseEvent& e)
{
    const int r = rowAt (e.getPosition());
    const int c = columnAt (e.getPosition());
    auto head = gridArea();
    head.removeFromTop (groupH);
    const bool h = head.removeFromTop (headerH).contains (e.getPosition());
    if (r != hoverRow || c != hoverCol || h != hoverHeader)
    {
        hoverRow = r;
        hoverCol = c;
        hoverHeader = h;
        setMouseCursor (h ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
        if (onBandHover)
            onBandHover (r >= 0 ? rows[(size_t) order[(size_t) r]].freqHz : 0.0);
    }
}

void BandTable::mouseExit (const juce::MouseEvent&)
{
    if (hoverRow >= 0 && onBandHover)
        onBandHover (0.0);
    hoverRow = hoverCol = -1;
    hoverHeader = false;
    repaint();
}

void BandTable::mouseDown (const juce::MouseEvent& e)
{
    if (! hoverHeader)
        return;
    const int c = columnAt (e.getPosition());
    if (c < 0)
        return;
    if (c == sortCol)
        sortAscending = ! sortAscending;
    else
    {
        sortCol = c;
        sortAscending = (c == freq || c == target || c == lBefore || c == lAfter || c == rBefore || c == rAfter || c == sum);
    }
    resort();
    repaint();
}

void BandTable::paint (juce::Graphics& g)
{
    Card::paint (g);
    if (rows.empty())
        return;

    auto grid = gridArea();
    const float radius = theme::radiusSm;

    // grid frame
    g.setColour (theme::bg1);
    g.fillRoundedRectangle (grid.toFloat(), radius);

    // ---- group header (LEFT / RIGHT) ----------------------------------------
    auto group = grid.removeFromTop (groupH);
    {
        auto left = columnRect (group, lBefore).getUnion (columnRect (group, lEq));
        auto right = columnRect (group, rBefore).getUnion (columnRect (group, rEq));
        g.setColour (theme::curveLeft.withAlpha (0.10f));
        g.fillRect (left);
        g.setColour (theme::curveRight.withAlpha (0.10f));
        g.fillRect (right);
        theme::drawEyebrow (g, "LEFT", left, theme::curveLeft, juce::Justification::centred);
        theme::drawEyebrow (g, "RIGHT", right, theme::curveRight, juce::Justification::centred);
        theme::drawEyebrow (g, "SUM", columnRect (group, sum), theme::curveSum, juce::Justification::centred);
    }

    // ---- column headers -----------------------------------------------------
    auto header = grid.removeFromTop (headerH);
    g.setColour (theme::bg3);
    g.fillRect (header);
    g.setFont (theme::fontMedium (11.5f));
    for (int c = 0; c < numCols; ++c)
    {
        auto r = columnRect (header, c).reduced (8, 0);
        const bool active = c == sortCol;
        const bool hot = hoverHeader && hoverCol == c;
        g.setColour (active ? theme::accent : hot ? theme::text : theme::textDim);
        juce::String name = columnNames[c];
        if (active)
            name += sortAscending ? juce::String (juce::CharPointer_UTF8 ("  \xe2\x96\xb4"))
                                  : juce::String (juce::CharPointer_UTF8 ("  \xe2\x96\xbe"));
        g.drawText (name, r, c == freq ? juce::Justification::centredLeft : juce::Justification::centredRight, true);
    }
    g.setColour (theme::border);
    g.fillRect (header.getX(), header.getBottom() - 1, header.getWidth(), 1);

    // ---- rows ----------------------------------------------------------------
    const auto monoF = theme::mono (12.0f);
    for (size_t i = 0; i < order.size(); ++i)
    {
        const auto& b = rows[(size_t) order[i]];
        auto row = grid.removeFromTop (rowH);
        const bool hot = (int) i == hoverRow;

        if (hot)
        {
            g.setColour (theme::bg3);
            g.fillRect (row);
            g.setColour (theme::accent);
            g.fillRect (row.getX(), row.getY(), 2, row.getHeight());
        }
        else if (i % 2 == 1)
        {
            g.setColour (juce::Colours::white.withAlpha (0.018f));
            g.fillRect (row);
        }

        for (int c = 0; c < numCols; ++c)
        {
            auto cell = columnRect (row, c).reduced (8, 0);
            const double v = cellValue (b, c);

            switch (c)
            {
                case freq:
                    g.setFont (theme::fontMedium (12.0f));
                    g.setColour (hot ? theme::text : theme::textDim);
                    g.drawText (theme::formatHz (b.freqHz, 1), cell, juce::Justification::centredLeft, true);
                    break;

                case lDelta:
                case rDelta:
                {
                    const auto colour = insightsui::heatColour (v);
                    const auto txt = theme::formatDb (v, 1, true);
                    const int pw = (int) std::ceil (theme::textWidth (monoF, txt)) + 14;
                    auto pill = cell.removeFromRight (std::min (pw, cell.getWidth())).withSizeKeepingCentre (pw, 18).toFloat();
                    g.setColour (colour.withAlpha (0.16f));
                    g.fillRoundedRectangle (pill, 5.0f);
                    g.setColour (colour);
                    g.setFont (monoF);
                    g.drawText (txt, pill.toNearestInt(), juce::Justification::centred, true);
                    break;
                }

                case lEq:
                case rEq:
                {
                    // mini centred bar under the value: boost to the right, cut to the left
                    auto bar = cell.removeFromBottom (4).reduced (2, 0).toFloat();
                    bar.setY (bar.getY() - 2.0f);
                    const float mid = bar.getCentreX();
                    const float half = bar.getWidth() * 0.5f;
                    const float extent = (float) juce::jlimit (-1.0, 1.0, v / 8.0) * half;
                    g.setColour (theme::bg4);
                    g.fillRect (bar);
                    g.setColour (v >= 0.0 ? theme::accent : theme::warn);
                    g.fillRect (juce::Rectangle<float> (std::min (mid, mid + extent), bar.getY(), std::abs (extent), bar.getHeight()));
                    g.setFont (monoF);
                    g.setColour (std::abs (v) < 0.05 ? theme::textMuted : theme::text);
                    g.drawText (theme::formatDb (v, 1, true), cell.withTrimmedBottom (2), juce::Justification::centredRight, true);
                    break;
                }

                case target:
                case lBefore:
                case rBefore:
                    g.setFont (monoF);
                    g.setColour (theme::textDim);
                    g.drawText (theme::formatDb (v, 1, false), cell, juce::Justification::centredRight, true);
                    break;

                default:
                    g.setFont (monoF);
                    g.setColour (theme::text);
                    g.drawText (theme::formatDb (v, 1, false), cell, juce::Justification::centredRight, true);
                    break;
            }
        }

        g.setColour (theme::border.withAlpha (0.6f));
        g.fillRect (row.getX(), row.getBottom() - 1, row.getWidth(), 1);
    }

    // ---- column separators ------------------------------------------------
    auto full = gridArea();
    g.setColour (theme::border.withAlpha (0.8f));
    for (int c : { lBefore, rBefore, sum })
    {
        const auto r = columnRect (full, c);
        g.fillRect (r.getX(), full.getY() + groupH, 1, full.getHeight() - groupH);
    }
    g.setColour (theme::border);
    g.drawRoundedRectangle (full.toFloat().reduced (0.5f), radius, 1.0f);

    // ---- footer summary ----------------------------------------------------
    auto footer = getContentArea().removeFromBottom (footerH).withTrimmedTop (8);
    const int n = (int) rows.size();
    auto chip = [&] (const juce::String& txt, juce::Colour c)
    {
        const int w = insightsui::drawPill (g, footer, txt, c);
        footer.removeFromLeft (w + 8);
    };
    chip ("L within 1 dB: " + juce::String (withinL) + " / " + juce::String (n), theme::curveLeft);
    chip ("R within 1 dB: " + juce::String (withinR) + " / " + juce::String (n), theme::curveRight);
    if (worstLHz > 0.0)
        chip ("Worst L " + theme::formatDb (worstL, 1) + " @ " + theme::formatHz (worstLHz, 0), insightsui::heatColour (worstL));
    if (worstRHz > 0.0)
        chip ("Worst R " + theme::formatDb (worstR, 1) + " @ " + theme::formatHz (worstRHz, 0), insightsui::heatColour (worstR));
}
} // namespace fa::ui
