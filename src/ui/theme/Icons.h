#pragma once

#include <JuceHeader.h>

namespace fa::ui
{
/** Vector icons drawn with juce::Path, designed on a 24x24 grid with 1.75px strokes. */
namespace icons
{
    enum class Id
    {
        bolt, car, plug, waveform, activity, clock, filter, chip, document,
        play, stop, check, warning, cross, plus, trash, eye, eyeOff, copy, folder,
        download, refresh, settings, mic, speaker, chevronDown, chevronRight, info,
        save, open, link, unlink, sun, move, target, sliders, external, minimise, maximise, restore,
        flask, radio, arrowRight, search, pause, help, power, bell
    };

    /** Returns the icon as a path in 0..24 coordinates. Stroked icons are already converted to fills. */
    juce::Path get (Id id);

    /** Draws the icon centred in `area`, scaled to fit. */
    void draw (juce::Graphics& g, Id id, juce::Rectangle<float> area, juce::Colour colour);
}
} // namespace fa::ui
