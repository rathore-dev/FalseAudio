#pragma once

#include "SystemSolver.h"
#include <atomic>

namespace fa::method
{
//==============================================================================
// Sweep level per driver role
//==============================================================================

/** Loudest measurement sweep (dBFS) a driver role may receive, whatever the level check found. */
double sweepCapDbFS (DriverRole role) noexcept;

/** Level actually used for a role: min (requested, role cap, safetyCeiling). SafetyLimits::sweepLevelDbFS
    is a hard ceiling, so the level check can never climb past it. */
double sweepLevelFor (DriverRole role, double requestedDbFS, double safetyCeilingDbFS) noexcept;

//==============================================================================
// Imaging: left/right balance by LEVEL only
//==============================================================================
struct ImagingResult
{
    bool changed = false;
    int channelsAdjusted = 0;
    double judgeBefore = 0.0, judgeAfter = 0.0;   // centre focus + imaging consistency of advanced::judge
    juce::StringArray notes;
};

/** Pairs every left driver with its right twin, compares their predicted level over the driver's passband
    (150 Hz..8 kHz) and lowers the LOUDER side's channel gain (never boosts, max 3 dB, DSP gain step).
    Delays, polarity, crossovers and EQ are not touched. The change is kept only if the judge's centre/imaging
    categories do not get worse; otherwise it is reverted. No-op for single-side systems. */
ImagingResult balanceImagingByLevel (SystemConfig& config, const SystemSolver& solver, const DriverDataMap& data);

//==============================================================================
// L+R sum check that acts
//==============================================================================
struct SumCheckResult
{
    bool corrected = false;
    double worstBeforeDb = 0.0, worstAfterDb = 0.0;   // deepest L+R cancellation dip
    juce::StringArray notes;
};

/** If L+R shows a cancellation (dip) that L and R alone do not have, tries polarity flips (a whole side, the subs;
    single drivers keep the polarity the polarity stage measured) and keeps the best one that cuts the average cancellation by >= 0.5 dB without making any side's
    own crossover summation worse. Otherwise leaves the settings alone and says so. */
SumCheckResult correctSumCancellation (SystemConfig& config, SystemSolver& solver);

//==============================================================================
// Final noise check
//==============================================================================

/** Optional capability of a MeasurementSource (implemented by the virtual rig; hardware sources may add it):
    record the microphone with no stimulus playing. */
struct NoiseProbe
{
    virtual ~NoiseProbe() = default;
    virtual bool recordSilence (std::vector<float>& samples, double& sampleRate, std::atomic<bool>& cancelFlag,
                                juce::String& error) = 0;
};

struct NoiseReading
{
    double rmsDbFS = -120.0;    // total
    double humDbFS = -120.0;    // 50/60 Hz and the first harmonics
    double hissDbFS = -120.0;   // broadband, white-equivalent (from the high-passed signal)
};

NoiseReading analyseNoise (const std::vector<float>& samples, double sampleRate);

struct NamedNoise
{
    juce::String id, label;
    NoiseReading reading;
};

struct NoiseFinding
{
    juce::String channelId;
    bool hum = false;       // false = hiss
    double addedDb = 0.0;
    juce::String message;
};

/** Compares each channel (silent signal, its output enabled) with the all-muted baseline. A channel that
    adds >= 3 dB of noise is flagged, as ground-loop hum or as hiss, with the fix to try. */
std::vector<NoiseFinding> assessNoise (const NoiseReading& baseline, const std::vector<NamedNoise>& channels);
} // namespace fa::method
