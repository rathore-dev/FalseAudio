#pragma once

#include "../Types.h"

namespace fa
{
/** A target frequency response (dB vs frequency), either built from a few musical
    parameters or imported from a text/CSV file (e.g. REW target files). */
struct TargetCurve
{
    juce::String name = "FalseAudio Car Reference";

    // Parametric definition (used when `points` is empty)
    double bassBoostDb      = 10.0;   // shelf amount below the transition
    double transitionLowHz  = 60.0;   // bass boost fully on below here
    double transitionHighHz = 160.0;  // flat midrange from here
    double trebleTiltDb     = -6.0;   // level at 20 kHz relative to midrange
    double tiltStartHz      = 1000.0; // tilt begins here
    double infrasonicRollHz = 20.0;   // gentle roll-off below this (protects subs)

    // Custom definition (overrides the parametric one if not empty)
    std::vector<std::pair<double, double>> points;

    double levelAt (double f) const;
    std::vector<double> levels (const std::vector<double>& freqs) const;

    static TargetCurve carReference();      // Audiofrog-style car curve (process PDF)
    static TargetCurve harmanLike();        // slightly warmer, softer top
    static TargetCurve flat();              // home/studio flat
    static TargetCurve parse (const juce::String& text, const juce::String& name);

    /** Same as parse(), optionally keeping the file's absolute levels (no 1 kHz normalisation). */
    static TargetCurve parse (const juce::String& text, const juce::String& name, bool normaliseAt1k);

    juce::var toVar() const;
    static TargetCurve fromVar (const juce::var& v);
};
} // namespace fa
