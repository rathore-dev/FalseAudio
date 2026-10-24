#include "Plots.h"

namespace fa::ui
{
namespace
{
    constexpr float kStartAngle = -2.35619449f;   // -135 deg: gauge opens at the bottom
    constexpr float kSweep      =  4.71238898f;   // 270 deg
}

ScoreRing::ScoreRing()
{
    setOpaque (false);
    setInterceptsMouseClicks (false, false);
}

void ScoreRing::setScore (int score, bool animate)
{
    target = (float) juce::jlimit (0, 100, score);
    if (! animate)
    {
        shown = target;
        stopTimer();
        repaint();
        return;
    }
    if (! isTimerRunning())
        startTimerHz (60);
}

void ScoreRing::setCaption (const juce::String& c)
{
    caption = c;
    repaint();
}

void ScoreRing::timerCallback()
{
    const float diff = target - shown;
    if (std::abs (diff) < 0.05f)
    {
        shown = target;
        stopTimer();
    }
    else
    {
        shown += diff * 0.10f;
    }
    repaint();
}

void ScoreRing::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    const bool hasCaption = caption.isNotEmpty();
    const float captionH = hasCaption ? 18.0f : 0.0f;

    const float size = std::min (bounds.getWidth(), bounds.getHeight() - captionH);
    if (size < 24.0f)
        return;

    juce::Rectangle<float> ring (size, size);
    ring.setCentre (bounds.getCentreX(), bounds.getCentreY() - captionH * 0.5f);

    const float thickness = juce::jlimit (4.0f, 18.0f, size * 0.085f);
    const float radius = size * 0.5f - thickness * 0.5f - size * 0.06f;
    const auto centre = ring.getCentre();

    const int scoreNow = juce::roundToInt (shown);
    const juce::Colour colour = theme::scoreColour (juce::roundToInt (target));
    const float t = shown / 100.0f;

    // ---- soft halo behind the whole gauge
    {
        juce::ColourGradient halo (colour.withAlpha (0.10f * t + 0.03f), centre.x, centre.y,
                                   colour.withAlpha (0.0f), centre.x, centre.y + radius * 1.35f, true);
        g.setGradientFill (halo);
        g.fillEllipse (ring.expanded (size * 0.05f));
    }

    // ---- track
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, kStartAngle, kStartAngle + kSweep, true);
    g.setColour (theme::bg3);
    g.strokePath (track, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (theme::borderHi.withAlpha (0.5f));
    g.strokePath (track, juce::PathStrokeType (1.0f));

    // ---- tick marks on the track (every 10 points)
    for (int i = 0; i <= 10; ++i)
    {
        const float a = kStartAngle + kSweep * (float) i / 10.0f;
        const float r0 = radius + thickness * 0.5f + 3.0f, r1 = r0 + (i % 5 == 0 ? 5.0f : 3.0f);
        const juce::Point<float> p0 (centre.x + r0 * std::sin (a), centre.y - r0 * std::cos (a));
        const juce::Point<float> p1 (centre.x + r1 * std::sin (a), centre.y - r1 * std::cos (a));
        g.setColour ((float) i / 10.0f <= t + 0.001f ? colour.withAlpha (0.55f) : theme::border);
        g.drawLine ({ p0, p1 }, i % 5 == 0 ? 1.5f : 1.0f);
    }

    // ---- value arc with gradient + glow
    if (t > 0.002f)
    {
        const float endAngle = kStartAngle + kSweep * t;
        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, kStartAngle, endAngle, true);

        const juce::PathStrokeType stroke (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        g.setColour (colour.withAlpha (0.18f));
        g.strokePath (arc, juce::PathStrokeType (thickness + 8.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (colour.withAlpha (0.10f));
        g.strokePath (arc, juce::PathStrokeType (thickness + 16.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // gradient runs from the start of the sweep (dim) to the tip (bright)
        const juce::Point<float> gStart (centre.x + radius * std::sin (kStartAngle), centre.y - radius * std::cos (kStartAngle));
        const juce::Point<float> gEnd   (centre.x + radius * std::sin (endAngle),    centre.y - radius * std::cos (endAngle));
        juce::ColourGradient grad (colour.darker (0.45f).withSaturation (0.7f), gStart.x, gStart.y, colour.brighter (0.15f), gEnd.x, gEnd.y, false);
        grad.addColour (0.5, colour);
        g.setGradientFill (grad);
        g.strokePath (arc, stroke);

        // inner highlight line for a glassy feel
        juce::Path hi;
        hi.addCentredArc (centre.x, centre.y, radius + thickness * 0.28f, radius + thickness * 0.28f, 0.0f, kStartAngle, endAngle, true);
        g.setColour (juce::Colours::white.withAlpha (0.16f));
        g.strokePath (hi, juce::PathStrokeType (1.0f));

        // tip dot
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.fillEllipse (gEnd.x - thickness * 0.22f, gEnd.y - thickness * 0.22f, thickness * 0.44f, thickness * 0.44f);
        g.setColour (colour.withAlpha (0.35f));
        g.fillEllipse (gEnd.x - thickness * 0.6f, gEnd.y - thickness * 0.6f, thickness * 1.2f, thickness * 1.2f);
    }

    // ---- big number
    const float numSize = size * 0.30f;
    g.setFont (theme::display (numSize));
    g.setColour (theme::text);
    juce::Rectangle<float> numArea (centre.x - radius, centre.y - numSize * 0.62f, radius * 2.0f, numSize * 1.1f);
    g.drawText (juce::String (scoreNow), numArea, juce::Justification::centred, false);

    // "/100" tag
    g.setFont (theme::font (std::max (9.0f, size * 0.07f)));
    g.setColour (theme::textMuted);
    g.drawText ("/ 100", juce::Rectangle<float> (centre.x - radius, numArea.getBottom() - numSize * 0.08f, radius * 2.0f, size * 0.1f),
                juce::Justification::centred, false);

    // ---- caption
    if (hasCaption)
    {
        g.setFont (theme::fontMedium (std::max (10.0f, std::min (13.0f, size * 0.09f))));
        g.setColour (theme::textDim);
        g.drawText (caption, juce::Rectangle<float> (bounds.getX(), ring.getBottom() - size * 0.10f, bounds.getWidth(), captionH + 4.0f),
                    juce::Justification::centred, true);
    }
}
} // namespace fa::ui
