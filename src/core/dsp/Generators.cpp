#include "Generators.h"
#include "Spectrum.h"

namespace fa
{
std::vector<float> LogSweep::generate() const
{
    const int n = lengthSamples();
    std::vector<float> out ((size_t) totalSamples(), 0.0f);

    const double amp = magFromDb (levelDbFS);
    const double w1 = 2.0 * pi * fStart;
    const double w2 = 2.0 * pi * fEnd;
    const double k = std::log (w2 / w1);
    const double fadeIn  = std::min (0.05 * sampleRate, n * 0.05);
    const double fadeOut = std::min (0.005 * sampleRate, n * 0.01);

    for (int i = 0; i < n; ++i)
    {
        const double t = i / sampleRate;
        double s = std::sin (w1 * seconds / k * (std::exp (t / seconds * k) - 1.0));

        double g = 1.0;
        if (i < fadeIn)         g = 0.5 - 0.5 * std::cos (pi * i / fadeIn);
        if (n - i < fadeOut)    g = 0.5 - 0.5 * std::cos (pi * (n - i) / fadeOut);

        out[(size_t) i] = (float) (amp * g * s);
    }
    return out;
}

std::vector<float> makePeriodicPinkNoise (int period, double fs, double levelDbFS,
                                          double fLow, double fHigh, juce::uint32 seed)
{
    jassert (period == nextPowerOfTwo (period));
    juce::Random rng ((juce::int64) seed);

    std::vector<cplx> bins ((size_t) period / 2 + 1, cplx (0.0, 0.0));
    const double binHz = fs / period;
    for (size_t k = 1; k < bins.size(); ++k)
    {
        const double f = k * binHz;
        if (f < fLow || f > fHigh)
            continue;
        bins[k] = std::polar (1.0 / std::sqrt (f), rng.nextDouble() * 2.0 * pi);
    }

    auto x = inverseRealFFT (bins, period);

    double rms = 0.0;
    for (auto v : x)
        rms += v * v;
    rms = std::sqrt (rms / (double) x.size());

    const double gain = magFromDb (levelDbFS) / std::max (rms, 1e-12);
    std::vector<float> out (x.size());
    for (size_t i = 0; i < x.size(); ++i)
        out[i] = (float) juce::jlimit (-1.0, 1.0, x[i] * gain);
    return out;
}

std::vector<float> deconvolve (const std::vector<float>& recorded,
                               const std::vector<float>& reference,
                               double fs, double fLow, double fHigh)
{
    const int n = nextPowerOfTwo ((int) (recorded.size() + reference.size()));
    const auto Y = realFFT (recorded.data(), (int) recorded.size(), n);
    const auto X = realFFT (reference.data(), (int) reference.size(), n);

    double maxPow = 0.0;
    for (auto& x : X)
        maxPow = std::max (maxPow, std::norm (x));

    // Tikhonov regularisation, stronger outside the swept band so we don't amplify noise.
    const double binHz = fs / n;
    std::vector<cplx> H (Y.size());
    for (size_t k = 0; k < Y.size(); ++k)
    {
        const double f = k * binHz;
        const bool inBand = f >= fLow * 0.8 && f <= fHigh * 1.05;
        const double eps = maxPow * (inBand ? 1e-6 : 1e-2);
        H[k] = Y[k] * std::conj (X[k]) / (std::norm (X[k]) + eps);
    }

    const auto h = inverseRealFFT (H, n);

    // Keep the causal part plus a little pre-roll (wrapped from the end) for the left window.
    const int preRoll = deconvolutionPreRoll (fs);
    const int keep = (int) recorded.size();
    std::vector<float> ir ((size_t) (keep + preRoll));
    for (int i = 0; i < preRoll; ++i)
        ir[(size_t) i] = (float) h[(size_t) (n - preRoll + i)];
    for (int i = 0; i < keep; ++i)
        ir[(size_t) (preRoll + i)] = (float) h[(size_t) i];
    return ir;
}
} // namespace fa
