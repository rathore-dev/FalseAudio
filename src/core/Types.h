#pragma once

#include <complex>
#include <vector>
#include <cmath>
#include <algorithm>
#include <juce_core/juce_core.h>

namespace fa
{
using cplx = std::complex<double>;

constexpr double pi = 3.14159265358979323846;
constexpr double speedOfSound = 343.0; // m/s at ~20 degC

inline double dbFromMag (double mag)       { return 20.0 * std::log10 (std::max (mag, 1.0e-12)); }
inline double magFromDb (double db)        { return std::pow (10.0, db / 20.0); }
inline double dbFromPower (double power)   { return 10.0 * std::log10 (std::max (power, 1.0e-24)); }
inline double msToMetres (double ms)       { return ms * 0.001 * speedOfSound; }
inline double metresToMs (double m)        { return m / speedOfSound * 1000.0; }

/** Logarithmically spaced frequency axis, the common grid used by all analysis code. */
std::vector<double> makeLogFrequencies (double fLow, double fHigh, int pointsPerOctave);

/** Complex frequency response sampled on an arbitrary frequency grid. */
struct FrequencyResponse
{
    std::vector<double> freqs;
    std::vector<cplx>   values;

    bool  isEmpty() const noexcept              { return freqs.empty(); }
    size_t size() const noexcept                { return freqs.size(); }

    double magnitudeDb (size_t i) const         { return dbFromMag (std::abs (values[i])); }
    double phaseDeg (size_t i) const            { return std::arg (values[i]) * 180.0 / pi; }

    std::vector<double> magnitudeDb() const;

    /** Linear-in-log-frequency interpolation of the complex value (magnitude and unwrapped phase). */
    cplx interpolate (double f) const;

    /** Resamples onto another frequency grid. */
    FrequencyResponse resampled (const std::vector<double>& newFreqs) const;

    FrequencyResponse& operator*= (const FrequencyResponse& other);
    FrequencyResponse& operator+= (const FrequencyResponse& other);
    FrequencyResponse& scale (cplx factor);

    /** Multiplies by e^{-j w t}: positive delay shifts the response later in time. */
    FrequencyResponse& applyDelay (double seconds);

    static FrequencyResponse unity (const std::vector<double>& freqs);
};

FrequencyResponse operator* (FrequencyResponse a, const FrequencyResponse& b);
FrequencyResponse operator+ (FrequencyResponse a, const FrequencyResponse& b);

/** Mean of magnitude in dB over [f1, f2]. */
double averageLevelDb (const FrequencyResponse& r, double f1, double f2);

/** Power-average (RMS) of several responses on the same grid; phase taken from the first. */
FrequencyResponse powerAverage (const std::vector<FrequencyResponse>& responses);
} // namespace fa
