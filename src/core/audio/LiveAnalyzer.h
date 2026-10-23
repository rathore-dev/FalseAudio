#pragma once

#include "AudioEngine.h"

namespace fa
{
/** Real-time analysis on the live input: RTA and dual-channel transfer function
    (magnitude, phase, coherence, delay finder). Call the compute functions from a timer;
    each call consumes the latest audio. */
class LiveAnalyzer
{
public:
    struct Settings
    {
        int    fftSize = 16384;
        double averagingTimeS = 1.0;     // exponential averaging time constant (0 = none)
        double smoothingFraction = 6.0;  // 1/N octave, 0 = none
        bool   hannWindow = false;       // rectangular suits periodic pink noise
        double splOffsetDb = 0.0;        // dBFS -> dB SPL for the selected mic
    };

    struct TransferFunction
    {
        FrequencyResponse h;             // output / reference
        std::vector<double> coherence;   // 0..1 on h.freqs
        double delayMs = 0.0;            // delay compensation currently applied
    };

    explicit LiveAnalyzer (AudioEngine& engine);

    void setSettings (const Settings& s);
    Settings getSettings() const { return settings; }
    void reset();

    /** Averaged spectrum of a mic (index into routing.micInputs), in dB (SPL if calibrated). */
    FrequencyResponse computeRTA (int micIndex);

    /** Averaged transfer function mic / generator (or loopback) reference. */
    TransferFunction computeTransferFunction (int micIndex);

    /** Measures the delay between reference and mic (impulse peak) and applies it. Returns ms. */
    double findDelay (int micIndex);
    void   setDelayCompensation (double ms) { delayCompMs = ms; }

    /** Broadband level of the mic in dB (SPL if calibrated), with A/C/Z weighting. */
    enum class Weighting { Z, A, C };
    double computeLevel (int micIndex, Weighting w);

private:
    AudioEngine& engine;
    Settings settings;
    double delayCompMs = 0.0;

    std::vector<double> rtaAvg;
    std::vector<cplx>   sxyAvg;
    std::vector<double> sxxAvg, syyAvg;
    juce::uint32 lastRta = 0, lastTf = 0;

    double averagingFactor (juce::uint32& lastTime) const;
    bool readMic (int micIndex, std::vector<float>& dest, int extra = 0) const;
    std::vector<float> window (int n) const;
};
} // namespace fa
