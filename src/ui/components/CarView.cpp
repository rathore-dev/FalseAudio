#include "Plots.h"

/*  CarView: top-down car silhouette.

    The car is modelled in a normalised space: x in -0.5..0.5 (left..right), y in 0..2.1
    (front..rear). Everything is drawn through one AffineTransform that fits that box into the
    component, so the layout scales from a 120 px thumbnail to a full-page view.             */

namespace fa::ui
{
namespace
{
    constexpr float kCarW = 1.0f, kCarL = 2.1f;

    juce::Path makeBody()
    {
        juce::Path p;
        p.startNewSubPath (-0.30f, 0.02f);
        p.cubicTo (-0.10f, -0.02f, 0.10f, -0.02f, 0.30f, 0.02f);     // front bumper
        p.cubicTo (0.43f, 0.04f, 0.475f, 0.14f, 0.485f, 0.32f);     // front right corner
        p.cubicTo (0.50f, 0.62f, 0.50f, 1.50f, 0.485f, 1.86f);      // right flank
        p.cubicTo (0.475f, 2.03f, 0.40f, 2.085f, 0.28f, 2.09f);     // rear right corner
        p.cubicTo (0.10f, 2.11f, -0.10f, 2.11f, -0.28f, 2.09f);     // rear bumper
        p.cubicTo (-0.40f, 2.085f, -0.475f, 2.03f, -0.485f, 1.86f);
        p.cubicTo (-0.50f, 1.50f, -0.50f, 0.62f, -0.485f, 0.32f);
        p.cubicTo (-0.475f, 0.14f, -0.43f, 0.04f, -0.30f, 0.02f);
        p.closeSubPath();
        return p;
    }

    juce::Path makeCabinGlass()      // windshield + roof + rear window as one rounded cabin
    {
        juce::Path p;
        p.startNewSubPath (-0.30f, 0.56f);
        p.cubicTo (-0.10f, 0.52f, 0.10f, 0.52f, 0.30f, 0.56f);      // windshield top (dash edge)
        p.cubicTo (0.37f, 0.58f, 0.41f, 0.70f, 0.43f, 0.84f);       // A-pillar
        p.lineTo (0.43f, 1.56f);                                    // roof side
        p.cubicTo (0.42f, 1.68f, 0.36f, 1.76f, 0.27f, 1.79f);       // C-pillar
        p.cubicTo (0.10f, 1.82f, -0.10f, 1.82f, -0.27f, 1.79f);     // rear window bottom
        p.cubicTo (-0.36f, 1.76f, -0.42f, 1.68f, -0.43f, 1.56f);
        p.lineTo (-0.43f, 0.84f);
        p.cubicTo (-0.41f, 0.70f, -0.37f, 0.58f, -0.30f, 0.56f);
        p.closeSubPath();
        return p;
    }

    juce::Path makeRoof()
    {
        juce::Path p;
        p.addRoundedRectangle (-0.40f, 0.86f, 0.80f, 0.70f, 0.09f);
        return p;
    }

    void addSeat (juce::Path& seats, juce::Path& headrests, float cx, float cy, float w)
    {
        seats.addRoundedRectangle (cx - w * 0.5f, cy - 0.12f, w, 0.27f, 0.045f);
        headrests.addRoundedRectangle (cx - w * 0.28f, cy - 0.16f, w * 0.56f, 0.065f, 0.025f);
    }

    juce::String roleTag (DriverRole r)
    {
        switch (r)
        {
            case DriverRole::subwoofer: return "SUB";
            case DriverRole::midbass:   return "MB";
            case DriverRole::midrange:  return "MID";
            case DriverRole::tweeter:   return "TW";
            case DriverRole::fullrange: return "FR";
        }
        return {};
    }
}

CarView::CarView()
{
    setOpaque (false);
}

void CarView::setConfig (const SystemConfig& cfg)
{
    config = cfg;
    repaint();
}

void CarView::setHighlight (const juce::String& channelId)
{
    highlight = channelId;
    repaint();
}

void CarView::setArrivals (std::map<juce::String, double> arrivalMs)
{
    arrivals = std::move (arrivalMs);
    repaint();
}

void CarView::setSeat (int s)
{
    seat = juce::jlimit (0, 2, s);
    repaint();
}

void CarView::setRightHandDrive (bool rhd)
{
    rightHandDrive = rhd;
    repaint();
}

juce::AffineTransform CarView::carTransform() const
{
    auto b = getLocalBounds().toFloat().reduced (8.0f);
    if (b.getWidth() > b.getHeight() * 1.25f)
    {
        // landscape host: car drives to the right, its left side at the top
        const float scale = std::min (b.getWidth() / (kCarL * 1.06f), b.getHeight() / (kCarW * 1.3f));
        return juce::AffineTransform::scale (scale)
                    .rotated (juce::MathConstants<float>::halfPi)
                    .translated (b.getCentreX() + kCarL * 0.5f * scale, b.getCentreY());
    }
    const float scale = std::min (b.getWidth() / (kCarW * 1.25f), b.getHeight() / (kCarL * 1.06f));
    return juce::AffineTransform::scale (scale)
                .translated (b.getCentreX(), b.getCentreY() - kCarL * 0.5f * scale);
}

juce::Point<float> CarView::seatPosition() const
{
    const float driverX = rightHandDrive ? 0.22f : -0.22f;
    const float x = seat == 0 ? driverX : seat == 1 ? -driverX : 0.0f;
    return { x, 1.0f };
}

std::map<juce::String, juce::Point<float>> CarView::speakerPositions() const
{
    std::map<juce::String, juce::Point<float>> out;
    std::map<juce::String, int> slotUse;   // offsets channels that land on the same spot

    for (auto& ch : config.channels)
    {
        const float sx = ch.side == Side::left ? -1.0f : ch.side == Side::right ? 1.0f : 0.0f;
        const juce::String idLower = ch.id.toLowerCase();
        juce::Point<float> p;
        juce::String slot;

        if (ch.role == DriverRole::subwoofer)
        {
            p = { ch.side == Side::mono ? 0.0f : 0.24f * sx, 1.95f };
            slot = "sub" + juce::String ((int) ch.side);
        }
        else if (ch.role == DriverRole::tweeter)
        {
            p = { 0.405f * sx, 0.60f };                                 // A-pillar base / sail panel
            slot = "tw" + juce::String ((int) ch.side);
        }
        else if (ch.role == DriverRole::midrange)
        {
            p = { 0.285f * sx, 0.47f };                                 // dash corner
            slot = "mid" + juce::String ((int) ch.side);
        }
        else if (ch.role == DriverRole::midbass && idLower.contains ("kick"))
        {
            p = { 0.36f * sx, 0.72f };                                  // kick panel
            slot = "kick" + juce::String ((int) ch.side);
        }
        else if (ch.role == DriverRole::midbass)
        {
            p = { 0.445f * sx, 0.97f };                                 // front door, low
            slot = "mb" + juce::String ((int) ch.side);
        }
        else // fullrange: front door mid height
        {
            p = { 0.445f * sx, 0.86f };
            slot = "fr" + juce::String ((int) ch.side);
        }

        if (idLower.startsWith ("rl") || idLower.startsWith ("rr") || idLower.contains ("rear"))
        {
            p = { 0.445f * sx, 1.40f };                                 // rear doors
            slot = "rear" + juce::String ((int) ch.side);
        }

        const int n = slotUse[slot]++;
        if (n > 0)
            p.y += 0.09f * (float) n;
        out[ch.id] = p;
    }
    return out;
}

void CarView::paint (juce::Graphics& g)
{
    const auto tf = carTransform();
    const float scale = std::sqrt (std::abs (tf.getDeterminant()));
    if (scale < 10.0f)
        return;

    // ---- body shadow / ambient glow
    {
        auto body = makeBody();
        body.applyTransform (tf);
        auto bb = body.getBounds();
        theme::drawGlow (g, bb, theme::accent, bb.getWidth() * 0.3f, 10.0f);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillPath (body, juce::AffineTransform::translation (0.0f, 3.0f));

        // body
        juce::ColourGradient grad (theme::bg3.brighter (0.08f), bb.getX(), bb.getY(), theme::bg2, bb.getRight(), bb.getBottom(), false);
        g.setGradientFill (grad);
        g.fillPath (body);
        g.setColour (theme::borderHi);
        g.strokePath (body, juce::PathStrokeType (1.2f));
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.strokePath (body, juce::PathStrokeType (1.0f), juce::AffineTransform::translation (0.0f, 1.0f));
    }

    // ---- wheels (drawn on top but half tucked under the body outline)
    {
        juce::Path wheels;
        for (float sx : { -1.0f, 1.0f })
        {
            wheels.addRoundedRectangle (sx * 0.505f - 0.045f, 0.28f, 0.09f, 0.26f, 0.035f);
            wheels.addRoundedRectangle (sx * 0.505f - 0.045f, 1.60f, 0.09f, 0.26f, 0.035f);
        }
        wheels.applyTransform (tf);
        g.setColour (theme::bg0);
        g.fillPath (wheels);
        g.setColour (theme::border);
        g.strokePath (wheels, juce::PathStrokeType (1.0f));
    }

    // ---- mirrors
    {
        juce::Path m;
        m.addRoundedRectangle (-0.56f, 0.58f, 0.075f, 0.05f, 0.015f);
        m.addRoundedRectangle (0.485f, 0.58f, 0.075f, 0.05f, 0.015f);
        m.applyTransform (tf);
        g.setColour (theme::bg4);
        g.fillPath (m);
        g.setColour (theme::borderHi);
        g.strokePath (m, juce::PathStrokeType (1.0f));
    }

    // ---- cabin glass + roof
    {
        auto glass = makeCabinGlass();
        glass.applyTransform (tf);
        auto gb = glass.getBounds();
        juce::ColourGradient grad (theme::accent.withAlpha (0.16f), gb.getCentreX(), gb.getY(),
                                   theme::accent.withAlpha (0.05f), gb.getCentreX(), gb.getBottom(), false);
        g.setGradientFill (grad);
        g.fillPath (glass);
        g.setColour (theme::accent.withAlpha (0.22f));
        g.strokePath (glass, juce::PathStrokeType (1.0f));

        auto roof = makeRoof();
        roof.applyTransform (tf);
        g.setColour (theme::bg2.withAlpha (0.92f));
        g.fillPath (roof);
        g.setColour (theme::border);
        g.strokePath (roof, juce::PathStrokeType (1.0f));
    }

    // ---- dashboard hint: a glowing line just behind the windshield base
    {
        juce::Path dash;
        dash.startNewSubPath (-0.40f, 0.60f);
        dash.cubicTo (-0.15f, 0.56f, 0.15f, 0.56f, 0.40f, 0.60f);
        dash.applyTransform (tf);
        g.setColour (theme::accent.withAlpha (0.12f));
        g.strokePath (dash, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (theme::accent.withAlpha (0.55f));
        g.strokePath (dash, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // ---- door seams
    {
        juce::Path seams;
        for (float sx : { -1.0f, 1.0f })
        {
            for (float y : { 0.64f, 1.14f, 1.62f })
            {
                seams.startNewSubPath (sx * 0.50f, y);
                seams.lineTo (sx * 0.43f, y);
            }
            // handles
            seams.addRoundedRectangle (sx * 0.47f - 0.012f, 0.98f, 0.024f, 0.07f, 0.01f);
            seams.addRoundedRectangle (sx * 0.47f - 0.012f, 1.46f, 0.024f, 0.07f, 0.01f);
        }
        seams.applyTransform (tf);
        g.setColour (theme::borderHi.withAlpha (0.9f));
        g.strokePath (seams, juce::PathStrokeType (1.0f));
    }

    // ---- seats
    {
        juce::Path seats, heads;
        addSeat (seats, heads, -0.22f, 1.00f, 0.26f);
        addSeat (seats, heads,  0.22f, 1.00f, 0.26f);
        seats.addRoundedRectangle (-0.37f, 1.36f, 0.74f, 0.22f, 0.045f);   // rear bench
        heads.addRoundedRectangle (-0.33f, 1.33f, 0.16f, 0.06f, 0.025f);
        heads.addRoundedRectangle ( 0.17f, 1.33f, 0.16f, 0.06f, 0.025f);
        seats.applyTransform (tf);
        heads.applyTransform (tf);
        g.setColour (theme::bg4.withAlpha (0.85f));
        g.fillPath (seats);
        g.setColour (theme::borderHi.withAlpha (0.8f));
        g.strokePath (seats, juce::PathStrokeType (1.0f));
        g.setColour (theme::bg4.brighter (0.15f));
        g.fillPath (heads);
    }

    const auto positions = speakerPositions();

    // ---- listening position (head with ears)
    {
        const auto sp = seatPosition().transformedBy (tf);
        const float r = 0.065f * scale;
        g.setColour (theme::accent.withAlpha (0.10f));
        g.fillEllipse (sp.x - r * 2.6f, sp.y - r * 2.6f, r * 5.2f, r * 5.2f);
        g.setColour (theme::accent.withAlpha (0.18f));
        g.fillEllipse (sp.x - r * 1.7f, sp.y - r * 1.7f, r * 3.4f, r * 3.4f);
        g.setColour (theme::accent.withAlpha (0.35f));
        g.drawEllipse (sp.x - r * 1.7f, sp.y - r * 1.7f, r * 3.4f, r * 3.4f, 1.0f);
        // ears (placed in car space so they follow the car's orientation)
        g.setColour (theme::accent.withAlpha (0.9f));
        for (float side : { -1.0f, 1.0f })
        {
            const auto ear = (seatPosition() + juce::Point<float> (side * 0.085f, 0.0f)).transformedBy (tf);
            g.fillEllipse (ear.x - r * 0.32f, ear.y - r * 0.32f, r * 0.64f, r * 0.64f);
        }
        // head
        g.setColour (theme::bg1);
        g.fillEllipse (sp.x - r, sp.y - r, r * 2.0f, r * 2.0f);
        g.setColour (theme::accent);
        g.drawEllipse (sp.x - r, sp.y - r, r * 2.0f, r * 2.0f, 1.6f);
        g.setColour (theme::accent.withAlpha (0.9f));
        g.fillEllipse (sp.x - r * 0.28f, sp.y - r * 0.28f, r * 0.56f, r * 0.56f);
    }

    // ---- arrival rings (concentric wavefronts scaled so the latest arrival reaches ~0.45 car widths)
    if (! arrivals.empty())
    {
        double maxMs = 0.0;
        for (auto& [id, ms] : arrivals) maxMs = std::max (maxMs, ms);
        const float unit = maxMs > 1.0e-6 ? 0.32f * scale / (float) maxMs : 0.0f;
        for (auto& [id, ms] : arrivals)
        {
            auto it = positions.find (id);
            auto ch = config.find (id);
            if (it == positions.end() || ch == nullptr || ms <= 0.0) continue;
            const auto c = it->second.transformedBy (tf);
            const auto col = theme::sideTint (theme::roleColour (ch->role), ch->side);
            const bool lit = highlight.isEmpty() || highlight == id || hover == id;
            const float rr = (float) ms * unit;
            for (int k = 3; k >= 1; --k)
            {
                const float r = rr * (float) k / 3.0f;
                g.setColour (col.withAlpha ((lit ? 0.34f : 0.10f) * (k == 3 ? 1.0f : 0.5f)));
                g.drawEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f, k == 3 ? 1.5f : 1.0f);
            }
            if (lit)
            {
                // label sits just outside the outer ring, on the outboard side of the car
                const float outward = it->second.x < -0.01f ? -1.0f : it->second.x > 0.01f ? 1.0f : 0.0f;
                juce::Point<float> dir (outward, outward == 0.0f ? 1.0f : 0.0f);
                dir.applyTransform (tf.withAbsoluteTranslation (0.0f, 0.0f));
                if (dir.getDistanceFromOrigin() > 0.0f) dir /= dir.getDistanceFromOrigin();
                const auto text = juce::String (ms, 2) + " ms";
                const auto font = theme::mono (10.0f);
                const float w = juce::GlyphArrangement::getStringWidth (font, text) + 10.0f;
                const auto anchor = c + dir * (rr + 10.0f);
                juce::Rectangle<float> tag (w, 15.0f);
                tag.setCentre (anchor + juce::Point<float> (dir.x * w * 0.5f, dir.y * 7.5f));
                tag.setX (juce::jlimit (2.0f, std::max (2.0f, (float) getWidth() - w - 2.0f), tag.getX()));
                tag.setY (juce::jlimit (2.0f, std::max (2.0f, (float) getHeight() - 17.0f), tag.getY()));
                g.setColour (theme::bg0.withAlpha (0.85f));
                g.fillRoundedRectangle (tag, 4.0f);
                g.setColour (col);
                g.setFont (font);
                g.drawText (text, tag, juce::Justification::centred, false);
            }
        }
    }

    // ---- speakers
    const float sr = juce::jlimit (5.0f, 14.0f, 0.052f * scale);
    for (auto& ch : config.channels)
    {
        auto it = positions.find (ch.id);
        if (it == positions.end()) continue;
        const auto c = it->second.transformedBy (tf);
        const auto col = theme::sideTint (theme::roleColour (ch.role), ch.side);
        const bool isHi = highlight == ch.id;
        const bool isHover = hover == ch.id;
        const bool dim = highlight.isNotEmpty() && ! isHi && ! isHover;
        const float r = isHi || isHover ? sr * 1.15f : sr;

        if (isHi || isHover)
        {
            g.setColour (col.withAlpha (0.16f));
            g.fillEllipse (c.x - r * 2.8f, c.y - r * 2.8f, r * 5.6f, r * 5.6f);
            g.setColour (col.withAlpha (0.30f));
            g.fillEllipse (c.x - r * 1.9f, c.y - r * 1.9f, r * 3.8f, r * 3.8f);
        }
        else
        {
            g.setColour (col.withAlpha (dim ? 0.06f : 0.16f));
            g.fillEllipse (c.x - r * 1.8f, c.y - r * 1.8f, r * 3.6f, r * 3.6f);
        }

        // driver: outer cone ring + dust cap
        g.setColour (theme::bg0);
        g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
        g.setColour (col.withAlpha (dim ? 0.45f : 1.0f));
        g.drawEllipse (c.x - r + 0.75f, c.y - r + 0.75f, r * 2.0f - 1.5f, r * 2.0f - 1.5f, isHi ? 2.0f : 1.5f);
        g.setColour (col.withAlpha (dim ? 0.35f : 0.9f));
        g.fillEllipse (c.x - r * 0.42f, c.y - r * 0.42f, r * 0.84f, r * 0.84f);
        if (isHi)
        {
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.fillEllipse (c.x - r * 0.18f, c.y - r * 0.18f, r * 0.36f, r * 0.36f);
        }

        // role tag next to the speaker, outboard (direction follows the car's orientation);
        // skipped when an arrival label is shown for this speaker to keep the area clean
        const bool hasArrivalLabel = arrivals.count (ch.id) > 0 && (highlight.isEmpty() || isHi || isHover);
        if (scale > 110.0f && ! hasArrivalLabel)
        {
            const float outward = it->second.x < -0.01f ? -1.0f : it->second.x > 0.01f ? 1.0f : 0.0f;
            juce::Point<float> dir (outward, outward == 0.0f ? 1.0f : 0.0f);   // sub: towards the rear
            dir.applyTransform (tf.withAbsoluteTranslation (0.0f, 0.0f));
            if (dir.getDistanceFromOrigin() > 0.0f) dir /= dir.getDistanceFromOrigin();
            const auto tag = roleTag (ch.role);
            g.setFont (theme::fontMedium (9.0f));
            g.setColour (col.withAlpha (dim ? 0.35f : 0.85f));
            const auto anchor = c + dir * (r + 7.0f);
            juce::Rectangle<float> tr (30.0f, 12.0f);
            juce::Justification just = juce::Justification::centred;
            if (std::abs (dir.x) > std::abs (dir.y))
            {
                tr.setPosition (dir.x < 0 ? anchor.x - 30.0f : anchor.x, anchor.y - 6.0f);
                just = dir.x < 0 ? juce::Justification::centredRight : juce::Justification::centredLeft;
            }
            else
            {
                tr.setPosition (anchor.x - 15.0f, dir.y < 0 ? anchor.y - 12.0f : anchor.y);
            }
            g.drawText (tag, tr, just, false);
        }
    }

    // ---- hover tooltip
    if (hover.isNotEmpty())
    {
        if (auto* ch = config.find (hover))
        {
            auto it = positions.find (hover);
            if (it != positions.end())
            {
                const auto c = it->second.transformedBy (tf);
                const auto col = theme::sideTint (theme::roleColour (ch->role), ch->side);
                juce::String line2 = "Out " + juce::String (ch->dspOutput + 1);
                if (auto a = arrivals.find (hover); a != arrivals.end())
                    line2 += "   " + juce::String (a->second, 2) + " ms";
                const auto f1 = theme::fontMedium (12.0f), f2 = theme::mono (10.5f);
                const float w = std::max (juce::GlyphArrangement::getStringWidth (f1, ch->label),
                                          juce::GlyphArrangement::getStringWidth (f2, line2)) + 38.0f;
                juce::Rectangle<float> card (c.x + 14.0f, c.y - 20.0f, w, 40.0f);
                if (card.getRight() > getWidth() - 4.0f) card.setX (c.x - 14.0f - w);
                card.setY (juce::jlimit (4.0f, std::max (4.0f, getHeight() - 44.0f), card.getY()));
                theme::drawGlow (g, card, juce::Colours::black, 7.0f, 6.0f);
                theme::fillCard (g, card, theme::bg3.withAlpha (0.97f), col.withAlpha (0.5f), 7.0f);
                auto inner = card.reduced (11.0f, 5.0f);
                g.setColour (col);
                g.fillEllipse (inner.getX(), inner.getY() + 5.0f, 7.0f, 7.0f);
                g.setColour (theme::text);
                g.setFont (f1);
                g.drawText (ch->label, inner.removeFromTop (16.0f).withTrimmedLeft (12.0f), juce::Justification::centredLeft, true);
                g.setColour (theme::textDim);
                g.setFont (f2);
                g.drawText (line2, inner.withTrimmedLeft (12.0f), juce::Justification::centredLeft, true);
            }
        }
    }
}

juce::String CarView::channelAt (juce::Point<float> p) const
{
    const auto tf = carTransform();
    const float scale = std::sqrt (std::abs (tf.getDeterminant()));
    const float hit = std::max (12.0f, 0.07f * scale);
    juce::String best;
    float bestD = hit;
    for (auto& [id, pos] : speakerPositions())
    {
        const float d = p.getDistanceFrom (pos.transformedBy (tf));
        if (d < bestD) { bestD = d; best = id; }
    }
    return best;
}

void CarView::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasDraggedSinceMouseDown())
        return;
    const auto id = channelAt (e.position);
    if (id.isNotEmpty() && onChannelClicked)
        onChannelClicked (id);
}

void CarView::mouseMove (const juce::MouseEvent& e)
{
    const auto id = channelAt (e.position);
    if (id != hover)
    {
        hover = id;
        setMouseCursor (hover.isNotEmpty() ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void CarView::mouseExit (const juce::MouseEvent&)
{
    if (hover.isNotEmpty())
    {
        hover.clear();
        repaint();
    }
}
} // namespace fa::ui
