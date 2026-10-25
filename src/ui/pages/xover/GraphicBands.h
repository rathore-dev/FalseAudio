#pragma once

#include "../../Page.h"
#include "../../components/Widgets.h"
#include "core/tune/GraphicEQ.h"

namespace fa::ui
{
/** Fixed-frequency graphic EQ: one vertical fader per centre, gain quantised to the DSP step and
    limited to its boost/cut range. Drag to set, double-click to zero a band. */
class GraphicBands : public juce::Component
{
public:
    GraphicBands() { setWantsKeyboardFocus (false); }

    void setBands (const std::vector<FilterSpec>& peq, const DspCapabilities& c, juce::Colour col)
    {
        caps = c;
        accent = col;
        centres = caps.graphicCentres();
        gains = GraphicEQ::gainsOf (caps, peq);
        gains.resize (centres.size(), 0.0);
        repaint();
    }

    /** Called continuously while dragging with the full filter list (one per centre). */
    std::function<void (const std::vector<FilterSpec>&, bool finished)> onChanged;
    std::function<void()> onResetAll;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        theme::fillCard (g, r, theme::bg2, theme::border);
        auto head = getLocalBounds().removeFromTop (headH).reduced (theme::pad, 0);
        g.setColour (theme::text);
        g.setFont (theme::fontMedium (13.0f));
        g.drawFittedText ("Graphic EQ  -  " + juce::String ((int) centres.size()) + " bands, step "
                              + juce::String (caps.graphicGainStepDb, 1) + " dB, "
                              + juce::String (juce::roundToInt (caps.graphicMaxCutDb)) + " / +" + juce::String (juce::roundToInt (caps.graphicMaxBoostDb)) + " dB",
                          head.withTrimmedRight (110), juce::Justification::centredLeft, 1);

        const auto area = faderArea();
        if (centres.empty() || area.isEmpty()) return;
        const float w = area.getWidth() / (float) centres.size();
        const float zeroY = yFor (0.0);

        g.setColour (theme::border);
        g.fillRect (area.getX(), zeroY, area.getWidth(), 1.0f);

        for (size_t i = 0; i < centres.size(); ++i)
        {
            const float cx = area.getX() + w * ((float) i + 0.5f);
            g.setColour (theme::border.withAlpha (0.6f));
            g.fillRect (cx - 1.0f, area.getY(), 2.0f, area.getHeight());
            const float y = yFor (gains[i]);
            const bool active = std::abs (gains[i]) > 1e-6;
            g.setColour (active ? accent.withAlpha (0.8f) : theme::textMuted.withAlpha (0.4f));
            g.fillRect (cx - 1.5f, juce::jmin (y, zeroY), 3.0f, std::abs (y - zeroY));
            g.setColour (active ? accent : theme::textMuted);
            g.fillRoundedRectangle (cx - juce::jmin (9.0f, w * 0.4f), y - 4.0f, juce::jmin (18.0f, w * 0.8f), 8.0f, 3.0f);

            if (w >= 22.0f || i % 2 == 0)
            {
                g.setColour (theme::textMuted);
                g.setFont (theme::fontMedium (10.5f));
                g.drawFittedText (freqLabel (centres[i]), juce::Rectangle<int> ((int) (cx - w / 2), (int) area.getBottom() + 2, (int) w, 14),
                                  juce::Justification::centred, 1);
            }
            if (active && w >= 20.0f)
            {
                g.setColour (theme::text);
                g.setFont (theme::fontMedium (10.0f));
                g.drawFittedText (juce::String (gains[i], 1), juce::Rectangle<int> ((int) (cx - w / 2), (int) (y - 20), (int) w, 12),
                                  juce::Justification::centred, 1);
            }
        }
    }

    void resized() override
    {
        resetButton.setBounds (getLocalBounds().removeFromTop (headH).removeFromRight (110).reduced (4, 8));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragIndex = indexAt (e.position);
        if (dragIndex >= 0) setFromY (dragIndex, e.position.y, false);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragIndex >= 0) setFromY (dragIndex, e.position.y, false);
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (dragIndex >= 0) setFromY (dragIndex, e.position.y, true);
        dragIndex = -1;
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int i = indexAt (e.position);
        if (i >= 0) { gains[(size_t) i] = 0.0; emit (true); repaint(); }
    }

    static constexpr int headH = 44;

private:
    DspCapabilities caps;
    std::vector<double> centres, gains;
    juce::Colour accent = theme::accent;
    int dragIndex = -1;
    FaButton resetButton { "Flatten", FaButton::Style::ghost, icons::Id::refresh };

    struct Init { Init (GraphicBands& b) { b.addAndMakeVisible (b.resetButton); b.resetButton.onClick = [&b] { if (b.onResetAll) b.onResetAll(); }; } };
    Init init { *this };

    juce::Rectangle<float> faderArea() const
    {
        return getLocalBounds().toFloat().withTrimmedTop ((float) headH + 22.0f).withTrimmedBottom (22.0f)
                   .reduced ((float) theme::pad, 0.0f);
    }
    float yFor (double db) const
    {
        const auto a = faderArea();
        const double lo = juce::jmin (-1.0, caps.graphicMaxCutDb), hi = juce::jmax (1.0, caps.graphicMaxBoostDb);
        return a.getBottom() - (float) ((db - lo) / (hi - lo)) * a.getHeight();
    }
    int indexAt (juce::Point<float> p) const
    {
        const auto a = faderArea();
        if (centres.empty() || a.isEmpty()) return -1;
        const float w = a.getWidth() / (float) centres.size();
        return juce::jlimit (0, (int) centres.size() - 1, (int) ((p.x - a.getX()) / w));
    }
    void setFromY (int i, float y, bool finished)
    {
        const auto a = faderArea();
        const double lo = juce::jmin (-1.0, caps.graphicMaxCutDb), hi = juce::jmax (1.0, caps.graphicMaxBoostDb);
        const double db = lo + (a.getBottom() - y) / a.getHeight() * (hi - lo);
        gains[(size_t) i] = GraphicEQ::quantise (caps, db, caps.graphicMaxCutDb, caps.graphicMaxBoostDb);
        emit (finished);
        repaint();
    }
    void emit (bool finished)
    {
        if (onChanged) onChanged (GraphicEQ::makeGraphic (caps, gains), finished);
    }
    static juce::String freqLabel (double f)
    {
        return f >= 1000.0 ? juce::String (f / 1000.0, f >= 10000.0 ? 0 : (std::abs (f / 1000.0 - std::round (f / 1000.0)) < 0.01 ? 0 : 1)) + "k"
                           : juce::String ((int) std::round (f));
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GraphicBands)
};
} // namespace fa::ui
