#include "Spectrum.h"
#include <functional>

namespace fa
{
int nextPowerOfTwo (int n)
{
    int p = 1;
    while (p < n)
        p <<= 1;
    return p;
}

namespace
{
    int log2Int (int n)
    {
        int o = 0;
        while ((1 << o) < n)
            ++o;
        return o;
    }

    // Simple iterative radix-2 complex FFT in double precision. juce::dsp::FFT is float-only,
    // and deconvolution of long sweeps benefits from double precision.
    void fftInPlace (std::vector<cplx>& a, bool inverse)
    {
        const size_t n = a.size();
        for (size_t i = 1, j = 0; i < n; ++i)
        {
            size_t bit = n >> 1;
            for (; j & bit; bit >>= 1)
                j ^= bit;
            j ^= bit;
            if (i < j)
                std::swap (a[i], a[j]);
        }

        for (size_t len = 2; len <= n; len <<= 1)
        {
            const double ang = 2.0 * pi / (double) len * (inverse ? 1.0 : -1.0);
            const cplx wlen (std::cos (ang), std::sin (ang));
            for (size_t i = 0; i < n; i += len)
            {
                cplx w (1.0, 0.0);
                for (size_t k = 0; k < len / 2; ++k)
                {
                    const cplx u = a[i + k];
                    const cplx v = a[i + k + len / 2] * w;
                    a[i + k] = u + v;
                    a[i + k + len / 2] = u - v;
                    w *= wlen;
                }
            }
        }

        if (inverse)
            for (auto& x : a)
                x /= (double) n;
    }
}

std::vector<cplx> realFFT (const float* x, int numSamples, int fftSize)
{
    jassert (fftSize == nextPowerOfTwo (fftSize));
    std::vector<cplx> a ((size_t) fftSize, cplx (0.0, 0.0));
    for (int i = 0; i < std::min (numSamples, fftSize); ++i)
        a[(size_t) i] = cplx (x[i], 0.0);

    fftInPlace (a, false);
    a.resize ((size_t) fftSize / 2 + 1);
    return a;
}

std::vector<double> inverseRealFFT (const std::vector<cplx>& bins, int fftSize)
{
    std::vector<cplx> a ((size_t) fftSize);
    const size_t half = (size_t) fftSize / 2;
    for (size_t k = 0; k <= half && k < bins.size(); ++k)
        a[k] = bins[k];
    for (size_t k = 1; k < half; ++k)
        a[(size_t) fftSize - k] = std::conj (a[k]);

    fftInPlace (a, true);

    std::vector<double> out ((size_t) fftSize);
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = a[i].real();
    return out;
}

FrequencyResponse impulseToResponse (const std::vector<float>& ir,
                                     double fs,
                                     const std::vector<double>& freqs,
                                     int referenceSample,
                                     const IRWindow& window)
{
    const int left  = (int) std::round (window.leftMs * 0.001 * fs);
    const int right = (int) std::round (window.rightMs * 0.001 * fs);
    const int start = std::max (0, referenceSample - left);
    const int end   = std::min ((int) ir.size(), referenceSample + right);
    const int len   = std::max (1, end - start);

    std::vector<float> seg ((size_t) len);
    const int taperR = std::max (1, right / 4);
    for (int i = 0; i < len; ++i)
    {
        const int n = start + i;
        double w = 1.0;
        if (n < referenceSample && left > 0)
        {
            const double t = (double) (n - (referenceSample - left)) / (double) left; // 0..1
            w = 0.5 - 0.5 * std::cos (pi * juce::jlimit (0.0, 1.0, t));
        }
        else
        {
            const int fromEnd = end - n;
            if (fromEnd < taperR)
                w = 0.5 - 0.5 * std::cos (pi * (double) fromEnd / (double) taperR);
        }
        seg[(size_t) i] = (float) (ir[(size_t) n] * w);
    }

    // Plenty of zero padding so low frequencies get dense bins.
    const int fftSize = nextPowerOfTwo (std::max (len, (int) (fs / 2.0)) * 2);
    const auto bins = realFFT (seg.data(), len, fftSize);

    const double binHz = fs / fftSize;
    const double shift = (double) (referenceSample - start) / fs; // re-reference phase to the peak

    FrequencyResponse r;
    r.freqs = freqs;
    r.values.resize (freqs.size());
    for (size_t i = 0; i < freqs.size(); ++i)
    {
        const double pos = freqs[i] / binHz;
        const size_t k0 = (size_t) std::floor (pos);
        const size_t k1 = std::min (k0 + 1, bins.size() - 1);
        const double t = pos - (double) k0;

        // Interpolate after removing the linear phase of the segment start, which varies fast.
        const cplx b0 = bins[std::min (k0, bins.size() - 1)] * std::polar (1.0, 2.0 * pi * (double) k0 * binHz * shift);
        const cplx b1 = bins[k1] * std::polar (1.0, 2.0 * pi * (double) k1 * binHz * shift);
        r.values[i] = b0 * (1.0 - t) + b1 * t;
    }
    return r;
}

namespace
{
    double pointsPerOctave (const FrequencyResponse& r)
    {
        if (r.size() < 2)
            return 1.0;
        return (double) (r.size() - 1) / std::log2 (r.freqs.back() / r.freqs.front());
    }

    FrequencyResponse smoothWithFraction (const FrequencyResponse& r, const std::function<double (double)>& fractionAt, bool cubicMean)
    {
        FrequencyResponse out = r;
        const double ppo = pointsPerOctave (r);
        const auto n = (int) r.size();

        std::vector<double> power ((size_t) n);
        for (int i = 0; i < n; ++i)
            power[(size_t) i] = std::norm (r.values[(size_t) i]);

        for (int i = 0; i < n; ++i)
        {
            const double frac = fractionAt (r.freqs[(size_t) i]);
            const int half = std::max (0, (int) std::round (ppo / frac / 2.0));
            const int a = std::max (0, i - half), b = std::min (n - 1, i + half);

            double acc = 0.0, wsum = 0.0;
            for (int k = a; k <= b; ++k)
            {
                // Hann-shaped kernel in log frequency
                const double x = half > 0 ? (double) (k - i) / (double) (half + 1) : 0.0;
                const double w = 0.5 + 0.5 * std::cos (pi * x);
                acc += w * (cubicMean ? std::pow (power[(size_t) k], 1.5) : power[(size_t) k]);
                wsum += w;
            }
            double p = acc / wsum;
            if (cubicMean)
                p = std::pow (p, 1.0 / 1.5);

            out.values[(size_t) i] = std::polar (std::sqrt (p), std::arg (r.values[(size_t) i]));
        }
        return out;
    }
}

FrequencyResponse smoothFractionalOctave (const FrequencyResponse& r, double fraction)
{
    return smoothWithFraction (r, [fraction] (double) { return fraction; }, false);
}

FrequencyResponse smoothVariable (const FrequencyResponse& r)
{
    return smoothWithFraction (r, [] (double f)
    {
        if (f <= 100.0)    return 48.0;
        if (f >= 10000.0)  return 3.0;
        const double t = std::log10 (f / 100.0) / 2.0; // 0..1 over 100 Hz..10 kHz
        return 48.0 * std::pow (3.0 / 48.0, t);
    }, false);
}

FrequencyResponse smoothPsychoacoustic (const FrequencyResponse& r)
{
    return smoothWithFraction (r, [] (double f)
    {
        if (f <= 100.0)   return 3.0;
        if (f >= 1000.0)  return 6.0;
        return 3.0 + 3.0 * std::log10 (f / 100.0);
    }, true);
}

std::vector<double> unwrappedPhase (const FrequencyResponse& r)
{
    std::vector<double> ph (r.size());
    double offset = 0.0;
    for (size_t i = 0; i < r.size(); ++i)
    {
        const double p = std::arg (r.values[i]);
        if (i > 0)
            offset -= 2.0 * pi * std::round ((p - std::arg (r.values[i - 1])) / (2.0 * pi));
        ph[i] = p + offset;
    }
    return ph;
}

std::vector<double> groupDelayMs (const FrequencyResponse& r)
{
    const auto ph = unwrappedPhase (r);
    std::vector<double> gd (r.size(), 0.0);
    for (size_t i = 1; i + 1 < r.size(); ++i)
    {
        const double dw = 2.0 * pi * (r.freqs[i + 1] - r.freqs[i - 1]);
        gd[i] = -(ph[i + 1] - ph[i - 1]) / dw * 1000.0;
    }
    if (r.size() > 2)
    {
        gd.front() = gd[1];
        gd.back()  = gd[r.size() - 2];
    }
    return gd;
}

FrequencyResponse minimumPhaseOf (const FrequencyResponse& r)
{
    // Resample log magnitude on a linear grid, compute the real cepstrum, fold it to get the
    // minimum-phase spectrum, then read back onto the original log grid.
    const double fs = std::max (48000.0, r.freqs.back() * 2.2);
    const int n = 1 << 16;
    const double binHz = fs / n;

    std::vector<cplx> logMag ((size_t) n);
    for (int k = 0; k <= n / 2; ++k)
    {
        const double f = std::max (k * binHz, r.freqs.front());
        const double m = std::abs (r.interpolate (std::min (f, r.freqs.back())));
        logMag[(size_t) k] = cplx (std::log (std::max (m, 1e-9)), 0.0);
    }
    for (int k = 1; k < n / 2; ++k)
        logMag[(size_t) (n - k)] = logMag[(size_t) k];

    fftInPlace (logMag, true); // real cepstrum
    for (int i = 1; i < n / 2; ++i)
    {
        logMag[(size_t) i] *= 2.0;
        logMag[(size_t) (n - i)] = 0.0;
    }
    fftInPlace (logMag, false);

    FrequencyResponse out = r;
    for (size_t i = 0; i < r.size(); ++i)
    {
        const double pos = r.freqs[i] / binHz;
        const size_t k = std::min ((size_t) std::round (pos), (size_t) n / 2);
        out.values[i] = std::polar (std::abs (r.values[i]), logMag[k].imag());
    }
    return out;
}
} // namespace fa
