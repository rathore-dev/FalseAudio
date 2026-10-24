#include "Plots.h"
#include "core/dsp/Spectrum.h"

/*  FrequencyPlot + PlotTrace helpers.

    Rendering strategy: everything that is expensive (grid geometry, axis labels, trace paths,
    dashed strokes, area fills) is cached and only rebuilt when the view, the size or that
    particular trace changes. paint() then only fills/strokes cached paths, so a 30 fps live
    RTA that replaces one trace per frame rebuilds exactly one path per frame.             */

namespace fa::ui
{
namespace
{
    constexpr float kNodeRadius   = 6.0f;
    constexpr float kNodeHitRadius = 11.0f;
    constexpr int   kAlphaBuckets = 6;

    inline float textWidth (const juce::Font& f, const juce::String& s)
    {
        return juce::GlyphArrangement::getStringWidth (f, s);
    }

    /** "20", "1k", "2.5k" - compact frequency tick labels. */
    juce::String tickLabelHz (double hz)
    {
        if (hz >= 1000.0)
        {
            const double k = hz / 1000.0;
            juce::String s = juce::String (k, k < 10.0 ? 2 : 1);
            if (s.contains ("."))
                s = s.trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
            return s + "k";
        }
        return juce::String (juce::roundToInt (hz));
    }

    juce::String tickLabelValue (double v, double step)
    {
        if (step >= 1.0)
            return juce::String (juce::roundToInt (v));
        if (step >= 0.1)
            return juce::String (v, 1);
        return juce::String (v, 2);
    }

    /** Picks a "nice" step so that the range is divided into roughly `targetDivisions`. */
    double niceStep (double range, int targetDivisions, bool degrees)
    {
        static const double generic[] = { 0.01, 0.02, 0.05, 0.1, 0.2, 0.25, 0.5, 1, 2, 2.5, 5, 10, 20, 25, 50, 100, 200, 500, 1000 };
        static const double angles[]  = { 1, 2, 5, 10, 15, 30, 45, 90, 180, 360, 720 };
        const double raw = range / (double) std::max (1, targetDivisions);
        if (degrees)
        {
            for (double a : angles)
                if (a >= raw) return a;
            return 720.0;
        }
        for (double s : generic)
            if (s >= raw) return s;
        return 1000.0;
    }

    /** Linear interpolation in log-f of a trace value; returns false when outside the trace. */
    bool traceValueAt (const PlotTrace& t, double f, double& out, double& alphaOut, bool nearestOnly)
    {
        const auto& fr = t.freqs;
        if (fr.size() < 2 || f < fr.front() || f > fr.back())
            return false;
        auto it = std::lower_bound (fr.begin(), fr.end(), f);
        size_t i1 = (size_t) std::distance (fr.begin(), it);
        if (i1 == 0) i1 = 1;
        const size_t i0 = i1 - 1;
        const double l0 = std::log (fr[i0]), l1 = std::log (fr[i1]);
        const double u = l1 > l0 ? juce::jlimit (0.0, 1.0, (std::log (f) - l0) / (l1 - l0)) : 0.0;
        const double v0 = t.values[i0], v1 = t.values[i1];
        if (! std::isfinite (v0) || ! std::isfinite (v1))
            return false;
        if (nearestOnly || std::abs (v1 - v0) > 180.0)
            out = u < 0.5 ? v0 : v1;
        else
            out = v0 + (v1 - v0) * u;
        alphaOut = 1.0;
        if (t.alpha.size() == t.values.size())
            alphaOut = t.alpha[i0] + (t.alpha[i1] - t.alpha[i0]) * u;
        return true;
    }
}

// =====================================================================================
//  PlotTrace
// =====================================================================================
PlotTrace PlotTrace::fromResponse (const juce::String& id, const juce::String& label, juce::Colour c,
                                   const FrequencyResponse& r, double smoothingFraction)
{
    PlotTrace t;
    t.id = id; t.label = label; t.colour = c;
    if (r.isEmpty())
        return t;

    FrequencyResponse smoothed;
    const FrequencyResponse* src = &r;
    if (smoothingFraction > 0.0)
    {
        smoothed = smoothFractionalOctave (r, smoothingFraction);
        src = &smoothed;
    }
    else if (smoothingFraction < 0.0)      // negative = psychoacoustic
    {
        smoothed = smoothPsychoacoustic (r);
        src = &smoothed;
    }
    t.freqs = src->freqs;
    t.values = src->magnitudeDb();
    return t;
}

PlotTrace PlotTrace::fromPhase (const juce::String& id, const juce::String& label, juce::Colour c,
                                const FrequencyResponse& r)
{
    PlotTrace t;
    t.id = id; t.label = label; t.colour = c;
    t.freqs = r.freqs;
    t.values.resize (r.size());
    for (size_t i = 0; i < r.size(); ++i)
        t.values[i] = r.phaseDeg (i);     // wrapped -180..180; the plot breaks the path at wraps
    return t;
}

PlotTrace PlotTrace::fromDb (const juce::String& id, const juce::String& label, juce::Colour c,
                             const std::vector<double>& freqs, const std::vector<double>& db)
{
    PlotTrace t;
    t.id = id; t.label = label; t.colour = c;
    const size_t n = std::min (freqs.size(), db.size());
    t.freqs.assign (freqs.begin(), freqs.begin() + (long) n);
    t.values.assign (db.begin(), db.begin() + (long) n);
    return t;
}

// =====================================================================================
//  FrequencyPlot::Impl
// =====================================================================================
struct FrequencyPlot::Impl
{
    explicit Impl (FrequencyPlot& o) : owner (o) {}

    FrequencyPlot& owner;
    Units units = Units::dB;

    // reset view and current view
    double fMin0 = 10.0, fMax0 = 24000.0, yMin0 = -30.0, yMax0 = 20.0;
    double fMin = 10.0,  fMax = 24000.0,  yMin = -30.0,  yMax = 20.0;

    std::vector<PlotTrace> traces;
    std::vector<std::tuple<double, double, juce::Colour>> bands;
    std::vector<Node> nodes;
    juce::String title;
    bool showLegend = true;

    // ---- layout -------------------------------------------------------------------
    juce::Rectangle<float> plotArea;
    float marginL = 46.0f, marginR = 14.0f, marginT = 10.0f, marginB = 24.0f;

    // ---- caches -------------------------------------------------------------------
    struct Label { juce::String text; float pos; bool major; };
    juce::Path gridMinor, gridMajor, gridZero;
    std::vector<Label> xLabels, yLabels;
    bool gridDirty = true;

    struct TraceCache
    {
        juce::Path line;                       // plain polyline (or bar rectangles)
        juce::Path stroke;                     // pre-stroked (dashed) outline when dashed
        juce::Path fill;                       // closed area to bottom
        juce::Path barCaps;                    // bright tops of RTA bars
        std::vector<juce::Path> alphaLines;    // per-alpha-bucket segments (coherence weighted)
        float topY = 0.0f;                     // highest point, for gradient fills
        bool hasAlpha = false;
        bool dirty = true;
    };
    std::vector<TraceCache> caches;

    // ---- interaction --------------------------------------------------------------
    bool hovering = false;
    juce::Point<float> mouse;
    struct Chip { juce::Rectangle<float> r; int trace; };
    std::vector<Chip> chips;
    int hoverChip = -1;
    int hoverNode = -1, dragNode = -1;
    bool draggingQ = false;
    Node dragStartNode;
    bool panning = false;
    juce::Point<float> dragStart;
    double panF0 = 0, panF1 = 0, panY0 = 0, panY1 = 0;

    // ---------------------------------------------------------------------------------
    void applyDefaultRanges()
    {
        switch (units)
        {
            case Units::dB:        fMin0 = 10.0; fMax0 = 24000.0; yMin0 = -30.0;  yMax0 = 20.0;  break;
            case Units::degrees:   fMin0 = 10.0; fMax0 = 24000.0; yMin0 = -180.0; yMax0 = 180.0; break;
            case Units::ms:        fMin0 = 10.0; fMax0 = 24000.0; yMin0 = -2.0;   yMax0 = 20.0;  break;
            case Units::coherence: fMin0 = 10.0; fMax0 = 24000.0; yMin0 = 0.0;    yMax0 = 1.0;   break;
        }
        resetView();
    }

    void resetView()
    {
        fMin = fMin0; fMax = fMax0; yMin = yMin0; yMax = yMax0;
        invalidateAll();
    }

    void invalidateAll()
    {
        gridDirty = true;
        for (auto& c : caches)
            c.dirty = true;
    }

    juce::String unitSuffix() const
    {
        switch (units)
        {
            case Units::dB:        return "dB";
            case Units::degrees:   return juce::String (juce::CharPointer_UTF8 ("\xc2\xb0"));
            case Units::ms:        return "ms";
            case Units::coherence: return "";
        }
        return {};
    }

    juce::String formatValue (double v) const
    {
        switch (units)
        {
            case Units::dB:        return theme::formatDb (v, 1);
            case Units::degrees:   return juce::String (v, 0) + juce::String (juce::CharPointer_UTF8 ("\xc2\xb0"));
            case Units::ms:        return theme::formatMs (v, 2);
            case Units::coherence: return juce::String (v, 2);
        }
        return juce::String (v, 2);
    }

    double minYSpan() const
    {
        switch (units)
        {
            case Units::dB:        return 2.0;
            case Units::degrees:   return 10.0;
            case Units::ms:        return 0.2;
            case Units::coherence: return 0.1;
        }
        return 1.0;
    }

    // ---- coordinates ----------------------------------------------------------------
    float xForFreq (double f) const
    {
        const double lf0 = std::log (fMin), lf1 = std::log (fMax);
        const double u = (std::log (std::max (f, 1.0e-3)) - lf0) / (lf1 - lf0);
        return plotArea.getX() + (float) u * plotArea.getWidth();
    }
    float yForValue (double v) const
    {
        const double u = (v - yMin) / (yMax - yMin);
        return plotArea.getBottom() - (float) u * plotArea.getHeight();
    }
    double freqForX (float x) const
    {
        const double u = plotArea.getWidth() > 0 ? (x - plotArea.getX()) / plotArea.getWidth() : 0.0;
        return std::exp (std::log (fMin) + u * (std::log (fMax) - std::log (fMin)));
    }
    double valueForY (float y) const
    {
        const double u = plotArea.getHeight() > 0 ? (plotArea.getBottom() - y) / plotArea.getHeight() : 0.0;
        return yMin + u * (yMax - yMin);
    }

    void layout()
    {
        marginT = showLegend && ! traces.empty() ? 30.0f : 10.0f;
        auto b = owner.getLocalBounds().toFloat();
        plotArea = juce::Rectangle<float> (b.getX() + marginL, b.getY() + marginT,
                                           std::max (10.0f, b.getWidth() - marginL - marginR),
                                           std::max (10.0f, b.getHeight() - marginT - marginB));
        invalidateAll();
    }

    // ---- grid -----------------------------------------------------------------------
    void rebuildGrid()
    {
        gridMinor.clear(); gridMajor.clear(); gridZero.clear();
        xLabels.clear(); yLabels.clear();

        const float x0 = plotArea.getX(), x1 = plotArea.getRight();
        const float y0 = plotArea.getY(), y1 = plotArea.getBottom();

        // frequency lines: 1..9 x each decade
        const double pxPerDecade = plotArea.getWidth() / std::max (1.0e-6, std::log10 (fMax / fMin));
        const auto labelFont = theme::mono (10.0f);
        float lastLabelRight = -1.0e9f;

        for (double decade = 1.0; decade <= 100000.0; decade *= 10.0)
        {
            for (int m = 1; m <= 9; ++m)
            {
                const double f = decade * m;
                if (f < fMin * 0.999 || f > fMax * 1.001)
                    continue;
                const float x = std::round (xForFreq (f)) + 0.5f;
                auto& p = (m == 1) ? gridMajor : gridMinor;
                p.startNewSubPath (x, y0);
                p.lineTo (x, y1);

                // label priority: 1,2,5 always; 3,4,6,8 when wide; 7,9 when very wide
                const int prio = (m == 1 || m == 2 || m == 5) ? 0 : (m == 3 || m == 4 || m == 6 || m == 8) ? 1 : 2;
                const bool wantLabel = prio == 0 || (prio == 1 && pxPerDecade > 260.0) || (prio == 2 && pxPerDecade > 520.0);
                if (wantLabel)
                {
                    const auto text = tickLabelHz (f);
                    const float w = textWidth (labelFont, text);
                    if (x - w * 0.5f > lastLabelRight + 6.0f)
                    {
                        xLabels.push_back ({ text, x, m == 1 });
                        lastLabelRight = x + w * 0.5f;
                    }
                }
            }
        }

        // value lines
        const int divisions = juce::jlimit (3, 12, (int) (plotArea.getHeight() / 34.0f));
        const double step = niceStep (yMax - yMin, divisions, units == Units::degrees);
        const double first = std::ceil (yMin / step - 1.0e-6) * step;
        for (double v = first; v <= yMax + step * 1.0e-6; v += step)
        {
            const float y = std::round (yForValue (v)) + 0.5f;
            const bool zero = std::abs (v) < step * 1.0e-6;
            auto& p = zero ? gridZero : gridMinor;
            p.startNewSubPath (x0, y);
            p.lineTo (x1, y);
            yLabels.push_back ({ tickLabelValue (zero ? 0.0 : v, step), y, zero });
        }
        gridDirty = false;
    }

    // ---- traces ---------------------------------------------------------------------
    void syncCaches()
    {
        if (caches.size() != traces.size())
        {
            caches.resize (traces.size());
            for (auto& c : caches)
                c.dirty = true;
        }
    }

    void rebuildTrace (size_t idx)
    {
        auto& t = traces[idx];
        auto& c = caches[idx];
        c.line.clear(); c.stroke.clear(); c.fill.clear(); c.barCaps.clear();
        c.alphaLines.clear();
        c.hasAlpha = false;
        c.topY = plotArea.getBottom();
        c.dirty = false;

        const size_t n = std::min (t.freqs.size(), t.values.size());
        if (n == 0)
            return;

        // index window: one point beyond each edge so lines run to the border
        size_t i0 = 0, i1 = n - 1;
        while (i0 + 1 < n && t.freqs[i0 + 1] < fMin) ++i0;
        while (i1 > 0 && t.freqs[i1 - 1] > fMax) --i1;

        const float yLo = plotArea.getY() - 2000.0f, yHi = plotArea.getBottom() + 2000.0f;
        auto clampY = [&] (double v) { return juce::jlimit (yLo, yHi, yForValue (v)); };

        if (t.bars)
        {
            // each point is one band; bar edges at geometric midpoints to the neighbours
            const float bottom = plotArea.getBottom();
            for (size_t i = i0; i <= i1; ++i)
            {
                if (! std::isfinite (t.values[i]))
                    continue;
                const double fl = i > 0     ? std::sqrt (t.freqs[i - 1] * t.freqs[i]) : t.freqs[i] / std::sqrt (t.freqs[i + 1 < n ? i + 1 : i] / t.freqs[i]);
                const double fr = i + 1 < n ? std::sqrt (t.freqs[i] * t.freqs[i + 1]) : t.freqs[i] * std::sqrt (t.freqs[i] / t.freqs[i > 0 ? i - 1 : i]);
                float xa = xForFreq (fl), xb = xForFreq (fr);
                const float gap = juce::jlimit (0.5f, 2.0f, (xb - xa) * 0.12f);
                xa += gap * 0.5f; xb -= gap * 0.5f;
                if (xb - xa < 1.0f) { const float cx = (xa + xb) * 0.5f; xa = cx - 0.5f; xb = cx + 0.5f; }
                const float y = clampY (t.values[i]);
                if (y >= bottom)
                    continue;
                c.line.addRectangle (xa, y, xb - xa, bottom - y);
                c.barCaps.addRectangle (xa, y, xb - xa, 1.5f);
                c.topY = std::min (c.topY, y);
            }
            return;
        }

        const bool wrapAware = units == Units::degrees;
        const bool useAlpha = t.alpha.size() >= n;
        c.hasAlpha = useAlpha;
        if (useAlpha)
            c.alphaLines.resize (kAlphaBuckets);

        bool pen = false;
        int lastBucket = -1;
        float prevX = 0, prevY = 0;
        for (size_t i = i0; i <= i1; ++i)
        {
            const double v = t.values[i];
            if (! std::isfinite (v) || t.freqs[i] <= 0.0)
            {
                pen = false; lastBucket = -1;
                continue;
            }
            const float x = xForFreq (t.freqs[i]);
            const float y = clampY (v);
            c.topY = std::min (c.topY, y);

            bool brk = ! pen;
            if (pen && wrapAware && std::abs (v - t.values[i - 1]) > 180.0)
                brk = true;

            if (brk)
            {
                c.line.startNewSubPath (x, y);
                // the fill polygon ignores phase wraps: it just follows the curve
                if (! pen) c.fill.startNewSubPath (x, plotArea.getBottom());
                c.fill.lineTo (x, y);
                lastBucket = -1;
            }
            else
            {
                c.line.lineTo (x, y);
                c.fill.lineTo (x, y);
            }

            if (useAlpha && pen && ! brk)
            {
                const double a = 0.5 * (t.alpha[i - 1] + t.alpha[i]);
                const int bucket = juce::jlimit (0, kAlphaBuckets - 1, (int) std::floor (a * kAlphaBuckets));
                auto& bp = c.alphaLines[(size_t) bucket];
                if (bucket != lastBucket)
                    bp.startNewSubPath (prevX, prevY);
                bp.lineTo (x, y);
                lastBucket = bucket;
            }

            pen = true;
            prevX = x; prevY = y;
        }
        if (pen)
            c.fill.lineTo (prevX, plotArea.getBottom());
        c.fill.closeSubPath();

        if (t.dashed)
        {
            const float lengths[] = { 6.0f, 4.0f };
            juce::PathStrokeType (t.thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)
                .createDashedStroke (c.stroke, c.line, lengths, 2);
        }
    }

    // ---- hit testing ------------------------------------------------------------------
    int nodeAt (juce::Point<float> p) const
    {
        int best = -1;
        float bestD = kNodeHitRadius;
        for (int i = 0; i < (int) nodes.size(); ++i)
        {
            const auto& nd = nodes[(size_t) i];
            const float d = p.getDistanceFrom ({ xForFreq (nd.freq), yForValue (nd.value) });
            if (d < bestD) { bestD = d; best = i; }
        }
        return best;
    }

    int chipAt (juce::Point<float> p) const
    {
        for (int i = 0; i < (int) chips.size(); ++i)
            if (chips[(size_t) i].r.contains (p))
                return i;
        return -1;
    }

    // ---- zoom / pan -------------------------------------------------------------------
    void zoomX (double factor, float aboutX)
    {
        const double lf = std::log (freqForX (aboutX));
        double l0 = std::log (fMin), l1 = std::log (fMax);
        l0 = lf + (l0 - lf) * factor;
        l1 = lf + (l1 - lf) * factor;
        const double minSpan = std::log (1.26), maxSpan = std::log (fMax0 / fMin0);
        double span = juce::jlimit (minSpan, maxSpan, l1 - l0);
        const double mid = 0.5 * (l0 + l1);
        l0 = mid - span * 0.5; l1 = mid + span * 0.5;
        const double lo = std::log (fMin0), hi = std::log (fMax0);
        if (l0 < lo) { l1 += lo - l0; l0 = lo; }
        if (l1 > hi) { l0 -= l1 - hi; l1 = hi; }
        fMin = std::exp (l0); fMax = std::exp (l1);
        invalidateAll();
    }

    void zoomY (double factor, float aboutY)
    {
        const double v = valueForY (aboutY);
        double a = v + (yMin - v) * factor, b = v + (yMax - v) * factor;
        const double span = juce::jlimit (minYSpan(), yMax0 - yMin0, b - a);
        const double mid = 0.5 * (a + b);
        a = mid - span * 0.5; b = mid + span * 0.5;
        if (a < yMin0) { b += yMin0 - a; a = yMin0; }
        if (b > yMax0) { a -= b - yMax0; b = yMax0; }
        yMin = a; yMax = b;
        invalidateAll();
    }

    void panTo (juce::Point<float> delta)
    {
        const double lspan = std::log (panF1 / panF0);
        double dl = -(double) delta.x / std::max (1.0f, plotArea.getWidth()) * lspan;
        double l0 = std::log (panF0) + dl, l1 = std::log (panF1) + dl;
        const double lo = std::log (fMin0), hi = std::log (fMax0);
        if (l0 < lo) { l1 += lo - l0; l0 = lo; }
        if (l1 > hi) { l0 -= l1 - hi; l1 = hi; }
        fMin = std::exp (l0); fMax = std::exp (l1);

        const double yspan = panY1 - panY0;
        double dy = (double) delta.y / std::max (1.0f, plotArea.getHeight()) * yspan;
        double a = panY0 + dy, b = panY1 + dy;
        if (a < yMin0) { b += yMin0 - a; a = yMin0; }
        if (b > yMax0) { a -= b - yMax0; b = yMax0; }
        yMin = a; yMax = b;
        invalidateAll();
    }

    // ---- painting ---------------------------------------------------------------------
    void paintBands (juce::Graphics& g)
    {
        for (auto& [f0, f1, col] : bands)
        {
            const float xa = juce::jlimit (plotArea.getX(), plotArea.getRight(), xForFreq (f0));
            const float xb = juce::jlimit (plotArea.getX(), plotArea.getRight(), xForFreq (f1));
            if (xb <= xa)
                continue;
            juce::Rectangle<float> r (xa, plotArea.getY(), xb - xa, plotArea.getHeight());
            g.setGradientFill (juce::ColourGradient (col.withAlpha (0.16f), xa, 0.0f, col.withAlpha (0.06f), xb, 0.0f, false));
            g.fillRect (r);
            g.setColour (col.withAlpha (0.35f));
            g.fillRect (xa, plotArea.getY(), 1.0f, plotArea.getHeight());
            g.fillRect (xb - 1.0f, plotArea.getY(), 1.0f, plotArea.getHeight());
        }
    }

    void paintGrid (juce::Graphics& g)
    {
        if (gridDirty)
            rebuildGrid();
        g.setColour (theme::border.withAlpha (0.55f));
        g.strokePath (gridMinor, juce::PathStrokeType (1.0f));
        g.setColour (theme::borderHi.withAlpha (0.95f));
        g.strokePath (gridMajor, juce::PathStrokeType (1.0f));
        g.setColour (theme::textMuted.withAlpha (0.7f));
        g.strokePath (gridZero, juce::PathStrokeType (1.0f));
    }

    void paintAxisLabels (juce::Graphics& g)
    {
        const auto font = theme::mono (10.0f);
        g.setFont (font);
        const float yText = plotArea.getBottom() + 4.0f;
        for (auto& l : xLabels)
        {
            g.setColour (l.major ? theme::textDim : theme::textMuted);
            const float w = textWidth (font, l.text) + 4.0f;
            g.drawText (l.text, juce::Rectangle<float> (l.pos - w * 0.5f, yText, w, 14.0f), juce::Justification::centred, false);
        }
        for (auto& l : yLabels)
        {
            g.setColour (l.major ? theme::textDim : theme::textMuted);
            g.drawText (l.text, juce::Rectangle<float> (0.0f, l.pos - 7.0f, marginL - 7.0f, 14.0f), juce::Justification::centredRight, false);
        }
        // unit tag at the top of the value axis
        const auto suffix = unitSuffix();
        if (suffix.isNotEmpty())
        {
            g.setColour (theme::textMuted);
            g.setFont (theme::font (9.5f));
            g.drawText (suffix, juce::Rectangle<float> (0.0f, plotArea.getY() - 8.0f, marginL - 7.0f, 12.0f), juce::Justification::centredRight, false);
        }
    }

    void paintTraces (juce::Graphics& g)
    {
        syncCaches();
        const float bottom = plotArea.getBottom();

        // 1. area fills (under everything else)
        for (size_t i = 0; i < traces.size(); ++i)
        {
            auto& t = traces[i];
            if (! t.visible) continue;
            auto& c = caches[i];
            if (c.dirty) rebuildTrace (i);

            if (t.bars)
            {
                const float top = std::max (plotArea.getY(), c.topY);
                g.setGradientFill (juce::ColourGradient (t.colour.withAlpha (0.95f), 0.0f, top,
                                                         t.colour.withAlpha (0.18f), 0.0f, bottom, false));
                g.fillPath (c.line);
                g.setColour (t.colour.brighter (0.6f).withAlpha (0.95f));
                g.fillPath (c.barCaps);
            }
            else if (t.fillToBottom)
            {
                const float top = std::max (plotArea.getY(), c.topY);
                g.setGradientFill (juce::ColourGradient (t.colour.withAlpha (0.28f), 0.0f, top,
                                                         t.colour.withAlpha (0.0f), 0.0f, bottom, false));
                g.fillPath (c.fill);
            }
        }

        // 2. lines, with a soft glow underneath
        for (size_t i = 0; i < traces.size(); ++i)
        {
            auto& t = traces[i];
            if (! t.visible || t.bars) continue;
            auto& c = caches[i];

            const juce::PathStrokeType stroke (t.thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
            const juce::PathStrokeType glow (t.thickness + 3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

            if (t.dashed)
            {
                g.setColour (t.colour.withAlpha (0.9f));
                g.fillPath (c.stroke);
                continue;
            }

            if (c.hasAlpha)
            {
                for (int b = 0; b < kAlphaBuckets; ++b)
                {
                    const auto& p = c.alphaLines[(size_t) b];
                    if (p.isEmpty()) continue;
                    const float a = juce::jlimit (0.08f, 1.0f, ((float) b + 0.5f) / (float) kAlphaBuckets);
                    g.setColour (t.colour.withAlpha (0.10f * a));
                    g.strokePath (p, glow);
                    g.setColour (t.colour.withAlpha (a));
                    g.strokePath (p, stroke);
                }
                continue;
            }

            g.setColour (t.colour.withAlpha (0.12f));
            g.strokePath (c.line, glow);
            g.setColour (t.colour);
            g.strokePath (c.line, stroke);
        }
    }

    void paintNodes (juce::Graphics& g)
    {
        for (int i = 0; i < (int) nodes.size(); ++i)
        {
            const auto& nd = nodes[(size_t) i];
            const float x = xForFreq (nd.freq), y = yForValue (nd.value);
            const bool active = nd.selected || i == hoverNode || i == dragNode;

            if (active)
            {
                // bandwidth bracket (f / Q) at the node's level
                const double half = std::pow (2.0, 1.0 / (2.0 * std::max (0.05, nd.q)));
                const float xa = xForFreq (nd.freq / half), xb = xForFreq (nd.freq * half);
                g.setColour (nd.colour.withAlpha (0.18f));
                g.fillRoundedRectangle (xa, y - 2.0f, xb - xa, 4.0f, 2.0f);
                g.setColour (nd.colour.withAlpha (0.5f));
                g.fillRect (xa, y - 5.0f, 1.0f, 10.0f);
                g.fillRect (xb - 1.0f, y - 5.0f, 1.0f, 10.0f);

                // vertical guide
                g.setColour (nd.colour.withAlpha (0.25f));
                g.fillRect (x - 0.5f, plotArea.getY(), 1.0f, plotArea.getHeight());

                // glow
                g.setColour (nd.colour.withAlpha (0.22f));
                g.fillEllipse (x - kNodeRadius * 2.2f, y - kNodeRadius * 2.2f, kNodeRadius * 4.4f, kNodeRadius * 4.4f);
            }

            g.setColour (theme::bg1);
            g.fillEllipse (x - kNodeRadius, y - kNodeRadius, kNodeRadius * 2.0f, kNodeRadius * 2.0f);
            if (nd.selected)
            {
                g.setColour (nd.colour);
                g.fillEllipse (x - kNodeRadius + 1.5f, y - kNodeRadius + 1.5f, kNodeRadius * 2.0f - 3.0f, kNodeRadius * 2.0f - 3.0f);
            }
            g.setColour (nd.colour);
            g.drawEllipse (x - kNodeRadius, y - kNodeRadius, kNodeRadius * 2.0f, kNodeRadius * 2.0f, 2.0f);

            if (active)
            {
                const auto text = theme::formatHz (nd.freq) + "   " + formatValue (nd.value) + "   Q " + juce::String (nd.q, 2);
                const auto font = theme::mono (10.5f);
                const float w = textWidth (font, text) + 16.0f;
                juce::Rectangle<float> r (x - w * 0.5f, y - kNodeRadius - 28.0f, w, 20.0f);
                if (r.getX() < plotArea.getX()) r.setX (plotArea.getX());
                if (r.getRight() > plotArea.getRight()) r.setX (plotArea.getRight() - w);
                if (r.getY() < plotArea.getY()) r.setY (y + kNodeRadius + 8.0f);
                theme::fillCard (g, r, theme::bg3.withAlpha (0.96f), nd.colour.withAlpha (0.45f), 5.0f);
                g.setColour (theme::text);
                g.setFont (font);
                g.drawText (text, r, juce::Justification::centred, false);
            }
        }
    }

    void paintCrosshair (juce::Graphics& g)
    {
        if (! hovering || panning || dragNode >= 0 || hoverNode >= 0 || ! plotArea.contains (mouse))
            return;

        const double f = freqForX (mouse.x);

        // vertical hairline + x tag
        g.setColour (theme::text.withAlpha (0.22f));
        g.fillRect (std::round (mouse.x), plotArea.getY(), 1.0f, plotArea.getHeight());
        g.setColour (theme::text.withAlpha (0.10f));
        g.fillRect (plotArea.getX(), std::round (mouse.y), plotArea.getWidth(), 1.0f);

        struct Row { juce::Colour col; juce::String label, value; float y; bool has; };
        std::vector<Row> rows;
        rows.reserve (traces.size());
        for (auto& t : traces)
        {
            if (! t.visible) continue;
            double v = 0.0, a = 1.0;
            const bool has = traceValueAt (t, f, v, a, units == Units::degrees || t.bars);
            rows.push_back ({ t.colour.withAlpha (has ? (float) juce::jlimit (0.3, 1.0, a) : 0.3f), t.label,
                              has ? formatValue (v) : juce::String ("--"), has ? yForValue (v) : 0.0f, has });
        }

        // dots on the curves
        for (auto& r : rows)
        {
            if (! r.has || r.y < plotArea.getY() || r.y > plotArea.getBottom()) continue;
            g.setColour (r.col.withAlpha (0.35f));
            g.fillEllipse (mouse.x - 6.0f, r.y - 6.0f, 12.0f, 12.0f);
            g.setColour (theme::bg0);
            g.fillEllipse (mouse.x - 3.5f, r.y - 3.5f, 7.0f, 7.0f);
            g.setColour (r.col);
            g.fillEllipse (mouse.x - 2.5f, r.y - 2.5f, 5.0f, 5.0f);
        }

        // readout card
        const auto headFont = theme::fontMedium (12.0f);
        const auto rowFont  = theme::font (11.0f);
        const auto valFont  = theme::mono (11.0f);
        const auto head = theme::formatHz (f, f < 100.0 ? 1 : 0)
                        + (units == Units::dB ? juce::String() : juce::String())
                        + "   " + formatValue (valueForY (mouse.y));
        float w = textWidth (headFont, head) + 24.0f;
        for (auto& r : rows)
            w = std::max (w, 24.0f + 14.0f + textWidth (rowFont, r.label) + 18.0f + textWidth (valFont, r.value));
        w = std::min (w, 320.0f);
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
                g.setColour (r.has ? theme::textDim : theme::textMuted);
                g.setFont (rowFont);
                g.drawText (r.label, line.withTrimmedLeft (14.0f).withTrimmedRight (textWidth (valFont, r.value) + 8.0f),
                            juce::Justification::centredLeft, true);
                g.setColour (r.has ? theme::text : theme::textMuted);
                g.setFont (valFont);
                g.drawText (r.value, line, juce::Justification::centredRight, false);
            }
        }
    }

    void paintLegend (juce::Graphics& g)
    {
        chips.clear();
        if (! showLegend || traces.empty())
            return;

        const auto font = theme::font (11.0f);
        const float chipH = 20.0f;
        const float y = plotArea.getY() - 26.0f;
        float x = plotArea.getRight();

        for (int i = (int) traces.size() - 1; i >= 0; --i)
        {
            const auto& t = traces[(size_t) i];
            const float w = textWidth (font, t.label) + 30.0f;
            x -= w;
            if (x < plotArea.getX())
                break;
            juce::Rectangle<float> r (x, y, w, chipH);
            chips.push_back ({ r, i });

            const bool hov = hoverChip >= 0 && chips.back().trace == i && hoverChip == (int) chips.size() - 1;
            juce::Colour fill = t.visible ? theme::bg3 : theme::bg2;
            if (hov) fill = theme::bg4;
            g.setColour (fill);
            g.fillRoundedRectangle (r, chipH * 0.5f);
            g.setColour (t.visible ? t.colour.withAlpha (0.35f) : theme::border);
            g.drawRoundedRectangle (r.reduced (0.5f), chipH * 0.5f, 1.0f);

            const float dotR = 4.0f;
            g.setColour (t.visible ? t.colour : theme::textMuted);
            if (t.visible)
                g.fillEllipse (r.getX() + 9.0f, r.getCentreY() - dotR, dotR * 2.0f, dotR * 2.0f);
            else
                g.drawEllipse (r.getX() + 9.5f, r.getCentreY() - dotR + 0.5f, dotR * 2.0f - 1.0f, dotR * 2.0f - 1.0f, 1.0f);

            g.setColour (t.visible ? theme::text : theme::textMuted);
            g.setFont (font);
            g.drawText (t.label, r.withTrimmedLeft (22.0f).withTrimmedRight (8.0f), juce::Justification::centredLeft, true);
            x -= 6.0f;
        }
    }

    void paintTitle (juce::Graphics& g)
    {
        if (title.isEmpty())
            return;
        const auto font = theme::fontMedium (10.5f);
        const auto text = title.toUpperCase();
        const float w = std::min (plotArea.getWidth() - 20.0f, textWidth (font, text) + 16.0f);
        juce::Rectangle<float> r (plotArea.getX() + 8.0f, plotArea.getY() + 7.0f, w, 17.0f);
        g.setColour (theme::bg1.withAlpha (0.82f));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (theme::border.withAlpha (0.8f));
        g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
        g.setColour (theme::textDim);
        g.setFont (font);
        g.drawText (text, r.reduced (8.0f, 0.0f), juce::Justification::centredLeft, true);
    }
};

// =====================================================================================
//  FrequencyPlot
// =====================================================================================
FrequencyPlot::FrequencyPlot() : impl (std::make_unique<Impl> (*this))
{
    setOpaque (false);
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
    setWantsKeyboardFocus (false);
}

FrequencyPlot::~FrequencyPlot() = default;

void FrequencyPlot::setUnits (Units u)
{
    impl->units = u;
    impl->applyDefaultRanges();
    repaint();
}

void FrequencyPlot::setRanges (double fMin, double fMax, double yMin, double yMax)
{
    if (fMax <= fMin) std::swap (fMin, fMax);
    if (yMax <= yMin) std::swap (yMin, yMax);
    impl->fMin0 = std::max (1.0, fMin); impl->fMax0 = std::max (impl->fMin0 * 1.26, fMax);
    impl->yMin0 = yMin; impl->yMax0 = std::max (yMin + impl->minYSpan(), yMax);
    impl->resetView();
    repaint();
}

void FrequencyPlot::setTrace (const PlotTrace& t)
{
    impl->syncCaches();
    for (size_t i = 0; i < impl->traces.size(); ++i)
    {
        if (impl->traces[i].id == t.id)
        {
            const bool keepVisible = impl->traces[i].visible;
            impl->traces[i] = t;
            impl->traces[i].visible = keepVisible && t.visible;
            impl->caches[i].dirty = true;
            repaint();
            return;
        }
    }
    const bool hadNone = impl->traces.empty();
    impl->traces.push_back (t);
    impl->caches.emplace_back();
    if (hadNone)
        impl->layout();
    repaint();
}

void FrequencyPlot::removeTrace (const juce::String& id)
{
    impl->syncCaches();
    for (size_t i = 0; i < impl->traces.size(); ++i)
    {
        if (impl->traces[i].id == id)
        {
            impl->traces.erase (impl->traces.begin() + (long) i);
            impl->caches.erase (impl->caches.begin() + (long) i);
            if (impl->traces.empty())
                impl->layout();
            repaint();
            return;
        }
    }
}

void FrequencyPlot::clearTraces()
{
    impl->traces.clear();
    impl->caches.clear();
    impl->layout();
    repaint();
}

void FrequencyPlot::setTraceVisible (const juce::String& id, bool visible)
{
    for (auto& t : impl->traces)
        if (t.id == id)
            t.visible = visible;
    repaint();
}

void FrequencyPlot::setShowLegend (bool b)
{
    impl->showLegend = b;
    impl->layout();
    repaint();
}

void FrequencyPlot::setTitle (const juce::String& t)
{
    impl->title = t;
    repaint();
}

void FrequencyPlot::setBands (std::vector<std::tuple<double, double, juce::Colour>> regions)
{
    impl->bands = std::move (regions);
    repaint();
}

void FrequencyPlot::setNodes (std::vector<Node> nodes)
{
    impl->nodes = std::move (nodes);
    if (impl->dragNode >= (int) impl->nodes.size()) impl->dragNode = -1;
    if (impl->hoverNode >= (int) impl->nodes.size()) impl->hoverNode = -1;
    repaint();
}

void FrequencyPlot::resetView()
{
    impl->resetView();
    repaint();
}

float  FrequencyPlot::xForFreq (double f) const  { return impl->xForFreq (f); }
float  FrequencyPlot::yForValue (double v) const { return impl->yForValue (v); }
double FrequencyPlot::freqForX (float x) const   { return impl->freqForX (x); }
double FrequencyPlot::valueForY (float y) const  { return impl->valueForY (y); }
juce::Rectangle<float> FrequencyPlot::getPlotArea() const { return impl->plotArea; }

void FrequencyPlot::resized()
{
    impl->layout();
}

void FrequencyPlot::paint (juce::Graphics& g)
{
    auto& im = *impl;
    const auto area = im.plotArea;
    if (area.getWidth() < 10.0f || area.getHeight() < 10.0f)
        return;

    // plot background: a touch deeper than the card it sits in
    g.setColour (theme::bg1.darker (0.25f));
    g.fillRoundedRectangle (area, theme::radiusSm);

    {
        juce::Graphics::ScopedSaveState ss (g);
        juce::Path clip;
        clip.addRoundedRectangle (area, theme::radiusSm);
        g.reduceClipRegion (clip);

        im.paintBands (g);
        im.paintGrid (g);
        im.paintTraces (g);
        im.paintNodes (g);
        im.paintCrosshair (g);
    }

    g.setColour (theme::border);
    g.drawRoundedRectangle (area.reduced (0.5f), theme::radiusSm, 1.0f);

    im.paintAxisLabels (g);
    im.paintTitle (g);
    im.paintLegend (g);
}

void FrequencyPlot::mouseMove (const juce::MouseEvent& e)
{
    auto& im = *impl;
    im.hovering = true;
    im.mouse = e.position;
    im.hoverChip = im.chipAt (e.position);
    im.hoverNode = im.hoverChip < 0 ? im.nodeAt (e.position) : -1;
    setMouseCursor (im.hoverChip >= 0 ? juce::MouseCursor::PointingHandCursor
                   : im.hoverNode >= 0 ? juce::MouseCursor::DraggingHandCursor
                   : im.plotArea.contains (e.position) ? juce::MouseCursor::CrosshairCursor
                   : juce::MouseCursor::NormalCursor);
    repaint();
}

void FrequencyPlot::mouseExit (const juce::MouseEvent&)
{
    impl->hovering = false;
    impl->hoverChip = -1;
    impl->hoverNode = -1;
    repaint();
}

void FrequencyPlot::mouseDown (const juce::MouseEvent& e)
{
    auto& im = *impl;
    im.mouse = e.position;

    if (const int chip = im.chipAt (e.position); chip >= 0)
    {
        auto& t = im.traces[(size_t) im.chips[(size_t) chip].trace];
        t.visible = ! t.visible;
        repaint();
        return;
    }

    if (const int n = im.nodeAt (e.position); n >= 0)
    {
        im.dragNode = n;
        im.draggingQ = e.mods.isShiftDown();
        im.dragStartNode = im.nodes[(size_t) n];
        for (auto& nd : im.nodes)
            nd.selected = false;
        im.nodes[(size_t) n].selected = true;
        const auto id = im.nodes[(size_t) n].id;
        repaint();
        if (onNodeSelected)
            onNodeSelected (id);
        return;
    }

    if (im.plotArea.contains (e.position))
    {
        im.panning = true;
        im.dragStart = e.position;
        im.panF0 = im.fMin; im.panF1 = im.fMax; im.panY0 = im.yMin; im.panY1 = im.yMax;
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    }
}

void FrequencyPlot::mouseDrag (const juce::MouseEvent& e)
{
    auto& im = *impl;
    im.mouse = e.position;

    if (im.dragNode >= 0 && im.dragNode < (int) im.nodes.size())
    {
        Node nd = im.nodes[(size_t) im.dragNode];
        const auto start = im.dragStartNode;
        if (im.draggingQ || e.mods.isShiftDown())
        {
            const float dy = e.position.y - e.getMouseDownPosition().toFloat().y;
            nd.q = juce::jlimit (0.1, 30.0, start.q * std::exp (-dy / 60.0));
        }
        else
        {
            nd.freq  = juce::jlimit (im.fMin0, im.fMax0, im.freqForX (e.position.x));
            nd.value = juce::jlimit (im.yMin0, im.yMax0, im.valueForY (e.position.y));
            if (e.mods.isCtrlDown())        // fine: snap gain to 0.5 steps and freq to 1/24 oct
            {
                nd.value = std::round (nd.value * 2.0) * 0.5;
                nd.freq  = std::pow (2.0, std::round (std::log2 (nd.freq) * 24.0) / 24.0);
            }
        }
        im.nodes[(size_t) im.dragNode] = nd;
        repaint();
        if (onNodeChanged)
            onNodeChanged (nd);
        return;
    }

    if (im.panning)
    {
        im.panTo (e.position - im.dragStart);
        repaint();
    }
}

void FrequencyPlot::mouseUp (const juce::MouseEvent& e)
{
    auto& im = *impl;
    im.dragNode = -1;
    im.draggingQ = false;
    if (im.panning)
    {
        im.panning = false;
        setMouseCursor (juce::MouseCursor::CrosshairCursor);
    }
    im.mouse = e.position;
    repaint();
}

void FrequencyPlot::mouseDoubleClick (const juce::MouseEvent& e)
{
    auto& im = *impl;
    if (im.chipAt (e.position) >= 0 || im.nodeAt (e.position) >= 0)
        return;
    if (! im.plotArea.contains (e.position))
        return;
    if (onBackgroundDoubleClick && ! e.mods.isCtrlDown())
    {
        onBackgroundDoubleClick (im.freqForX (e.position.x), im.valueForY (e.position.y));
        return;
    }
    resetView();
}

void FrequencyPlot::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    auto& im = *impl;
    const float delta = w.deltaY != 0.0f ? w.deltaY : -w.deltaX;
    if (delta == 0.0f)
        return;

    // wheel over an EQ node: adjust its Q
    const int n = im.dragNode >= 0 ? im.dragNode : im.nodeAt (e.position);
    if (n >= 0 && n < (int) im.nodes.size())
    {
        Node nd = im.nodes[(size_t) n];
        nd.q = juce::jlimit (0.1, 30.0, nd.q * std::exp ((double) delta * 1.5));
        im.nodes[(size_t) n] = nd;
        im.hoverNode = n;
        repaint();
        if (onNodeChanged)
            onNodeChanged (nd);
        return;
    }

    const double factor = std::exp (-(double) delta * 1.1);
    const bool overYAxis = e.position.x < im.plotArea.getX();
    if (e.mods.isCtrlDown() || e.mods.isCommandDown() || overYAxis)
        im.zoomY (factor, juce::jlimit (im.plotArea.getY(), im.plotArea.getBottom(), e.position.y));
    else
        im.zoomX (factor, juce::jlimit (im.plotArea.getX(), im.plotArea.getRight(), e.position.x));
    im.mouse = e.position;
    repaint();
}
} // namespace fa::ui
