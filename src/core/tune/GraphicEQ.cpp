#include "GraphicEQ.h"
#include "../dsp/Spectrum.h"

namespace fa
{
namespace
{
    constexpr double proportionalFullScaleDb = 12.0;

    /** Solves A x = b (n x n, row-major) by Gaussian elimination with partial pivoting. */
    std::vector<double> solveLinear (std::vector<double> A, std::vector<double> b)
    {
        const size_t n = b.size();
        for (size_t c = 0; c < n; ++c)
        {
            size_t piv = c;
            for (size_t r = c + 1; r < n; ++r)
                if (std::abs (A[r * n + c]) > std::abs (A[piv * n + c]))
                    piv = r;
            if (std::abs (A[piv * n + c]) < 1e-12)
                continue;
            if (piv != c)
            {
                for (size_t k = 0; k < n; ++k)
                    std::swap (A[c * n + k], A[piv * n + k]);
                std::swap (b[c], b[piv]);
            }
            for (size_t r = c + 1; r < n; ++r)
            {
                const double f = A[r * n + c] / A[c * n + c];
                if (f == 0.0)
                    continue;
                for (size_t k = c; k < n; ++k)
                    A[r * n + k] -= f * A[c * n + k];
                b[r] -= f * b[c];
            }
        }
        std::vector<double> x (n, 0.0);
        for (size_t c = n; c-- > 0;)
        {
            if (std::abs (A[c * n + c]) < 1e-12)
                continue;
            double s = b[c];
            for (size_t k = c + 1; k < n; ++k)
                s -= A[c * n + k] * x[k];
            x[c] = s / A[c * n + c];
        }
        return x;
    }

    struct Problem
    {
        const DspCapabilities& caps;
        const GraphicEQ::Settings& s;
        std::vector<double> centres;
        std::vector<double> f, m, t, w, dipW;
        std::vector<bool> active;
        std::vector<double> lo, hi;               // per band gain limits
        double wsum = 1.0;

        std::vector<double> bandDb (size_t j, double g) const
        {
            std::vector<double> r (f.size(), 0.0);
            if (std::abs (g) < 1e-9)
                return r;
            const auto c = BiquadCoeffs::peaking (centres[j], GraphicEQ::bandQ (caps, j, g), g, caps.sampleRate);
            for (size_t i = 0; i < f.size(); ++i)
                r[i] = dbFromMag (std::abs (c.response (f[i], caps.sampleRate)));
            return r;
        }

        std::vector<double> error (const std::vector<std::vector<double>>& resp) const
        {
            std::vector<double> e (f.size());
            for (size_t i = 0; i < f.size(); ++i)
            {
                double eq = 0.0;
                for (auto& r : resp)
                    eq += r[i];
                e[i] = m[i] + eq - t[i];
            }
            return e;
        }

        double cost (const std::vector<double>& g, const std::vector<std::vector<double>>& resp) const
        {
            const auto e = error (resp);
            double c = 0.0;
            for (size_t i = 0; i < e.size(); ++i)
                c += (e[i] > 0.0 ? w[i] : w[i] * dipW[i]) * e[i] * e[i];
            c /= wsum;
            for (size_t j = 0; j < g.size(); ++j)
            {
                if (g[j] > 0.0)
                    c += s.boostPenalty * g[j] * g[j];
                c += 1e-4 * g[j] * g[j];
                if (j + 1 < g.size() && active[j] && active[j + 1])
                    c += s.smoothness * std::pow (g[j] - g[j + 1], 2.0);
            }
            return c;
        }

        double rms (const std::vector<std::vector<double>>& resp) const
        {
            const auto e = error (resp);
            double acc = 0.0;
            for (size_t i = 0; i < e.size(); ++i)
                acc += w[i] * e[i] * e[i];
            return std::sqrt (acc / wsum);
        }
    };
}

//==============================================================================
double GraphicEQ::nominalQ (const DspCapabilities& caps, size_t index)
{
    if (caps.graphicQ > 0.0)
        return caps.graphicQ;

    const auto c = caps.graphicCentres();
    if (c.size() < 2 || index >= c.size())
        return 1.41;

    // Spacing in octaves: half the distance between the two neighbours (edges: the one neighbour).
    double bw;
    if (index == 0)                 bw = std::log2 (c[1] / c[0]);
    else if (index + 1 == c.size()) bw = std::log2 (c[index] / c[index - 1]);
    else                            bw = 0.5 * std::log2 (c[index + 1] / c[index - 1]);
    bw = juce::jlimit (1.0 / 12.0, 3.0, bw);
    const double r = std::pow (2.0, bw);
    return std::sqrt (r) / (r - 1.0);
}

double GraphicEQ::bandQ (const DspCapabilities& caps, size_t index, double gainDb)
{
    const double qn = nominalQ (caps, index);
    if (caps.graphicQ > 0.0)
        return qn;
    return qn * (1.0 / 3.0 + 2.0 / 3.0 * std::min (1.0, std::abs (gainDb) / proportionalFullScaleDb));
}

std::vector<FilterSpec> GraphicEQ::makeGraphic (const DspCapabilities& caps, const std::vector<double>& gainsDb)
{
    const auto c = caps.graphicCentres();
    std::vector<FilterSpec> out;
    for (size_t j = 0; j < c.size(); ++j)
    {
        const double g = j < gainsDb.size() ? gainsDb[j] : 0.0;
        out.push_back (FilterSpec::peq (c[j], std::round (bandQ (caps, j, g) * 100.0) / 100.0, g));
    }
    return out;
}

std::vector<double> GraphicEQ::gainsOf (const DspCapabilities& caps, const std::vector<FilterSpec>& peq)
{
    const auto c = caps.graphicCentres();
    std::vector<double> g (c.size(), 0.0);
    for (auto& f : peq)
    {
        if (f.type != FilterType::peaking || ! f.enabled)
            continue;
        for (size_t j = 0; j < c.size(); ++j)
            if (std::abs (std::log2 (f.freq / c[j])) < 1.0 / 48.0)
                g[j] = f.gainDb;
    }
    return g;
}

double GraphicEQ::quantise (const DspCapabilities& caps, double gainDb, double cutLimitDb, double boostLimitDb)
{
    const double st = std::max (1e-3, caps.graphicGainStepDb);
    double q = std::round (gainDb / st) * st;
    if (q > boostLimitDb + 1e-9) q = std::floor (boostLimitDb / st + 1e-9) * st;
    if (q < cutLimitDb - 1e-9)   q = std::ceil (cutLimitDb / st - 1e-9) * st;
    return std::abs (q) < 1e-9 ? 0.0 : q;
}

//==============================================================================
GraphicEQ::Result GraphicEQ::solve (const FrequencyResponse& measured,
                                    const std::vector<double>& targetDb,
                                    const DspCapabilities& caps,
                                    const Settings& s,
                                    const std::vector<double>& weights)
{
    Result res;
    Problem p { caps, s, caps.graphicCentres() };
    const size_t nb = p.centres.size();
    res.gainsDb.assign (nb, 0.0);
    res.filters = makeGraphic (caps, res.gainsDb);
    if (measured.isEmpty() || nb == 0)
        return res;

    const auto psy   = smoothPsychoacoustic (measured);
    const auto broad = smoothFractionalOctave (measured, 2.0);
    for (size_t i = 0; i < measured.size(); i += 2)
    {
        const double f = measured.freqs[i];
        if (f < s.fLow || f > s.fHigh)
            continue;
        const double mi = psy.magnitudeDb (i);
        p.f.push_back (f);
        p.m.push_back (mi);
        p.t.push_back (targetDb[i]);
        p.w.push_back (weights.empty() ? 1.0 : weights[i]);
        p.dipW.push_back (mi - broad.magnitudeDb (i) < -4.0 ? s.dipWeight * 0.1 : s.dipWeight);
    }
    if (p.f.size() < 4)
        return res;
    p.wsum = 0.0;
    for (auto x : p.w)
        p.wsum += x;
    p.wsum = std::max (p.wsum, 1e-9);

    // Bands whose centre is within ~1/6 octave of the fitted range may move; the rest stay at 0 dB.
    const double cutLimit = std::max (s.maxCutDb, caps.graphicMaxCutDb);
    for (size_t j = 0; j < nb; ++j)
    {
        const double fc = p.centres[j];
        p.active.push_back (fc >= s.fLow * std::pow (2.0, -1.0 / 6.0) && fc <= s.fHigh * std::pow (2.0, 1.0 / 6.0));
        double boost = std::min (s.maxBoostDb, caps.graphicMaxBoostDb);
        if (s.lfBoostLimitBelowHz > 0.0 && fc < s.lfBoostLimitBelowHz)
            boost = std::min (boost, s.lfBoostLimitDb);
        p.lo.push_back (p.active.back() ? std::min (0.0, cutLimit) : 0.0);
        p.hi.push_back (p.active.back() ? std::max (0.0, boost) : 0.0);
    }

    std::vector<double> g (nb, 0.0);
    std::vector<std::vector<double>> resp (nb, std::vector<double> (p.f.size(), 0.0));
    res.rmsErrorBeforeDb = p.rms (resp);
    double best = p.cost (g, resp);

    // 1) Damped Gauss-Newton on all active gains jointly.
    std::vector<size_t> idx;
    for (size_t j = 0; j < nb; ++j)
        if (p.active[j])
            idx.push_back (j);
    const size_t na = idx.size();
    double lambda = 1e-3;

    for (int it = 0; it < 40 && na > 0; ++it)
    {
        const auto e = p.error (resp);
        std::vector<double> we (e.size());
        for (size_t i = 0; i < e.size(); ++i)
            we[i] = (e[i] > 0.0 ? p.w[i] : p.w[i] * p.dipW[i]) / p.wsum;

        // Numerical Jacobian (exact for the proportional-Q model).
        std::vector<std::vector<double>> J (na);
        for (size_t a = 0; a < na; ++a)
        {
            const size_t j = idx[a];
            const double h = 0.25;
            const auto up = p.bandDb (j, g[j] + h), dn = p.bandDb (j, g[j] - h);
            J[a].resize (p.f.size());
            for (size_t i = 0; i < p.f.size(); ++i)
                J[a][i] = (up[i] - dn[i]) / (2.0 * h);
        }

        std::vector<double> A (na * na, 0.0), rhs (na, 0.0);
        for (size_t a = 0; a < na; ++a)
        {
            for (size_t b = a; b < na; ++b)
            {
                double acc = 0.0;
                for (size_t i = 0; i < p.f.size(); ++i)
                    acc += we[i] * J[a][i] * J[b][i];
                A[a * na + b] = A[b * na + a] = acc;
            }
            double acc = 0.0;
            for (size_t i = 0; i < p.f.size(); ++i)
                acc += we[i] * J[a][i] * e[i];
            rhs[a] = -acc;

            const size_t j = idx[a];
            const double ridge = 1e-4 + (g[j] > 0.0 ? s.boostPenalty : 0.0);
            A[a * na + a] += ridge;
            rhs[a] -= ridge * g[j];
            if (a + 1 < na && idx[a + 1] == j + 1)
            {
                const double d = g[j] - g[j + 1];
                A[a * na + a] += s.smoothness;
                A[(a + 1) * na + a + 1] += s.smoothness;
                A[a * na + a + 1] -= s.smoothness;
                A[(a + 1) * na + a] -= s.smoothness;
                rhs[a] -= s.smoothness * d;
                rhs[a + 1] += s.smoothness * d;
            }
        }

        bool accepted = false;
        for (int tries = 0; tries < 6 && ! accepted; ++tries)
        {
            auto Ad = A;
            for (size_t a = 0; a < na; ++a)
                Ad[a * na + a] += lambda * (1.0 + A[a * na + a]);
            const auto d = solveLinear (Ad, rhs);

            auto g2 = g;
            auto resp2 = resp;
            for (size_t a = 0; a < na; ++a)
            {
                const size_t j = idx[a];
                g2[j] = juce::jlimit (p.lo[j], p.hi[j], g[j] + d[a]);
                if (std::abs (g2[j] - g[j]) > 1e-9)
                    resp2[j] = p.bandDb (j, g2[j]);
            }
            const double c = p.cost (g2, resp2);
            if (c < best - 1e-9)
            {
                const double gainRel = (best - c) / std::max (best, 1e-9);
                best = c;
                g = std::move (g2);
                resp = std::move (resp2);
                lambda = std::max (1e-6, lambda * 0.3);
                accepted = true;
                if (gainRel < 1e-4)
                    it = 1000;   // converged
            }
            else
            {
                lambda *= 8.0;
            }
        }
        if (! accepted)
            break;
    }

    // 2) Quantise to the DSP's gain step, then polish on the step grid.
    for (size_t j = 0; j < nb; ++j)
    {
        g[j] = quantise (caps, g[j], p.lo[j], p.hi[j]);
        resp[j] = p.bandDb (j, g[j]);
    }
    best = p.cost (g, resp);
    const double st = std::max (1e-3, caps.graphicGainStepDb);
    for (int sweep = 0; sweep < 20; ++sweep)
    {
        bool improved = false;
        for (auto j : idx)
        {
            for (double dir : { 1.0, -1.0 })
            {
                const double ng = quantise (caps, g[j] + dir * st, p.lo[j], p.hi[j]);
                if (std::abs (ng - g[j]) < 1e-9)
                    continue;
                const double old = g[j];
                auto oldResp = std::move (resp[j]);
                g[j] = ng;
                resp[j] = p.bandDb (j, ng);
                const double c = p.cost (g, resp);
                if (c < best - 1e-9)
                {
                    best = c;
                    improved = true;
                    break;
                }
                g[j] = old;
                resp[j] = std::move (oldResp);
            }
        }
        if (! improved)
            break;
    }

    res.gainsDb = g;
    res.filters = makeGraphic (caps, g);
    res.rmsErrorAfterDb = p.rms (resp);
    res.freqs = p.f;
    res.measuredDb = p.m;
    res.targetDb = p.t;
    const auto e = p.error (resp);
    res.correctedDb.resize (e.size());
    for (size_t i = 0; i < e.size(); ++i)
        res.correctedDb[i] = e[i] + p.t[i];
    return res;
}
} // namespace fa
