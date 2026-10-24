#include "PredictionModel.h"
#include <functional>

namespace fa::advanced::detail
{
namespace
{
    std::vector<std::pair<size_t, size_t>> windows (const std::vector<double>& f, std::function<double (double)> halfWidthHz)
    {
        std::vector<std::pair<size_t, size_t>> w (f.size());
        for (size_t i = 0; i < f.size(); ++i)
        {
            const double hw = halfWidthHz (f[i]);
            const auto lo = (size_t) (std::lower_bound (f.begin(), f.end(), f[i] - hw) - f.begin());
            auto hi = (size_t) (std::upper_bound (f.begin(), f.end(), f[i] + hw) - f.begin());
            hi = std::max (hi, i + 1);
            w[i] = { std::min (lo, i), hi };   // [lo, hi)
        }
        return w;
    }

    std::vector<double> boxSmooth (const std::vector<double>& x, const std::vector<std::pair<size_t, size_t>>& win)
    {
        std::vector<double> prefix (x.size() + 1, 0.0);
        for (size_t i = 0; i < x.size(); ++i)
            prefix[i + 1] = prefix[i] + x[i];
        std::vector<double> y (x.size());
        for (size_t i = 0; i < x.size(); ++i)
            y[i] = (prefix[win[i].second] - prefix[win[i].first]) / (double) (win[i].second - win[i].first);
        return y;
    }
}

Model::Model (const SystemConfig& cfg, const DriverDataMap& data) : fs (cfg.caps.sampleRate)
{
    const auto& all = analysisFrequencies();
    std::vector<size_t> idx;
    for (size_t i = 0; i < all.size(); i += 2)
        if (all[i] >= 20.0 && all[i] <= 20000.0)
        {
            idx.push_back (i);
            f.push_back (all[i]);
        }

    target = cfg.target.levels (f);

    // ERB smoothing windows, clamped to 1/12..1/3 octave so bass modes stay visible.
    erbWin = windows (f, [] (double x)
    {
        return 0.5 * juce::jlimit (0.0578 * x, 0.2316 * x, erbHz (x));
    });

    double mean = 0.0;
    wErb.resize (f.size());
    for (size_t i = 0; i < f.size(); ++i)
    {
        wErb[i] = std::sqrt (f[i] / erbHz (f[i]));
        mean += wErb[i];
    }
    for (auto& w : wErb)
        w /= mean / (double) f.size();

    size_t maxPos = 0;
    for (auto& [id, dd] : data)
        maxPos = std::max (maxPos, dd.positions.size());
    positions = (int) std::max<size_t> (1, maxPos);

    for (size_t c = 0; c < cfg.channels.size(); ++c)
    {
        const auto& ch = cfg.channels[c];
        auto it = data.find (ch.id);
        if (it == data.end() || it->second.positions.empty())
            continue;   // SystemSolver::predict returns silence for these

        const auto& dd = it->second;
        Channel mc;
        mc.cfgIndex = c;
        mc.side = ch.side;
        mc.role = ch.role;

        const auto oldR = dd.measuredWith.response (f, fs, true);

        for (int p = 0; p < positions; ++p)
        {
            const size_t pp = std::min ((size_t) p, dd.positions.size() - 1);
            const auto& meas = dd.positions[pp];
            const bool sameGrid = meas.freqs == all;
            const double onset = pp < dd.onsetMs.size() ? dd.onsetMs[pp] * 0.001 : 0.0;

            std::vector<cplx> b (f.size());
            for (size_t i = 0; i < f.size(); ++i)
            {
                cplx v = sameGrid ? meas.values[idx[i]] : meas.interpolate (f[i]);
                v *= std::polar (1.0, -2.0 * pi * f[i] * onset);

                cplx o = oldR.values[i];
                const double om = std::abs (o);
                if (om < 1e-2) // same protection as SystemSolver::predict
                    o = om > 0.0 ? o * (1e-2 / om) : cplx (1e-2, 0.0);
                b[i] = v / o;
            }
            mc.base.push_back (std::move (b));
        }
        chans.push_back (std::move (mc));
    }
}

std::vector<cplx> Model::filterPart (const ChannelSettings& s) const
{
    std::vector<cplx> r (f.size(), cplx (1.0, 0.0));
    if (s.muted)
        return std::vector<cplx> (f.size(), cplx (0.0, 0.0));

    auto mul = [&] (const FilterSpec& spec)
    {
        if (! spec.enabled)
            return;
        for (size_t i = 0; i < f.size(); ++i)
            r[i] *= spec.response (f[i], fs);
    };
    mul (s.highPass);
    mul (s.lowPass);
    for (auto& p : s.peq)
        mul (p);
    return r;
}

void Model::contribution (size_t k, const ChannelSettings& s, const std::vector<cplx>& filt,
                          std::vector<std::vector<cplx>>& out) const
{
    const auto& ch = chans[k];
    out.resize ((size_t) positions);
    const double g = s.muted ? 0.0 : magFromDb (s.gainDb) * (s.inverted ? -1.0 : 1.0);
    const double t = s.delayMs * 0.001;

    std::vector<cplx> elec (f.size());
    for (size_t i = 0; i < f.size(); ++i)
        elec[i] = filt[i] * std::polar (g, -2.0 * pi * f[i] * t);

    for (int p = 0; p < positions; ++p)
    {
        auto& o = out[(size_t) p];
        o.resize (f.size());
        const auto& b = ch.base[(size_t) p];
        for (size_t i = 0; i < f.size(); ++i)
            o[i] = b[i] * elec[i];
    }
}

Model::Field Model::sum (const std::vector<std::vector<std::vector<cplx>>>& contrib) const
{
    std::vector<const std::vector<std::vector<cplx>>*> ptrs;
    for (auto& c : contrib)
        ptrs.push_back (&c);
    return sum (ptrs);
}

Model::Field Model::sum (const std::vector<const std::vector<std::vector<cplx>>*>& contrib) const
{
    Field fl;
    const auto P = (size_t) positions, N = f.size();
    auto zc = [&] { return std::vector<std::vector<cplx>> (P, std::vector<cplx> (N, cplx (0.0, 0.0))); };
    auto zd = [&] { return std::vector<std::vector<double>> (P, std::vector<double> (N, 0.0)); };
    fl.frontL = zc(); fl.frontR = zc(); fl.mono = zc();
    fl.magL = zd(); fl.magR = zd(); fl.magMono = zd();

    for (size_t k = 0; k < chans.size() && k < contrib.size(); ++k)
    {
        const auto side = chans[k].side;
        for (size_t p = 0; p < P; ++p)
        {
            const auto& c = (*contrib[k])[p];
            auto& dst = side == Side::left ? fl.frontL[p] : side == Side::right ? fl.frontR[p] : fl.mono[p];
            for (size_t i = 0; i < N; ++i)
            {
                dst[i] += c[i];
                const double m = std::abs (c[i]);
                if (side != Side::right) fl.magL[p][i] += m;
                if (side != Side::left)  fl.magR[p][i] += m;
                if (side == Side::mono)  fl.magMono[p][i] += m;
            }
        }
    }
    return fl;
}

std::vector<double> Model::erbSmooth (const std::vector<double>& power) const
{
    return boxSmooth (power, erbWin);
}

std::vector<double> Model::octaveSmooth (const std::vector<double>& power, double fraction) const
{
    const double k = std::pow (2.0, 0.5 / fraction);
    std::vector<std::pair<size_t, size_t>> w (f.size());
    for (size_t i = 0; i < f.size(); ++i)
    {
        const auto lo = (size_t) (std::lower_bound (f.begin(), f.end(), f[i] / k) - f.begin());
        const auto hi = (size_t) (std::upper_bound (f.begin(), f.end(), f[i] * k) - f.begin());
        w[i] = { std::min (lo, i), std::max (hi, i + 1) };
    }
    return boxSmooth (power, w);
}

std::vector<double> Model::sideDb (const Field& fl, int sideIndex, bool erb) const
{
    std::vector<double> pw (f.size(), 0.0);
    for (int p = 0; p < positions; ++p)
        for (size_t i = 0; i < f.size(); ++i)
            pw[i] += std::norm (sideIndex == 0 ? fl.left (p, i) : fl.right (p, i));
    for (auto& v : pw)
        v /= (double) positions;
    if (erb)
        pw = erbSmooth (pw);
    for (auto& v : pw)
        v = dbFromPower (v);
    return pw;
}

std::vector<std::vector<double>> Model::sideDbPerPosition (const Field& fl, int sideIndex) const
{
    std::vector<std::vector<double>> out;
    for (int p = 0; p < positions; ++p)
    {
        std::vector<double> pw (f.size());
        for (size_t i = 0; i < f.size(); ++i)
            pw[i] = std::norm (sideIndex == 0 ? fl.left (p, i) : fl.right (p, i));
        pw = erbSmooth (pw);
        for (auto& v : pw)
            v = dbFromPower (v);
        out.push_back (std::move (pw));
    }
    return out;
}

std::vector<double> spatialWeights (const Model& model, const Model::Field& field, int sideIndex)
{
    const auto per = model.sideDbPerPosition (field, sideIndex);
    std::vector<double> w (model.size(), 1.0);
    if (per.size() < 2)
        return w;
    for (size_t i = 0; i < model.size(); ++i)
    {
        double mean = 0.0, sq = 0.0;
        for (auto& r : per)
            mean += r[i];
        mean /= (double) per.size();
        for (auto& r : per)
            sq += (r[i] - mean) * (r[i] - mean);
        const double sd = std::sqrt (sq / (double) per.size());
        w[i] = 1.0 / (1.0 + (sd / 3.0) * (sd / 3.0));
    }
    return w;
}

double anchorOffset (const Model& model, const std::vector<double>& l, const std::vector<double>& r)
{
    const auto& f = model.freqs();
    const auto& t = model.targetDb();
    double acc = 0.0;
    int n = 0;
    for (size_t i = 0; i < f.size(); ++i)
        if (f[i] >= 200.0 && f[i] <= 2000.0)
        {
            acc += 0.5 * ((l[i] - t[i]) + (r[i] - t[i]));
            ++n;
        }
    return n > 0 ? acc / n : 0.0;
}
} // namespace fa::advanced::detail
