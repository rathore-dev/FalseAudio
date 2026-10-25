#include "StatStrip.h"

namespace fa::ui
{
void StatStrip::setTitle (const juce::String& t, juce::Colour c)
{
    title = t;
    swatch = c;
    repaint();
}

void StatStrip::setStats (std::vector<Stat> s)
{
    stats = std::move (s);
    repaint();
}

void StatStrip::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    theme::fillCard (g, r, theme::bg2, theme::border);

    auto inner = getLocalBounds().reduced (theme::pad, 0);

    if (title.isNotEmpty())
    {
        auto titleArea = inner.removeFromLeft (juce::jmin (260, inner.getWidth() / 3));

        if (! swatch.isTransparent())
        {
            auto sw = titleArea.removeFromLeft (10).toFloat().withSizeKeepingCentre (10.0f, 10.0f);
            g.setColour (swatch);
            g.fillEllipse (sw);
            theme::drawGlow (g, sw, swatch, 5.0f, 6.0f);
            titleArea.removeFromLeft (10);
        }

        g.setColour (theme::text);
        g.setFont (theme::fontMedium (13.0f));
        g.drawFittedText (title, titleArea, juce::Justification::centredLeft, 2);

        // separator
        g.setColour (theme::border);
        g.fillRect (inner.getX(), inner.getY() + 10, 1, inner.getHeight() - 20);
        inner.removeFromLeft (theme::pad);
    }

    if (stats.empty())
        return;

    const int tileW = juce::jmax (72, inner.getWidth() / (int) stats.size());

    for (const auto& s : stats)
    {
        auto tile = inner.removeFromLeft (tileW).reduced (4, 8);

        g.setColour (theme::textMuted);
        g.setFont (theme::fontMedium (10.0f));
        g.drawText (s.label.toUpperCase(), tile.removeFromTop (14), juce::Justification::centredLeft);

        tile.removeFromTop (2);

        if (s.chip)
        {
            g.setFont (theme::fontBold (11.0f));
            const int w = juce::jmin (tile.getWidth(), (int) std::ceil (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), s.value)) + 18);
            auto chip = tile.removeFromLeft (w).withHeight (20).toFloat();
            g.setColour (s.colour.withAlpha (0.16f));
            g.fillRoundedRectangle (chip, 10.0f);
            g.setColour (s.colour.withAlpha (0.6f));
            g.drawRoundedRectangle (chip.reduced (0.5f), 10.0f, 1.0f);
            g.setColour (s.colour);
            g.drawText (s.value, chip, juce::Justification::centred);
            continue;
        }

        g.setColour (s.colour);
        g.setFont (theme::mono (16.0f));
        const float vw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), s.value);
        auto valueArea = tile.removeFromTop (22);
        g.drawText (s.value, valueArea, juce::Justification::centredLeft);

        if (s.unit.isNotEmpty())
        {
            g.setColour (theme::textDim);
            g.setFont (theme::font (11.0f));
            auto unitArea = valueArea.withTrimmedLeft ((int) vw + 4);
            g.drawText (s.unit, unitArea.withTrimmedTop (5), juce::Justification::centredLeft);
        }
    }
}
} // namespace fa::ui
