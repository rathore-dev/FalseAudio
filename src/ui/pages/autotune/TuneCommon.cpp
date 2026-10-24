#include "TuneCommon.h"
#include "core/dsp/Spectrum.h"

namespace fa::ui
{
namespace
{
    int wrappedHeight (const juce::Font& font, const juce::String& text, int width)
    {
        if (text.isEmpty() || width <= 0)
            return 0;
        juce::AttributedString as;
        as.append (text, font);
        juce::TextLayout tl;
        tl.createLayout (as, (float) width);
        return (int) std::ceil (tl.getHeight());
    }

    void drawWrapped (juce::Graphics& g, const juce::Font& font, juce::Colour c, const juce::String& text,
                      juce::Rectangle<int> area)
    {
        juce::AttributedString as;
        as.append (text, font, c);
        juce::TextLayout tl;
        tl.createLayout (as, (float) area.getWidth());
        tl.draw (g, area.toFloat());
    }
}

namespace tune
{
    juce::Colour colourForChannelId (const SystemConfig& cfg, const juce::String& id)
    {
        if (auto* ch = cfg.find (id))
            return theme::sideTint (theme::roleColour (ch->role), ch->side);

        if (id.containsIgnoreCase ("L+R") || id.contains ("+"))
            return theme::curveSum;
        if (id.endsWithIgnoreCase (" L") || id.startsWithIgnoreCase ("FL") || id.containsIgnoreCase ("left"))
            return theme::curveLeft;
        if (id.endsWithIgnoreCase (" R") || id.startsWithIgnoreCase ("FR") || id.containsIgnoreCase ("right"))
            return theme::curveRight;
        return theme::textDim;
    }

    juce::String labelForChannelId (const SystemConfig& cfg, const juce::String& id)
    {
        if (auto* ch = cfg.find (id))
            return ch->label;
        return id;
    }

    juce::String layoutSummary (const SystemConfig& cfg)
    {
        int ways = 0, subs = 0;
        bool fullrange = false;
        for (auto& c : cfg.channels)
        {
            if (c.role == DriverRole::subwoofer)      ++subs;
            else if (c.side == Side::left)            ++ways;
            if (c.role == DriverRole::fullrange)      fullrange = true;
        }
        if (ways == 0 && subs == 0)
            return "No outputs";

        juce::String s;
        if (ways <= 1 && fullrange) s = "Full range";
        else if (ways >= 1)         s = juce::String (ways) + "-way active";
        if (subs > 0)
            s += (s.isEmpty() ? "" : " + ") + juce::String (subs > 1 ? "stereo sub" : "sub");
        return s;
    }

    juce::String dspKindName (Session& session)
    {
        switch (session.getDspKind())
        {
            case Session::DspKind::virtualDemo:  return "Virtual DSP";
            case Session::DspKind::manual:       return "Manual (guided)";
            case Session::DspKind::uiAutomation: return "Automated DSP app";
        }
        return "DSP";
    }

    juce::String formatDuration (double seconds)
    {
        const int total = (int) std::round (seconds);
        const int m = total / 60, s = total % 60;
        if (m == 0)
            return juce::String (s) + " s";
        return juce::String (m) + ":" + juce::String (s).paddedLeft ('0', 2);
    }

    juce::String formatDelayCm (double ms)
    {
        return juce::String (msToMetres (ms) * 100.0, 1) + " cm";
    }

    std::vector<Finding> buildFindings (const AutoTuneEngine::Result& r, const SystemConfig& cfg)
    {
        std::vector<Finding> out;

        // Wiring mistakes found (and fixed) come first - they're the headline.
        for (auto& [id, inverted] : r.report.wiringInverted)
        {
            if (! inverted)
                continue;
            Finding f;
            f.kind = Finding::Kind::problemFixed;
            f.title = labelForChannelId (cfg, id) + " wired in reverse - corrected";
            f.detail = "The driver's polarity was flipped in the DSP so it sums correctly with its neighbours. "
                       "Swap the + / - wires at the speaker to fix it permanently.";
            f.colour = colourForChannelId (cfg, id);
            f.icon = icons::Id::check;
            out.push_back (f);
        }

        for (auto& [f0, depth] : r.report.sumDips)
        {
            Finding f;
            const bool bad = depth >= 9.0;
            f.kind = bad ? Finding::Kind::warning : Finding::Kind::info;
            f.title = "L+R cancellation of " + juce::String (depth, 1) + " dB at " + theme::formatHz (f0, 0);
            f.detail = bad ? "Left and right partly cancel here. Check delays and the sub/midbass crossover region."
                           : "Mild left/right interference; usually inaudible.";
            f.colour = bad ? theme::warn : theme::textDim;
            f.icon = bad ? icons::Id::warning : icons::Id::info;
            out.push_back (f);
        }

        for (auto& w : r.report.warnings)
        {
            // Already covered above in a nicer form.
            if (w.contains ("appears wired in reverse") || w.startsWith ("L+R cancellation"))
                continue;
            Finding f;
            f.kind = Finding::Kind::warning;
            const int colon = w.indexOfChar (':');
            if (colon > 0 && colon < 40)
            {
                f.title = w.substring (0, colon).trim();
                f.detail = w.substring (colon + 1).trim();
                f.colour = theme::warn;
                for (auto& c : cfg.channels)
                    if (c.label == f.title || c.id == f.title)
                        f.colour = theme::sideTint (theme::roleColour (c.role), c.side);
            }
            else
            {
                f.title = w;
                f.colour = theme::warn;
            }
            f.icon = icons::Id::warning;
            out.push_back (f);
        }

        if (out.empty() && r.success)
        {
            Finding f;
            f.kind = Finding::Kind::good;
            f.title = "No problems found";
            f.detail = "Every driver measured cleanly, polarity is correct and left and right sum without cancellations.";
            f.colour = theme::good;
            f.icon = icons::Id::check;
            out.push_back (f);
        }
        return out;
    }

    std::vector<NoteGroup> groupNotes (const juce::StringArray& notes, const SystemConfig& cfg)
    {
        std::vector<NoteGroup> groups;
        NoteGroup system;
        system.title = "System";
        system.colour = theme::accent;

        for (auto& c : cfg.channels)
        {
            NoteGroup g;
            g.title = c.label;
            g.colour = theme::sideTint (theme::roleColour (c.role), c.side);
            groups.push_back (g);
        }

        for (auto& n : notes)
        {
            const int colon = n.indexOfChar (':');
            bool placed = false;
            if (colon > 0)
            {
                const auto key = n.substring (0, colon).trim();
                const auto rest = n.substring (colon + 1).trim();
                for (size_t i = 0; i < cfg.channels.size(); ++i)
                {
                    if (cfg.channels[i].label == key || cfg.channels[i].id == key)
                    {
                        groups[i].lines.add (rest);
                        placed = true;
                        break;
                    }
                }
            }
            if (! placed)
                system.lines.add (n);
        }

        std::vector<NoteGroup> out;
        for (auto& g : groups)
            if (! g.lines.isEmpty())
                out.push_back (g);
        if (! system.lines.isEmpty())
            out.push_back (system);
        return out;
    }

    double referenceLevel (const std::vector<double>& freqs, const std::vector<double>& db, double f1, double f2)
    {
        double acc = 0.0;
        int n = 0;
        for (size_t i = 0; i < freqs.size() && i < db.size(); ++i)
            if (freqs[i] >= f1 && freqs[i] <= f2)
                { acc += db[i]; ++n; }
        return n > 0 ? acc / n : 0.0;
    }

    void populateResultPlot (FrequencyPlot& plot, const AutoTuneEngine::Result& r, int side)
    {
        plot.clearTraces();
        const auto& freqs = analysisFrequencies();
        const double ref = referenceLevel (freqs, r.targetDb);

        auto shifted = [ref] (const FrequencyResponse& resp, double fraction)
        {
            auto sm = smoothFractionalOctave (resp, fraction);
            auto db = sm.magnitudeDb();
            for (auto& v : db) v -= ref;
            return std::make_pair (sm.freqs, db);
        };

        if (! r.targetDb.empty())
        {
            std::vector<double> t = r.targetDb;
            for (auto& v : t) v -= ref;
            auto target = PlotTrace::fromDb ("target", "Target", theme::curveTarget, freqs, t);
            target.dashed = true;
            target.thickness = 1.5f;
            plot.setTrace (target);
        }

        const FrequencyResponse* before = side == 0 ? &r.leftBefore : side == 1 ? &r.rightBefore : nullptr;
        const FrequencyResponse* after  = side == 0 ? &r.leftAfter  : side == 1 ? &r.rightAfter  : &r.bothAfter;

        if (before != nullptr && ! before->isEmpty())
        {
            auto [f, db] = shifted (*before, 6.0);
            auto t = PlotTrace::fromDb ("before", "Before", theme::curveBefore, f, db);
            t.thickness = 1.75f;
            plot.setTrace (t);
        }
        if (after != nullptr && ! after->isEmpty())
        {
            auto [f, db] = shifted (*after, 6.0);
            auto t = PlotTrace::fromDb ("after", side == 2 ? "After (L+R)" : "After",
                                        side == 1 ? theme::curveRight : side == 2 ? theme::curveSum : theme::curveAfter, f, db);
            t.thickness = 2.25f;
            t.fillToBottom = true;
            plot.setTrace (t);
        }
    }
} // namespace tune

//==============================================================================
MetricTile::MetricTile (const juce::String& c, const juce::String& u) : caption (c), unit (u) {}

void MetricTile::set (const juce::String& v, juce::Colour t, const juce::String& h)
{
    value = v;
    tint = t;
    hint = h;
    repaint();
}

void MetricTile::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    theme::fillCard (g, r, theme::bg3.withAlpha (0.55f), theme::border, theme::radiusSm);

    auto area = getLocalBounds().reduced (14, 10);
    g.setColour (theme::textDim);
    g.setFont (theme::fontMedium (11.0f));
    g.drawText (caption.toUpperCase(), area.removeFromTop (16), juce::Justification::centredLeft);

    auto valueRow = area.removeFromTop (30);
    g.setColour (tint);
    g.setFont (theme::display (24.0f));
    const int vw = (int) std::ceil (juce::GlyphArrangement::getStringWidth (theme::display (24.0f), value)) + 2;
    g.drawText (value, valueRow.removeFromLeft (vw), juce::Justification::centredLeft);
    if (unit.isNotEmpty())
    {
        g.setColour (theme::textMuted);
        g.setFont (theme::font (12.0f));
        g.drawText (unit, valueRow.translated (4, 4), juce::Justification::centredLeft);
    }
    if (hint.isNotEmpty() && area.getHeight() >= 14)
    {
        g.setColour (theme::textMuted);
        g.setFont (theme::font (11.0f));
        g.drawText (hint, area.removeFromTop (16), juce::Justification::centredLeft);
    }
}

//==============================================================================
void FindingsList::setFindings (std::vector<tune::Finding> f)
{
    findings = std::move (f);
    repaint();
}

int FindingsList::rowHeight (const tune::Finding& f, int width) const
{
    const int textW = width - 56;
    int h = 22;
    if (f.detail.isNotEmpty())
        h += wrappedHeight (theme::font (12.5f), f.detail, textW) + 2;
    return std::max (44, h + 16);
}

int FindingsList::getIdealHeight (int width) const
{
    int h = 0;
    for (auto& f : findings)
        h += rowHeight (f, width);
    return std::max (h, 24);
}

void FindingsList::paint (juce::Graphics& g)
{
    auto area = getLocalBounds();
    for (size_t i = 0; i < findings.size(); ++i)
    {
        const auto& f = findings[i];
        auto row = area.removeFromTop (rowHeight (f, getWidth()));

        // Icon bubble
        auto bubble = row.removeFromLeft (44).withSizeKeepingCentre (30, 30).withY (row.getY() + 8).toFloat();
        g.setColour (f.colour.withAlpha (0.14f));
        g.fillEllipse (bubble);
        g.setColour (f.colour.withAlpha (0.5f));
        g.drawEllipse (bubble.reduced (0.5f), 1.0f);
        icons::draw (g, f.icon, bubble.reduced (8.0f), f.colour);

        auto text = row.reduced (0, 8);
        text.removeFromLeft (10);
        g.setColour (theme::text);
        g.setFont (theme::fontMedium (13.5f));
        auto titleRow = text.removeFromTop (20);
        g.drawText (f.title, titleRow, juce::Justification::centredLeft, true);

        if (f.kind == tune::Finding::Kind::problemFixed)
        {
            const juce::String tag = "FIXED";
            const int tw = (int) juce::GlyphArrangement::getStringWidth (theme::fontBold (10.0f), tag) + 14;
            const int titleW = (int) juce::GlyphArrangement::getStringWidth (theme::fontMedium (13.5f), f.title) + 10;
            auto tagRect = titleRow.withX (titleRow.getX() + std::min (titleW, titleRow.getWidth() - tw)).withWidth (tw).reduced (0, 2).toFloat();
            g.setColour (theme::good.withAlpha (0.18f));
            g.fillRoundedRectangle (tagRect, 4.0f);
            g.setColour (theme::good);
            g.setFont (theme::fontBold (10.0f));
            g.drawText (tag, tagRect, juce::Justification::centred);
        }

        if (f.detail.isNotEmpty())
            drawWrapped (g, theme::font (12.5f), theme::textDim, f.detail, text.withTrimmedTop (2));

        if (i + 1 < findings.size())
        {
            g.setColour (theme::border);
            g.fillRect (row.getX() + 54, row.getBottom() - 1, row.getWidth() - 54, 1);
        }
    }
}

//==============================================================================
namespace
{
    constexpr int judgeTotalW = 150;
    constexpr int judgeBarRowH = 30;
}

void JudgePanel::set (const advanced::JudgeScore& score, double before, double after)
{
    categories.clear();
    // Judge order and weights (Judge.cpp): tonal 25, bass 15, stage 10, centre 20, imaging 10, linearity 10, noise 10.
    const std::vector<std::pair<juce::String, int>> order = {
        { "Tonal balance", 25 }, { "Bass extension & up front", 15 }, { "Stage width", 10 }, { "Centre focus", 20 },
        { "Imaging consistency", 10 }, { "Linearity", 10 }, { "Noise", 10 } };
    for (auto& [name, w] : order)
        if (auto it = score.categories.find (name); it != score.categories.end())
            categories.push_back ({ name, it->second, w });
    for (auto& [name, v] : score.categories)
        if (std::none_of (categories.begin(), categories.end(), [&] (const Category& c) { return c.name == name; }))
            categories.push_back ({ name, v, 0 });

    total = score.total;
    comments = score.comments;
    costBefore = before;
    costAfter = after;
    repaint();
}

int JudgePanel::barsHeight (int width) const
{
    const int cols = columns (width);
    const int rows = ((int) categories.size() + cols - 1) / cols;
    return rows * judgeBarRowH;
}

int JudgePanel::commentsHeight (int width) const
{
    int h = 0;
    for (auto& c : comments)
        h += std::max (18, wrappedHeight (theme::font (12.0f), c, width - judgeTotalW - 36)) + 4;
    return h;
}

int JudgePanel::getIdealHeight (int width) const
{
    if (categories.empty())
        return 0;
    const int right = barsHeight (width) + (comments.isEmpty() ? 0 : 14 + commentsHeight (width));
    return std::max (118, right) + 8;
}

void JudgePanel::paint (juce::Graphics& g)
{
    if (categories.empty())
        return;

    auto area = getLocalBounds();
    auto left = area.removeFromLeft (judgeTotalW);
    area.removeFromLeft (20);

    // Total
    const int t = juce::roundToInt (total);
    const auto colour = theme::scoreColour (t);
    g.setColour (theme::textMuted);
    g.setFont (theme::fontBold (10.5f));
    g.drawText ("SQ JUDGE", left.removeFromTop (16), juce::Justification::centredLeft);
    auto big = left.removeFromTop (54);
    g.setColour (colour);
    g.setFont (theme::display (44.0f));
    const int bw = (int) std::ceil (juce::GlyphArrangement::getStringWidth (theme::display (44.0f), juce::String (t))) + 2;
    g.drawText (juce::String (t), big.removeFromLeft (bw), juce::Justification::centredLeft);
    g.setColour (theme::textMuted);
    g.setFont (theme::font (13.0f));
    g.drawText ("/ 100", big.translated (4, 10), juce::Justification::centredLeft);
    g.setColour (theme::textDim);
    g.setFont (theme::fontMedium (12.0f));
    g.drawText (t >= 85 ? "Competition-ready" : t >= 70 ? "Strong" : t >= 50 ? "Decent" : "Work to do", left.removeFromTop (18), juce::Justification::centredLeft);
    if (costBefore > 0.0)
    {
        const double pct = 100.0 * (costBefore - costAfter) / costBefore;
        g.setColour (theme::textMuted);
        g.setFont (theme::font (11.0f));
        g.drawText ("Refine: cost " + juce::String (costBefore, 2) + " -> " + juce::String (costAfter, 2),
                    left.removeFromTop (16), juce::Justification::centredLeft, true);
        g.setColour (pct > 0.5 ? theme::good : theme::textMuted);
        g.drawText (pct > 0.5 ? "-" + juce::String (juce::roundToInt (pct)) + "% perceptual error" : "kept as solved",
                    left.removeFromTop (16), juce::Justification::centredLeft, true);
    }

    // Category bars
    const int cols = columns (getWidth());
    const int colW = (area.getWidth() - (cols - 1) * 24) / cols;
    auto bars = area.removeFromTop (barsHeight (getWidth()));
    for (size_t i = 0; i < categories.size(); ++i)
    {
        const auto& c = categories[i];
        const int col = (int) i % cols, row = (int) i / cols;
        juce::Rectangle<int> cell (bars.getX() + col * (colW + 24), bars.getY() + row * judgeBarRowH, colW, judgeBarRowH);
        auto labelRow = cell.removeFromTop (16);
        g.setColour (theme::textDim);
        g.setFont (theme::font (11.5f));
        g.drawText (c.name + (c.weightPercent > 0 ? "  (" + juce::String (c.weightPercent) + "%)" : juce::String()),
                    labelRow.withTrimmedRight (40), juce::Justification::centredLeft, true);
        const auto cc = theme::scoreColour (juce::roundToInt (c.value));
        g.setColour (theme::text);
        g.setFont (theme::mono (11.5f));
        g.drawText (juce::String (juce::roundToInt (c.value)), labelRow, juce::Justification::centredRight);
        auto track = cell.withHeight (5).translated (0, 2).toFloat();
        g.setColour (theme::bg4);
        g.fillRoundedRectangle (track, 2.5f);
        g.setColour (cc);
        g.fillRoundedRectangle (track.withWidth ((float) track.getWidth() * (float) juce::jlimit (0.0, 1.0, c.value / 100.0)), 2.5f);
    }

    if (! comments.isEmpty())
    {
        area.removeFromTop (14);
        for (auto& c : comments)
        {
            const int h = std::max (18, wrappedHeight (theme::font (12.0f), c, area.getWidth() - 16));
            auto line = area.removeFromTop (h + 4);
            g.setColour (theme::textMuted);
            g.fillEllipse (line.removeFromLeft (12).withSizeKeepingCentre (4, 4).withY (line.getY() + 7).toFloat());
            line.removeFromLeft (4);
            drawWrapped (g, theme::font (12.0f), theme::textDim, c, line);
        }
    }
}

//==============================================================================
void NotesView::setGroups (std::vector<tune::NoteGroup> g)
{
    groups = std::move (g);
    repaint();
}

int NotesView::groupHeight (const tune::NoteGroup& g, int width) const
{
    int h = 24;
    for (auto& l : g.lines)
        h += std::max (18, wrappedHeight (theme::font (12.5f), l, width - 40)) + 3;
    return h + 12;
}

int NotesView::getIdealHeight (int width) const
{
    int h = 0;
    for (auto& g : groups)
        h += groupHeight (g, width);
    return std::max (h, 24);
}

void NotesView::paint (juce::Graphics& g)
{
    auto area = getLocalBounds();
    for (auto& grp : groups)
    {
        auto block = area.removeFromTop (groupHeight (grp, getWidth()));
        auto stripe = block.removeFromLeft (3).reduced (0, 6).toFloat();
        g.setColour (grp.colour);
        g.fillRoundedRectangle (stripe, 1.5f);

        block.removeFromLeft (13);
        g.setColour (grp.colour);
        g.setFont (theme::fontMedium (12.5f));
        g.drawText (grp.title, block.removeFromTop (22), juce::Justification::centredLeft);

        for (auto& l : grp.lines)
        {
            const int h = std::max (18, wrappedHeight (theme::font (12.5f), l, getWidth() - 40));
            auto line = block.removeFromTop (h + 3);
            g.setColour (theme::textMuted);
            g.fillEllipse (line.removeFromLeft (12).withSizeKeepingCentre (4, 4).withY (line.getY() + 7).toFloat());
            line.removeFromLeft (4);
            drawWrapped (g, theme::font (12.5f), theme::textDim, l, line);
        }
    }
}
} // namespace fa::ui
