#include "GeneratorPanel.h"

namespace fa::ui
{
namespace
{
    int indexFor (GeneratorSettings::Signal s)
    {
        switch (s)
        {
            case GeneratorSettings::Signal::off:          return 0;
            case GeneratorSettings::Signal::pinkPeriodic: return 1;
            case GeneratorSettings::Signal::pinkRandom:   return 2;
            case GeneratorSettings::Signal::whiteNoise:   return 3;
            case GeneratorSettings::Signal::sine:         return 4;
        }
        return 0;
    }

    GeneratorSettings::Signal signalFor (int i)
    {
        switch (i)
        {
            case 1:  return GeneratorSettings::Signal::pinkPeriodic;
            case 2:  return GeneratorSettings::Signal::pinkRandom;
            case 3:  return GeneratorSettings::Signal::whiteNoise;
            case 4:  return GeneratorSettings::Signal::sine;
            default: return GeneratorSettings::Signal::off;
        }
    }

    int indexFor (Feed f)   { return f == Feed::left ? 0 : (f == Feed::right ? 1 : 2); }
    Feed feedFor (int i)    { return i == 0 ? Feed::left : (i == 1 ? Feed::right : Feed::both); }
}

GeneratorPanel::GeneratorPanel()
{
    signal.onChange = [this] (int) { updateEnablement(); emit(); };
    addAndMakeVisible (signal);

    level.setValue (-20.0);
    level.onChange = [this] (double) { emit(); };
    addAndMakeVisible (level);

    sineHz.setValue (1000.0);
    sineHz.onChange = [this] (double) { emit(); };
    addAndMakeVisible (sineHz);

    feed.setSelected (2);
    feed.onChange = [this] (int) { emit(); };
    addAndMakeVisible (feed);

    addAndMakeVisible (outputMeter);

    stopButton.setHero (true);
    stopButton.setTooltip ("Silence the generator immediately");
    stopButton.onClick = [this]
    {
        signal.setSelected (0);
        updateEnablement();
        emit();
    };
    addAndMakeVisible (stopButton);

    updateEnablement();
}

void GeneratorPanel::setSettings (const GeneratorSettings& s)
{
    signal.setSelected (indexFor (s.signal));
    level.setValue (s.levelDbFS);
    sineHz.setValue (s.sineHz);
    feed.setSelected (indexFor (s.feed));
    updateEnablement();
}

GeneratorSettings GeneratorPanel::getSettings() const
{
    GeneratorSettings s;
    s.signal = signalFor (signal.getSelected());
    s.levelDbFS = level.getValue();
    s.sineHz = sineHz.getValue();
    s.feed = feedFor (feed.getSelected());
    return s;
}

void GeneratorPanel::setOutputPeak (float dbfs)
{
    outPeak = dbfs;
    outputMeter.setLevel (dbfs);
}

void GeneratorPanel::setHardwareAvailable (bool available)
{
    hardware = available;
    updateEnablement();
    repaint();
}

void GeneratorPanel::emit()
{
    repaint();
    if (onChange)
        onChange (getSettings());
}

void GeneratorPanel::updateEnablement()
{
    const bool on = signal.getSelected() != 0;
    const bool sine = signal.getSelected() == 4;
    sineHz.setEnabled (sine);
    sineHz.setAlpha (sine ? 1.0f : 0.4f);
    stopButton.setEnabled (on && hardware);
    stopButton.setAlpha (on ? 1.0f : 0.5f);
    signal.setEnabled (hardware);
    level.setEnabled (hardware);
    feed.setEnabled (hardware);
}

void GeneratorPanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    const bool on = signal.getSelected() != 0 && hardware;
    theme::fillCard (g, r.toFloat(), theme::bg2, on ? theme::warn.withAlpha (0.5f) : theme::border);

    auto header = r.removeFromTop (44).reduced (theme::pad, 0);
    g.setColour (theme::text);
    g.setFont (theme::fontMedium (14.0f));
    g.drawText ("Generator", header, juce::Justification::centredLeft);

    // status dot + text (right of the title)
    const int titleW = (int) std::ceil (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), "Generator"));
    auto dot = juce::Rectangle<int> (header.getX() + titleW + 12, header.getCentreY() - 4, 8, 8);
    const auto c = on ? theme::warn : theme::textMuted;
    g.setColour (c);
    g.fillEllipse (dot.toFloat());
    if (on)
        theme::drawGlow (g, dot.toFloat(), theme::warn, 4.0f, 6.0f);
    g.setFont (theme::fontMedium (11.0f));
    g.setColour (on ? theme::warn : theme::textMuted);
    g.drawText (on ? "OUTPUT ON" : "SILENT", dot.getRight() + 6, header.getY(), 90, header.getHeight(), juce::Justification::centredLeft);

    // captions
    auto cap = [&] (juce::Component& comp, const juce::String& text)
    {
        g.setColour (theme::textMuted);
        g.setFont (theme::fontMedium (10.0f));
        g.drawText (text.toUpperCase(), comp.getX(), comp.getY() - 14, comp.getWidth(), 12, juce::Justification::centredLeft);
    };
    cap (signal, "Signal");
    cap (feed, "Feed");
    cap (outputMeter, "Output");

    g.setColour (theme::textDim);
    g.setFont (theme::mono (10.0f));
    g.drawText (juce::String (outPeak, 1) + " dBFS", outputMeter.getX(), outputMeter.getY() - 14, outputMeter.getWidth(), 12,
                juce::Justification::centredRight);

    if (! hardware)
    {
        g.setColour (theme::textMuted);
        g.setFont (theme::font (11.0f));
        g.drawText ("Needs Live mode", header, juce::Justification::centredRight);
    }
}

void GeneratorPanel::resized()
{
    auto r = getLocalBounds().reduced (theme::pad, 0);
    r.removeFromTop (44);
    const int h = theme::controlHeight;
    const int gap = 22;

    r.removeFromTop (14);
    signal.setBounds (r.removeFromTop (h));
    r.removeFromTop (theme::grid);

    auto row = r.removeFromTop (48);   // tall fields: eyebrow label + big value
    level.setBounds (row.removeFromLeft ((row.getWidth() - theme::grid) / 2));
    row.removeFromLeft (theme::grid);
    sineHz.setBounds (row);

    r.removeFromTop (gap);
    feed.setBounds (r.removeFromTop (h));

    r.removeFromTop (gap);
    outputMeter.setBounds (r.removeFromTop (10));

    auto stop = r.removeFromBottom (theme::pad + 48).withTrimmedBottom (theme::pad);
    stopButton.setBounds (stop);
}
} // namespace fa::ui
