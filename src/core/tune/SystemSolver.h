#pragma once

#include "SystemConfig.h"
#include "AutoEQ.h"
#include "../measure/Measurement.h"
#include <map>

namespace fa
{
/** Measured data for one output channel: one response per mic position, plus the DSP settings
    that were active while it was measured (so they can be divided out). Responses keep their
    phase referenced to the IR onset (slowly varying, safe to unwrap); the onset time is kept
    separately and re-applied analytically. */
struct DriverData
{
    std::vector<FrequencyResponse> positions;   // Measurement::response of each mic position
    std::vector<double> onsetMs;                // Measurement::onsetMs of each mic position
    ChannelSettings measuredWith;

    void add (const Measurement& m)
    {
        positions.push_back (m.response);
        onsetMs.push_back (m.onsetMs);
    }
};

using DriverDataMap = std::map<juce::String, DriverData>;

/** Offline solver: predicts what any combination of DSP settings will sound like at the mic
    (all operations are linear, so prediction from per-driver measurements is exact) and
    optimises delay, polarity, level and EQ against the target. */
class SystemSolver
{
public:
    struct Options
    {
        bool matchRightToLeft = true;   // process rule: tune left to target, then right to left
        bool allowPolarityFlip = true;  // fix reversed wiring in the DSP
        int  extraPhaseSearchSteps = 81;
    };

    struct Report
    {
        juce::StringArray notes;        // human-readable explanation of every decision
        juce::StringArray warnings;
        std::map<juce::String, double> arrivalMs;
        std::map<juce::String, bool>   wiringInverted;
        double leftRmsErrorDb = 0.0, rightRmsErrorDb = 0.0, leftRightMatchDb = 0.0;
        std::vector<std::pair<double, double>> sumDips; // (freq, depth) of L+R cancellation dips
    };

    SystemSolver (SystemConfig& config, const DriverDataMap& data, Options options);

    /** Predicted response of one channel at one mic position with the given settings. */
    FrequencyResponse predict (const juce::String& channelId, const ChannelSettings& settings, int position = 0) const;

    /** Predicted acoustic sum of a set of channels (current settings) at one mic position. */
    FrequencyResponse predictSum (const std::vector<const OutputChannel*>& channels, int position = 0) const;

    /** Channels audible for a side (that side's drivers plus mono sub). */
    std::vector<const OutputChannel*> sideChannels (Side side) const;

    int numPositions() const;

    // Individual steps (each one updates the config's channel settings):
    void checkPolarity();
    void alignTime();
    void refineCrossoverPhase();
    void matchLevels();
    void equalise();
    void analyseSum();

    /** Runs every step in the process order. */
    Report solveAll();

    const Report& getReport() const { return report; }

private:
    SystemConfig& cfg;
    const DriverDataMap& data;
    Options opts;
    Report report;
    std::vector<double> freqs;

    std::pair<double, double> dominantBand (const OutputChannel& ch) const;
    double inPhaseCost (const FrequencyResponse& a, const FrequencyResponse& b, double fLow, double fHigh) const;
    void equaliseSide (Side side, const std::vector<double>& targetDb, double& rmsOut);
    FrequencyResponse averagedSideSum (Side side) const;
    double quantiseDelay (double ms) const;
};
} // namespace fa
