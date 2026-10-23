#pragma once

#include "SystemConfig.h"
#include "../measure/Measurement.h"
#include <map>

namespace fa
{
/** Chooses crossover points following the process document's rules:
     - LR 24 dB/oct everywhere
     - tweeter HP at 1.6..2 x Fs (never below the safety floor)
     - low-pass points where the driver's dispersion is still wide (ka ~ 1.5)
     - sub LP half an octave below the midbass HP
    and, when measurements are available, checks each driver keeps a flat response at least
    an octave beyond its crossover (moving the point if it does not). */
class CrossoverPlanner
{
public:
    struct Decision
    {
        juce::String channelId;
        double hpHz = 0.0, lpHz = 0.0;
        juce::StringArray reasons;
    };

    /** Safe, measurement-free defaults; used before the first sweep so tweeters are protected. */
    static std::vector<Decision> planDefaults (const SystemConfig& cfg);

    /** Refines points using the drivers' measured (protected) responses. Keyed by channel id. */
    static std::vector<Decision> planFromMeasurements (const SystemConfig& cfg,
                                                       const std::map<juce::String, FrequencyResponse>& driverResponses);

    /** Writes decisions into cfg (LR4, snapped to the DSP's grid). */
    static void apply (SystemConfig& cfg, const std::vector<Decision>& decisions);

    /** Frequency above which a driver of this size starts beaming (ka ~ 1.5). */
    static double dispersionLimitHz (const OutputChannel& ch);

    /** Lowest safe HP for the driver. */
    static double safeHighPassHz (const OutputChannel& ch, const SafetyLimits& s);

    /** Snap to the DSP's frequency grid (1/24 octave ISO steps if freqStepHz < 0, or round to step). */
    static double snapFrequency (double f, const DspCapabilities& caps);
};
} // namespace fa
