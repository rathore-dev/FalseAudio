#include "Analysis.h"
#include "../dsp/Filters.h"
#include "../dsp/Generators.h"

namespace fa
{
namespace analysis
{
namespace
{
    /** Subset of the analysis grid inside [fLow, fHigh] (a log grid, so it can be smoothed). */
    std::vector<double> gridBetween (double fLow, double fHigh)
    {
        std::vector<double> out;
        for (auto f : analysisFrequencies())
            if (f >= fLow && f <= fHigh)
                out.push_back (f);
        return out;
    }

    std::vector<double> scaled (const std::vector<double>& f, double k)
    {
        auto out = f;
        for (auto& x : out)
            x *= k;
        return out;
    }

    /** Linear-peak sample of the IR (after time zero). */
    double linearPeak (const Measurement& m)
    {
        return findPeak (m.ir, m.zeroSample);
    }

    /** Runs x through the biquads of a filter chain (direct form II transposed). */
    std::vector<double> filtered (const std::vector<float>& x, const std::vector<FilterSpec>& chain, double fs)
    {
        std::vector<double> y (x.begin(), x.end());
        for (auto& spec : chain)
        {
            for (auto& c : spec.toBiquads (fs))
            {
                double z1 = 0.0, z2 = 0.0;
                for (auto& s : y)
                {
                    const double in = s;
                    const double out = c.b0 * in + z1;
                    z1 = c.b1 * in - c.a1 * out + z2;
                    z2 = c.b2 * in - c.a2 * out;
                    s = out;
                }
            }
        }
        return y;
    }
}

//==============================================================================
Distortion harmonicDistortion (const Measurement& m, double sweepSeconds, double sweepFStart,
                               double sweepFEnd, int maxHarmonic)
{
    Distortion d;
    const double fs = m.sampleRate;
    if (m.ir.empty() || fs <= 0.0 || sweepSeconds <= 0.0 || sweepFEnd <= sweepFStart * 2.0 || maxHarmonic < 2)
        return d;

    LogSweep sweep;
    sweep.sampleRate = fs;
    sweep.seconds = sweepSeconds;
    sweep.fStart = sweepFStart;
    sweep.fEnd = sweepFEnd;
    auto advance = [&sweep] (int k) { return harmonicAdvanceSeconds (sweep, k); };

    const double peak = linearPeak (m);

    // Window: a short rise before each impulse, and a right side that stops well before the next
    // (later) impulse. The smallest gap is between the highest harmonic and the one above it.
    const double leftS = 0.002;
    int K = std::min (maxHarmonic, 6);
    while (K >= 2 && peak - advance (K) * fs - 2.0 * leftS * fs < 0.0)
        --K;                                     // not enough pre-roll for this harmonic
    if (K < 2)
        return d;

    const double minGap = advance (K + 1) - advance (K);
    const double rightS = juce::jlimit (0.005, 0.3, 0.85 * minGap - leftS);
    const IRWindow window { leftS * 1000.0, rightS * 1000.0 };

    // Highest frequency with valid harmonic data: the harmonic sweep ends at fEnd (and Nyquist).
    const double fTop = std::min (sweepFEnd, 0.45 * fs);
    const double fLow = std::max ({ sweepFStart * 1.5, 20.0, 2.0 / rightS });
    const auto dense = gridBetween (fLow, fTop / 2.0);
    if (dense.size() < 8)
        return d;

    auto magnitudeDb = [&] (int k)
    {
        const int ref = (int) std::round (peak - advance (k) * fs);
        auto r = impulseToResponse (m.ir, fs, scaled (dense, (double) k), ref, window);
        r = smoothFractionalOctave (r, 6.0);
        return r.magnitudeDb();
    };

    const auto fundamental = magnitudeDb (1);

    std::vector<std::vector<double>> harmonics;
    for (int k = 2; k <= K; ++k)
    {
        auto h = magnitudeDb (k);
        for (size_t i = 0; i < dense.size(); ++i)
            h[i] = dense[i] * k <= fTop ? h[i] - fundamental[i] : -120.0;
        harmonics.push_back (std::move (h));
    }

    // Decimate to 24 points per octave for display.
    constexpr size_t step = 4;
    for (size_t i = 0; i < dense.size(); i += step)
    {
        d.freqs.push_back (dense[i]);
        double power = 0.0;
        for (auto& h : harmonics)
            if (h[i] > -119.0)
                power += std::pow (10.0, h[i] / 10.0);
        d.thdPercent.push_back (100.0 * std::sqrt (power));
    }
    d.harmonicDb.assign (harmonics.size(), {});
    for (size_t k = 0; k < harmonics.size(); ++k)
        for (size_t i = 0; i < dense.size(); i += step)
            d.harmonicDb[k].push_back (harmonics[k][i]);

    d.valid = true;
    return d;
}

//==============================================================================
Waterfall spectralDecay (const Measurement& m, double durationMs, int slices, double fLow, double fHigh)
{
    Waterfall w;
    const double fs = m.sampleRate;
    if (m.ir.empty() || fs <= 0.0 || slices < 1 || durationMs <= 0.0)
        return w;

    fHigh = std::min (fHigh, 0.48 * fs);
    if (fHigh <= fLow)
        return w;

    const auto dense = makeLogFrequencies (fLow, fHigh, 48);
    for (size_t i = 0; i < dense.size(); i += 2)
        w.freqs.push_back (dense[i]);

    const double peak = linearPeak (m);
    const IRWindow window { 0.5, durationMs }; // 0.5 ms rise, then a fixed-length right side

    double ref = -1.0e9;
    for (int s = 0; s < slices; ++s)
    {
        const double tMs = durationMs * s / slices;
        const int start = (int) std::round (peak + tMs * 0.001 * fs);
        if (start >= (int) m.ir.size() - 2)
            break;

        auto r = smoothFractionalOctave (impulseToResponse (m.ir, fs, dense, start, window), 12.0);
        std::vector<double> levels;
        for (size_t i = 0; i < dense.size(); i += 2)
            levels.push_back (r.magnitudeDb (i));

        if (s == 0)
            for (auto l : levels)
                ref = std::max (ref, l);

        for (auto& l : levels)
            l = std::max (l - ref, -150.0);

        w.timesMs.push_back (tMs);
        w.levelDb.push_back (std::move (levels));
    }
    return w;
}

//==============================================================================
std::vector<float> stepResponse (const Measurement& m)
{
    std::vector<float> out (m.ir.size(), 0.0f);
    if (m.ir.empty())
        return out;

    // Start just before the arrival so pre-roll noise (and Farina harmonics) don't accumulate.
    const double peak = linearPeak (m);
    const int start = std::max (0, std::min ((int) std::round (peak - 0.01 * m.sampleRate), (int) m.ir.size() - 1));

    double acc = 0.0, maxAbs = 0.0;
    for (size_t i = (size_t) start; i < m.ir.size(); ++i)
    {
        acc += m.ir[i];
        out[i] = (float) acc;
        maxAbs = std::max (maxAbs, std::abs (acc));
    }
    if (maxAbs > 0.0)
        for (auto& x : out)
            x = (float) (x / maxAbs);
    return out;
}

//==============================================================================
std::vector<float> energyTimeCurve (const Measurement& m)
{
    const int len = (int) m.ir.size();
    std::vector<float> out ((size_t) len, -150.0f);
    if (len < 4)
        return out;

    // Hilbert transform: -j * X for positive frequencies (inverseRealFFT supplies the negative half).
    const int n = nextPowerOfTwo (len);
    auto bins = realFFT (m.ir.data(), len, n);
    bins.front() = 0.0;
    bins.back() = 0.0;
    for (size_t k = 1; k + 1 < bins.size(); ++k)
        bins[k] *= cplx (0.0, -1.0);
    const auto hil = inverseRealFFT (bins, n);

    double maxEnv = 0.0;
    std::vector<double> env ((size_t) len);
    for (int i = 0; i < len; ++i)
    {
        const double x = m.ir[(size_t) i];
        env[(size_t) i] = x * x + hil[(size_t) i] * hil[(size_t) i];
        maxEnv = std::max (maxEnv, env[(size_t) i]);
    }
    if (maxEnv <= 0.0)
        return out;

    for (int i = 0; i < len; ++i)
        out[(size_t) i] = (float) std::max (-150.0, dbFromPower (env[(size_t) i] / maxEnv));
    return out;
}

//==============================================================================
std::vector<std::pair<double, double>> decayTimes (const Measurement& m)
{
    std::vector<std::pair<double, double>> out;
    const double fs = m.sampleRate;
    if (m.ir.size() < (size_t) (0.1 * fs) || fs <= 0.0)
        return out;

    for (double fc : { 63.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0 })
    {
        if (fc * std::sqrt (2.0) >= 0.45 * fs)
            break;

        const auto y = filtered (m.ir, { FilterSpec::crossover (FilterType::highPass, fc / std::sqrt (2.0), 24, Alignment::butterworth),
                                         FilterSpec::crossover (FilterType::lowPass,  fc * std::sqrt (2.0), 24, Alignment::butterworth) }, fs);
        const int len = (int) y.size();

        int peak = juce::jlimit (0, len - 1, m.zeroSample);
        double peakPow = 0.0;
        for (int i = peak; i < len; ++i)
            if (y[(size_t) i] * y[(size_t) i] > peakPow)
                { peakPow = y[(size_t) i] * y[(size_t) i]; peak = i; }
        if (peakPow <= 0.0)
            continue;

        // Noise power from the last 10 % of the IR.
        const int tail = std::max (1, len / 10);
        double noise = 0.0;
        for (int i = len - tail; i < len; ++i)
            noise += y[(size_t) i] * y[(size_t) i];
        noise /= tail;

        // Truncate where the 10 ms smoothed energy reaches 3 dB above the noise.
        const int avg = std::max (1, (int) (0.01 * fs));
        int end = len;
        double run = 0.0;
        for (int i = peak; i < len; ++i)
        {
            run += y[(size_t) i] * y[(size_t) i];
            if (i - peak >= avg)
            {
                run -= y[(size_t) (i - avg)] * y[(size_t) (i - avg)];
                if (run / avg < 2.0 * noise)
                    { end = i; break; }
            }
        }

        // Schroeder backward integration, noise-compensated.
        std::vector<double> edc ((size_t) (end - peak) + 1, 0.0);
        double acc = 0.0;
        for (int i = end - 1; i >= peak; --i)
        {
            acc += std::max (0.0, y[(size_t) i] * y[(size_t) i] - noise);
            edc[(size_t) (i - peak)] = acc;
        }
        if (acc <= 0.0)
            continue;

        // Linear fit of the EDC between -5 and -25 dB (T20).
        double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
        int n = 0;
        bool reached = false;
        for (size_t i = 0; i + 1 < edc.size(); ++i)
        {
            const double db = dbFromPower (edc[i] / acc);
            if (db > -5.0)
                continue;
            if (db < -25.0)
                { reached = true; break; }
            const double t = (double) i / fs;
            sx += t; sy += db; sxx += t * t; sxy += t * db;
            ++n;
        }
        if (! reached || n < 8)
            continue;

        const double slope = (n * sxy - sx * sy) / std::max (1.0e-30, n * sxx - sx * sx); // dB/s
        if (slope < -1.0e-3)
            out.emplace_back (fc, -60.0 / slope);
    }
    return out;
}
} // namespace analysis
} // namespace fa
