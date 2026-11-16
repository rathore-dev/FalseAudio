#pragma once

#include "Calibration.h"

namespace fa
{
/** Input-only microphone sanity check. Pure maths on a recorded buffer (no hardware, no output).

    Level convention (miniDSP UMIK-1, 18 dB gain firmware): a mic whose calibration file says
    "Sens Factor = s dB" has  dB SPL = dBFS + 112 - s,  where dBFS = 10 log10 (mean square) of a
    +/-1.0 float signal (so a full-scale sine reads -3 dBFS). Nominal sensitivity is -18 dBFS at
    94 dB SPL (hence 94 + 18 = 112). The calibration curve is subtracted per frequency. */
struct MicSelfTestReport
{
    enum class Verdict { ok, tooQuiet, clipping, hum, noData };

    // what was captured
    double sampleRate = 0.0;          // as reported by the device
    double measuredRate = 0.0;        // samples / wall clock, 0 if unknown
    double seconds = 0.0;
    int    numSamples = 0;

    // raw digital levels
    double peakDbFS = -200.0, rmsDbFS = -200.0;
    int    clippedSamples = 0;
    bool   clipping = false;
    double dcOffset = 0.0;            // linear mean
    double dcDbFS = -200.0;

    // calibrated acoustic levels (absolute is true when a calibration with a sens factor was applied)
    bool   absolute = false;
    double sensFactorDb = 0.0;
    double levelZDbSpl = -200.0;      // 20 Hz .. 20 kHz, unweighted
    double levelADbSpl = -200.0;      // A-weighted

    // 1/3-octave noise spectrum (20 Hz .. 20 kHz), dB SPL (dBFS + 112 when not absolute)
    std::vector<double> bandCentreHz, bandDbSpl;

    // mains hum
    bool   humDetected = false;
    double humFundamentalHz = 0.0;    // 50 or 60
    double humDbSpl = -200.0;         // power sum of the detected harmonics
    juce::String humDetail;           // e.g. "50 Hz +14 dB, 100 Hz +11 dB"

    Verdict verdict = Verdict::noData;
    juce::String message;

    static juce::String verdictName (Verdict v);
    juce::String toText() const;
};

/** A-weighting in dB, normalised to exactly 0 dB at 1 kHz. */
double aWeightingDb (double hz);

/** Analyses one channel of recorded input. `cal` may be null (levels then referenced to dBFS + 112). */
MicSelfTestReport analyseMicRecording (const std::vector<float>& samples, double sampleRate,
                                       const MicCalibration* cal);
} // namespace fa
