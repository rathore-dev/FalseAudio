#include "LiveAnalyzer.h"
#include "../dsp/Spectrum.h"

namespace fa
{
LiveAnalyzer::LiveAnalyzer (AudioEngine& e) : engine (e) {}

void LiveAnalyzer::setSettings (const Settings& s)
{
    const bool sizeChanged = s.fftSize != settings.fftSize;
    settings = s;
    settings.fftSize = nextPowerOfTwo (juce::jlimit (1024, 1 << 17, s.fftSize));
    if (sizeChanged)
        reset();
}

void LiveAnalyzer::reset()
{
    rtaAvg.clear();
    sxyAvg.clear();
    sxxAvg.clear();
    syyAvg.clear();
}

double LiveAnalyzer::averagingFactor (juce::uint32& lastTime) const
{
    const auto now = juce::Time::getMillisecondCounter();
    const double dt = lastTime == 0 ? 1.0 : (now - lastTime) / 1000.0;
    lastTime = now;
    if (settings.averagingTimeS <= 0.0)
        return 1.0;
    return juce::jlimit (0.01, 1.0, 1.0 - std::exp (-dt / settings.averagingTimeS));
}

std::vector<float> LiveAnalyzer::window (int n) const
{
    std::vector<float> w ((size_t) n, 1.0f);
    if (settings.hannWindow)
        for (int i = 0; i < n; ++i)
            w[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * pi * i / n));
    return w;
}

bool LiveAnalyzer::readMic (int micIndex, std::vector<float>& dest, int extra) const
{
    const auto routing = engine.getRouting();
    if (micIndex < 0 || micIndex >= (int) routing.micInputs.size())
        return false;
    dest.resize ((size_t) (settings.fftSize + extra));
    return engine.copyLatestInput (routing.micInputs[(size_t) micIndex], dest.data(), (int) dest.size());
}

FrequencyResponse LiveAnalyzer::computeRTA (int micIndex)
{
    const int n = settings.fftSize;
    std::vector<float> x;
    if (! readMic (micIndex, x))
        return {};

    const auto w = window (n);
    double wPower = 0.0;
    for (int i = 0; i < n; ++i)
    {
        x[(size_t) i] *= w[(size_t) i];
        wPower += (double) w[(size_t) i] * w[(size_t) i];
    }

    const auto bins = realFFT (x.data(), n, n);
    if (rtaAvg.size() != bins.size())
        rtaAvg.assign (bins.size(), 0.0);

    // Power per bin, normalised so a full-scale sine reads 0 dBFS (RMS -3 dB) in its bin.
    const double alpha = averagingFactor (lastRta);
    for (size_t k = 0; k < bins.size(); ++k)
    {
        const double p = 2.0 * std::norm (bins[k]) / (wPower * n);
        rtaAvg[k] = rtaAvg[k] + alpha * (p - rtaAvg[k]);
    }

    // Integrate power into fractional-octave bands centred on the analysis grid.
    const auto& f = analysisFrequencies();
    const double fs = engine.getSampleRate();
    const double binHz = fs / n;
    const double frac = settings.smoothingFraction > 0.0 ? settings.smoothingFraction : 48.0;
    const double halfBand = std::pow (2.0, 0.5 / frac);
    const auto cal = engine.getCalibration (micIndex);

    FrequencyResponse out;
    out.freqs = f;
    out.values.resize (f.size());
    for (size_t i = 0; i < f.size(); ++i)
    {
        const double lo = f[i] / halfBand, hi = f[i] * halfBand;
        const int k0 = std::max (1, (int) std::ceil (lo / binHz));
        const int k1 = std::min ((int) rtaAvg.size() - 1, (int) std::floor (hi / binHz));

        double p = 0.0;
        if (k1 >= k0)
            for (int k = k0; k <= k1; ++k)
                p += rtaAvg[(size_t) k];
        else // band narrower than a bin: interpolate the density
            p = rtaAvg[(size_t) juce::jlimit (0, (int) rtaAvg.size() - 1, (int) std::round (f[i] / binHz))] * (hi - lo) / binHz;

        double db = dbFromPower (p) + settings.splOffsetDb;
        if (cal.isValid())
            db -= cal.deviationDb (f[i]);
        out.values[i] = cplx (magFromDb (db), 0.0);
    }
    return out;
}

LiveAnalyzer::TransferFunction LiveAnalyzer::computeTransferFunction (int micIndex)
{
    TransferFunction tf;
    tf.delayMs = delayCompMs;

    const int n = settings.fftSize;
    const int delaySamples = juce::jmax (0, (int) std::round (delayCompMs * 0.001 * engine.getSampleRate()));

    std::vector<float> y, xFull ((size_t) (n + delaySamples));
    if (! readMic (micIndex, y) || ! engine.copyLatestReference (xFull.data(), (int) xFull.size()))
        return tf;

    // Reference delayed so it lines up with the (later) acoustic arrival at the mic.
    std::vector<float> x (xFull.begin(), xFull.begin() + n);

    const auto w = window (n);
    for (int i = 0; i < n; ++i)
    {
        x[(size_t) i] *= w[(size_t) i];
        y[(size_t) i] *= w[(size_t) i];
    }

    const auto X = realFFT (x.data(), n, n);
    const auto Y = realFFT (y.data(), n, n);

    if (sxyAvg.size() != X.size())
    {
        sxyAvg.assign (X.size(), cplx (0.0, 0.0));
        sxxAvg.assign (X.size(), 0.0);
        syyAvg.assign (X.size(), 0.0);
    }

    const double alpha = averagingFactor (lastTf);
    for (size_t k = 0; k < X.size(); ++k)
    {
        sxyAvg[k] += alpha * (Y[k] * std::conj (X[k]) - sxyAvg[k]);
        sxxAvg[k] += alpha * (std::norm (X[k]) - sxxAvg[k]);
        syyAvg[k] += alpha * (std::norm (Y[k]) - syyAvg[k]);
    }

    const auto& f = analysisFrequencies();
    const double binHz = engine.getSampleRate() / n;
    const double frac = settings.smoothingFraction > 0.0 ? settings.smoothingFraction : 48.0;
    const double halfBand = std::pow (2.0, 0.5 / frac);
    const auto cal = engine.getCalibration (micIndex);

    tf.h.freqs = f;
    tf.h.values.resize (f.size());
    tf.coherence.resize (f.size());
    for (size_t i = 0; i < f.size(); ++i)
    {
        const int k0 = std::max (1, (int) std::floor (f[i] / halfBand / binHz));
        const int k1 = std::min ((int) X.size() - 1, std::max (k0, (int) std::ceil (f[i] * halfBand / binHz)));

        cplx sxy (0.0, 0.0);
        double sxx = 0.0, syy = 0.0;
        for (int k = k0; k <= k1; ++k)
        {
            sxy += sxyAvg[(size_t) k];
            sxx += sxxAvg[(size_t) k];
            syy += syyAvg[(size_t) k];
        }

        cplx h = sxx > 0.0 ? sxy / sxx : cplx (0.0, 0.0);
        if (cal.isValid())
            h /= std::polar (magFromDb (cal.deviationDb (f[i])), cal.deviationPhaseDeg (f[i]) * pi / 180.0);
        tf.h.values[i] = h;
        tf.coherence[i] = (sxx > 0.0 && syy > 0.0) ? juce::jlimit (0.0, 1.0, std::norm (sxy) / (sxx * syy)) : 0.0;
    }
    return tf;
}

double LiveAnalyzer::findDelay (int micIndex)
{
    // Cross-correlate a long block via the averaged cross spectrum's impulse response.
    const int n = 1 << 16;
    const auto routing = engine.getRouting();
    if (micIndex < 0 || micIndex >= (int) routing.micInputs.size())
        return delayCompMs;

    std::vector<float> x ((size_t) n), y ((size_t) n);
    if (! engine.copyLatestReference (x.data(), n) || ! engine.copyLatestInput (routing.micInputs[(size_t) micIndex], y.data(), n))
        return delayCompMs;

    const auto X = realFFT (x.data(), n, n);
    const auto Y = realFFT (y.data(), n, n);
    std::vector<cplx> H (X.size());
    for (size_t k = 0; k < X.size(); ++k)
        H[k] = Y[k] * std::conj (X[k]) / (std::norm (X[k]) + 1e-9);
    const auto ir = inverseRealFFT (H, n);

    // Only positive lags up to 0.5 s are physical.
    const int maxLag = std::min (n / 2, (int) (0.5 * engine.getSampleRate()));
    int best = 0;
    for (int i = 1; i < maxLag; ++i)
        if (std::abs (ir[(size_t) i]) > std::abs (ir[(size_t) best]))
            best = i;

    delayCompMs = best / engine.getSampleRate() * 1000.0;
    reset();
    return delayCompMs;
}

double LiveAnalyzer::computeLevel (int micIndex, Weighting wt)
{
    auto rta = computeRTA (micIndex);
    if (rta.isEmpty())
        return -120.0;

    auto weightDb = [wt] (double f)
    {
        const double f2 = f * f;
        if (wt == Weighting::A)
        {
            const double ra = (12194.0 * 12194.0 * f2 * f2)
                            / ((f2 + 20.6 * 20.6) * std::sqrt ((f2 + 107.7 * 107.7) * (f2 + 737.9 * 737.9)) * (f2 + 12194.0 * 12194.0));
            return 20.0 * std::log10 (ra) + 2.0;
        }
        if (wt == Weighting::C)
        {
            const double rc = (12194.0 * 12194.0 * f2) / ((f2 + 20.6 * 20.6) * (f2 + 12194.0 * 12194.0));
            return 20.0 * std::log10 (rc) + 0.06;
        }
        return 0.0;
    };

    // The RTA bands overlap by the smoothing fraction; sum unique 1/frac-octave bands.
    const double frac = settings.smoothingFraction > 0.0 ? settings.smoothingFraction : 48.0;
    const int stride = std::max (1, (int) std::round (96.0 / frac));
    double p = 0.0;
    for (size_t i = 0; i < rta.size(); i += (size_t) stride)
        if (rta.freqs[i] >= 10.0 && rta.freqs[i] <= 20000.0)
            p += std::pow (10.0, (rta.magnitudeDb (i) + weightDb (rta.freqs[i])) / 10.0);
    return dbFromPower (p);
}
} // namespace fa
