#pragma once

#include "SystemConfig.h"
#include "../measure/Measurement.h"
#include <map>

namespace fa
{
/** A narrow dip in the measured response of a passive set (or coax) in its crossover region.
    It comes from the passive network and the physical offset between woofer and tweeter; the
    DSP cannot change either, so EQ must not boost into it and the judge must not count it. */
struct PassiveNotch
{
    double freqHz = 0.0;     // deepest point
    double depthDb = 0.0;    // below the surrounding level
    double loHz = 0.0, hiHz = 0.0;   // extent to protect (padded)
};

namespace passive
{
    /** Finds crossover-region notches in the combined response of one passive set.
        crossoverHz = 0 when unknown (searches 700 Hz..7 kHz); otherwise 0.5x..2x the crossover. */
    std::vector<PassiveNotch> findNotches (const FrequencyResponse& combined, double crossoverHz,
                                           double minDepthDb = 5.0);

    /** Notches of every passive set in the config (responses keyed by channel id). */
    std::map<juce::String, std::vector<PassiveNotch>> findAll (const SystemConfig& cfg,
                                                               const std::map<juce::String, FrequencyResponse>& responses);

    /** (lo, hi) ranges for AutoEQ::Settings::noBoostRanges. */
    std::vector<std::pair<double, double>> ranges (const std::vector<PassiveNotch>& notches);

    /** True when f lies inside any notch extent. */
    bool contains (const std::vector<PassiveNotch>& notches, double f);

    /** Human-readable insight for the report. */
    juce::String describe (const OutputChannel& ch, const PassiveNotch& n);
}
} // namespace fa
