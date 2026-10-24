#pragma once

#include "../system/PageKit.h"

namespace fa::ui
{
/** Horizontal time axis with one track per driver: hollow marker = measured arrival (before),
    filled marker = arrival + DSP delay (after), dashed line = the alignment plane everything
    should land on. Without measured arrivals it shows the applied delays alone. */
class ArrivalTimeline : public juce::Component
{
public:
    struct Item
    {
        juce::String id, label;
        juce::Colour colour;
        std::optional<double> arrivalMs;   // measured, no delay
        double delayMs = 0.0;
        bool muted = false;
    };

    static constexpr int rowH = 28, axisH = 34, labelW = 170;

    void setItems (std::vector<Item> newItems)
    {
        items = std::move (newItems);
        repaint();
    }
    void setHighlight (const juce::String& id) { highlight = id; repaint(); }
    std::function<void (const juce::String&)> onRowClicked;

    int idealHeight() const { return (int) items.size() * rowH + axisH + 8; }
    bool hasArrivals() const
    {
        return std::any_of (items.begin(), items.end(), [] (const Item& i) { return i.arrivalMs.has_value(); });
    }

    void paint (juce::Graphics& g) override
    {
        if (items.empty())
            return;

        const bool measured = hasArrivals();
        double maxMs = 1.0;
        for (auto& i : items)
            maxMs = juce::jmax (maxMs, (i.arrivalMs ? *i.arrivalMs : 0.0) + i.delayMs);
        const double step = niceStep (maxMs);
        const double span = std::ceil ((maxMs * 1.08) / step) * step;

        auto area = getLocalBounds().reduced (12, 4);
        auto axis = area.removeFromBottom (axisH);
        auto tracks = area.withTrimmedLeft (labelW);
        const float x0 = (float) tracks.getX(), x1 = (float) tracks.getRight();
        auto xFor = [&] (double ms) { return x0 + (float) (ms / span) * (x1 - x0); };

        // grid + axis labels
        g.setFont (theme::mono (10.5f));
        for (double t = 0.0; t <= span + 1e-9; t += step)
        {
            const float x = xFor (t);
            g.setColour (theme::border.withAlpha (t == 0.0 ? 1.0f : 0.6f));
            g.drawVerticalLine ((int) x, (float) area.getY(), (float) axis.getY() + 6.0f);
            g.setColour (theme::textMuted);
            g.drawText (juce::String (t, step < 1.0 ? 1 : 0) + " ms", juce::Rectangle<int> ((int) x - 30, axis.getY() + 8, 60, 14),
                        juce::Justification::centred, false);
        }

        // alignment plane
        double plane = 0.0;
        for (auto& i : items)
            plane = juce::jmax (plane, (i.arrivalMs ? *i.arrivalMs : 0.0) + i.delayMs);
        {
            juce::Path p;
            p.startNewSubPath (xFor (plane), (float) area.getY());
            p.lineTo (xFor (plane), (float) axis.getY() + 6.0f);
            g.setColour (theme::accent.withAlpha (0.7f));
            theme::drawDashedPath (g, p, 1.2f, 5.0f, 4.0f);
            g.setFont (theme::fontMedium (10.5f));
            g.drawText (measured ? "aligned" : "latest", juce::Rectangle<int> ((int) xFor (plane) - 40, axis.getBottom() - 12, 80, 12),
                        juce::Justification::centred, false);
        }

        // tracks
        int y = area.getY();
        for (auto& item : items)
        {
            auto row = juce::Rectangle<int> (area.getX(), y, area.getWidth(), rowH);
            const bool hi = item.id == highlight;
            if (hi)
            {
                g.setColour (item.colour.withAlpha (0.08f));
                g.fillRoundedRectangle (row.toFloat(), 4.0f);
            }
            const float cy = (float) row.getCentreY();
            const auto col = item.muted ? item.colour.withAlpha (0.35f) : item.colour;

            g.setColour (item.muted ? theme::textMuted : (hi ? theme::text : theme::textDim));
            g.setFont (theme::font (12.0f));
            g.drawText (item.label, row.removeFromLeft (labelW).reduced (6, 0), juce::Justification::centredLeft, true);

            g.setColour (theme::border.withAlpha (0.5f));
            g.fillRect (x0, cy - 0.5f, x1 - x0, 1.0f);

            const double before = item.arrivalMs ? *item.arrivalMs : 0.0;
            const double after  = before + item.delayMs;
            const float xb = xFor (before), xa = xFor (after);

            // delay segment
            g.setColour (col.withAlpha (0.55f));
            g.fillRoundedRectangle (juce::jmin (xb, xa), cy - 2.0f, std::abs (xa - xb), 4.0f, 2.0f);

            if (measured)
            {
                // measured arrival: hollow
                g.setColour (col);
                g.drawEllipse (xb - 5.0f, cy - 5.0f, 10.0f, 10.0f, 1.5f);
            }
            // after delay: filled with glow
            g.setColour (col.withAlpha (0.25f));
            g.fillEllipse (xa - 8.0f, cy - 8.0f, 16.0f, 16.0f);
            g.setColour (col);
            g.fillEllipse (xa - 5.0f, cy - 5.0f, 10.0f, 10.0f);

            g.setColour (theme::textDim);
            g.setFont (theme::mono (10.5f));
            const juce::String text = measured ? theme::formatMs (after, 2) : "+" + theme::formatMs (item.delayMs, 2);
            const float tx = xa + 12.0f;
            if (tx + 60.0f < x1)
                g.drawText (text, juce::Rectangle<int> ((int) tx, (int) cy - 7, 70, 14), juce::Justification::centredLeft, false);
            else
                g.drawText (text, juce::Rectangle<int> ((int) xa - 82, (int) cy - 7, 70, 14), juce::Justification::centredRight, false);

            y += rowH;
        }

        // legend
        g.setFont (theme::font (10.5f));
        auto legend = juce::Rectangle<int> (area.getX(), axis.getBottom() - 14, labelW + 160, 14);
        float lx = (float) legend.getX() + 6.0f;
        const float ly = (float) legend.getCentreY();
        if (measured)
        {
            g.setColour (theme::textDim);
            g.drawEllipse (lx, ly - 4.0f, 8.0f, 8.0f, 1.2f);
            g.setColour (theme::textMuted);
            g.drawText ("measured", juce::Rectangle<int> ((int) lx + 12, legend.getY(), 60, 14), juce::Justification::centredLeft, false);
            lx += 76.0f;
        }
        g.setColour (theme::textDim);
        g.fillEllipse (lx, ly - 4.0f, 8.0f, 8.0f);
        g.setColour (theme::textMuted);
        g.drawText (measured ? "after delay" : "delay applied", juce::Rectangle<int> ((int) lx + 12, legend.getY(), 90, 14), juce::Justification::centredLeft, false);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        const int row = (e.y - 4) / rowH;
        if (row >= 0 && row < (int) items.size() && onRowClicked)
            onRowClicked (items[(size_t) row].id);
    }

private:
    static double niceStep (double maxMs)
    {
        const double raw = maxMs / 6.0;
        for (double s : { 0.1, 0.2, 0.25, 0.5, 1.0, 2.0, 2.5, 5.0, 10.0, 20.0 })
            if (raw <= s)
                return s;
        return 50.0;
    }

    std::vector<Item> items;
    juce::String highlight;
};
} // namespace fa::ui
