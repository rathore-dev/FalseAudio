#include "WaterfallView.h"

namespace fa::ui
{
void WaterfallView::setData (const analysis::Waterfall& wf, juce::Colour c, const juce::String& cap)
{
    data = wf;
    colour = c;
    caption = cap;

    maxDb = -200.0;
    for (const auto& slice : data.levelDb)
        for (double v : slice)
            if (std::isfinite (v))
                maxDb = std::max (maxDb, v);
    if (maxDb < -150.0)
        maxDb = 0.0;

    repaint();
}

void WaterfallView::clear()
{
    data = {};
    repaint();
}

WaterfallView::Geometry WaterfallView::geometry() const
{
    Geometry gm;
    gm.frame = getLocalBounds().toFloat().reduced (12.0f);
    const float left = 44.0f, bottom = 26.0f, top = 28.0f, right = 56.0f;
    auto plot = gm.frame.withTrimmedLeft (left).withTrimmedBottom (bottom).withTrimmedTop (top).withTrimmedRight (right);

    gm.dx = juce::jlimit (40.0f, 160.0f, plot.getWidth() * 0.16f);
    gm.dy = juce::jlimit (60.0f, 260.0f, plot.getHeight() * 0.42f);
    gm.x0 = plot.getX();
    gm.width = plot.getWidth() - gm.dx;
    gm.yBack = plot.getBottom() - gm.dy;
    gm.curveHeight = plot.getHeight() - gm.dy;

    if (! data.freqs.empty())
    {
        gm.fLow = std::max (10.0, data.freqs.front());
        gm.fHigh = data.freqs.back();
    }
    gm.dbHigh = std::ceil (maxDb / 5.0) * 5.0;
    gm.dbLow = gm.dbHigh - 60.0;
    return gm;
}

void WaterfallView::paint (juce::Graphics& g)
{
    auto gm = geometry();
    const int n = (int) data.levelDb.size();

    // ---- caption ----------------------------------------------------------
    g.setColour (theme::textDim);
    g.setFont (theme::fontMedium (12.0f));
    g.drawText (caption.isNotEmpty() ? caption : juce::String ("Spectral decay"),
                gm.frame.removeFromTop (18.0f).toNearestInt(), juce::Justification::topLeft);

    if (n == 0 || data.freqs.size() < 2)
    {
        g.setColour (theme::textMuted);
        g.setFont (theme::font (13.0f));
        g.drawText ("Select a measurement to see its spectral decay", getLocalBounds(), juce::Justification::centred);
        return;
    }

    // ---- floor grid (parallelogram) --------------------------------------
    {
        juce::Path floor;
        floor.startNewSubPath (gm.xFor (gm.fLow, 0.0f), gm.baseFor (0.0f));
        floor.lineTo (gm.xFor (gm.fHigh, 0.0f), gm.baseFor (0.0f));
        floor.lineTo (gm.xFor (gm.fHigh, 1.0f), gm.baseFor (1.0f));
        floor.lineTo (gm.xFor (gm.fLow, 1.0f), gm.baseFor (1.0f));
        floor.closeSubPath();
        g.setColour (theme::bg1.withAlpha (0.8f));
        g.fillPath (floor);
        g.setColour (theme::border);
        g.strokePath (floor, juce::PathStrokeType (1.0f));

        // frequency lines across the floor + labels at the front edge
        g.setFont (theme::mono (10.0f));
        for (double decade = 10.0; decade <= 20000.0; decade *= 10.0)
        {
            for (int k = 1; k < 10; ++k)
            {
                const double f = decade * k;
                if (f < gm.fLow || f > gm.fHigh) continue;
                const bool major = (k == 1 || k == 2 || k == 5);
                g.setColour (major ? theme::borderHi : theme::border.withAlpha (0.6f));
                g.drawLine (gm.xFor (f, 0.0f), gm.baseFor (0.0f), gm.xFor (f, 1.0f), gm.baseFor (1.0f), 1.0f);
                if (major)
                {
                    g.setColour (theme::textMuted);
                    g.drawText (theme::formatHz (f, 0), juce::Rectangle<float> (gm.xFor (f, 1.0f) - 24.0f, gm.baseFor (1.0f) + 4.0f, 48.0f, 14.0f),
                                juce::Justification::centred);
                }
            }
        }

        // dB axis on the left of the front slice (vertical wall)
        for (double db = gm.dbLow; db <= gm.dbHigh + 0.01; db += 10.0)
        {
            const float y = gm.yFor (db, 1.0f);
            const float x = gm.xFor (gm.fLow, 1.0f);
            g.setColour (theme::border.withAlpha (0.6f));
            g.drawLine (x - 4.0f, y, x, y, 1.0f);
            g.setColour (theme::textMuted);
            g.drawText (juce::String ((int) std::round (db)), juce::Rectangle<float> (x - 44.0f, y - 7.0f, 36.0f, 14.0f),
                        juce::Justification::centredRight);
        }

        // back wall edges
        g.setColour (theme::border);
        g.drawLine (gm.xFor (gm.fLow, 0.0f), gm.baseFor (0.0f), gm.xFor (gm.fLow, 0.0f), gm.baseFor (0.0f) - gm.curveHeight, 1.0f);
        g.drawLine (gm.xFor (gm.fLow, 1.0f), gm.baseFor (1.0f), gm.xFor (gm.fLow, 1.0f), gm.baseFor (1.0f) - gm.curveHeight, 1.0f);
        g.drawLine (gm.xFor (gm.fLow, 0.0f), gm.baseFor (0.0f), gm.xFor (gm.fLow, 1.0f), gm.baseFor (1.0f), 1.0f);
    }

    // ---- slices, back (t = 0) to front (latest) --------------------------
    const juce::Colour backColour = colour.brighter (0.2f);
    const juce::Colour frontColour = theme::roleSub.darker (0.15f);
    const int labelEvery = juce::jmax (1, n / 4);

    for (int k = 0; k < n; ++k)
    {
        const float u = n > 1 ? (float) k / (float) (n - 1) : 0.0f;
        const auto& slice = data.levelDb[(size_t) k];
        if (slice.size() != data.freqs.size()) continue;

        juce::Path p;
        bool started = false;
        for (size_t i = 0; i < data.freqs.size(); ++i)
        {
            const double f = data.freqs[i];
            if (f < gm.fLow || f > gm.fHigh) continue;
            const double v = std::isfinite (slice[i]) ? slice[i] : gm.dbLow;
            const float x = gm.xFor (f, u), y = gm.yFor (v, u);
            if (! started) { p.startNewSubPath (x, y); started = true; }
            else           p.lineTo (x, y);
        }
        if (! started) continue;

        auto line = p;
        p.lineTo (gm.xFor (gm.fHigh, u), gm.baseFor (u));
        p.lineTo (gm.xFor (gm.fLow, u), gm.baseFor (u));
        p.closeSubPath();

        const juce::Colour c = backColour.interpolatedWith (frontColour, u);
        g.setColour (theme::bg1.interpolatedWith (c, 0.10f + 0.08f * (1.0f - u)));
        g.fillPath (p);
        g.setColour (c.withAlpha (juce::jmap (u, 0.95f, 0.55f)));
        g.strokePath (line, juce::PathStrokeType (u < 0.02f ? 1.8f : 1.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // time labels down the right-hand edge
        if (k % labelEvery == 0 || k == n - 1)
        {
            const double t = k < (int) data.timesMs.size() ? data.timesMs[(size_t) k] : 0.0;
            g.setColour (theme::textMuted);
            g.setFont (theme::mono (10.0f));
            g.drawText (juce::String (juce::roundToInt (t)) + " ms",
                        juce::Rectangle<float> (gm.xFor (gm.fHigh, u) + 6.0f, gm.baseFor (u) - 8.0f, 50.0f, 14.0f),
                        juce::Justification::centredLeft);
        }
    }

    // ---- hover readout on the front slice --------------------------------
    if (hover.x >= 0.0f)
    {
        const float xFront0 = gm.xFor (gm.fLow, 1.0f), xFront1 = gm.xFor (gm.fHigh, 1.0f);
        if (hover.x >= xFront0 && hover.x <= xFront1)
        {
            const double t = (hover.x - xFront0) / (xFront1 - xFront0);
            const double f = gm.fLow * std::pow (gm.fHigh / gm.fLow, t);
            g.setColour (theme::accent.withAlpha (0.5f));
            g.drawLine (gm.xFor (f, 0.0f), gm.baseFor (0.0f), gm.xFor (f, 1.0f), gm.baseFor (1.0f), 1.0f);

            // find decay at this frequency: time until level drops 20 dB below first slice
            size_t fi = 0;
            for (size_t i = 0; i < data.freqs.size(); ++i)
                if (std::abs (std::log (data.freqs[i] / f)) < std::abs (std::log (data.freqs[fi] / f))) fi = i;
            juce::String decay = "-";
            const double start = data.levelDb[0].size() > fi ? data.levelDb[0][fi] : 0.0;
            for (size_t k = 1; k < data.levelDb.size(); ++k)
                if (data.levelDb[k].size() > fi && data.levelDb[k][fi] <= start - 20.0)
                { decay = juce::String (juce::roundToInt (data.timesMs[k])) + " ms to -20 dB"; break; }

            const auto text = theme::formatHz (f) + "   " + decay;
            g.setFont (theme::mono (11.0f));
            const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text) + 16.0f;
            auto box = juce::Rectangle<float> (juce::jlimit (gm.frame.getX(), gm.frame.getRight() - w, hover.x + 12.0f), gm.frame.getY() + 2.0f, w, 20.0f);
            g.setColour (theme::bg3.withAlpha (0.95f));
            g.fillRoundedRectangle (box, 5.0f);
            g.setColour (theme::borderHi);
            g.drawRoundedRectangle (box.reduced (0.5f), 5.0f, 1.0f);
            g.setColour (theme::text);
            g.drawText (text, box, juce::Justification::centred);
        }
    }
}

void WaterfallView::mouseMove (const juce::MouseEvent& e)
{
    hover = e.position;
    repaint();
}

void WaterfallView::mouseExit (const juce::MouseEvent&)
{
    hover = { -1.0f, -1.0f };
    repaint();
}
} // namespace fa::ui
