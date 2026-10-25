#include "SplMeter.h"

namespace fa::ui
{
SplMeter::SplMeter()
{
    weighting.setSelected (0);
    weighting.onChange = [this] (int i) { resetHold(); if (onWeightingChanged) onWeightingChanged (i); };
    addAndMakeVisible (weighting);

    resetButton.onClick = [this] { resetHold(); };
    addAndMakeVisible (resetButton);

    addAndMakeVisible (inputMeter);
}

void SplMeter::setLevel (double db, bool cal)
{
    idleReason = {};
    calibrated = cal;
    level = db;
    // meter ballistics: fast attack, slower release (called at ~25 fps)
    if (shown < -150.0 || db > shown) shown = db;
    else                              shown += (db - shown) * 0.35;
    if (std::isfinite (db) && db > -150.0)
    {
        maxHold = std::max (maxHold, db);
        minHold = std::min (minHold, db);
    }
    repaint();
}

void SplMeter::setInputPeak (float dbfs)
{
    inputPeak = dbfs;
    inputMeter.setLevel (dbfs);
}

void SplMeter::setIdle (const juce::String& reason)
{
    idleReason = reason;
    shown = level = -200.0;
    inputMeter.setLevel (-90.0f);
    repaint();
}

void SplMeter::resetHold()
{
    maxHold = -200.0;
    minHold = 200.0;
    repaint();
}

void SplMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    theme::fillCard (g, r.toFloat(), theme::bg2, theme::border);

    auto header = r.removeFromTop (44).reduced (theme::pad, 0);
    g.setColour (theme::text);
    g.setFont (theme::fontMedium (14.0f));
    g.drawText ("SPL", header, juce::Justification::centredLeft);

    auto body = r.reduced (theme::pad, 0);
    body.removeFromBottom (theme::pad + 10 + 8); // meter + padding

    const bool idle = idleReason.isNotEmpty() || shown < -150.0;
    const juce::String unit = calibrated ? "dB SPL" : "dBFS";

    // big number
    auto numberArea = body.removeFromTop (juce::jmax (56, body.getHeight() - 40));
    const juce::String text = idle ? juce::String ("--.-") : juce::String (shown, 1);

    const juce::Colour numColour = idle ? theme::textMuted
                                 : (inputPeak > -1.0f ? theme::bad : (inputPeak > -6.0f ? theme::warn : theme::text));

    const float size = juce::jlimit (36.0f, 64.0f, (float) numberArea.getHeight() * 0.8f);
    g.setFont (theme::display (size));
    const float numW = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text);
    g.setColour (numColour);
    auto numRect = numberArea.withSizeKeepingCentre ((int) numW + 4, numberArea.getHeight()).translated (-22, 0);
    g.drawText (text, numRect, juce::Justification::centred);

    g.setColour (theme::textDim);
    g.setFont (theme::fontMedium (12.0f));
    g.drawText (unit, numRect.getRight() + 6, numRect.getCentreY() + (int) (size * 0.08f), 60, 16, juce::Justification::centredLeft);

    if (idle)
    {
        g.setColour (theme::textMuted);
        g.setFont (theme::font (11.0f));
        g.drawFittedText (idleReason.isNotEmpty() ? idleReason : juce::String ("waiting for input"),
                          body.removeFromTop (16), juce::Justification::centred, 1);
        return;
    }

    // max / min
    auto holds = body.removeFromTop (32);
    auto drawHold = [&] (juce::Rectangle<int> area, const juce::String& label, double v, juce::Colour c)
    {
        g.setColour (theme::textMuted);
        g.setFont (theme::fontMedium (10.0f));
        g.drawText (label, area.removeFromTop (12), juce::Justification::centredLeft);
        g.setColour (c);
        g.setFont (theme::mono (15.0f));
        g.drawText (std::abs (v) < 150.0 ? juce::String (v, 1) : juce::String ("--.-"), area, juce::Justification::centredLeft);
    };
    const int half = holds.getWidth() / 2;
    drawHold (holds.removeFromLeft (half), "MAX", maxHold, theme::text);
    drawHold (holds, "MIN", minHold, theme::textDim);

    // input peak caption
    auto bottom = getLocalBounds().reduced (theme::pad, 0).removeFromBottom (theme::pad + 10 + 8).removeFromTop (12);
    g.setColour (theme::textMuted);
    g.setFont (theme::fontMedium (10.0f));
    g.drawText ("INPUT PEAK", bottom, juce::Justification::centredLeft);
    g.setColour (inputPeak > -1.0f ? theme::bad : theme::textDim);
    g.setFont (theme::mono (10.0f));
    g.drawText (juce::String (inputPeak, 1) + " dBFS", bottom, juce::Justification::centredRight);
}

void SplMeter::resized()
{
    auto r = getLocalBounds();
    auto header = r.removeFromTop (44).reduced (theme::pad, 6);
    weighting.setBounds (header.removeFromRight (108));
    header.removeFromRight (4);
    resetButton.setBounds (header.removeFromRight (76));

    auto bottom = r.reduced (theme::pad, 0).removeFromBottom (theme::pad + 10);
    inputMeter.setBounds (bottom.removeFromTop (10));
}
} // namespace fa::ui
