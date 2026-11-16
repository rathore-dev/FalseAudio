#pragma once

#include "Calibration.h"
#include "../audio/EndpointVolume.h"

namespace fa
{
/** Input-only microphone sanity check. Pure maths on a recorded buffer (no hardware, no output).

    Level convention (verified against REW 5.40 with a UMIK-1, "Gain: 18dB" firmware, within 0.2 dB):
        dB SPL = dBFS_rms + 124.02 - sens - endpointDb
    where dBFS_rms = 10 log10 (mean square) of a +/-1.0 float signal (a full-scale sine reads -3 dBFS),
    sens is the "Sens Factor" of the calibration file and endpointDb is the Windows capture-endpoint level in dB
    (0 dB when unknown). 124.02 = 94 dB + 24 dB (the level at which the Sens Factor was taken) + 3.01 dB (REW
    states dBFS with a full-scale sine = 0 dBFS) + 3.01 dB (the same sine/RMS step inside the sens reference).
    The calibration curve is subtracted per frequency. */
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
    double fullScaleDbSpl = 0.0;      // SPL of a full-scale sine at this gain (headroom)

    // Windows capture endpoint volume (the mic level slider). Read-only here; the UI may offer to set it.
    bool   endpointKnown = false;
    double endpointDb = 0.0;          // folded into the SPL maths
    double endpointScalar = 0.0;
    double recommendedEndpointDb = 0.0;
    bool   volumeWarning = false;     // true when endpointDb differs from the recommended level
    juce::String volumeMessage;       // empty when fine; explains and says "Set to recommended" is available

    // 1/3-octave noise spectrum (20 Hz .. 20 kHz), dB SPL (uncalibrated: sens 0)
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

/** Analyses one channel of recorded input. `cal` may be null (levels then referenced to dBFS + 124.02). */
MicSelfTestReport analyseMicRecording (const std::vector<float>& samples, double sampleRate,
                                       const MicCalibration* cal, const EndpointVolume* endpoint = nullptr);

/** Level offset (dB) added to 10 log10 (mean square) to get dB SPL. */
double micSplOffsetDb (double sensFactorDb, double endpointDb);

/** Recommended Windows capture level for the UMIK-1 (dB): 0 dB, the Windows default. Levels are corrected for any
    other value, but a higher level costs headroom (+24 dB leaves only ~98 dB SPL before full scale). */
constexpr double kRecommendedEndpointDb = 0.0;
} // namespace fa
