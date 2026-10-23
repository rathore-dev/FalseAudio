#include "SystemSolver.h"
#include "CrossoverPlanner.h"
#include "../dsp/Spectrum.h"

namespace fa
{
SystemSolver::SystemSolver (SystemConfig& config, const DriverDataMap& d, Options o)
    : cfg (config), data (d), opts (o), freqs (analysisFrequencies())
{
}

int SystemSolver::numPositions() const
{
    size_t n = 0;
    for (auto& [id, dd] : data)
        n = std::max (n, dd.positions.size());
    return (int) std::max<size_t> (1, n);
}

FrequencyResponse SystemSolver::predict (const juce::String& id, const ChannelSettings& settings, int position) const
{
    auto it = data.find (id);
    if (it == data.end() || it->second.positions.empty())
        return FrequencyResponse::unity (freqs).scale (0.0);

    const auto& dd = it->second;
    const size_t p = std::min ((size_t) std::max (0, position), dd.positions.size() - 1);

    auto r = dd.positions[p].freqs == freqs ? dd.positions[p] : dd.positions[p].resampled (freqs);
    r.applyDelay (dd.onsetMs[p] * 0.001);

    const auto oldR = dd.measuredWith.response (freqs, cfg.caps.sampleRate, true);
    const auto newR = settings.response (freqs, cfg.caps.sampleRate, true);

    for (size_t i = 0; i < freqs.size(); ++i)
    {
        cplx o = oldR.values[i];
        const double om = std::abs (o);
        if (om < 1e-2) // the protective filter removed this region; don't amplify noise
            o = om > 0.0 ? o * (1e-2 / om) : cplx (1e-2, 0.0);
        r.values[i] *= newR.values[i] / o;
    }
    return r;
}

FrequencyResponse SystemSolver::predictSum (const std::vector<const OutputChannel*>& chans, int position) const
{
    auto sum = FrequencyResponse::unity (freqs).scale (0.0);
    for (auto* c : chans)
        sum += predict (c->id, c->settings, position);
    return sum;
}

std::vector<const OutputChannel*> SystemSolver::sideChannels (Side side) const
{
    std::vector<const OutputChannel*> v;
    for (auto& c : cfg.channels)
        if (c.side == side || c.side == Side::mono)
            v.push_back (&c);
    return v;
}

FrequencyResponse SystemSolver::averagedSideSum (Side side) const
{
    std::vector<FrequencyResponse> sums;
    const auto chans = sideChannels (side);
    for (int p = 0; p < numPositions(); ++p)
        sums.push_back (predictSum (chans, p));
    return powerAverage (sums);
}

std::pair<double, double> SystemSolver::dominantBand (const OutputChannel& ch) const
{
    const auto& s = ch.settings;
    const double lo = (s.highPass.enabled && ch.role != DriverRole::subwoofer) ? s.highPass.freq : 20.0;
    double hi = s.lowPass.enabled ? s.lowPass.freq : 20000.0;
    if (ch.role == DriverRole::tweeter || ch.role == DriverRole::fullrange)
        hi = std::min (hi, 16000.0);
    return { lo, std::max (hi, lo * 1.5) };
}

double SystemSolver::quantiseDelay (double ms) const
{
    const double step = std::max (1e-4, cfg.caps.delayStepMs);
    return juce::jlimit (0.0, cfg.caps.maxDelayMs, std::round (ms / step) * step);
}

double SystemSolver::inPhaseCost (const FrequencyResponse& a, const FrequencyResponse& b, double fLow, double fHigh) const
{
    // 0 dB when the two arrive in phase; grows as they cancel.
    double acc = 0.0;
    int n = 0;
    for (size_t i = 0; i < freqs.size(); ++i)
    {
        if (freqs[i] < fLow || freqs[i] > fHigh)
            continue;
        const double ideal = std::abs (a.values[i]) + std::abs (b.values[i]);
        const double real  = std::abs (a.values[i] + b.values[i]);
        acc += dbFromMag (ideal) - dbFromMag (real);
        ++n;
    }
    return n > 0 ? acc / n : 0.0;
}

//==============================================================================
void SystemSolver::checkPolarity()
{
    for (auto& ch : cfg.channels)
    {
        auto it = data.find (ch.id);
        if (it == data.end() || it->second.positions.empty())
            continue;

        auto [lo, hi] = dominantBand (ch);
        const auto est = estimateArrival (it->second.positions[0], lo * 1.2, hi / 1.2);

        // What the DSP was doing during the measurement must be removed from the verdict.
        const bool acousticInverted = est.inverted != it->second.measuredWith.inverted;
        report.wiringInverted[ch.id] = acousticInverted && est.confidence > 0.5;

        if (est.confidence <= 0.5)
        {
            report.warnings.add (ch.label + ": polarity uncertain (reflections dominate); kept as wired.");
            continue;
        }

        if (acousticInverted)
        {
            ch.settings.inverted = ! ch.settings.inverted;
            report.warnings.add (ch.label + " appears wired in reverse polarity. Corrected in the DSP; "
                                 "consider swapping the + / - wires at the speaker.");
        }
    }
}

void SystemSolver::alignTime()
{
    std::map<juce::String, double> arrival;

    for (auto& ch : cfg.channels)
    {
        auto it = data.find (ch.id);
        if (it == data.end() || it->second.positions.empty())
            continue;

        auto [lo, hi] = dominantBand (ch);
        if (ch.role == DriverRole::subwoofer)
        {
            lo = 25.0;
            hi = ch.settings.lowPass.enabled ? ch.settings.lowPass.freq : 80.0;
        }

        double sum = 0.0;
        int n = 0;
        for (size_t p = 0; p < it->second.positions.size(); ++p)
        {
            const auto est = estimateArrival (it->second.positions[p], lo * 1.2, hi / 1.2);
            const double fine = est.confidence > 0.4 ? est.delayMs : 0.0;
            sum += it->second.onsetMs[p] + fine;
            ++n;
        }

        arrival[ch.id] = sum / n - it->second.measuredWith.delayMs;
        report.arrivalMs[ch.id] = arrival[ch.id];
    }

    if (arrival.empty())
        return;

    double latest = -1.0e9;
    for (auto& [id, a] : arrival)
        latest = std::max (latest, a);

    for (auto& [id, a] : arrival)
    {
        if (auto* ch = cfg.find (id))
        {
            ch->settings.delayMs = quantiseDelay (latest - a);
            report.notes.add (ch->label + ": arrives at " + juce::String (a, 2) + " ms ("
                              + juce::String (msToMetres (a) * 100.0, 0) + " cm), delay "
                              + juce::String (ch->settings.delayMs, 2) + " ms");
        }
    }
}

void SystemSolver::refineCrossoverPhase()
{
    auto searchPair = [this] (OutputChannel& lower, const std::vector<const OutputChannel*>& partners,
                              double fLow, double fHigh, double fc)
    {
        const double halfPeriodMs = 500.0 / fc;
        const int steps = std::max (11, opts.extraPhaseSearchSteps);
        const auto original = lower.settings;

        double bestCost = 1.0e9, bestDelta = 0.0;
        bool bestFlip = false;

        for (int flip = 0; flip < (opts.allowPolarityFlip ? 2 : 1); ++flip)
        {
            for (int s = 0; s < steps; ++s)
            {
                const double delta = -halfPeriodMs + 2.0 * halfPeriodMs * s / (steps - 1);
                ChannelSettings trial = original;
                trial.delayMs = original.delayMs + delta;
                trial.inverted = flip ? ! original.inverted : original.inverted;

                double cost = 0.0;
                for (int p = 0; p < numPositions(); ++p)
                {
                    const auto a = predict (lower.id, trial, p);
                    for (auto* partner : partners)
                        cost += inPhaseCost (a, predict (partner->id, partner->settings, p), fLow, fHigh);
                }
                cost /= (double) (numPositions() * (int) partners.size());

                // Prefer the acoustic (distance) solution and the wired polarity unless the gain is real.
                cost += 0.05 * std::abs (delta) / halfPeriodMs + (flip ? 0.75 : 0.0);

                if (cost < bestCost)
                {
                    bestCost = cost;
                    bestDelta = delta;
                    bestFlip = flip != 0;
                }
            }
        }

        lower.settings.delayMs = original.delayMs + bestDelta; // may go negative; normalised later
        if (bestFlip)
            lower.settings.inverted = ! original.inverted;

        report.notes.add (lower.label + ": crossover phase at " + juce::String (fc, 0) + " Hz refined by "
                          + juce::String (bestDelta, 2) + " ms" + (bestFlip ? " and polarity flipped" : "")
                          + " (summing loss " + juce::String (bestCost, 2) + " dB)");
    };

    // Front stage, top-down: tweeter stays put, each lower way is aligned to the one above.
    for (auto side : { Side::left, Side::right })
    {
        std::vector<OutputChannel*> chain;
        for (auto& c : cfg.channels)
            if (c.side == side && c.role != DriverRole::subwoofer && data.count (c.id) > 0)
                chain.push_back (&c);
        std::sort (chain.begin(), chain.end(), [] (auto* a, auto* b)
        {
            const double fa = a->settings.lowPass.enabled ? a->settings.lowPass.freq : 1.0e6;
            const double fb = b->settings.lowPass.enabled ? b->settings.lowPass.freq : 1.0e6;
            return fa > fb;
        });

        for (size_t i = 1; i < chain.size(); ++i)
        {
            auto& lower = *chain[i];
            const double fc = lower.settings.lowPass.freq;
            searchPair (lower, { chain[i - 1] }, fc / 2.0, fc * 2.0, fc);
        }
    }

    // Subwoofer(s) against the lowest front way of each side.
    for (auto& sub : cfg.channels)
    {
        if (sub.role != DriverRole::subwoofer || data.count (sub.id) == 0)
            continue;

        std::vector<const OutputChannel*> partners;
        for (auto side : { Side::left, Side::right })
        {
            if (sub.side != Side::mono && sub.side != side)
                continue;
            const OutputChannel* lowest = nullptr;
            for (auto& c : cfg.channels)
                if (c.side == side && c.role != DriverRole::subwoofer && data.count (c.id) > 0)
                    if (lowest == nullptr || c.settings.highPass.freq < lowest->settings.highPass.freq)
                        lowest = &c;
            if (lowest != nullptr)
                partners.push_back (lowest);
        }
        if (partners.empty())
            continue;

        const double fSub = sub.settings.lowPass.enabled ? sub.settings.lowPass.freq : 80.0;
        const double fFront = partners.front()->settings.highPass.freq;
        searchPair (sub, partners, fSub / 2.0, fFront * 2.0, std::sqrt (fSub * fFront));
    }

    // Normalise so the smallest delay is zero.
    double minDelay = 1.0e9;
    for (auto& c : cfg.channels)
        minDelay = std::min (minDelay, c.settings.delayMs);
    for (auto& c : cfg.channels)
    {
        c.settings.delayMs = quantiseDelay (c.settings.delayMs - minDelay);
        if (c.settings.delayMs >= cfg.caps.maxDelayMs)
            report.warnings.add (c.label + ": required delay exceeds the DSP's maximum.");
    }
}

void SystemSolver::matchLevels()
{
    struct Level { OutputChannel* ch; double offset; };
    std::vector<Level> levels;

    for (auto& ch : cfg.channels)
    {
        if (data.count (ch.id) == 0)
            continue;

        auto [lo, hi] = dominantBand (ch);
        lo *= 1.25;
        hi /= 1.25;
        if (hi <= lo)
            continue;

        std::vector<FrequencyResponse> pos;
        for (int p = 0; p < numPositions(); ++p)
            pos.push_back (predict (ch.id, ch.settings, p));
        const auto avg = smoothFractionalOctave (powerAverage (pos), 3.0);

        double measured = 0.0, target = 0.0;
        int n = 0;
        for (size_t i = 0; i < freqs.size(); ++i)
        {
            if (freqs[i] < lo || freqs[i] > hi)
                continue;
            measured += avg.magnitudeDb (i);
            target += cfg.target.levelAt (freqs[i]);
            ++n;
        }
        if (n > 0)
            levels.push_back ({ &ch, measured / n - target / n });
    }

    if (levels.empty())
        return;

    // Align everything to the quietest channel (relative to target) so no channel needs boost.
    double minOffset = 1.0e9;
    for (auto& l : levels)
        minOffset = std::min (minOffset, l.offset);

    for (auto& l : levels)
    {
        auto& s = l.ch->settings;
        const double change = -(l.offset - minOffset);
        s.gainDb = juce::jlimit (cfg.caps.minChannelGainDb, cfg.caps.maxChannelGainDb,
                                 std::round ((s.gainDb + change) / cfg.caps.gainStepDb) * cfg.caps.gainStepDb);
        report.notes.add (l.ch->label + ": level " + juce::String (s.gainDb, 1) + " dB");
    }
}

void SystemSolver::equaliseSide (Side side, const std::vector<double>& targetDb, double& rmsOut)
{
    std::vector<OutputChannel*> chain;
    for (auto& c : cfg.channels)
        if (c.side == side && c.role != DriverRole::subwoofer && data.count (c.id) > 0)
            chain.push_back (&c);
    std::sort (chain.begin(), chain.end(), [] (auto* a, auto* b) { return a->settings.highPass.freq < b->settings.highPass.freq; });

    for (auto* ch : chain)
    {
        auto [lo, hi] = dominantBand (*ch);

        std::vector<FrequencyResponse> sums;
        for (int p = 0; p < numPositions(); ++p)
            sums.push_back (predictSum (sideChannels (side), p));

        AutoEQ::Settings s;
        s.sampleRate = cfg.caps.sampleRate;
        s.fLow = lo;
        s.fHigh = hi;
        s.maxBands = cfg.caps.peqBandsPerChannel;
        s.maxBoostDb = std::min (cfg.safety.maxBoostDb, cfg.caps.peqMaxGainDb);
        s.maxCutDb = cfg.caps.peqMinGainDb;
        s.minQ = cfg.caps.peqMinQ;
        s.maxQ = std::min (cfg.caps.peqMaxQ, 10.0);
        s.lfBoostLimitBelowHz = lo * 2.0;
        s.lfBoostLimitDb = cfg.safety.maxLowFreqBoostDb;
        s.gainStepDb = cfg.caps.gainStepDb;

        const auto res = AutoEQ::solve (powerAverage (sums), targetDb, s, AutoEQ::spatialWeights (sums));
        ch->settings.peq = res.filters;

        report.notes.add (ch->label + ": " + juce::String ((int) res.filters.size()) + " EQ bands, error "
                          + juce::String (res.rmsErrorBeforeDb, 1) + " -> " + juce::String (res.rmsErrorAfterDb, 1) + " dB");
    }

    // Final error of the whole side vs its target (1/6 oct, 40 Hz..12 kHz)
    const auto sum = smoothFractionalOctave (averagedSideSum (side), 6.0);
    double acc = 0.0, ref = 0.0;
    int n = 0;
    for (size_t i = 0; i < freqs.size(); ++i)
        if (freqs[i] >= 200.0 && freqs[i] <= 2000.0)
            { ref += sum.magnitudeDb (i) - targetDb[i]; ++n; }
    ref = n > 0 ? ref / n : 0.0;
    n = 0;
    for (size_t i = 0; i < freqs.size(); ++i)
        if (freqs[i] >= 40.0 && freqs[i] <= 12000.0)
            { acc += std::pow (sum.magnitudeDb (i) - targetDb[i] - ref, 2.0); ++n; }
    rmsOut = n > 0 ? std::sqrt (acc / n) : 0.0;
}

void SystemSolver::equalise()
{
    // The target is relative; anchor it to the level the system actually plays at (200 Hz..2 kHz).
    auto anchoredTarget = [this] (Side side)
    {
        const auto sum = smoothFractionalOctave (averagedSideSum (side), 3.0);
        double acc = 0.0;
        int n = 0;
        for (size_t i = 0; i < freqs.size(); ++i)
            if (freqs[i] >= 200.0 && freqs[i] <= 2000.0)
                { acc += sum.magnitudeDb (i) - cfg.target.levelAt (freqs[i]); ++n; }
        const double offset = n > 0 ? acc / n : 0.0;
        auto t = cfg.target.levels (freqs);
        for (auto& v : t)
            v += offset;
        return t;
    };

    const auto leftTarget = anchoredTarget (Side::left);

    // 1) Subwoofer(s): EQ against the average of both sides in the sub's region.
    for (auto& sub : cfg.channels)
    {
        if (sub.role != DriverRole::subwoofer || data.count (sub.id) == 0)
            continue;

        std::vector<FrequencyResponse> sums;
        for (int p = 0; p < numPositions(); ++p)
            for (auto side : { Side::left, Side::right })
                if (sub.side == Side::mono || sub.side == side)
                    sums.push_back (predictSum (sideChannels (side), p));

        AutoEQ::Settings s;
        s.sampleRate = cfg.caps.sampleRate;
        s.fLow = 20.0;
        s.fHigh = sub.settings.lowPass.enabled ? sub.settings.lowPass.freq : 80.0;
        s.maxBands = cfg.caps.peqBandsPerChannel;
        s.maxBoostDb = std::min (cfg.safety.maxBoostDb, cfg.caps.peqMaxGainDb);
        s.maxCutDb = cfg.caps.peqMinGainDb;
        s.minQ = cfg.caps.peqMinQ;
        s.maxQ = std::min (cfg.caps.peqMaxQ, 8.0);
        s.gainStepDb = cfg.caps.gainStepDb;

        const auto res = AutoEQ::solve (powerAverage (sums), leftTarget, s, AutoEQ::spatialWeights (sums));
        sub.settings.peq = res.filters;
        report.notes.add (sub.label + ": " + juce::String ((int) res.filters.size()) + " EQ bands");
    }

    // 2) Left to target.
    equaliseSide (Side::left, leftTarget, report.leftRmsErrorDb);

    // 3) Right to target, or to the corrected left side (process rule: keeps the centre image).
    std::vector<double> rightTarget;
    if (opts.matchRightToLeft)
    {
        const auto left = smoothFractionalOctave (averagedSideSum (Side::left), 3.0);
        rightTarget = left.magnitudeDb();
    }
    else
    {
        rightTarget = anchoredTarget (Side::right);
    }
    equaliseSide (Side::right, rightTarget, report.rightRmsErrorDb);
}

void SystemSolver::analyseSum()
{
    report.sumDips.clear();

    std::vector<FrequencyResponse> both, lefts, rights;
    std::vector<const OutputChannel*> all;
    for (auto& c : cfg.channels)
        all.push_back (&c);

    for (int p = 0; p < numPositions(); ++p)
    {
        both.push_back (predictSum (all, p));
        lefts.push_back (predictSum (sideChannels (Side::left), p));
        rights.push_back (predictSum (sideChannels (Side::right), p));
    }

    const auto sumAvg = smoothFractionalOctave (powerAverage (both), 6.0);
    const auto lAvg   = smoothFractionalOctave (powerAverage (lefts), 6.0);
    const auto rAvg   = smoothFractionalOctave (powerAverage (rights), 6.0);

    // A dip in L+R that isn't in L or R alone is a phase (delay) problem.
    bool inDip = false;
    double dipF = 0.0, dipDepth = 0.0;
    for (size_t i = 0; i < freqs.size(); ++i)
    {
        if (freqs[i] < 30.0 || freqs[i] > 16000.0)
            continue;
        const double expected = dbFromMag (std::abs (lAvg.values[i]) + std::abs (rAvg.values[i])) - 3.0;
        const double deficit = expected - sumAvg.magnitudeDb (i);
        if (deficit > 6.0)
        {
            if (! inDip || deficit > dipDepth)
            {
                dipF = freqs[i];
                dipDepth = deficit;
            }
            inDip = true;
        }
        else if (inDip)
        {
            report.sumDips.emplace_back (dipF, dipDepth);
            inDip = false;
            dipDepth = 0.0;
        }
    }

    for (auto& [f, d] : report.sumDips)
        report.warnings.add ("L+R cancellation of " + juce::String (d, 1) + " dB at " + juce::String (f, 0)
                             + " Hz" + (d >= 9.0 ? " - check delays (process step 7)." : "."));

    // Left/right tonal match (what keeps the centre image stable).
    double acc = 0.0;
    int n = 0;
    for (size_t i = 0; i < freqs.size(); ++i)
        if (freqs[i] >= 100.0 && freqs[i] <= 10000.0)
            { acc += std::pow (lAvg.magnitudeDb (i) - rAvg.magnitudeDb (i), 2.0); ++n; }
    report.leftRightMatchDb = n > 0 ? std::sqrt (acc / n) : 0.0;
}

SystemSolver::Report SystemSolver::solveAll()
{
    report = {};
    checkPolarity();
    alignTime();
    refineCrossoverPhase();
    matchLevels();
    equalise();
    analyseSum();
    return report;
}
} // namespace fa
