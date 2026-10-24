#pragma once

#include "../SystemSolver.h"

namespace fa::advanced
{
/** Competition-grade refinement that runs AFTER SystemSolver::solveAll() on the same data.
    Jointly optimises (within DSP limits and safety rules) crossover-region delay/polarity/level,
    sub-to-front integration, EQ distribution across overlapping drivers and left/right symmetry
    over all mic positions, using a perceptual cost (ERB-weighted tonal error, L/R mismatch,
    L+R summation loss, boost penalty, excursion/safety constraints).
    Must never make the predicted result worse than the input config. */
struct Options
{
    int maxIterations = 400;
    double tonalWeight = 1.0, symmetryWeight = 1.5, summationWeight = 1.2, boostPenalty = 0.3;
    bool allowAllPass = false;        // only if caps.hasAllPass
    bool tuneSubIntegration = true;
};

struct Outcome
{
    double costBefore = 0, costAfter = 0;
    juce::StringArray notes;
};

Outcome refine (SystemConfig& config, const DriverDataMap& data, const Options& options);

/** The perceptual cost refine() minimises, split into its terms (all roughly in dB).
    Evaluated for `config` with itself as the reference (so the regularisation term is zero). */
struct CostBreakdown
{
    double tonal = 0, symmetry = 0, summationLR = 0, summationSide = 0, boost = 0, regularisation = 0, total = 0;
};

CostBreakdown evaluateCost (const SystemConfig& config, const DriverDataMap& data, const Options& options = {});

/** Simulated judge: scores a predicted/measured result the way competition judges listen
    (tonal accuracy, stage width/height proxies, centre focus, bass up front, linearity,
    noise). 0..100 with category breakdown. */
struct JudgeScore
{
    double total = 0;
    std::map<juce::String, double> categories;
    juce::StringArray comments;
};

JudgeScore judge (const SystemConfig& config, const DriverDataMap& data);
} // namespace fa::advanced
