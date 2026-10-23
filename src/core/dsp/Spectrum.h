#pragma once

#include "../Types.h"

namespace fa
{
int nextPowerOfTwo (int n);

/** Forward real FFT of x (zero padded to fftSize). Returns bins 0..fftSize/2. */
std::vector<cplx> realFFT (const float* x, int numSamples, int fftSize);

/** Inverse of realFFT: takes bins 0..fftSize/2 and returns fftSize real samples. */
std::vector<double> inverseRealFFT (const std::vector<cplx>& bins, int fftSize);

/** How an impulse response is gated before conversion to a frequency response. */
struct IRWindow
{
    double leftMs  = 5.0;    // before the reference (peak) sample, half-Hann taper
    double rightMs = 500.0;  // after the reference sample, half-Hann taper over last 25%
};

/** Converts an impulse response to a complex response on a log grid.
    The phase is referenced to `referenceSample` (i.e. that sample is time zero). */
FrequencyResponse impulseToResponse (const std::vector<float>& ir,
                                     double sampleRate,
                                     const std::vector<double>& freqs,
                                     int referenceSample,
                                     const IRWindow& window = {});

/** Fractional-octave power smoothing on a log-spaced grid. fraction = 3 means 1/3 octave. */
FrequencyResponse smoothFractionalOctave (const FrequencyResponse& r, double fraction);

/** REW-style "variable" smoothing: 1/48 oct at LF ramping to 1/3 oct at HF. */
FrequencyResponse smoothVariable (const FrequencyResponse& r);

/** Psychoacoustic smoothing: 1/3 oct below 100 Hz, 1/6 above 1 kHz, plus cubic-mean weighting of peaks. */
FrequencyResponse smoothPsychoacoustic (const FrequencyResponse& r);

/** Returns a copy whose phase is the minimum phase implied by the magnitude (via Hilbert transform). */
FrequencyResponse minimumPhaseOf (const FrequencyResponse& r);

/** Unwrapped phase in radians. */
std::vector<double> unwrappedPhase (const FrequencyResponse& r);

/** Group delay in ms from the unwrapped phase. */
std::vector<double> groupDelayMs (const FrequencyResponse& r);
} // namespace fa
