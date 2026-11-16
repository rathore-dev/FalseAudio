#pragma once

#include "../dsp/Filters.h"

namespace fa
{
/** Fits parametric EQ bands so a measured response follows a target.

    Design choices (all aimed at results that *sound* right, not just look right):
     - psychoacoustic smoothing of the measurement before fitting
     - peaks are cut with full weight, dips are filled with reduced weight and limited boost
     - narrow deep dips (acoustic cancellations that EQ can't fix) are ignored
     - optional per-frequency weights (e.g. low where mic positions disagree)
     - greedy band placement followed by joint refinement of all bands
     - results quantised to what the DSP accepts */
class AutoEQ
{
public:
    struct Settings
    {
        double fLow  = 20.0;
        double fHigh = 16000.0;
        int    maxBands = 10;
        double maxBoostDb = 6.0;
        double maxCutDb   = -15.0;
        double minQ = 0.5;
        double maxQ = 10.0;
        double dipWeight = 0.4;          // relative importance of filling dips vs cutting peaks
        double stopRmsDb = 0.4;          // stop adding bands once error is this small
        double sampleRate = 48000.0;
        bool   fitGainOffset = false;    // also solve a broadband level offset
        double lfBoostLimitBelowHz = 0.0;// stricter boost limit below this frequency
        double lfBoostLimitDb = 3.0;
        double gainStepDb = 0.1;
        /** Frequency ranges (lo, hi) the EQ must not try to fix (passive-network notches): they are
            left out of the fit and any band centred inside is limited to noBoostLimitDb. */
        std::vector<std::pair<double, double>> noBoostRanges;
        double noBoostLimitDb = 0.0;
    };

    struct Result
    {
        std::vector<FilterSpec> filters;
        double gainOffsetDb = 0.0;
        double rmsErrorBeforeDb = 0.0;
        double rmsErrorAfterDb  = 0.0;
        std::vector<double> freqs;          // fitting grid
        std::vector<double> measuredDb;     // smoothed measurement on the grid
        std::vector<double> correctedDb;    // measurement + EQ + offset
        std::vector<double> targetDb;
    };

    /** measured: complex response (smoothing applied internally).
        targetDb: target level on measured.freqs.
        weights: optional, same size as measured.freqs, 0..1. */
    static Result solve (const FrequencyResponse& measured,
                         const std::vector<double>& targetDb,
                         const Settings& settings,
                         const std::vector<double>& weights = {});

    /** Spatial-consistency weights from several mic positions: 1 where they agree, -> 0 where
        the standard deviation is large (position-dependent, not fixable by EQ). */
    static std::vector<double> spatialWeights (const std::vector<FrequencyResponse>& positions);
};
} // namespace fa
