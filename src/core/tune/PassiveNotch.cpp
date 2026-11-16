#include "PassiveNotch.h"
#include "../dsp/Spectrum.h"

namespace fa::passive
{
std::vector<PassiveNotch> findNotches (const FrequencyResponse& combined, double crossoverHz, double minDepthDb)
{
    std::vector<PassiveNotch> out;
    if (combined.isEmpty())
        return out;

    const double lo = crossoverHz > 0.0 ? crossoverHz * 0.5 : 700.0;
    const double hi = crossoverHz > 0.0 ? crossoverHz * 2.0 : 7000.0;

    const auto fine = smoothFractionalOctave (combined, 6.0);
    const size_t n = fine.size();

    auto refLevel = [&] (size_t i)
    {
        // Median of the surrounding +-1 octave: a notch narrower than an octave barely moves it.
        std::vector<double> v;
        for (size_t k = 0; k < n; ++k)
            if (fine.freqs[k] >= fine.freqs[i] / 2.0 && fine.freqs[k] <= fine.freqs[i] * 2.0)
                v.push_back (fine.magnitudeDb (k));
        if (v.empty())
            return fine.magnitudeDb (i);
        std::nth_element (v.begin(), v.begin() + (long) v.size() / 2, v.end());
        return v[v.size() / 2];
    };

    size_t i = 0;
    while (i < n)
    {
        if (fine.freqs[i] < lo || fine.freqs[i] > hi)
        {
            ++i;
            continue;
        }
        const double depth = refLevel (i) - fine.magnitudeDb (i);
        if (depth < minDepthDb)
        {
            ++i;
            continue;
        }

        // Walk the dip, keep its deepest point.
        size_t best = i, end = i;
        double bestDepth = depth;
        while (end < n && fine.freqs[end] <= hi && refLevel (end) - fine.magnitudeDb (end) >= minDepthDb * 0.5)
        {
            const double d = refLevel (end) - fine.magnitudeDb (end);
            if (d > bestDepth)
            {
                bestDepth = d;
                best = end;
            }
            ++end;
        }
        PassiveNotch nt;
        nt.freqHz = fine.freqs[best];
        nt.depthDb = bestDepth;
        nt.loHz = fine.freqs[i] / 1.15;
        nt.hiHz = fine.freqs[std::min (end, n - 1)] * 1.15;
        out.push_back (nt);
        i = std::max (end, i + 1);
    }
    return out;
}

std::map<juce::String, std::vector<PassiveNotch>> findAll (const SystemConfig& cfg,
                                                           const std::map<juce::String, FrequencyResponse>& responses)
{
    std::map<juce::String, std::vector<PassiveNotch>> out;
    for (auto& c : cfg.channels)
    {
        if (! c.isPassiveSet())
            continue;
        auto it = responses.find (c.id);
        if (it == responses.end())
            continue;
        auto notches = findNotches (it->second, c.passive.crossoverHz);
        if (! notches.empty())
            out[c.id] = std::move (notches);
    }
    return out;
}

std::vector<std::pair<double, double>> ranges (const std::vector<PassiveNotch>& notches)
{
    std::vector<std::pair<double, double>> r;
    for (auto& n : notches)
        r.emplace_back (n.loHz, n.hiHz);
    return r;
}

bool contains (const std::vector<PassiveNotch>& notches, double f)
{
    for (auto& n : notches)
        if (f >= n.loHz && f <= n.hiHz)
            return true;
    return false;
}

juce::String describe (const OutputChannel& ch, const PassiveNotch& n)
{
    return "Insight: " + ch.label + " has a " + juce::String (n.depthDb, 0) + " dB notch near "
         + juce::String (juce::roundToInt (n.freqHz)) + " Hz from its passive crossover and the woofer/tweeter offset. "
         "The DSP cannot change either, so it was not boosted. Fixes: re-aim or move the tweeter, check the "
         "tweeter polarity/wiring, or move the mic.";
}
} // namespace fa::passive
