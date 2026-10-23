#pragma once

#include "../Types.h"
#include "../dsp/Spectrum.h"
#include "Calibration.h"

namespace fa
{
/** The analysis grid used by every response in the app. */
const std::vector<double>& analysisFrequencies();

/** One captured impulse response and everything derived from it. */
struct Measurement
{
    juce::String id = juce::Uuid().toString();
    juce::String name;
    juce::String channelId;          // output channel(s) that played, e.g. "FL.tweeter" or "FL+FR"
    int          micPosition = 0;    // index for multi-position averaging
    juce::Time   timestamp = juce::Time::getCurrentTime();

    double sampleRate = 48000.0;
    std::vector<float> ir;
    int zeroSample = 0;              // sample index of "time zero" (output start, latency-compensated)

    // Derived
    int    peakSample  = 0;
    double onsetMs     = 0.0;        // first arrival relative to time zero
    double peakDbFS    = -120.0;
    double snrDb       = 0.0;
    bool   clipped     = false;

    /** Calibrated response with phase referenced to the onset (so it varies slowly). */
    FrequencyResponse response;

    /** Analyses the IR and fills the derived fields and response. */
    void analyse (const MicCalibration* cal, const IRWindow& window = {});

    /** Response with phase referenced to time zero (i.e. including propagation delay). */
    FrequencyResponse absoluteResponse() const;

    juce::var toVar (bool includeIR = true) const;
    static Measurement fromVar (const juce::var& v);
};

/** Result of fitting the excess phase of a driver over its passband. */
struct ArrivalEstimate
{
    double delayMs = 0.0;        // relative to the response's reference
    bool   inverted = false;
    double confidence = 0.0;     // 0..1, how well the excess phase fits a pure delay
};

ArrivalEstimate estimateArrival (const FrequencyResponse& response, double fLow, double fHigh);

/** Index of the absolute peak, with sub-sample refinement. */
double findPeak (const std::vector<float>& ir, int searchStart = 0, int searchEnd = -1);

/** First sample (before the peak) whose magnitude exceeds peak * 10^(thresholdDb/20). */
double findOnset (const std::vector<float>& ir, double peak, double sampleRate, double thresholdDb = -15.0);
} // namespace fa
