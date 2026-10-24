#pragma once

#include "PageKit.h"
#include "core/tune/TargetLibrary.h"

namespace fa::ui
{
/** A selectable tile showing a miniature of one target curve. */
class TargetTile : public juce::Component
{
public:
    static constexpr int height = 104;
    static constexpr int minWidth = 148;

    explicit TargetTile (const TargetLibrary::Entry& e) : entry (e)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        buildPath();
    }

    /** Special "Import..." tile. */
    TargetTile() : importTile (true)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    const TargetLibrary::Entry& getEntry() const noexcept { return entry; }
    bool isImportTile() const noexcept { return importTile; }

    void setSelected (bool b) { if (selected != b) { selected = b; repaint(); } }
    bool isSelected() const noexcept { return selected; }
    std::function<void (TargetTile&)> onClick;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);

        if (importTile)
        {
            g.setColour (hover ? theme::bg3 : theme::bg2.withAlpha (0.5f));
            g.fillRoundedRectangle (r, theme::radius);
            juce::Path outline;
            outline.addRoundedRectangle (r, theme::radius);
            g.setColour (hover ? theme::accent.withAlpha (0.7f) : theme::borderHi);
            theme::drawDashedPath (g, outline, 1.0f, 5.0f, 4.0f);

            auto c = getLocalBounds().toFloat();
            icons::draw (g, icons::Id::download, juce::Rectangle<float> (22.0f, 22.0f).withCentre ({ c.getCentreX(), c.getCentreY() - 22.0f }),
                         hover ? theme::accent : theme::textDim);
            g.setColour (hover ? theme::text : theme::textDim);
            g.setFont (theme::fontMedium (12.5f));
            g.drawText ("Import curve", getLocalBounds().withTrimmedTop (50), juce::Justification::centredTop, false);
            g.setColour (theme::textMuted);
            g.setFont (theme::font (10.5f));
            g.drawText ("REW / CSV / txt", getLocalBounds().withTrimmedTop (68), juce::Justification::centredTop, false);
            return;
        }

        if (selected)
            theme::drawGlow (g, r, theme::accent.withAlpha (0.35f), theme::radius, 10.0f);

        theme::fillCard (g, r, selected || hover ? theme::bg3 : theme::bg2,
                         selected ? theme::accent : (hover ? theme::borderHi : theme::border));

        // mini plot
        auto plot = getLocalBounds().reduced (12, 10).removeFromTop (50).toFloat();
        g.setColour (theme::border.withAlpha (0.8f));
        g.drawHorizontalLine ((int) yFor (0.0, plot), plot.getX(), plot.getRight());
        for (double f : { 100.0, 1000.0, 10000.0 })
            g.drawVerticalLine ((int) xFor (f, plot), plot.getY(), plot.getBottom());

        if (! curve.isEmpty())
        {
            auto t = juce::AffineTransform::scale (plot.getWidth(), plot.getHeight()).translated (plot.getX(), plot.getY());
            juce::Path p (curve);
            p.applyTransform (t);

            juce::Path fill (p);
            fill.lineTo (plot.getRight(), plot.getBottom());
            fill.lineTo (plot.getX(), plot.getBottom());
            fill.closeSubPath();
            g.setGradientFill (juce::ColourGradient (theme::accent.withAlpha (selected ? 0.28f : 0.14f), 0, plot.getY(),
                                                     theme::accent.withAlpha (0.0f), 0, plot.getBottom(), false));
            g.fillPath (fill);

            g.setColour (selected ? theme::accentHi : theme::curveTarget.withAlpha (0.85f));
            g.strokePath (p, juce::PathStrokeType (selected ? 1.8f : 1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        auto textArea = getLocalBounds().reduced (12, 10).withTrimmedTop (54);
        g.setColour (selected ? theme::text : theme::text.withAlpha (0.9f));
        g.setFont (theme::fontMedium (12.5f));
        g.drawText (entry.curve.name, textArea.removeFromTop (18), juce::Justification::centredLeft, true);

        juce::String sub = entry.family;
        if (! entry.bands.empty())
            sub += (sub.isEmpty() ? juce::String() : kit::sep()) + juce::String (entry.bands.size()) + " band targets";
        if (! entry.builtIn)
            sub = "Imported" + (sub.isEmpty() ? juce::String() : kit::sep() + sub);
        g.setColour (theme::textMuted);
        g.setFont (theme::font (10.5f));
        g.drawText (sub, textArea.removeFromTop (16), juce::Justification::centredLeft, true);

        if (selected)
            icons::draw (g, icons::Id::check, juce::Rectangle<float> (14.0f, 14.0f).withPosition ((float) getWidth() - 26.0f, 10.0f), theme::accent);
    }

    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit  (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (getLocalBounds().contains (e.getPosition()) && onClick)
            onClick (*this);
    }

private:
    static float xFor (double f, juce::Rectangle<float> a)
    {
        const double t = std::log10 (f / 20.0) / std::log10 (20000.0 / 20.0);
        return a.getX() + (float) t * a.getWidth();
    }
    static float yFor (double db, juce::Rectangle<float> a)
    {
        constexpr double yMin = -12.0, yMax = 14.0;
        return a.getBottom() - (float) ((db - yMin) / (yMax - yMin)) * a.getHeight();
    }

    /** Curve in normalised 0..1 coordinates (x = log freq, y = top-down dB). */
    void buildPath()
    {
        curve.clear();
        constexpr int n = 64;
        for (int i = 0; i < n; ++i)
        {
            const double t  = (double) i / (n - 1);
            const double f  = 20.0 * std::pow (1000.0, t);
            const double db = juce::jlimit (-12.0, 14.0, entry.curve.levelAt (f));
            const float x = (float) t;
            const float y = 1.0f - (float) ((db + 12.0) / 26.0);
            if (i == 0) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
        }
    }

    TargetLibrary::Entry entry;
    juce::Path curve;
    bool importTile = false, selected = false, hover = false;
};
} // namespace fa::ui
