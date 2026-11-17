#include "MicSelfTest.h"
#include "../dsp/Spectrum.h"

namespace fa
{
namespace
{
constexpr double kRefOffsetDb = 124.0206; // 94 + 24 + 2 x 3.0103, see MicSelfTest.h

double db10 (double p) { return 10.0 * std::log10 (std::max (p, 1e-20)); }

double rawA (double f)
{
    const double f2 = f * f;
    return (12194.0 * 12194.0 * f2 * f2)
         / ((f2 + 20.6 * 20.6) * std::sqrt ((f2 + 107.7 * 107.7) * (f2 + 737.9 * 737.9)) * (f2 + 12194.0 * 12194.0));
}
}

double micSplOffsetDb (double sens, double endpointDb) { return kRefOffsetDb - sens - endpointDb; }

double aWeightingDb (double hz)
{
    static const double norm = -20.0 * std::log10 (rawA (1000.0));
    return 20.0 * std::log10 (std::max (rawA (std::max (hz, 1e-3)), 1e-20)) + norm;
}

juce::String MicSelfTestReport::verdictName (Verdict v)
{
    switch (v)
    {
        case Verdict::ok:       return "OK";
        case Verdict::tooQuiet: return "Too quiet";
        case Verdict::clipping: return "Clipping";
        case Verdict::hum:      return "Mains hum";
        case Verdict::processed:return "Processed input";
        default:                return "No data";
    }
}

bool captureProcessingSuspected (double rmsDbFS, bool absolute, double levelZDbSpl, bool probeToneReceived)
{
    if (rmsDbFS <= kDigitalSilenceDbFS)
        return false;
    if (absolute && levelZDbSpl < kProcessingMaxNoiseZDbSpl)
        return true;
    return probeToneReceived && rmsDbFS < kProcessingMaxNoiseRmsDbFS;
}

MicSelfTestReport analyseMicRecording (const std::vector<float>& x, double sr, const MicCalibration* cal, const EndpointVolume* ep)
{
    MicSelfTestReport r;
    r.sampleRate = sr;
    r.numSamples = (int) x.size();
    if (x.size() < 4096 || sr <= 0.0)
    {
        r.message = "No audio was captured from the input.";
        return r;
    }
    r.seconds = (double) x.size() / sr;

    double sum = 0.0, sq = 0.0, peak = 0.0;
    for (float v : x)
    {
        sum += v;
        sq += (double) v * v;
        const double a = std::abs ((double) v);
        peak = std::max (peak, a);
        if (a >= 0.999) ++r.clippedSamples;
    }
    const double mean = sum / (double) x.size();
    const double ms = sq / (double) x.size();
    r.peakDbFS = db10 (peak * peak);
    r.rmsDbFS = db10 (ms);
    r.dcOffset = mean;
    r.dcDbFS = db10 (mean * mean);
    r.clipping = r.clippedSamples >= 2;

    const bool haveCurve = cal != nullptr && cal->isValid();
    r.absolute = haveCurve && cal->sensitivityDb != 0.0;
    r.sensFactorDb = r.absolute ? cal->sensitivityDb : 0.0;
    if (ep != nullptr && ep->valid)
    {
        r.endpointKnown = true;
        r.endpointDb = ep->levelDb;
        r.endpointScalar = ep->scalar;
    }
    r.recommendedEndpointDb = kRecommendedEndpointDb;
    const double offset = micSplOffsetDb (r.sensFactorDb, r.endpointDb);
    r.fullScaleDbSpl = offset - 10.0 * std::log10 (2.0);
    if (r.endpointKnown && std::abs (r.endpointDb - kRecommendedEndpointDb) > 0.6)
    {
        r.volumeWarning = true;
        r.volumeMessage = "Windows mic level is " + juce::String (r.endpointDb, 1) + " dB (" + juce::String (juce::roundToInt (r.endpointScalar * 100.0))
                        + "%), recommended " + juce::String (kRecommendedEndpointDb, 0) + " dB. Readings are corrected for it, but a higher level lowers headroom (full scale = "
                        + juce::String (r.fullScaleDbSpl, 0) + " dB SPL). Use \"Set to recommended\" and do not change it between calibration and tuning.";
    }

    // Welch power spectrum, Hann, 50% overlap, DC removed. Bin powers sum to the mean square.
    const int N = 32768;
    std::vector<double> win ((size_t) N);
    double sw2 = 0.0;
    for (int i = 0; i < N; ++i)
    {
        win[(size_t) i] = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / N);
        sw2 += win[(size_t) i] * win[(size_t) i];
    }
    const int hop = N / 2;
    std::vector<double> psd ((size_t) N / 2 + 1, 0.0);
    int frames = 0;
    std::vector<float> frame ((size_t) N);
    const int total = (int) x.size();
    for (int start = 0; start == 0 || start + N <= total; start += hop)
    {
        for (int i = 0; i < N; ++i)
        {
            const int idx = start + i;
            frame[(size_t) i] = idx < total ? (float) ((x[(size_t) idx] - mean) * win[(size_t) i]) : 0.0f;
        }
        const auto bins = realFFT (frame.data(), N, N);
        for (int k = 0; k <= N / 2; ++k)
        {
            double p = std::norm (bins[(size_t) k]) / ((double) N * sw2);
            if (k > 0 && k < N / 2) p *= 2.0;
            psd[(size_t) k] += p;
        }
        ++frames;
        if (start + N > total) break;
    }
    for (auto& p : psd) p /= (double) std::max (1, frames);

    const double binHz = sr / N;
    const double fMaxBand = std::min (20000.0, sr * 0.45);
    auto calibratedPower = [&] (int k)    // mean-square after removing the calibration curve
    {
        const double dev = haveCurve ? cal->deviationDb (k * binHz) : 0.0;
        return psd[(size_t) k] * std::pow (10.0, -dev / 10.0);
    };

    double zPow = 0.0, aPow = 0.0;
    for (int k = 1; k <= N / 2; ++k)
    {
        const double f = k * binHz;
        if (f < 20.0 || f > fMaxBand) continue;
        const double p = calibratedPower (k);
        zPow += p;
        aPow += p * std::pow (10.0, aWeightingDb (f) / 10.0);
    }
    r.levelZDbSpl = db10 (zPow) + offset;
    r.levelADbSpl = db10 (aPow) + offset;

    // 1/3-octave bands (base-ten centres 10^(n/10), nominal 20 Hz .. 20 kHz)
    for (int n = 13; n <= 43; ++n)
    {
        const double fc = std::pow (10.0, n / 10.0);
        if (fc > fMaxBand * 1.12) break;
        const double lo = fc / std::pow (2.0, 1.0 / 6.0), hi = fc * std::pow (2.0, 1.0 / 6.0);
        double p = 0.0;
        for (int k = 1; k <= N / 2; ++k)
        {
            const double f = k * binHz;
            if (f >= lo && f < hi) p += calibratedPower (k);
        }
        r.bandCentreHz.push_back (fc);
        r.bandDbSpl.push_back (db10 (p) + offset);
    }

    // Mains hum: narrow tone power vs. the median floor 4..14 Hz away, 50 and 60 Hz, harmonics 1..6
    auto toneExcess = [&] (double f, double& tonePow)
    {
        const int kc = (int) std::lround (f / binHz);
        const int kt = (int) std::ceil (2.0 / binHz);
        double tp = 0.0;
        for (int k = kc - kt; k <= kc + kt; ++k)
            if (k > 0 && k <= N / 2) tp += calibratedPower (k);
        std::vector<double> nb;
        const int span = (int) (14.0 / binHz);
        for (int k = kc - span; k <= kc + span; ++k)
            if (k > 0 && k <= N / 2 && std::abs (k - kc) * binHz >= 4.0)
                nb.push_back (calibratedPower (k));
        if (nb.empty()) return 0.0;
        std::nth_element (nb.begin(), nb.begin() + (long) (nb.size() / 2), nb.end());
        const double floorPerBin = std::max (nb[nb.size() / 2], 1e-20);
        tonePow = tp;
        return db10 ((tp / (double) (2 * kt + 1)) / floorPerBin);
    };

    double bestScore = 0.0;
    for (double fund : { 50.0, 60.0 })
    {
        double sumPow = 0.0, strongest = 0.0;
        int hits = 0;
        juce::StringArray detail;
        for (int h = 1; h <= 6; ++h)
        {
            const double f = fund * h;
            if (f > fMaxBand) break;
            double tp = 0.0;
            const double ex = toneExcess (f, tp);
            if (ex >= 12.0 && db10 (tp) > -110.0)
            {
                ++hits;
                sumPow += tp;
                strongest = std::max (strongest, ex);
                detail.add (juce::String ((int) f) + " Hz +" + juce::String (ex, 0) + " dB");
            }
        }
        const double score = hits * 100.0 + strongest;
        if (hits >= 1 && score > bestScore)
        {
            bestScore = score;
            r.humDetected = true;
            r.humFundamentalHz = fund;
            r.humDbSpl = db10 (sumPow) + offset;
            r.humDetail = detail.joinIntoString (", ");
        }
    }

    // Verdict
    r.processingSuspected = captureProcessingSuspected (r.rmsDbFS, r.absolute, r.levelZDbSpl);
    if (r.processingSuspected)
    {
        r.verdict = MicSelfTestReport::Verdict::processed;
        r.message = kProcessingWarning;
    }
    else if (r.rmsDbFS < -100.0 || (r.absolute && r.levelADbSpl < 10.0))
    {
        r.verdict = MicSelfTestReport::Verdict::tooQuiet;
        r.message = "Input is almost silent (" + juce::String (r.rmsDbFS, 1) + " dBFS RMS). Wrong device or channel, muted, or the mic is unplugged.";
    }
    else if (r.clipping)
    {
        r.verdict = MicSelfTestReport::Verdict::clipping;
        r.message = juce::String (r.clippedSamples) + " samples at full scale. Lower the mic gain or move away from the loud source.";
    }
    else if (r.humDetected)
    {
        r.verdict = MicSelfTestReport::Verdict::hum;
        r.message = "Mains hum at " + juce::String ((int) r.humFundamentalHz) + " Hz (" + r.humDetail
                  + "). Check the USB cable/ground loop, or a nearby power supply.";
    }
    else
    {
        r.verdict = MicSelfTestReport::Verdict::ok;
        r.message = r.absolute ? "Mic is working. Room noise " + juce::String (r.levelADbSpl, 1) + " dBA."
                               : juce::String ("Mic is working, but no calibration sensitivity is loaded so levels are relative.");
        if (r.absolute && r.levelADbSpl > 65.0)
            r.message += " The room is loud; the tuning noise floor will suffer.";
    }
    return r;
}

juce::String MicSelfTestReport::toText() const
{
    juce::String s;
    s << "Verdict        : " << verdictName (verdict) << " - " << message << "\n";
    s << "Sample rate    : " << juce::String (sampleRate, 0) << " Hz (device)";
    if (measuredRate > 0) s << ", " << juce::String (measuredRate, 0) << " Hz measured";
    s << ", " << juce::String (seconds, 2) << " s, " << numSamples << " samples\n";
    s << "Peak / RMS     : " << juce::String (peakDbFS, 1) << " / " << juce::String (rmsDbFS, 1) << " dBFS\n";
    s << "Clipped samples: " << clippedSamples << "\n";
    s << "DC offset      : " << juce::String (dcOffset * 100.0, 4) << " % FS (" << juce::String (dcDbFS, 1) << " dBFS)\n";
    s << "Calibrated     : " << (absolute ? "yes, sens " + juce::String (sensFactorDb, 3) + " dB (SPL = dBFS + 124.02 - sens - endpoint)" : juce::String ("no")) << "\n";
    s << "Windows level  : " << (endpointKnown ? juce::String (endpointDb, 2) + " dB (" + juce::String (endpointScalar * 100.0, 0) + "%)" : juce::String ("unknown")) << "\n";
    if (volumeWarning) s << "Level warning  : " << volumeMessage << "\n";
    s << "Full scale     : " << juce::String (fullScaleDbSpl, 1) << " dB SPL (sine)\n";
    s << "Noise floor    : " << juce::String (levelZDbSpl, 1) << " dB SPL Z, " << juce::String (levelADbSpl, 1) << " dBA\n";
    s << "Hum            : " << (humDetected ? juce::String ((int) humFundamentalHz) + " Hz family, " + humDetail
                                              + ", " + juce::String (humDbSpl, 1) + " dB SPL" : juce::String ("none")) << "\n";
    s << "1/3-octave dB SPL:\n";
    for (size_t i = 0; i < bandCentreHz.size(); ++i)
        s << "  " << juce::String (bandCentreHz[i], 0).paddedLeft (' ', 6) << " Hz  " << juce::String (bandDbSpl[i], 1) << "\n";
    return s;
}
} // namespace fa
