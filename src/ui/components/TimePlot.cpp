#include "Plots.h"

/*  TimePlot: impulse / step / ETC view.

    Linear mode normalises every trace to its own peak (so overlaid drivers compare by shape)
    and renders a min/max envelope per pixel column when zoomed out, a polyline when zoomed in.
    dB mode draws the peak envelope per pixel column in dB below the trace peak (0..-60 dB),
    which is the classic ETC look for reading reflections.                                     */

namespace fa::ui
{
namespace
{
    inline float textWidth (const juce::Font& f, const juce::String& s)
    {
        return juce::GlyphArrangement::getStringWidth (f, s);
    }

    double niceTimeStep (double rangeMs, int divisions)
    {
        static const double steps[] = { 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1, 2, 5, 10, 20, 50, 100, 200, 500, 1000 };
        const double raw = rangeMs / (double) std::max (1, divisions);
        for (double s : steps)
            if (s >= raw) return s;
        return 1000.0;
    }

    juce::String msLabel (double v, double step)
    {
        if (step >= 1.0) return juce::String (juce::roundToInt (v));
        if (step >= 0.1) return juce::String (v, 1);
        return juce::String (v, 2);
    }
}

struct TimePlot::Impl
{
    std::vector<Trace> traces;
    double t0 = -1.0, t1 = 20.0;       // reset range
    double v0 = -1.0, v1 = 20.0;       // view range
    bool dbMode = false;
    double dbFloor = -60.0;
    std::vector<std::pair<double, juce::Colour>> markers;

    juce::Rectangle<float> plotArea;
    juce::Rectangle<int> lastBounds;
    float marginL = 40.0f, marginR = 14.0f, marginT = 12.0f, marginB = 24.0f;

    struct Cache
    {
        juce::Path line;     // outline (polyline or envelope band outline)
        juce::Path fill;     // filled envelope band / area to baseline
        float peak = 1.0f;
        bool dirty = true;
    };
    std::vector<Cache> caches;

    bool hovering = false;
    juce::Point<float> mouse;
    juce::Time lastDragDown;
    double dragV0 = 0, dragV1 = 0;

    // ---- coordinates --------------------------------------------------------------------
    float xForMs (double ms) const
    {
        return plotArea.getX() + (float) ((ms - v0) / (v1 - v0)) * plotArea.getWidth();
    }
    double msForX (float x) const
    {
        return v0 + (double) ((x - plotArea.getX()) / std::max (1.0f, plotArea.getWidth())) * (v1 - v0);
    }
    float yForValue (double v) const      // linear: -1..1 ; dB: dbFloor..0
    {
        if (dbMode)
            return plotArea.getY() + (float) ((0.0 - v) / (0.0 - dbFloor)) * (plotArea.getHeight() - 6.0f) + 6.0f;
        return plotArea.getCentreY() - (float) v * plotArea.getHeight() * 0.46f;
    }

    void invalidate()
    {
        for (auto& c : caches)
            c.dirty = true;
    }

    void layout (juce::Rectangle<int> bounds)
    {
        if (bounds == lastBounds)
            return;
        lastBounds = bounds;
        auto b = bounds.toFloat();
        plotArea = { b.getX() + marginL, b.getY() + marginT,
                     std::max (10.0f, b.getWidth() - marginL - marginR),
                     std::max (10.0f, b.getHeight() - marginT - marginB) };
        invalidate();
    }

    void syncCaches()
    {
        if (caches.size() != traces.size())
        {
            caches.assign (traces.size(), {});
            for (auto& c : caches)
                c.dirty = true;
        }
    }

    void rebuild (size_t idx)
    {
        auto& t = traces[idx];
        auto& c = caches[idx];
        c.line.clear(); c.fill.clear();
        c.dirty = false;
        if (t.samples.empty() || t.sampleRate <= 0.0)
            return;

        float peak = 0.0f;
        for (float s : t.samples)
            peak = std::max (peak, std::abs (s));
        c.peak = peak > 1.0e-9f ? peak : 1.0f;
        const float inv = 1.0f / c.peak;

        const double msPerSample = 1000.0 / t.sampleRate;
        const int n = (int) t.samples.size();
        const float w = plotArea.getWidth();
        const double samplesPerPx = (v1 - v0) / msPerSample / std::max (1.0f, w);

        auto sampleIndexForMs = [&] (double ms) { return (ms - t.startMs) / msPerSample; };
        const float yBase = dbMode ? plotArea.getBottom() : yForValue (0.0);

        if (! dbMode && samplesPerPx < 1.5)
        {
            // zoomed in: real polyline through the samples
            int i0 = juce::jlimit (0, n - 1, (int) std::floor (sampleIndexForMs (v0)) - 1);
            int i1 = juce::jlimit (0, n - 1, (int) std::ceil  (sampleIndexForMs (v1)) + 1);
            bool pen = false;
            for (int i = i0; i <= i1; ++i)
            {
                const float x = xForMs (t.startMs + i * msPerSample);
                const float y = yForValue (t.samples[(size_t) i] * inv);
                if (! pen) { c.line.startNewSubPath (x, y); c.fill.startNewSubPath (x, yBase); pen = true; }
                else         c.line.lineTo (x, y);
                c.fill.lineTo (x, y);
            }
            if (pen)
            {
                c.fill.lineTo (c.line.getCurrentPosition().x, yBase);
                c.fill.closeSubPath();
            }
            return;
        }

        // one column per pixel: min/max (linear) or peak dB (dB mode)
        const int cols = (int) std::ceil (w);
        std::vector<float> mins ((size_t) cols, 0.0f), maxs ((size_t) cols, 0.0f);
        std::vector<char> has ((size_t) cols, 0);
        for (int px = 0; px < cols; ++px)
        {
            const double msA = msForX (plotArea.getX() + (float) px);
            const double msB = msForX (plotArea.getX() + (float) px + 1.0f);
            int a = (int) std::floor (sampleIndexForMs (msA));
            int b = (int) std::ceil  (sampleIndexForMs (msB));
            a = std::max (a, 0); b = std::min (b, n - 1);
            if (a > b) continue;
            float lo = 1.0e9f, hi = -1.0e9f;
            for (int i = a; i <= b; ++i)
            {
                const float s = t.samples[(size_t) i] * inv;
                lo = std::min (lo, s); hi = std::max (hi, s);
            }
            mins[(size_t) px] = lo; maxs[(size_t) px] = hi; has[(size_t) px] = 1;
        }

        if (dbMode)
        {
            bool pen = false;
            float lastX = 0.0f;
            for (int px = 0; px < cols; ++px)
            {
                if (! has[(size_t) px]) continue;
                const float env = std::max (std::abs (mins[(size_t) px]), std::abs (maxs[(size_t) px]));
                const double db = std::max (dbFloor - 3.0, 20.0 * std::log10 (std::max (1.0e-9, (double) env)));
                const float x = plotArea.getX() + (float) px + 0.5f;
                const float y = yForValue (db);
                if (! pen) { c.line.startNewSubPath (x, y); c.fill.startNewSubPath (x, yBase); pen = true; }
                else         c.line.lineTo (x, y);
                c.fill.lineTo (x, y);
                lastX = x;
            }
            if (pen)
            {
                c.fill.lineTo (lastX, yBase);
                c.fill.closeSubPath();
            }
            return;
        }

        // linear envelope band: forward along max, back along min
        bool pen = false;
        int firstPx = -1, lastPx = -1;
        for (int px = 0; px < cols; ++px)
        {
            if (! has[(size_t) px]) continue;
            const float x = plotArea.getX() + (float) px + 0.5f;
            const float y = yForValue (maxs[(size_t) px]);
            if (! pen) { c.fill.startNewSubPath (x, y); firstPx = px; pen = true; }
            else         c.fill.lineTo (x, y);
            lastPx = px;
        }
        for (int px = lastPx; px >= firstPx && px >= 0; --px)
        {
            if (! has[(size_t) px]) continue;
            c.fill.lineTo (plotArea.getX() + (float) px + 0.5f, yForValue (mins[(size_t) px]));
        }
        if (pen)
            c.fill.closeSubPath();

        // thin centre line through the mean of min/max keeps the trace readable at low zoom
        pen = false;
        for (int px = 0; px < cols; ++px)
        {
            if (! has[(size_t) px]) continue;
            const float x = plotArea.getX() + (float) px + 0.5f;
            const float y = yForValue (0.5f * (mins[(size_t) px] + maxs[(size_t) px]));
            if (! pen) { c.line.startNewSubPath (x, y); pen = true; }
            else         c.line.lineTo (x, y);
        }
    }

    // ---- value readout ----------------------------------------------------------------------
    bool valueAt (const Trace& t, const Cache& c, double ms, double& out) const
    {
        if (t.samples.empty()) return false;
        const double idx = (ms - t.startMs) * t.sampleRate / 1000.0;
        if (idx < 0.0 || idx >= (double) t.samples.size() - 1) return false;
        if (dbMode)
        {
            // peak within +-0.5 px worth of samples
            const double halfSpan = 0.5 * (v1 - v0) / std::max (1.0f, plotArea.getWidth()) * t.sampleRate / 1000.0;
            const int a = std::max (0, (int) std::floor (idx - halfSpan));
            const int b = std::min ((int) t.samples.size() - 1, (int) std::ceil (idx + halfSpan));
            float env = 0.0f;
            for (int i = a; i <= b; ++i) env = std::max (env, std::abs (t.samples[(size_t) i]));
            out = 20.0 * std::log10 (std::max (1.0e-9, (double) (env / c.peak)));
            return true;
        }
        const int i = (int) idx;
        const double u = idx - i;
        out = (t.samples[(size_t) i] * (1.0 - u) + t.samples[(size_t) i + 1] * u) / c.peak;
        return true;
    }

    // ---- zoom / pan -----------------------------------------------------------------------
    void zoom (double factor, float aboutX)
    {
        const double m = msForX (aboutX);
        double a = m + (v0 - m) * factor, b = m + (v1 - m) * factor;
        const double span = juce::jlimit (0.05, t1 - t0, b - a);
        const double mid = 0.5 * (a + b);
        a = mid - span * 0.5; b = mid + span * 0.5;
        if (a < t0) { b += t0 - a; a = t0; }
        if (b > t1) { a -= b - t1; b = t1; }
        v0 = a; v1 = b;
        invalidate();
    }

    void pan (float dx)
    {
        const double d = -(double) dx / std::max (1.0f, plotArea.getWidth()) * (dragV1 - dragV0);
        double a = dragV0 + d, b = dragV1 + d;
        if (a < t0) { b += t0 - a; a = t0; }
        if (b > t1) { a -= b - t1; b = t1; }
        v0 = a; v1 = b;
        invalidate();
    }

    // ---- painting -------------------------------------------------------------------------
    void paintGrid (juce::Graphics& g, bool lines, bool labels)
    {
        const auto font = theme::mono (10.0f);
        g.setFont (font);

        // time
        const int divs = juce::jlimit (3, 14, (int) (plotArea.getWidth() / 70.0f));
        const double step = niceTimeStep (v1 - v0, divs);
        for (double ms = std::ceil (v0 / step - 1.0e-6) * step; ms <= v1 + step * 1.0e-6; ms += step)
        {
            const float x = std::round (xForMs (ms)) + 0.5f;
            const bool zero = std::abs (ms) < step * 1.0e-6;
            if (lines)
            {
                g.setColour (zero ? theme::textMuted.withAlpha (0.8f) : theme::border.withAlpha (0.6f));
                g.fillRect (x - 0.5f, plotArea.getY(), 1.0f, plotArea.getHeight());
            }
            if (labels)
            {
                const bool last = ms + step > v1 + step * 1.0e-6;
                g.setColour (zero ? theme::textDim : theme::textMuted);
                g.drawText (msLabel (ms, step) + (last ? " ms" : ""),
                            juce::Rectangle<float> (x - 30.0f, plotArea.getBottom() + 4.0f, 60.0f, 14.0f),
                            juce::Justification::centred, false);
            }
        }
        if (! labels)
        {
            // level lines only
            if (dbMode)
            {
                const double dstep = plotArea.getHeight() > 220.0f ? 6.0 : 12.0;
                for (double db = 0.0; db >= dbFloor - 1.0e-6; db -= dstep)
                {
                    const float y = std::round (yForValue (db)) + 0.5f;
                    g.setColour (db == 0.0 ? theme::borderHi : theme::border.withAlpha (0.6f));
                    g.fillRect (plotArea.getX(), y - 0.5f, plotArea.getWidth(), 1.0f);
                }
            }
            else
            {
                for (double v = -1.0; v <= 1.0001; v += 0.5)
                {
                    const float y = std::round (yForValue (v)) + 0.5f;
                    const bool zero = std::abs (v) < 1.0e-6;
                    g.setColour (zero ? theme::textMuted.withAlpha (0.8f) : theme::border.withAlpha (0.6f));
                    g.fillRect (plotArea.getX(), y - 0.5f, plotArea.getWidth(), 1.0f);
                }
            }
            return;
        }
        // level
        g.setFont (font);
        if (dbMode)
        {
            const double dstep = plotArea.getHeight() > 220.0f ? 6.0 : 12.0;
            for (double db = 0.0; db >= dbFloor - 1.0e-6; db -= dstep)
            {
                const float y = std::round (yForValue (db)) + 0.5f;
                g.setColour (theme::textMuted);
                g.drawText (juce::String ((int) db), juce::Rectangle<float> (0.0f, y - 7.0f, marginL - 7.0f, 14.0f),
                            juce::Justification::centredRight, false);
            }
        }
        else
        {
            for (double v = -1.0; v <= 1.0001; v += 0.5)
            {
                const float y = std::round (yForValue (v)) + 0.5f;
                const bool zero = std::abs (v) < 1.0e-6;
                g.setColour (zero ? theme::textDim : theme::textMuted);
                g.drawText (juce::String (v, 1), juce::Rectangle<float> (0.0f, y - 7.0f, marginL - 7.0f, 14.0f),
                            juce::Justification::centredRight, false);
            }
        }
        g.setColour (theme::textMuted);
        g.setFont (theme::font (9.5f));
        g.drawText (dbMode ? "dB" : "norm", juce::Rectangle<float> (0.0f, plotArea.getY() - 9.0f, marginL - 7.0f, 12.0f),
                    juce::Justification::centredRight, false);
    }

    void paintTraces (juce::Graphics& g)
    {
        syncCaches();
        const float bottom = plotArea.getBottom();
        for (size_t i = 0; i < traces.size(); ++i)
        {
            auto& t = traces[i];
            auto& c = caches[i];
            if (c.dirty) rebuild (i);

            const double msPerSample = 1000.0 / std::max (1.0, t.sampleRate);
            const bool polyline = ! dbMode && (v1 - v0) / msPerSample / std::max (1.0f, plotArea.getWidth()) < 1.5;

            if (dbMode)
            {
                g.setGradientFill (juce::ColourGradient (t.colour.withAlpha (0.30f), 0.0f, plotArea.getY(),
                                                         t.colour.withAlpha (0.02f), 0.0f, bottom, false));
                g.fillPath (c.fill);
                g.setColour (t.colour.withAlpha (0.14f));
                g.strokePath (c.line, juce::PathStrokeType (4.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.setColour (t.colour);
                g.strokePath (c.line, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            else if (polyline)
            {
                g.setColour (t.colour.withAlpha (0.10f));
                g.fillPath (c.fill);
                g.setColour (t.colour.withAlpha (0.14f));
                g.strokePath (c.line, juce::PathStrokeType (4.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.setColour (t.colour);
                g.strokePath (c.line, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            else
            {
                g.setColour (t.colour.withAlpha (0.85f));
                g.fillPath (c.fill);
                g.setColour (t.colour.withAlpha (0.14f));
                g.strokePath (c.fill, juce::PathStrokeType (3.0f));
                g.setColour (t.colour.brighter (0.3f));
                g.strokePath (c.line, juce::PathStrokeType (1.0f));
            }
        }
    }

    void paintMarkers (juce::Graphics& g)
    {
        const auto font = theme::mono (10.0f);
        std::vector<juce::Rectangle<float>> placed;
        for (auto& [ms, col] : markers)
        {
            if (ms < v0 || ms > v1) continue;
            const float x = std::round (xForMs (ms)) + 0.5f;
            juce::Path p;
            p.startNewSubPath (x, plotArea.getY());
            p.lineTo (x, plotArea.getBottom());
            g.setColour (col.withAlpha (0.85f));
            theme::drawDashedPath (g, p, 1.0f, 4.0f, 3.0f);

            const auto text = juce::String (ms, 2);
            const float w = textWidth (font, text) + 10.0f;
            float tx = x + 4.0f;
            if (tx + w > plotArea.getRight()) tx = x - 4.0f - w;
            juce::Rectangle<float> tag (tx, plotArea.getY() + 4.0f, w, 15.0f);
            for (bool moved = true; moved;)
            {
                moved = false;
                for (auto& o : placed)
                    if (o.intersects (tag.expanded (2.0f))) { tag.setY (o.getBottom() + 3.0f); moved = true; }
            }
            placed.push_back (tag);
            g.setColour (col.withAlpha (0.18f));
            g.fillRoundedRectangle (tag, 4.0f);
            g.setColour (col);
            g.setFont (font);
            g.drawText (text, tag, juce::Justification::centred, false);
        }
    }

    void paintCrosshair (juce::Graphics& g)
    {
        if (! hovering || ! plotArea.contains (mouse))
            return;
        const double ms = msForX (mouse.x);
        g.setColour (theme::text.withAlpha (0.22f));
        g.fillRect (std::round (mouse.x), plotArea.getY(), 1.0f, plotArea.getHeight());

        struct Row { juce::Colour col; juce::String label, value; float y; bool has; };
        std::vector<Row> rows;
        for (size_t i = 0; i < traces.size() && i < caches.size(); ++i)
        {
            double v = 0.0;
            const bool has = valueAt (traces[i], caches[i], ms, v);
            rows.push_back ({ traces[i].colour, traces[i].label,
                              has ? (dbMode ? juce::String (v, 1) + " dB" : juce::String (v, 3)) : juce::String ("--"),
                              has ? yForValue (v) : 0.0f, has });
        }
        for (auto& r : rows)
        {
            if (! r.has) continue;
            g.setColour (theme::bg0);
            g.fillEllipse (mouse.x - 3.5f, r.y - 3.5f, 7.0f, 7.0f);
            g.setColour (r.col);
            g.fillEllipse (mouse.x - 2.5f, r.y - 2.5f, 5.0f, 5.0f);
        }

        const auto headFont = theme::fontMedium (12.0f), rowFont = theme::font (11.0f), valFont = theme::mono (11.0f);
        const auto head = theme::formatMs (ms, 2) + "   " + juce::String (msToMetres (ms) * 100.0, 1) + " cm";
        float w = textWidth (headFont, head) + 24.0f;
        for (auto& r : rows)
            w = std::max (w, 24.0f + 14.0f + textWidth (rowFont, r.label) + 18.0f + textWidth (valFont, r.value));
        const float rowH = 17.0f;
        const float h = 26.0f + (rows.empty() ? 0.0f : 6.0f + rowH * (float) rows.size()) + 4.0f;
        juce::Rectangle<float> card (mouse.x + 16.0f, mouse.y + 14.0f, w, h);
        if (card.getRight() > plotArea.getRight() - 4.0f) card.setX (mouse.x - 16.0f - w);
        if (card.getBottom() > plotArea.getBottom() - 4.0f) card.setY (mouse.y - 14.0f - h);
        card.setX (std::max (card.getX(), plotArea.getX() + 4.0f));
        card.setY (std::max (card.getY(), plotArea.getY() + 4.0f));

        theme::drawGlow (g, card, juce::Colours::black, 7.0f, 6.0f);
        theme::fillCard (g, card, theme::bg3.withAlpha (0.97f), theme::borderHi, 7.0f);
        auto inner = card.reduced (12.0f, 7.0f);
        g.setColour (theme::text);
        g.setFont (headFont);
        g.drawText (head, inner.removeFromTop (18.0f), juce::Justification::centredLeft, false);
        if (! rows.empty())
        {
            g.setColour (theme::border);
            g.fillRect (inner.removeFromTop (6.0f).withHeight (1.0f).translated (0.0f, 2.0f));
            for (auto& r : rows)
            {
                auto line = inner.removeFromTop (rowH);
                g.setColour (r.col);
                g.fillEllipse (line.getX(), line.getCentreY() - 3.5f, 7.0f, 7.0f);
                g.setColour (theme::textDim);
                g.setFont (rowFont);
                g.drawText (r.label, line.withTrimmedLeft (14.0f), juce::Justification::centredLeft, true);
                g.setColour (r.has ? theme::text : theme::textMuted);
                g.setFont (valFont);
                g.drawText (r.value, line, juce::Justification::centredRight, false);
            }
        }
    }
};

TimePlot::TimePlot() : impl (std::make_unique<Impl>())
{
    setOpaque (false);
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
}

TimePlot::~TimePlot() = default;

void TimePlot::setTraces (std::vector<Trace> traces)
{
    impl->traces = std::move (traces);
    impl->caches.clear();
    repaint();
}

void TimePlot::setRangeMs (double t0, double t1)
{
    if (t1 <= t0) std::swap (t0, t1);
    impl->t0 = t0; impl->t1 = std::max (t0 + 0.05, t1);
    impl->v0 = impl->t0; impl->v1 = impl->t1;
    impl->invalidate();
    repaint();
}

void TimePlot::setDbMode (bool envelopeDb)
{
    impl->dbMode = envelopeDb;
    impl->invalidate();
    repaint();
}

void TimePlot::setMarkers (std::vector<std::pair<double, juce::Colour>> msMarkers)
{
    impl->markers = std::move (msMarkers);
    repaint();
}

void TimePlot::paint (juce::Graphics& g)
{
    auto& im = *impl;
    im.layout (getLocalBounds());
    const auto area = im.plotArea;
    if (area.getWidth() < 10.0f || area.getHeight() < 10.0f)
        return;

    g.setColour (theme::bg1.darker (0.25f));
    g.fillRoundedRectangle (area, theme::radiusSm);
    {
        juce::Graphics::ScopedSaveState ss (g);
        juce::Path clip;
        clip.addRoundedRectangle (area, theme::radiusSm);
        g.reduceClipRegion (clip);
        im.paintGrid (g, true, false);
        im.paintTraces (g);
        im.paintMarkers (g);
        im.paintCrosshair (g);
    }
    g.setColour (theme::border);
    g.drawRoundedRectangle (area.reduced (0.5f), theme::radiusSm, 1.0f);
    im.paintGrid (g, false, true);
}

void TimePlot::mouseMove (const juce::MouseEvent& e)
{
    impl->hovering = true;
    impl->mouse = e.position;
    repaint();
}

void TimePlot::mouseExit (const juce::MouseEvent&)
{
    impl->hovering = false;
    repaint();
}

void TimePlot::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const float delta = w.deltaY != 0.0f ? w.deltaY : -w.deltaX;
    if (delta == 0.0f) return;
    impl->zoom (std::exp (-(double) delta * 1.1), juce::jlimit (impl->plotArea.getX(), impl->plotArea.getRight(), e.position.x));
    impl->mouse = e.position;
    repaint();
}

void TimePlot::mouseDrag (const juce::MouseEvent& e)
{
    auto& im = *impl;
    if (e.mouseDownTime != im.lastDragDown)      // first drag event of this gesture: remember the view
    {
        im.lastDragDown = e.mouseDownTime;
        im.dragV0 = im.v0; im.dragV1 = im.v1;
    }
    im.pan ((float) e.getDistanceFromDragStartX());
    im.mouse = e.position;
    repaint();
}

void TimePlot::mouseDoubleClick (const juce::MouseEvent&)
{
    impl->v0 = impl->t0; impl->v1 = impl->t1;
    impl->invalidate();
    repaint();
}
} // namespace fa::ui
