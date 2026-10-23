#pragma once

#include "Measurement.h"

namespace fa
{
/** Advanced analysis derived from a sweep impulse response (beyond SPL/phase). */
namespace analysis
{
    /** Harmonic distortion from an exponential-sweep IR (Farina): the harmonics appear as
        separate impulses before the linear one. Levels are relative to the fundamental. */
    struct Distortion
    {
        std::vector<double> freqs;               // analysisFrequencies() subset
        std::vector<std::vector<double>> harmonicDb; // [harmonic 2..6][freq], dB re fundamental
        std::vector<double> thdPercent;          // total, on freqs
        bool valid = false;
    };
    Distortion harmonicDistortion (const Measurement& m, double sweepSeconds, double sweepFStart,
                                   double sweepFEnd, int maxHarmonic = 6);

    /** Cumulative spectral decay (waterfall): slices of the response every `stepMs`. */
    struct Waterfall
    {
        std::vector<double> freqs;
        std::vector<double> timesMs;
        std::vector<std::vector<double>> levelDb; // [slice][freq]
    };
    Waterfall spectralDecay (const Measurement& m, double durationMs = 300.0, int slices = 30,
                             double fLow = 20.0, double fHigh = 20000.0);

    /** Step response (integrated IR), normalised to peak 1. */
    std::vector<float> stepResponse (const Measurement& m);

    /** Energy-time curve in dB (Hilbert envelope), normalised to 0 dB peak. */
    std::vector<float> energyTimeCurve (const Measurement& m);

    /** RT60-style decay estimate per octave band (T20), seconds. Mostly for rooms. */
    std::vector<std::pair<double, double>> decayTimes (const Measurement& m);
}
} // namespace fa
