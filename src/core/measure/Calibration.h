#pragma once

#include "../Types.h"

namespace fa
{
/** A microphone (or input chain) calibration file.
    Accepts every common text format: "freq dB [phase]" separated by spaces, tabs, commas or
    semicolons, with arbitrary header/comment lines. Recognises the miniDSP UMIK
    "Sens Factor =x.xxdB" header for absolute SPL. */
struct MicCalibration
{
    juce::String name;
    juce::String serial;
    double sensitivityDb = 0.0;     // UMIK-style sensitivity factor
    bool   hasPhase = false;
    std::vector<double> freqs, gainDb, phaseDeg;

    bool isValid() const noexcept { return freqs.size() >= 2; }

    static MicCalibration parse (const juce::String& text, const juce::String& name = {});
    static MicCalibration load (const juce::File& file);

    /** Calibration deviation (what the mic adds) at f, interpolated in log frequency. */
    double deviationDb (double f) const;
    double deviationPhaseDeg (double f) const;

    /** Divides out the mic's deviation from a measured response. */
    FrequencyResponse corrected (const FrequencyResponse& measured) const;

    juce::var toVar() const;
    static MicCalibration fromVar (const juce::var& v);
};
} // namespace fa
