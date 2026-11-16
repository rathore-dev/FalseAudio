#include "Optimizer.h"
#include "../PassiveNotch.h"
#include "PredictionModel.h"
#include "../CrossoverPlanner.h"
#include "../GraphicEQ.h"
#include <array>

namespace fa::advanced
{
using detail::Model;

namespace
{
/** Owns the optimisation state: one ChannelSettings per model channel plus cached responses,
    so moving one parameter only recomputes the channel(s) it touches.

    Cost (all terms roughly in dB, lower is better):
      tonal      ERB-smoothed, position-power-averaged response of each side vs the anchored target,
                 25 Hz..16 kHz, weighted by sqrt(ERB density) x spatial consistency (frequencies where
                 the mic positions disagree are position-dependent and not worth chasing); dips count
                 0.6x (less audible than peaks). RMS, mean of both sides.
      symmetry   RMS L-R difference 100 Hz..10 kHz (x1.5 in the 200 Hz..4 kHz imaging band).
      sumLR      coherent-summation loss of L+R (+sub): 10log10(mean (|FL|+|FR|+|M|)^2 / mean |FL+FR+M|^2),
                 ERB smoothed, 40 Hz..1.5 kHz (where seat-to-seat phase is still controllable).
      sumSide    same per side over all its drivers, 40 Hz..10 kHz (crossover holes).
      boost      boostPenalty x (0.1 x every dB of EQ boost + 0.2 x boost that lands where positions
                 disagree, i.e. boosting into cancellations + 0.1 x channel gain added).
      reg        tiny pull towards the input (keeps changes meaningful and deterministic). */
class Problem
{
public:
    Problem (const SystemConfig& c, const DriverDataMap& d, const Options& o)
        : cfg (c), model (c, d), opts (o)
    {
        for (auto& mc : model.channels())
            init.push_back (cfg.channels[mc.cfgIndex].settings);

        // Topology: a passive set's crossover-region notches and (without a sub) the bass below the
        // lowest front driver are things the DSP cannot fix; exclude them from the tonal cost and
        // never let a band boost into a notch.
        we = model.erbWeights();
        notches.resize (init.size());
        double lowestHp = 0.0;
        bool anySub = false;
        for (size_t k = 0; k < init.size(); ++k)
        {
            const auto& ch = cfg.channels[model.channels()[k].cfgIndex];
            if (ch.role == DriverRole::subwoofer) { anySub = true; continue; }
            if (init[k].highPass.enabled)
                lowestHp = lowestHp > 0.0 ? std::min (lowestHp, init[k].highPass.freq) : init[k].highPass.freq;
            if (! ch.isPassiveSet())
                continue;
            auto it = d.find (ch.id);
            if (it == d.end() || it->second.positions.empty())
                continue;
            notches[k] = passive::findNotches (powerAverage (it->second.positions), ch.passive.crossoverHz);
            for (size_t i = 0; i < we.size(); ++i)
                if (passive::contains (notches[k], model.freqs()[i]))
                    we[i] = 0.0;
        }
        tonalLow = anySub ? 25.0 : std::max (25.0, lowestHp * 1.25);
        state = init;
        cache.resize (state.size());
        for (size_t k = 0; k < state.size(); ++k)
            rebuild (k);

        const auto fl = model.sum (contribs());
        wsL = detail::spatialWeights (model, fl, 0);
        wsR = detail::spatialWeights (model, fl, 1);

        for (size_t k = 0; k < state.size(); ++k)
            halfPeriod.push_back (500.0 / phaseFrequency (k));
    }

    const Model& getModel() const { return model; }
    bool inNotch (size_t k, double freq) const { return k < notches.size() && passive::contains (notches[k], freq); }
    size_t numChannels() const { return state.size(); }
    const OutputChannel& channel (size_t k) const { return cfg.channels[model.channels()[k].cfgIndex]; }

    /** Frequency whose half period bounds a channel's fine delay search. */
    double phaseFrequency (size_t k) const
    {
        const auto& s = init[k];
        const auto role = model.channels()[k].role;
        if (role == DriverRole::subwoofer)
            return s.lowPass.enabled ? s.lowPass.freq : 80.0;
        if (role == DriverRole::tweeter || ! s.lowPass.enabled)
            return s.highPass.enabled ? s.highPass.freq : 1000.0;
        return s.lowPass.freq;
    }

    double getHalfPeriod (size_t k) const { return halfPeriod[k]; }

    //==========================================================================
    struct Cache
    {
        std::vector<std::vector<cplx>> parts;     // HP, LP, peq...
        std::vector<cplx> filt;
        std::vector<std::vector<cplx>> contrib;   // [pos][freq]
    };

    std::vector<ChannelSettings> state, init;
    std::vector<Cache> cache;

    std::vector<const std::vector<std::vector<cplx>>*> contribs() const
    {
        std::vector<const std::vector<std::vector<cplx>>*> c;
        for (auto& ch : cache)
            c.push_back (&ch.contrib);
        return c;
    }

    std::vector<cplx> partOf (const FilterSpec& spec) const
    {
        const auto r = spec.response (model.freqs(), model.sampleRate());
        return r.values;
    }

    void product (size_t k)
    {
        auto& c = cache[k];
        c.filt.assign (model.size(), cplx (1.0, 0.0));
        for (auto& p : c.parts)
            for (size_t i = 0; i < p.size(); ++i)
                c.filt[i] *= p[i];
        model.contribution (k, state[k], c.filt, c.contrib);
    }

    void rebuild (size_t k)
    {
        auto& c = cache[k];
        c.parts.clear();
        c.parts.push_back (partOf (state[k].highPass));
        c.parts.push_back (partOf (state[k].lowPass));
        for (auto& p : state[k].peq)
            c.parts.push_back (partOf (p));
        product (k);
    }

    void contribOnly (size_t k) { model.contribution (k, state[k], cache[k].filt, cache[k].contrib); }

    //==========================================================================
    CostBreakdown cost() const
    {
        CostBreakdown cb;
        const auto& f = model.freqs();
        const auto& t = model.targetDb();
        const auto N = model.size();
        const int P = model.numPositions();
        // One pass over channels/positions accumulates every power spectrum the terms need
        // (identical to Model::sum + sideDb, without the per-position storage).
        auto& pL = scratch[0]; auto& pR = scratch[1]; auto& idLR = scratch[2]; auto& acLR = scratch[3];
        auto& idL = scratch[4]; auto& idR = scratch[5];
        for (auto* v : { &pL, &pR, &idLR, &acLR, &idL, &idR })
            v->assign (N, 0.0);
        sFL.resize (N); sFR.resize (N); sM.resize (N); mL.resize (N); mR.resize (N);

        for (int p = 0; p < P; ++p)
        {
            std::fill (sFL.begin(), sFL.end(), cplx (0.0, 0.0));
            std::fill (sFR.begin(), sFR.end(), cplx (0.0, 0.0));
            std::fill (sM.begin(), sM.end(), cplx (0.0, 0.0));
            std::fill (mL.begin(), mL.end(), 0.0);
            std::fill (mR.begin(), mR.end(), 0.0);
            for (size_t k = 0; k < cache.size(); ++k)
            {
                const auto side = model.channels()[k].side;
                const auto& c = cache[k].contrib[(size_t) p];
                auto& dst = side == Side::left ? sFL : side == Side::right ? sFR : sM;
                for (size_t i = 0; i < N; ++i)
                {
                    dst[i] += c[i];
                    const double m = std::abs (c[i]);
                    if (side != Side::right) mL[i] += m;
                    if (side != Side::left)  mR[i] += m;
                }
            }
            for (size_t i = 0; i < N; ++i)
            {
                const cplx l = sFL[i] + sM[i], r = sFR[i] + sM[i];
                pL[i] += std::norm (l);
                pR[i] += std::norm (r);
                const double ideal = std::abs (sFL[i]) + std::abs (sFR[i]) + std::abs (sM[i]);
                idLR[i] += ideal * ideal;
                acLR[i] += std::norm (sFL[i] + sFR[i] + sM[i]);
                idL[i] += mL[i] * mL[i];
                idR[i] += mR[i] * mR[i];
            }
        }

        std::vector<double> L = model.erbSmooth (pL), R = model.erbSmooth (pR);
        for (size_t i = 0; i < N; ++i)
        {
            L[i] = dbFromPower (L[i] / P);
            R[i] = dbFromPower (R[i] / P);
        }
        const double off = detail::anchorOffset (model, L, R);

        auto tonalSide = [&] (const std::vector<double>& d, const std::vector<double>& ws)
        {
            double acc = 0.0, wsum = 0.0;
            for (size_t i = 0; i < N; ++i)
            {
                if (f[i] < tonalLow || f[i] > 16000.0)
                    continue;
                const double e = d[i] - t[i] - off;
                const double w = we[i] * ws[i];
                acc += w * (e < 0.0 ? 0.6 : 1.0) * e * e;
                wsum += w;
            }
            return wsum > 0.0 ? std::sqrt (acc / wsum) : 0.0;
        };
        cb.tonal = 0.5 * (tonalSide (L, wsL) + tonalSide (R, wsR));

        {
            double acc = 0.0, wsum = 0.0;
            for (size_t i = 0; i < N; ++i)
            {
                if (f[i] < 100.0 || f[i] > 10000.0)
                    continue;
                const double w = we[i] * (f[i] >= 200.0 && f[i] <= 4000.0 ? 1.5 : 1.0) * std::min (wsL[i], wsR[i]);
                acc += w * (L[i] - R[i]) * (L[i] - R[i]);
                wsum += w;
            }
            cb.symmetry = wsum > 0.0 ? std::sqrt (acc / wsum) : 0.0;
        }

        auto summationLoss = [&] (const std::vector<double>& idealPow, const std::vector<double>& actualPow, double f1, double f2)
        {
            const auto id = model.erbSmooth (idealPow), ac = model.erbSmooth (actualPow);
            double acc = 0.0, wsum = 0.0;
            for (size_t i = 0; i < N; ++i)
            {
                if (f[i] < f1 || f[i] > f2 || id[i] <= 1e-20)
                    continue;
                acc += we[i] * std::max (0.0, dbFromPower (id[i]) - dbFromPower (ac[i]));
                wsum += we[i];
            }
            return wsum > 0.0 ? acc / wsum : 0.0;
        };

        cb.summationLR = summationLoss (idLR, acLR, 40.0, 1500.0);
        cb.summationSide = 0.5 * (summationLoss (idL, pL, 40.0, 10000.0) + summationLoss (idR, pR, 40.0, 10000.0));

        // Boost: every dB costs a little, boost where positions disagree costs more, and so does
        // channel gain added on top of what the solver chose.
        double boost = 0.0;
        for (size_t k = 0; k < state.size(); ++k)
        {
            const auto side = model.channels()[k].side;
            for (auto& b : state[k].peq)
            {
                if (! b.enabled || b.gainDb <= 0.0
                    || ! (b.type == FilterType::peaking || b.type == FilterType::lowShelf || b.type == FilterType::highShelf))
                    continue;
                const auto i = std::min (N - 1, (size_t) (std::lower_bound (f.begin(), f.end(), b.freq) - f.begin()));
                const double ws = side == Side::left ? wsL[i] : side == Side::right ? wsR[i] : std::min (wsL[i], wsR[i]);
                boost += 0.1 * b.gainDb + 0.2 * b.gainDb * (1.0 - ws);
            }
            boost += 0.1 * std::max (0.0, state[k].gainDb - init[k].gainDb);
        }
        cb.boost = opts.boostPenalty * boost;

        double reg = 0.0;
        for (size_t k = 0; k < state.size(); ++k)
        {
            reg += 0.02 * std::abs (state[k].delayMs - init[k].delayMs) / halfPeriod[k];
            reg += 0.005 * std::abs (state[k].gainDb - init[k].gainDb);
            reg += state[k].inverted != init[k].inverted ? 0.15 : 0.0;
            if (state[k].highPass.freq != init[k].highPass.freq) reg += 0.01;
            if (state[k].lowPass.freq != init[k].lowPass.freq)   reg += 0.01;
        }
        cb.regularisation = reg;

        cb.total = opts.tonalWeight * cb.tonal + opts.symmetryWeight * cb.symmetry
                 + opts.summationWeight * (cb.summationLR + cb.summationSide) + cb.boost + cb.regularisation;
        return cb;
    }

private:
    const SystemConfig& cfg;
    Model model;
    Options opts;
    std::vector<double> wsL, wsR, halfPeriod;
    std::vector<double> we;                          // ERB weights with unfixable regions zeroed
    double tonalLow = 25.0;
    std::vector<std::vector<PassiveNotch>> notches;  // per model channel

    // Scratch buffers reused by cost() (single-threaded use only).
    mutable std::array<std::vector<double>, 6> scratch;
    mutable std::vector<cplx> sFL, sFR, sM;
    mutable std::vector<double> mL, mR;
};

//==============================================================================
/** Coordinate descent with adaptive steps + deterministic random restarts. */
class Search
{
public:
    Search (Problem& p, const SystemConfig& c, const Options& o)
        : pr (p), cfg (c), opts (o), rng (0x5EED1234)
    {
        budget = std::max (2000, opts.maxIterations * 60);
        best = pr.cost().total;
        buildTwins();
    }

    enum class Kind { delay, gain, polarity, bandGain, xoverPair, xoverLP, xoverHP };

    struct Param
    {
        Kind kind;
        size_t k = 0, k2 = (size_t) -1;
        int band = -1;
        double lo = 0, hi = 0, step = 1, minStep = 1, cur = 0;
        double base1 = 0, base2 = 0;   // crossover reference frequencies
        double minHp = 0;              // safety floor for the HP that moves
        // Methodology: the same move is applied to the right-side twin(s) (equal crossovers, level
        // deltas, EQ deltas) and, for the front HP, to the sub LP(s), which stay at HP / sqrt(2).
        std::vector<size_t> mir, mir2, subs;
    };

    double run()
    {
        buildParams();
        descend();

        if (tryDiscreteMoves())
        {
            buildParams();
            descend();
        }

        // Random restarts from the best point (escape local minima of the phase landscape).
        for (int r = 0; r < 6 && evals < budget; ++r)
        {
            const auto saved = snapshot();
            const double savedCost = best;

            for (size_t k = 0; k < pr.numChannels(); ++k)
            {
                if (! subAllowed (k) || frozen (k))
                    continue;
                auto& s = pr.state[k];
                const double hp = pr.getHalfPeriod (k);
                if (! opts.constrained)   // delays are never touched under the methodology rules
                    s.delayMs = quantDelay (juce::jlimit (delayLo (k), delayHi (k), s.delayMs + (rng.nextDouble() - 0.5) * hp));
                const double oldGain = s.gainDb;
                s.gainDb = quantGain (juce::jlimit (gainLo (k), gainHi (k), s.gainDb + (rng.nextDouble() - 0.5) * 1.0));
                pr.contribOnly (k);
                if (twin[k] >= 0)
                {
                    const auto t = (size_t) twin[k];
                    pr.state[t].gainDb = quantGain (juce::jlimit (gainLo (t), gainHi (t), pr.state[t].gainDb + (s.gainDb - oldGain)));
                    pr.contribOnly (t);
                }
            }
            best = pr.cost().total;
            buildParams();
            descend();

            if (best >= savedCost)
            {
                restore (saved);
                best = savedCost;
            }
        }
        return best;
    }

    int evaluations() const { return evals; }

private:
    Problem& pr;
    const SystemConfig& cfg;
    Options opts;
    juce::Random rng;
    std::vector<Param> params;
    double best = 0.0;
    int evals = 0, budget = 0;
    std::vector<long> twin;   // left channel -> its right counterpart (constrained stereo only), else -1

    void buildTwins()
    {
        const auto& mch = pr.getModel().channels();
        twin.assign (mch.size(), -1);
        if (! opts.constrained || ! cfg.isStereo())
            return;
        std::vector<bool> used (mch.size(), false);
        for (size_t k = 0; k < mch.size(); ++k)
        {
            if (mch[k].side != Side::left)
                continue;
            for (size_t j = 0; j < mch.size(); ++j)
                if (! used[j] && mch[j].side == Side::right && mch[j].role == mch[k].role)
                {
                    twin[k] = (long) j;
                    used[j] = true;
                    break;
                }
        }
    }

    /** Right-side channels follow the left (crossovers, level deltas, EQ matched to the left afterwards). */
    bool frozen (size_t k) const
    {
        return opts.constrained && cfg.isStereo() && pr.getModel().channels()[k].side == Side::right;
    }

    bool subAllowed (size_t k) const
    {
        return opts.tuneSubIntegration || pr.getModel().channels()[k].role != DriverRole::subwoofer;
    }

    double quantDelay (double v) const
    {
        const double st = std::max (1e-4, cfg.caps.delayStepMs);
        return std::round (v / st) * st;
    }
    double quantGain (double v) const
    {
        const double st = std::max (1e-3, cfg.caps.gainStepDb);
        return std::round (v / st) * st;
    }
    double quantGraphic (double v) const
    {
        const double st = std::max (1e-3, cfg.caps.graphicGainStepDb);
        return std::round (v / st) * st;
    }

    /** Q of the graphic band centred at `freq` for gain `g` (rounded like GraphicEQ::makeGraphic). */
    double graphicQ (double freq, double g) const
    {
        const auto c = cfg.caps.graphicCentres();
        size_t idx = 0;
        for (size_t j = 1; j < c.size(); ++j)
            if (std::abs (std::log2 (freq / c[j])) < std::abs (std::log2 (freq / c[idx])))
                idx = j;
        return std::round (GraphicEQ::bandQ (cfg.caps, idx, g) * 100.0) / 100.0;
    }

    double delayLo (size_t k) const
    {
        const double st = std::max (1e-4, cfg.caps.delayStepMs);
        return std::ceil (std::max (0.0, pr.init[k].delayMs - pr.getHalfPeriod (k)) / st - 1e-9) * st;
    }
    double delayHi (size_t k) const
    {
        const double st = std::max (1e-4, cfg.caps.delayStepMs);
        return std::floor (std::min (cfg.caps.maxDelayMs, pr.init[k].delayMs + pr.getHalfPeriod (k)) / st + 1e-9) * st;
    }
    double gainLo (size_t k) const { return std::min (pr.init[k].gainDb, std::max (cfg.caps.minChannelGainDb, pr.init[k].gainDb - 3.0)); }
    double gainHi (size_t k) const { return std::max (pr.init[k].gainDb, std::min (cfg.caps.maxChannelGainDb, pr.init[k].gainDb + 3.0)); }

    double bandBoostLimit (size_t k, double freq) const
    {
        double lim = std::min (cfg.safety.maxBoostDb, cfg.caps.isGraphic() ? cfg.caps.graphicMaxBoostDb : cfg.caps.peqMaxGainDb);
        const auto& s = pr.init[k];
        if (pr.getModel().channels()[k].role != DriverRole::subwoofer && s.highPass.enabled && freq < 2.0 * s.highPass.freq)
            lim = std::min (lim, cfg.safety.maxLowFreqBoostDb);   // excursion protection
        if (pr.inNotch (k, freq))
            lim = std::min (lim, 0.0);                             // passive-network notch: never boost into it
        return lim;
    }

    double minHighPass (size_t k) const
    {
        const auto& ch = pr.channel (k);
        return CrossoverPlanner::safeHighPassHz (ch, cfg.safety);
    }

    //==========================================================================
    void buildParams()
    {
        params.clear();
        const auto& mch = pr.getModel().channels();

        // 1) phase: delays and polarity
        for (size_t k = 0; k < mch.size(); ++k)
        {
            if (opts.constrained || ! subAllowed (k) || pr.state[k].muted)
                continue;   // constrained: delay and polarity belong to the alignment stage
            Param p { Kind::delay, k };
            p.lo = delayLo (k);
            p.hi = delayHi (k);
            p.cur = pr.state[k].delayMs;
            p.minStep = std::max (1e-4, cfg.caps.delayStepMs);
            p.step = std::max (p.minStep, quantDelay (pr.getHalfPeriod (k) / 4.0));
            params.push_back (p);

            Param q { Kind::polarity, k };
            params.push_back (q);
        }

        // 2) levels
        for (size_t k = 0; k < mch.size(); ++k)
        {
            if (! subAllowed (k) || pr.state[k].muted || frozen (k))
                continue;
            Param p { Kind::gain, k };
            if (twin[k] >= 0) p.mir = { (size_t) twin[k] };
            p.lo = gainLo (k);
            p.hi = gainHi (k);
            p.cur = pr.state[k].gainDb;
            p.minStep = std::max (1e-3, cfg.caps.gainStepDb);
            p.step = std::max (p.minStep, 1.0);
            params.push_back (p);
        }

        // 3) crossover points (1/24-octave moves, +-1/3 octave, both sides of each pair together)
        auto chainOf = [&] (Side side)
        {
            std::vector<size_t> chain;
            for (size_t k = 0; k < mch.size(); ++k)
                if (mch[k].side == side && mch[k].role != DriverRole::subwoofer && ! pr.state[k].muted)
                    chain.push_back (k);
            std::sort (chain.begin(), chain.end(), [this] (size_t a, size_t b)
            {
                return pr.init[a].highPass.freq < pr.init[b].highPass.freq;
            });
            return chain;
        };
        std::vector<size_t> subsList;
        for (size_t k = 0; k < mch.size(); ++k)
            if (mch[k].role == DriverRole::subwoofer && opts.tuneSubIntegration && pr.init[k].lowPass.enabled && ! pr.state[k].muted)
                subsList.push_back (k);

        for (auto side : { Side::left, Side::right })
        {
            if (opts.constrained && cfg.isStereo() && side == Side::right)
                continue;   // right crossovers are moved together with the left ones
            const auto chain = chainOf (side);
            const auto rchain = (opts.constrained && cfg.isStereo() && side == Side::left) ? chainOf (Side::right) : std::vector<size_t>{};
            const bool haveTwin = rchain.size() == chain.size();

            for (size_t i = 0; i + 1 < chain.size(); ++i)
            {
                const auto lo = chain[i], up = chain[i + 1];
                if (! pr.init[lo].lowPass.enabled || ! pr.init[up].highPass.enabled)
                    continue;
                Param p { Kind::xoverPair, lo, up };
                p.base1 = pr.init[lo].lowPass.freq;
                p.minHp = minHighPass (up);
                p.base2 = std::max (pr.init[up].highPass.freq, p.minHp);
                p.cur = currentOctaveSteps (pr.state[up].highPass.freq, p.base2);
                p.lo = -8; p.hi = 8; p.step = 2; p.minStep = 1;
                if (haveTwin && ! rchain.empty()) { p.mir = { rchain[i] }; p.mir2 = { rchain[i + 1] }; }
                params.push_back (p);
            }

            // Sub <-> front: the lowest front way's HP moves on its own.
            if (! chain.empty() && cfg.hasSubwoofer() && opts.tuneSubIntegration && pr.init[chain[0]].highPass.enabled)
            {
                Param p { Kind::xoverHP, chain[0] };
                p.minHp = minHighPass (chain[0]);
                p.base2 = std::max (pr.init[chain[0]].highPass.freq, p.minHp);
                p.cur = currentOctaveSteps (pr.state[chain[0]].highPass.freq, p.base2);
                p.lo = -6; p.hi = 6; p.step = 2; p.minStep = 1;
                if (opts.constrained)
                {
                    p.subs = subsList;   // sub LP stays at HP / sqrt(2)
                    if (haveTwin && ! rchain.empty()) p.mir = { rchain[0] };
                }
                params.push_back (p);
            }
        }
        for (size_t k = 0; k < mch.size(); ++k)
        {
            if (opts.constrained)
                break;   // the sub LP is tied to the front HP above
            if (mch[k].role != DriverRole::subwoofer || ! opts.tuneSubIntegration || ! pr.init[k].lowPass.enabled)
                continue;
            Param p { Kind::xoverLP, k };
            p.base1 = pr.init[k].lowPass.freq;
            p.cur = currentOctaveSteps (pr.state[k].lowPass.freq, p.base1);
            p.lo = -6; p.hi = 6; p.step = 2; p.minStep = 1;
            params.push_back (p);
        }

        // 4) EQ band gains (redistribution happens through these plus discrete band moves).
        //    Graphic EQ: only the gains move, on the DSP's gain grid (centres and the Q model are
        //    fixed); a GEQ shared by a whole side is never re-balanced per channel.
        const bool graphic = cfg.caps.isGraphic();
        for (size_t k = 0; k < mch.size(); ++k)
        {
            if (! subAllowed (k) || pr.state[k].muted || frozen (k) || (graphic && ! cfg.caps.graphicPerChannel))
                continue;
            for (int b = 0; b < (int) pr.state[k].peq.size(); ++b)
            {
                const auto& band = pr.state[k].peq[(size_t) b];
                if (! band.enabled || ! (band.type == FilterType::peaking || band.type == FilterType::lowShelf
                                         || band.type == FilterType::highShelf))
                    continue;
                if (graphic)
                {
                    // Bands far outside the driver's passband do nothing audible; don't spend evaluations there.
                    const auto& st = pr.state[k];
                    const double plo = st.highPass.enabled && mch[k].role != DriverRole::subwoofer ? st.highPass.freq / 1.5 : 0.0;
                    const double phi = st.lowPass.enabled ? st.lowPass.freq * 1.5 : 1.0e6;
                    if (band.freq < plo || band.freq > phi)
                        continue;
                }
                Param p { Kind::bandGain, k };
                p.band = b;
                if (twin[k] >= 0)
                {
                    const auto t = (size_t) twin[k];
                    if ((size_t) b < pr.state[t].peq.size() && pr.state[t].peq[(size_t) b].type == band.type
                        && pr.state[t].peq[(size_t) b].freq == band.freq)
                        p.mir = { t };
                }
                p.cur = band.gainDb;
                p.lo = std::min (p.cur, std::max (graphic ? cfg.caps.graphicMaxCutDb : cfg.caps.peqMinGainDb, p.cur - 6.0));
                p.hi = std::max (p.cur, std::min (bandBoostLimit (k, band.freq), p.cur + 6.0));
                p.minStep = std::max (1e-3, graphic ? cfg.caps.graphicGainStepDb : cfg.caps.gainStepDb);
                p.step = std::max (p.minStep, 1.0);
                params.push_back (p);
            }
        }
    }

    static double currentOctaveSteps (double f, double base)
    {
        return std::round (std::log2 (f / base) * 24.0);
    }

    //==========================================================================
    struct Snapshot
    {
        std::vector<ChannelSettings> state;
        std::vector<Problem::Cache> cache;
    };
    Snapshot snapshot() const { return { pr.state, pr.cache }; }
    void restore (const Snapshot& s) { pr.state = s.state; pr.cache = s.cache; }

    /** Applies a parameter value (and its right-side twins / tied sub LPs); false if a constraint is violated
        (the caller restores the state). */
    bool apply (const Param& p, double v, std::vector<size_t>& touched)
    {
        if (! applyOne (p, v, touched))
            return false;
        for (size_t i = 0; i < p.mir.size(); ++i)
        {
            Param t = p;
            t.k = p.mir[i];
            t.k2 = i < p.mir2.size() ? p.mir2[i] : (size_t) -1;
            t.mir.clear(); t.mir2.clear(); t.subs.clear();
            double tv = v;
            if (p.kind == Kind::gain)
                tv = quantGain (juce::jlimit (gainLo (t.k), gainHi (t.k), pr.init[t.k].gainDb + (v - pr.init[p.k].gainDb)));
            else if (p.kind == Kind::bandGain)
            {
                const double d = v - pr.init[p.k].peq[(size_t) p.band].gainDb;
                const double lim = bandBoostLimit (t.k, pr.state[t.k].peq[(size_t) p.band].freq);
                tv = juce::jlimit (cfg.caps.isGraphic() ? cfg.caps.graphicMaxCutDb : cfg.caps.peqMinGainDb, lim,
                                   pr.init[t.k].peq[(size_t) p.band].gainDb + d);
                tv = cfg.caps.isGraphic() ? quantGraphic (tv) : quantGain (tv);
            }
            std::vector<size_t> tt;
            if (! applyOne (t, tv, tt))
                return false;
            touched.insert (touched.end(), tt.begin(), tt.end());
        }
        if (p.kind == Kind::xoverHP)
            for (auto sk : p.subs)
            {
                const double lp = CrossoverPlanner::snapFrequency (pr.state[p.k].highPass.freq / std::sqrt (2.0), cfg.caps);
                if (pr.state[sk].highPass.enabled && lp < pr.state[sk].highPass.freq * 1.5)
                    return false;
                pr.state[sk].lowPass.freq = lp;
                pr.cache[sk].parts[1] = pr.partOf (pr.state[sk].lowPass);
                pr.product (sk);
                touched.push_back (sk);
            }
        return true;
    }

    bool applyOne (const Param& p, double v, std::vector<size_t>& touched)
    {
        auto snap = [this] (double base, double steps) { return CrossoverPlanner::snapFrequency (base * std::pow (2.0, steps / 24.0), cfg.caps); };
        touched.clear();
        switch (p.kind)
        {
            case Kind::delay:
                pr.state[p.k].delayMs = v;
                pr.contribOnly (p.k);
                touched.push_back (p.k);
                return true;
            case Kind::gain:
                pr.state[p.k].gainDb = v;
                pr.contribOnly (p.k);
                touched.push_back (p.k);
                return true;
            case Kind::polarity:
                pr.state[p.k].inverted = ! pr.state[p.k].inverted;
                pr.contribOnly (p.k);
                touched.push_back (p.k);
                return true;
            case Kind::bandGain:
            {
                auto& band = pr.state[p.k].peq[(size_t) p.band];
                band.gainDb = v;
                if (cfg.caps.isGraphic())
                    band.q = graphicQ (band.freq, v);
                pr.cache[p.k].parts[(size_t) p.band + 2] = pr.partOf (band);
                pr.product (p.k);
                touched.push_back (p.k);
                return true;
            }
            case Kind::xoverPair:
            {
                const double lp = snap (p.base1, v), hp = snap (p.base2, v);
                const auto& upS = pr.state[p.k2];
                const auto& loS = pr.state[p.k];
                if (hp < p.minHp - 1e-6) return false;          // tweeter/mid safety: the floor alone, never relaxed by the start value
                if (upS.lowPass.enabled && hp > upS.lowPass.freq / 1.5) return false;
                if (loS.highPass.enabled && lp < loS.highPass.freq * 1.5) return false;
                pr.state[p.k].lowPass.freq = lp;
                pr.state[p.k2].highPass.freq = hp;
                pr.cache[p.k].parts[1] = pr.partOf (pr.state[p.k].lowPass);
                pr.cache[p.k2].parts[0] = pr.partOf (pr.state[p.k2].highPass);
                pr.product (p.k);
                pr.product (p.k2);
                touched = { p.k, p.k2 };
                return true;
            }
            case Kind::xoverHP:
            {
                const double hp = snap (p.base2, v);
                const auto& s = pr.state[p.k];
                if (hp < p.minHp - 1e-6) return false;
                if (s.lowPass.enabled && hp > s.lowPass.freq / 1.5) return false;
                pr.state[p.k].highPass.freq = hp;
                pr.cache[p.k].parts[0] = pr.partOf (pr.state[p.k].highPass);
                pr.product (p.k);
                touched.push_back (p.k);
                return true;
            }
            case Kind::xoverLP:
            {
                const double lp = snap (p.base1, v);
                const auto& s = pr.state[p.k];
                if (s.highPass.enabled && lp < s.highPass.freq * 1.5) return false;
                pr.state[p.k].lowPass.freq = lp;
                pr.cache[p.k].parts[1] = pr.partOf (pr.state[p.k].lowPass);
                pr.product (p.k);
                touched.push_back (p.k);
                return true;
            }
        }
        return false;
    }

    /** Tries one value; keeps it only if the cost drops. */
    bool tryValue (const Param& p, double v)
    {
        if (evals >= budget)
            return false;

        // Save only what the parameter can change (copying whole caches dominated the run time).
        struct Saved
        {
            size_t k;
            ChannelSettings s;
            std::vector<cplx> filt, part;
            std::vector<std::vector<cplx>> contrib;
            int partIndex = -1;
        };
        std::vector<Saved> saved;
        auto addSaved = [&] (size_t k, int partIndex)
        {
            if (k >= pr.numChannels())
                return;
            Saved sv { k, pr.state[k], {}, {}, pr.cache[k].contrib };
            sv.partIndex = partIndex;
            if (sv.partIndex >= 0)
            {
                sv.filt = pr.cache[k].filt;
                sv.part = pr.cache[k].parts[(size_t) sv.partIndex];
            }
            saved.push_back (std::move (sv));
        };
        auto addFor = [&] (const Param& q)
        {
            for (auto k : { q.k, q.k2 })
            {
                int pi = -1;
                if (q.kind == Kind::bandGain)       pi = q.band + 2;
                else if (q.kind == Kind::xoverHP)   pi = 0;
                else if (q.kind == Kind::xoverLP)   pi = 1;
                else if (q.kind == Kind::xoverPair) pi = k == q.k ? 1 : 0;
                addSaved (k, pi);
            }
        };
        addFor (p);
        for (size_t i = 0; i < p.mir.size(); ++i)
        {
            Param t = p;
            t.k = p.mir[i];
            t.k2 = i < p.mir2.size() ? p.mir2[i] : (size_t) -1;
            addFor (t);
        }
        for (auto sk : p.subs)
            addSaved (sk, 1);

        auto restoreSaved = [&]
        {
            for (auto it = saved.rbegin(); it != saved.rend(); ++it)
            {
                auto& sv = *it;
                auto& cache = pr.cache[sv.k];
                pr.state[sv.k] = std::move (sv.s);
                cache.contrib = std::move (sv.contrib);
                if (sv.partIndex >= 0)
                {
                    cache.filt = std::move (sv.filt);
                    cache.parts[(size_t) sv.partIndex] = std::move (sv.part);
                }
            }
        };

        std::vector<size_t> touched;
        if (! apply (p, v, touched))
        {
            restoreSaved();
            return false;
        }

        ++evals;
        const double c = pr.cost().total;
        if (c < best - 1e-9)
        {
            best = c;
            return true;
        }
        restoreSaved();
        return false;
    }

    double quantise (const Param& p, double v) const
    {
        switch (p.kind)
        {
            case Kind::delay:    return quantDelay (v);
            case Kind::gain:     return quantGain (v);
            case Kind::bandGain: return cfg.caps.isGraphic() ? quantGraphic (v) : quantGain (v);
            default:             return std::round (v);
        }
    }

    void descend()
    {
        std::vector<size_t> order (params.size());
        for (size_t i = 0; i < order.size(); ++i)
            order[i] = i;

        for (int sweep = 0; sweep < 200 && evals < budget; ++sweep)
        {
            // Deterministic shuffle within each group keeps the search unbiased.
            for (size_t i = order.size(); i > 1; --i)
                std::swap (order[i - 1], order[(size_t) rng.nextInt ((int) i)]);

            bool improved = false, allMin = true;
            for (auto idx : order)
            {
                auto& p = params[idx];
                if (p.kind == Kind::polarity)
                {
                    if (tryValue (p, 0.0))
                        improved = true;
                    continue;
                }

                bool moved = false;
                for (int dir : { 1, -1 })
                {
                    for (;;)
                    {
                        const double nv = quantise (p, juce::jlimit (p.lo, p.hi, p.cur + dir * p.step));
                        if (std::abs (nv - p.cur) < 1e-9 || nv < p.lo - 1e-9 || nv > p.hi + 1e-9)
                            break;
                        if (! tryValue (p, nv))
                            break;
                        p.cur = nv;
                        moved = true;
                    }
                    if (moved)
                        break;
                }
                if (moved)
                    improved = true;
                else
                    p.step = std::max (p.minStep, p.step * 0.5);
                if (p.step > p.minStep + 1e-12)
                    allMin = false;
            }
            if (! improved && allMin)
                break;
        }
    }

    /** Discrete moves: hand an EQ band to the overlapping driver, optional all-pass at a crossover. */
    bool tryDiscreteMoves()
    {
        bool any = false;
        if (cfg.caps.isGraphic())
            return false;   // fixed centres: no bands to hand over, no free slots for all-passes
        const auto& mch = pr.getModel().channels();
        const int slots = cfg.caps.peqBandsPerChannel;

        for (size_t k = 0; k < mch.size(); ++k)
        {
            if (frozen (k))
                continue;
            for (int b = (int) pr.state[k].peq.size() - 1; b >= 0; --b)
            {
                const auto band = pr.state[k].peq[(size_t) b];
                if (band.type != FilterType::peaking)
                    continue;
                for (size_t j = 0; j < mch.size(); ++j)
                {
                    if (j == k || mch[j].side != mch[k].side || (int) pr.state[j].peq.size() >= slots || ! subAllowed (j)
                        || frozen (j))
                        continue;
                    // Only between drivers that overlap at the band's frequency (within the crossover region).
                    const auto& sj = pr.state[j];
                    const double jlo = sj.highPass.enabled && mch[j].role != DriverRole::subwoofer ? sj.highPass.freq : 10.0;
                    const double jhi = sj.lowPass.enabled ? sj.lowPass.freq : 40000.0;
                    if (band.freq < jlo / 1.41 || band.freq > jhi * 1.41)
                        continue;
                    if (band.gainDb > bandBoostLimit (j, band.freq))
                        continue;

                    if (evals >= budget)
                        return any;
                    const auto saved = snapshot();
                    pr.state[k].peq.erase (pr.state[k].peq.begin() + b);
                    pr.state[j].peq.push_back (band);
                    pr.rebuild (k);
                    pr.rebuild (j);
                    ++evals;
                    const double c = pr.cost().total;
                    if (c < best - 1e-9)
                    {
                        best = c;
                        any = true;
                        break;
                    }
                    restore (saved);
                }
            }
        }

        if (opts.allowAllPass && cfg.caps.hasAllPass && ! opts.constrained)
        {
            for (auto& p : params)
            {
                if (p.kind != Kind::xoverPair)
                    continue;
                const double fc = pr.state[p.k].lowPass.freq;
                for (auto k : { p.k, p.k2 })
                {
                    if ((int) pr.state[k].peq.size() >= slots)
                        continue;
                    for (double fm : { 0.7, 1.0, 1.4 })
                        for (double q : { 0.5, 0.7, 1.0 })
                        {
                            if (evals >= budget)
                                return any;
                            FilterSpec ap;
                            ap.type = FilterType::allPass;
                            ap.freq = CrossoverPlanner::snapFrequency (fc * fm, cfg.caps);
                            ap.q = q;
                            const auto saved = snapshot();
                            pr.state[k].peq.push_back (ap);
                            pr.rebuild (k);
                            ++evals;
                            const double c = pr.cost().total;
                            if (c < best - 1e-9)
                            {
                                best = c;
                                any = true;
                            }
                            else
                                restore (saved);
                        }
                }
            }
        }
        return any;
    }
};

juce::String describeChange (const juce::String& what, double a, double b, int decimals, const juce::String& unit)
{
    const double d = b - a;
    return what + " " + juce::String (a, decimals) + " -> " + juce::String (b, decimals) + " " + unit
         + " (" + (d >= 0 ? "+" : "") + juce::String (d, decimals) + ")";
}
} // namespace

//==============================================================================
Outcome refine (SystemConfig& config, const DriverDataMap& data, const Options& options)
{
    Outcome out;
    // Safety first: any enabled HP that starts below its safe floor (loaded or hand-edited config) is
    // raised to the floor before anything else, so neither the search nor the "no improvement" path keeps it.
    for (auto& ch : config.channels)
    {
        auto& hp = ch.settings.highPass;
        if (ch.role == DriverRole::subwoofer || ! hp.enabled)
            continue;
        const double floorHz = CrossoverPlanner::safeHighPassHz (ch, config.safety);
        if (! std::isfinite (hp.freq) || hp.freq < floorHz)
        {
            out.notes.add (ch.label + ": high-pass " + juce::String (juce::roundToInt (hp.freq)) + " Hz was below the safe minimum; raised to "
                           + juce::String (juce::roundToInt (floorHz)) + " Hz");
            hp.freq = floorHz;
        }
    }
    const SystemConfig original = config;

    Problem pr (config, data, options);
    if (pr.numChannels() == 0)
    {
        out.notes.add ("Refinement skipped: no measured channels.");
        return out;
    }

    const auto before = pr.cost();
    out.costBefore = before.total;

    Search search (pr, config, options);
    search.run();

    if (config.caps.isGraphic())
    {
        const auto& caps = config.caps;
        for (size_t k = 0; k < pr.numChannels(); ++k)
        {
            auto& st = pr.state[k];
            const auto& in = pr.init[k];
            bool sameLayout = st.peq.size() == in.peq.size();
            for (size_t b = 0; sameLayout && b < st.peq.size(); ++b)
                sameLayout = st.peq[b].type == in.peq[b].type && st.peq[b].freq == in.peq[b].freq;

            if (! caps.graphicPerChannel || ! sameLayout)
            {
                st.peq = in.peq;
                continue;
            }
            if (in.peq.size() == caps.graphicCentres().size())
            {
                auto g = GraphicEQ::gainsOf (caps, st.peq);
                for (auto& x : g)
                    x = GraphicEQ::quantise (caps, x, caps.graphicMaxCutDb, caps.graphicMaxBoostDb);
                st.peq = GraphicEQ::makeGraphic (caps, g);
            }
        }
    }

    // Rebuild every cache from the settings and recompute from scratch, so the decision below
    // is made on exactly what will be written to the DSP.
    for (size_t k = 0; k < pr.numChannels(); ++k)
        pr.rebuild (k);
    const auto after = pr.cost();
    out.costAfter = after.total;

    if (! (after.total < before.total - 1e-9))
    {
        config = original;
        out.costAfter = out.costBefore;
        out.notes.add ("Refinement: no change improved the predicted result (cost "
                       + juce::String (before.total, 3) + "); tune left exactly as solved.");
        return out;
    }

    auto line = [] (const CostBreakdown& c)
    {
        return "tonal " + juce::String (c.tonal, 2) + " dB, L/R " + juce::String (c.symmetry, 2) + " dB, L+R loss "
             + juce::String (c.summationLR, 2) + " dB, crossover loss " + juce::String (c.summationSide, 2) + " dB";
    };
    out.notes.add ("Refinement (" + juce::String (search.evaluations()) + " evaluations): cost "
                   + juce::String (before.total, 3) + " -> " + juce::String (after.total, 3));
    out.notes.add ("  before: " + line (before));
    out.notes.add ("  after:  " + line (after));

    for (size_t k = 0; k < pr.numChannels(); ++k)
    {
        auto& ch = config.channels[pr.getModel().channels()[k].cfgIndex];
        const auto& a = pr.init[k];
        const auto& b = pr.state[k];
        const double fc = pr.phaseFrequency (k);

        if (std::abs (b.delayMs - a.delayMs) > 1e-6)
            out.notes.add (ch.label + ": " + describeChange ("delay", a.delayMs, b.delayMs, 2, "ms")
                           + (ch.role == DriverRole::subwoofer ? " - sub/front phase at " : " - phase match at ")
                           + juce::String (juce::roundToInt (fc)) + " Hz across all mic positions");
        if (a.inverted != b.inverted)
            out.notes.add (ch.label + ": polarity " + (b.inverted ? "inverted" : "normal")
                           + " - sums better with its neighbours (fewer cancellations).");
        if (std::abs (b.gainDb - a.gainDb) > 1e-6)
            out.notes.add (ch.label + ": " + describeChange ("level", a.gainDb, b.gainDb, 1, "dB") + " - band balance/L-R symmetry");
        if (std::abs (b.highPass.freq - a.highPass.freq) > 1e-6 && b.highPass.enabled)
            out.notes.add (ch.label + ": " + describeChange ("high-pass", a.highPass.freq, b.highPass.freq, 0, "Hz")
                           + " (safe minimum " + juce::String (juce::roundToInt (CrossoverPlanner::safeHighPassHz (ch, config.safety))) + " Hz respected)");
        if (std::abs (b.lowPass.freq - a.lowPass.freq) > 1e-6 && b.lowPass.enabled)
            out.notes.add (ch.label + ": " + describeChange ("low-pass", a.lowPass.freq, b.lowPass.freq, 0, "Hz")
                           + " - smoother summation with the neighbouring way");

        int changedBands = 0;
        double moved = 0.0;
        const size_t common = std::min (a.peq.size(), b.peq.size());
        for (size_t i = 0; i < common; ++i)
            if (std::abs (a.peq[i].gainDb - b.peq[i].gainDb) > 1e-6 || a.peq[i].freq != b.peq[i].freq)
            {
                ++changedBands;
                moved += std::abs (a.peq[i].gainDb - b.peq[i].gainDb);
            }
        if (changedBands > 0)
            out.notes.add (ch.label + ": " + juce::String (changedBands) + " EQ band(s) re-balanced (total "
                           + juce::String (moved, 1) + " dB moved)");
        if (b.peq.size() != a.peq.size())
            out.notes.add (ch.label + ": EQ bands " + juce::String ((int) a.peq.size()) + " -> " + juce::String ((int) b.peq.size())
                           + " (bands handed between overlapping drivers / all-pass added)");

        ch.settings = b;
    }
    return out;
}
} // namespace fa::advanced
