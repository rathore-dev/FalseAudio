#pragma once

#include "../Types.h"

namespace fa
{
/** Exponential (Farina) sine sweep, with the fade and timing parameters needed for deconvolution. */
struct LogSweep
{
    double sampleRate = 48000.0;
    double fStart     = 10.0;
    double fEnd       = 24000.0;
    double seconds    = 3.0;
    double levelDbFS  = -12.0;
    double tailSeconds = 1.0;   // silence recorded after the sweep for the room decay

    std::vector<float> generate() const;

    int lengthSamples() const       { return (int) std::round (seconds * sampleRate); }
    int totalSamples() const        { return lengthSamples() + (int) std::round (tailSeconds * sampleRate); }
};

/** Periodic pink noise: exactly periodic over `periodSamples`, so an RTA with a matching
    FFT length and a rectangular window shows a perfectly smooth spectrum (no averaging noise). */
std::vector<float> makePeriodicPinkNoise (int periodSamples, double sampleRate, double levelDbFS,
                                          double fLow = 20.0, double fHigh = 20000.0, juce::uint32 seed = 1234);

/** Number of samples of pre-roll that deconvolve() puts before time zero. */
inline int deconvolutionPreRoll (double sampleRate) { return (int) (0.05 * sampleRate); }

/** Deconvolves `recorded` by `reference` (the sweep as played, or a loopback recording of it)
    to obtain the impulse response. Out-of-band energy is regularised.
    Sample index deconvolutionPreRoll() of the result corresponds to zero delay. */
std::vector<float> deconvolve (const std::vector<float>& recorded,
                               const std::vector<float>& reference,
                               double sampleRate,
                               double fLow, double fHigh);
} // namespace fa
