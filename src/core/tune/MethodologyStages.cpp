#include "MethodologyStages.h"
#include "advanced/Optimizer.h"
#include <cmath>

namespace fa::method
{
//==============================================================================
double sweepCapDbFS (DriverRole role) noexcept
{
    switch (role)
    {
        case DriverRole::tweeter:   return -24.0;   // lowest: a dome tweeter has the least headroom
        case DriverRole::midrange:  return -18.0;
        case DriverRole::fullrange: return -18.0;
        case DriverRole::midbass:   return -12.0;
        case DriverRole::subwoofer: return -12.0;
    }
    return -18.0;
}

double sweepLevelFor (DriverRole role, double requestedDbFS, double safetyCeilingDbFS) noexcept
{
    return std::min ({ requestedDbFS, sweepCapDbFS (role), safetyCeilingDbFS });
}

//==============================================================================
namespace
{
double meanDbOver (const FrequencyResponse& r, double lo, double hi)
{
    double acc = 0.0;
    int n = 0;
    for (size_t i = 0; i < r.size(); ++i)
        if (r.freqs[i] >= lo && r.freqs[i] <= hi)
            { acc += r.magnitudeDb (i); ++n; }
    return n > 0 ? acc / n : -200.0;
}

double imagingScore (const SystemConfig& cfg, const DriverDataMap& data)
{
    const auto j = advanced::judge (cfg, data);
    double s = 0.0;
    for (auto* name : { "Centre focus", "Imaging consistency" })
        if (auto it = j.categories.find (name); it != j.categories.end())
            s += it->second;
    return s;
}
} // namespace

ImagingResult balanceImagingByLevel (SystemConfig& cfg, const SystemSolver& solver, const DriverDataMap& data)
{
    ImagingResult res;
    if (! cfg.isStereo())
    {
        res.notes.add ("Imaging: skipped, there is no left/right pair (single side / single driver).");
        return res;
    }

    res.judgeBefore = imagingScore (cfg, data);
    const auto original = cfg;
    const double step = std::max (0.1, cfg.caps.gainStepDb);

    std::vector<bool> used (cfg.channels.size(), false);
    for (size_t i = 0; i < cfg.channels.size(); ++i)
    {
        auto& l = cfg.channels[i];
        if (l.side != Side::left || l.role == DriverRole::subwoofer || data.count (l.id) == 0)
            continue;

        size_t j = cfg.channels.size();
        for (size_t k = 0; k < cfg.channels.size(); ++k)
            if (! used[k] && cfg.channels[k].side == Side::right && cfg.channels[k].role == l.role && data.count (cfg.channels[k].id) > 0)
                { j = k; break; }
        if (j == cfg.channels.size())
            continue;
        used[j] = true;
        auto& r = cfg.channels[j];

        // Compare over the part of the passband that matters for the centre image.
        const double lo = std::max (150.0, std::max (l.settings.highPass.enabled ? l.settings.highPass.freq : 0.0,
                                                     r.settings.highPass.enabled ? r.settings.highPass.freq : 0.0));
        const double hi = std::min (8000.0, std::min (l.settings.lowPass.enabled ? l.settings.lowPass.freq : 1.0e6,
                                                      r.settings.lowPass.enabled ? r.settings.lowPass.freq : 1.0e6));
        if (hi < lo * 1.3)
            continue;

        auto levelOf = [&] (const OutputChannel& c)
        {
            std::vector<FrequencyResponse> ps;
            for (int p = 0; p < solver.numPositions(); ++p)
                ps.push_back (solver.predict (c.id, c.settings, p));
            return meanDbOver (powerAverage (ps), lo, hi);
        };

        const double d = levelOf (l) - levelOf (r);   // > 0: left is louder
        if (std::abs (d) < 0.5)
            continue;
        const double cut = std::round (std::min (std::abs (d), 3.0) / step) * step;
        if (cut < step - 1e-9)
            continue;

        auto& loud = d > 0.0 ? l : r;
        const double before = loud.settings.gainDb;
        loud.settings.gainDb = juce::jlimit (cfg.caps.minChannelGainDb, cfg.caps.maxChannelGainDb, before - cut);
        if (std::abs (loud.settings.gainDb - before) < 1e-9)
            continue;
        ++res.channelsAdjusted;
        res.notes.add ("Imaging: " + loud.label + " level " + juce::String (before, 1) + " -> " + juce::String (loud.settings.gainDb, 1)
                       + " dB (" + juce::String (std::abs (d), 1) + " dB louder than its twin over "
                       + juce::String (juce::roundToInt (lo)) + "-" + juce::String (juce::roundToInt (hi)) + " Hz). Level only, no delay change.");
    }

    res.judgeAfter = imagingScore (cfg, data);
    if (res.channelsAdjusted == 0)
    {
        res.notes.add ("Imaging: left and right levels already match (within 0.5 dB per driver).");
        return res;
    }
    if (res.judgeAfter < res.judgeBefore - 0.05)
    {
        cfg = original;   // the predicted centre/imaging score got worse: keep the matched levels
        res.channelsAdjusted = 0;
        res.judgeAfter = res.judgeBefore;
        res.notes.clear();
        res.notes.add ("Imaging: level balancing did not improve the predicted centre image; levels left as matched.");
        return res;
    }
    res.changed = true;
    res.notes.add ("Imaging: centre/imaging score " + juce::String (res.judgeBefore, 1) + " -> " + juce::String (res.judgeAfter, 1)
                   + " (of 200), by level only.");
    return res;
}

//==============================================================================
namespace
{
double worstDip (const std::vector<std::pair<double, double>>& dips)
{
    double w = 0.0;
    for (auto& d : dips)
        w = std::max (w, d.second);
    return w;
}

/** Mean loss (dB) of each side's own driver summation vs an incoherent sum, 40 Hz..10 kHz (crossover holes). */
double sideSumLoss (const SystemSolver& solver)
{
    double total = 0.0;
    int count = 0;
    for (auto side : { Side::left, Side::right })
    {
        const auto chans = solver.sideChannels (side);
        for (int p = 0; p < solver.numPositions(); ++p)
        {
            std::vector<FrequencyResponse> parts;
            for (auto* c : chans)
                parts.push_back (solver.predict (c->id, c->settings, p));
            if (parts.empty())
                continue;
            double acc = 0.0;
            int n = 0;
            for (size_t i = 0; i < parts[0].size(); ++i)
            {
                const double f = parts[0].freqs[i];
                if (f < 40.0 || f > 10000.0)
                    continue;
                cplx sum (0.0, 0.0);
                double ideal = 0.0;
                for (auto& part : parts)
                {
                    sum += part.values[i];
                    ideal += std::norm (part.values[i]);
                }
                if (ideal <= 1e-20)
                    continue;
                acc += std::max (0.0, 10.0 * std::log10 (ideal / std::max (std::norm (sum), 1e-20)));
                ++n;
            }
            total += n > 0 ? acc / n : 0.0;
            ++count;
        }
    }
    return count > 0 ? total / count : 0.0;
}
} // namespace

SumCheckResult correctSumCancellation (SystemConfig& cfg, SystemSolver& solver)
{
    SumCheckResult res;
    if (! cfg.isStereo())
    {
        res.notes.add ("L+R check: skipped, there is no left/right pair.");
        return res;
    }

    auto dips = solver.findSumDips();
    res.worstBeforeDb = res.worstAfterDb = worstDip (dips);
    if (dips.empty())
    {
        res.notes.add ("L+R check: no cancellation between left and right (sum within 6 dB of the expected level).");
        return res;
    }

    for (int round = 0; round < 2 && ! dips.empty(); ++round)
    {
        struct Candidate { std::vector<size_t> idx; juce::String what; };
        std::vector<Candidate> cands;
        {
            // Whole-side flips only: each driver's own polarity was already measured and set by the polarity
            // stage, so only the left-versus-right relationship (and the mono sub) is questioned here.
            Candidate right { {}, "the whole right side" }, left { {}, "the whole left side" }, subs { {}, "the subwoofer(s)" };
            for (size_t i = 0; i < cfg.channels.size(); ++i)
            {
                const auto& c = cfg.channels[i];
                if (c.role == DriverRole::subwoofer)
                    subs.idx.push_back (i);
                else
                    (c.side == Side::right ? right : left).idx.push_back (i);
            }
            for (auto* c : { &right, &left, &subs })
                if (! c->idx.empty())
                    cands.push_back (*c);
        }

        const double lossBase = sideSumLoss (solver);
        const double worstBase = worstDip (dips);
        double meanBase = 0.0;
        solver.findSumDips (nullptr, &meanBase);
        if (meanBase < 1.0)
            break;   // narrow local dips are not a polarity problem: report them, don't flip anything
        double bestMean = meanBase, bestWorst = worstBase;
        int best = -1;
        for (size_t ci = 0; ci < cands.size(); ++ci)
        {
            for (auto i : cands[ci].idx)
                cfg.channels[i].settings.inverted = ! cfg.channels[i].settings.inverted;
            double mean = 0.0;
            const double w = worstDip (solver.findSumDips (nullptr, &mean));
            const bool sidesOk = sideSumLoss (solver) <= lossBase + 0.5;
            for (auto i : cands[ci].idx)
                cfg.channels[i].settings.inverted = ! cfg.channels[i].settings.inverted;
            if (sidesOk && mean < bestMean - 1e-6)
            {
                bestMean = mean;
                bestWorst = w;
                best = (int) ci;
            }
        }

        if (best < 0 || bestMean > meanBase - 0.5)   // must clearly reduce the average cancellation
            break;

        for (auto i : cands[(size_t) best].idx)
            cfg.channels[i].settings.inverted = ! cfg.channels[i].settings.inverted;
        res.corrected = true;
        res.notes.add ("L+R check: cancellation (worst " + juce::String (worstBase, 1) + " dB, average " + juce::String (meanBase, 1)
                       + " dB) corrected by inverting the polarity of " + cands[(size_t) best].what + " (now worst "
                       + juce::String (bestWorst, 1) + " dB, average " + juce::String (bestMean, 1) + " dB).");
        dips = solver.findSumDips();
    }

    res.worstAfterDb = worstDip (dips);
    if (! dips.empty())
        res.notes.add ("L+R check: " + juce::String ((int) dips.size()) + " cancellation dip(s) remain (worst "
                       + juce::String (res.worstAfterDb, 1) + " dB at " + juce::String (juce::roundToInt (dips.front().first))
                       + " Hz). A polarity change does not fix it (or it is too narrow to be one); check the delay and crossover phase around that frequency.");
    else if (res.corrected)
        res.notes.add ("L+R check: no cancellation left after the correction.");
    return res;
}

//==============================================================================
NoiseReading analyseNoise (const std::vector<float>& x, double sr)
{
    NoiseReading r;
    const size_t n = x.size();
    if (n < 64 || sr <= 0.0)
        return r;

    double mean = 0.0;
    for (auto v : x)
        mean += v;
    mean /= (double) n;

    double ms = 0.0, dms = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const double v = x[i] - mean;
        ms += v * v;
        if (i > 0)
        {
            const double d = (double) x[i] - x[i - 1];
            dms += d * d;
        }
    }
    ms /= (double) n;
    dms /= (double) (n - 1);
    r.rmsDbFS = 10.0 * std::log10 (std::max (ms, 1e-14));
    r.hissDbFS = 10.0 * std::log10 (std::max (dms * 0.5, 1e-14));   // white noise: var (diff) = 2 var

    // Mains hum: Goertzel power at 50/60 Hz and the first two harmonics (mean square of a sine = A^2 / 2).
    double hum = 0.0;
    for (double f : { 50.0, 100.0, 150.0, 60.0, 120.0, 180.0 })
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * f / sr;
        const double c = 2.0 * std::cos (w);
        double s1 = 0.0, s2 = 0.0;
        for (size_t i = 0; i < n; ++i)
        {
            const double s0 = (x[i] - mean) + c * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        const double power = s1 * s1 + s2 * s2 - c * s1 * s2;
        hum += 2.0 * power / ((double) n * (double) n);
    }
    r.humDbFS = 10.0 * std::log10 (std::max (hum, 1e-14));
    return r;
}

std::vector<NoiseFinding> assessNoise (const NoiseReading& base, const std::vector<NamedNoise>& channels)
{
    std::vector<NoiseFinding> out;
    for (auto& c : channels)
    {
        const double added = c.reading.rmsDbFS - base.rmsDbFS;
        if (added < 3.0)
            continue;

        NoiseFinding f;
        f.channelId = c.id;
        f.addedDb = added;
        const bool humDominant = c.reading.humDbFS > c.reading.rmsDbFS - 6.0 && c.reading.humDbFS > base.humDbFS + 3.0;
        f.hum = humDominant;
        if (humDominant)
            f.message = c.label + ": hum at mains frequency (50/60 Hz and harmonics), +" + juce::String (added, 1)
                      + " dB over the silent floor - a ground loop. Check the amp and DSP ground points (one common star ground), keep RCA "
                        "cables away from power cables, and try a ground-loop isolator or balanced connections.";
        else
            f.message = c.label + ": adds hiss when its output is on, +" + juce::String (added, 1)
                      + " dB over the silent floor - gain structure. Lower the amp's input gain, raise the DSP output level instead, "
                        "and make sure the head unit volume is not maxed to compensate.";
        out.push_back (std::move (f));
    }
    return out;
}
} // namespace fa::method
