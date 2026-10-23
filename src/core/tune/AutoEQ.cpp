#include "AutoEQ.h"
#include "../dsp/Spectrum.h"

namespace fa
{
namespace
{
    struct Band
    {
        double logF, gain, logQ;
        std::vector<double> db; // cached response on the grid
    };

    struct Problem
    {
        const AutoEQ::Settings& s;
        std::vector<double> f, m, t, w, dipW;
        double offset = 0.0;

        void computeBand (Band& b) const
        {
            const auto c = BiquadCoeffs::peaking (std::pow (2.0, b.logF), std::pow (2.0, b.logQ), b.gain, s.sampleRate);
            b.db.resize (f.size());
            for (size_t i = 0; i < f.size(); ++i)
                b.db[i] = dbFromMag (std::abs (c.response (f[i], s.sampleRate)));
        }

        double boostLimit (double freq) const
        {
            return (s.lfBoostLimitBelowHz > 0.0 && freq < s.lfBoostLimitBelowHz)
                       ? std::min (s.maxBoostDb, s.lfBoostLimitDb) : s.maxBoostDb;
        }

        void clamp (Band& b) const
        {
            b.logF = juce::jlimit (std::log2 (s.fLow), std::log2 (s.fHigh), b.logF);
            b.logQ = juce::jlimit (std::log2 (s.minQ), std::log2 (s.maxQ), b.logQ);
            b.gain = juce::jlimit (s.maxCutDb, boostLimit (std::pow (2.0, b.logF)), b.gain);
        }

        std::vector<double> errorWith (const std::vector<Band>& bands) const
        {
            std::vector<double> e (f.size());
            for (size_t i = 0; i < f.size(); ++i)
            {
                double eq = 0.0;
                for (auto& b : bands)
                    eq += b.db[i];
                e[i] = m[i] + offset + eq - t[i];
            }
            return e;
        }

        double cost (const std::vector<Band>& bands) const
        {
            const auto e = errorWith (bands);
            double c = 0.0, ws = 0.0;
            for (size_t i = 0; i < e.size(); ++i)
            {
                const double wi = e[i] > 0.0 ? w[i] : w[i] * dipW[i];
                c += wi * e[i] * e[i];
                ws += w[i];
            }
            c /= std::max (ws, 1e-9);

            // Regularisation: prefer cuts over boosts and broad filters over surgical ones.
            for (auto& b : bands)
            {
                if (b.gain > 0.0)
                    c += 0.02 * b.gain * b.gain;
                c += 0.01 * std::max (0.0, b.logQ - 2.0);
            }
            return c;
        }

        double rms (const std::vector<Band>& bands) const
        {
            const auto e = errorWith (bands);
            double acc = 0.0, ws = 0.0;
            for (size_t i = 0; i < e.size(); ++i)
            {
                acc += w[i] * e[i] * e[i];
                ws += w[i];
            }
            return std::sqrt (acc / std::max (ws, 1e-9));
        }
    };

    void refine (const Problem& p, std::vector<Band>& bands, int maxIterations)
    {
        double stepF = 1.0 / 12.0, stepG = 1.0, stepQ = 0.35;
        double best = p.cost (bands);

        for (int it = 0; it < maxIterations; ++it)
        {
            bool improved = false;
            for (auto& b : bands)
            {
                for (int param = 0; param < 3; ++param)
                {
                    for (double dir : { 1.0, -1.0 })
                    {
                        Band saved = b;
                        if (param == 0) b.logF += dir * stepF;
                        if (param == 1) b.gain += dir * stepG;
                        if (param == 2) b.logQ += dir * stepQ;
                        p.clamp (b);
                        p.computeBand (b);

                        const double c = p.cost (bands);
                        if (c < best - 1e-9)
                        {
                            best = c;
                            improved = true;
                            break;
                        }
                        b = std::move (saved);
                    }
                }
            }

            if (! improved)
            {
                stepF *= 0.5; stepG *= 0.5; stepQ *= 0.5;
                if (stepG < 0.05)
                    break;
            }
        }
    }
}

std::vector<double> AutoEQ::spatialWeights (const std::vector<FrequencyResponse>& positions)
{
    if (positions.size() < 2)
        return std::vector<double> (positions.empty() ? 0 : positions.front().size(), 1.0);

    std::vector<FrequencyResponse> smoothed;
    for (auto& p : positions)
        smoothed.push_back (smoothFractionalOctave (p, 6.0));

    const size_t n = smoothed.front().size();
    std::vector<double> w (n);
    for (size_t i = 0; i < n; ++i)
    {
        double mean = 0.0, sq = 0.0;
        for (auto& r : smoothed)
            mean += r.magnitudeDb (i);
        mean /= (double) smoothed.size();
        for (auto& r : smoothed)
            sq += std::pow (r.magnitudeDb (i) - mean, 2.0);
        const double sd = std::sqrt (sq / (double) smoothed.size());
        w[i] = 1.0 / (1.0 + std::pow (sd / 3.0, 2.0)); // 3 dB spread -> weight 0.5
    }
    return w;
}

AutoEQ::Result AutoEQ::solve (const FrequencyResponse& measured,
                              const std::vector<double>& targetDb,
                              const Settings& s,
                              const std::vector<double>& weights)
{
    Result res;
    if (measured.isEmpty())
        return res;

    const auto psy   = smoothPsychoacoustic (measured);
    const auto broad = smoothFractionalOctave (measured, 2.0);

    Problem p { s };
    for (size_t i = 0; i < measured.size(); i += 2) // 48 ppo is plenty for fitting
    {
        const double f = measured.freqs[i];
        if (f < s.fLow || f > s.fHigh)
            continue;

        const double mi = psy.magnitudeDb (i);
        p.f.push_back (f);
        p.m.push_back (mi);
        p.t.push_back (targetDb[i]);
        p.w.push_back (weights.empty() ? 1.0 : weights[i]);

        // Narrow deep dips (much lower than the broad trend) are cancellations: don't chase them.
        const double narrowness = mi - broad.magnitudeDb (i);
        p.dipW.push_back (narrowness < -4.0 ? s.dipWeight * 0.1 : s.dipWeight);
    }

    if (p.f.size() < 4)
        return res;

    if (s.fitGainOffset)
    {
        std::vector<double> d;
        for (size_t i = 0; i < p.f.size(); ++i)
            d.push_back (p.t[i] - p.m[i]);
        std::nth_element (d.begin(), d.begin() + (long) d.size() / 2, d.end());
        p.offset = std::round (d[d.size() / 2] / s.gainStepDb) * s.gainStepDb;
    }

    std::vector<Band> bands;
    res.rmsErrorBeforeDb = p.rms (bands);

    for (int b = 0; b < s.maxBands; ++b)
    {
        const auto e = p.errorWith (bands);

        // Find the contiguous same-sign error region with the largest weighted area.
        double bestArea = 0.0;
        size_t bestStart = 0, bestEnd = 0;
        size_t i = 0;
        while (i < e.size())
        {
            const bool positive = e[i] > 0.0;
            size_t j = i;
            double area = 0.0;
            while (j < e.size() && (e[j] > 0.0) == positive)
            {
                const double wi = positive ? p.w[j] : p.w[j] * p.dipW[j];
                area += wi * std::abs (e[j]);
                ++j;
            }
            if (area > bestArea)
            {
                bestArea = area;
                bestStart = i;
                bestEnd = j;
            }
            i = j;
        }

        if (bestEnd <= bestStart)
            break;

        size_t peak = bestStart;
        for (size_t k = bestStart; k < bestEnd; ++k)
            if (std::abs (e[k]) > std::abs (e[peak]))
                peak = k;

        if (std::abs (e[peak]) < 1.0 || p.rms (bands) < s.stopRmsDb)
            break;

        // Half-height bandwidth -> Q
        const double half = std::abs (e[peak]) / 2.0;
        size_t lo = peak, hi = peak;
        while (lo > bestStart && std::abs (e[lo]) > half) --lo;
        while (hi + 1 < bestEnd && std::abs (e[hi]) > half) ++hi;
        const double bwOct = std::max (1.0 / 24.0, std::log2 (p.f[hi] / p.f[lo]));
        const double q = std::sqrt (std::pow (2.0, bwOct)) / (std::pow (2.0, bwOct) - 1.0);

        Band band { std::log2 (p.f[peak]), -e[peak], std::log2 (q), {} };
        p.clamp (band);
        p.computeBand (band);

        const double before = p.cost (bands);
        bands.push_back (band);
        refine (p, bands, 25);

        if (p.cost (bands) > before - 1e-4)
        {
            bands.pop_back(); // this band didn't help (e.g. a dip we may not boost)
            break;
        }
    }

    refine (p, bands, 60);

    // Quantise to the DSP and drop insignificant bands.
    for (auto& b : bands)
    {
        if (std::abs (b.gain) < 0.5)
            continue;
        FilterSpec fs = FilterSpec::peq (std::round (std::pow (2.0, b.logF) * 10.0) / 10.0,
                                         std::round (std::pow (2.0, b.logQ) * 100.0) / 100.0,
                                         std::round (b.gain / s.gainStepDb) * s.gainStepDb);
        res.filters.push_back (fs);
    }
    std::sort (res.filters.begin(), res.filters.end(), [] (auto& a, auto& b) { return a.freq < b.freq; });

    // Report on the quantised filters.
    std::vector<Band> finalBands;
    for (auto& f : res.filters)
    {
        Band b { std::log2 (f.freq), f.gainDb, std::log2 (f.q), {} };
        p.computeBand (b);
        finalBands.push_back (b);
    }
    res.gainOffsetDb = p.offset;
    res.rmsErrorAfterDb = p.rms (finalBands);
    res.freqs = p.f;
    res.measuredDb = p.m;
    res.targetDb = p.t;
    const auto e = p.errorWith (finalBands);
    res.correctedDb.resize (e.size());
    for (size_t i = 0; i < e.size(); ++i)
        res.correctedDb[i] = e[i] + p.t[i];
    return res;
}
} // namespace fa
