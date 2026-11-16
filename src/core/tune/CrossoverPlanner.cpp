#include "CrossoverPlanner.h"

namespace fa
{
namespace
{
    int roleRank (const OutputChannel& c)
    {
        // Larger midbass drivers (e.g. an 8" kick) sit below a 6.5" midbass in a 4-way front.
        switch (c.role)
        {
            case DriverRole::subwoofer: return 0;
            case DriverRole::midbass:   return 20 - (int) std::round (c.driver.sizeInches > 0 ? c.driver.sizeInches / 2.0 : 3.0);
            case DriverRole::fullrange: return 25;
            case DriverRole::midrange:  return 30;
            case DriverRole::tweeter:   return 40;
        }
        return 0;
    }

    /** Front-stage drivers of one side, ordered from lowest to highest frequency. */
    std::vector<const OutputChannel*> orderedSide (const SystemConfig& cfg, Side side)
    {
        std::vector<const OutputChannel*> v;
        for (auto& c : cfg.channels)
            if (c.side == side && c.role != DriverRole::subwoofer)
                v.push_back (&c);
        std::stable_sort (v.begin(), v.end(), [] (auto* a, auto* b) { return roleRank (*a) < roleRank (*b); });
        return v;
    }

    double preferredCrossover (const OutputChannel& lower, const OutputChannel& upper)
    {
        if (upper.role == DriverRole::tweeter)
            return lower.role == DriverRole::midrange ? 3000.0 : 2500.0;
        if (upper.role == DriverRole::midrange)
            return 350.0;
        if (lower.role == DriverRole::midbass && upper.role == DriverRole::midbass)
            return 200.0;
        return 300.0;
    }

    /** Passband level (median-ish average) of a response between f1 and f2. */
    double passbandLevel (const FrequencyResponse& r, double f1, double f2)
    {
        std::vector<double> v;
        for (size_t i = 0; i < r.size(); ++i)
            if (r.freqs[i] >= f1 && r.freqs[i] <= f2)
                v.push_back (r.magnitudeDb (i));
        if (v.empty())
            return -120.0;
        std::nth_element (v.begin(), v.begin() + (long) v.size() / 2, v.end());
        return v[v.size() / 2];
    }
}

double CrossoverPlanner::dispersionLimitHz (const OutputChannel& ch)
{
    // ka = 2*pi*f*a/c ; beaming becomes significant around ka ~ 1.5
    const double radius = ch.driver.effectiveDiameterM (ch.role) / 2.0;
    return 1.5 * speedOfSound / (2.0 * pi * radius);
}

double CrossoverPlanner::safeHighPassHz (const OutputChannel& ch, const SafetyLimits& s)
{
    const double fs = ch.driver.fsHz;
    switch (ch.role)
    {
        case DriverRole::tweeter:   return fs > 0 ? fs * s.tweeterMinHpFactor : s.minTweeterHpHz;
        case DriverRole::midrange:  return std::max (s.minMidrangeHpHz, fs * 1.2);
        case DriverRole::midbass:   return std::max (s.minMidbassHpHz, fs);
        case DriverRole::fullrange: return std::max (s.minMidrangeHpHz * 0.4, fs * 1.2);
        case DriverRole::subwoofer: return 18.0;
    }
    return 20.0;
}

double CrossoverPlanner::snapFrequency (double f, const DspCapabilities& caps)
{
    if (caps.freqStepHz > 0.0)
        return std::max (caps.freqStepHz, std::round (f / caps.freqStepHz) * caps.freqStepHz);
    if (caps.freqStepHz < 0.0)
    {
        // 1/24 octave grid referenced to 1 kHz
        const double n = std::round (std::log2 (f / 1000.0) * 24.0);
        return 1000.0 * std::pow (2.0, n / 24.0);
    }
    return std::round (f);
}

std::vector<CrossoverPlanner::Decision> CrossoverPlanner::planDefaults (const SystemConfig& cfg)
{
    std::vector<Decision> out;
    const bool hasSub = cfg.hasSubwoofer();
    double lowestFrontHp = 1.0e9;

    for (auto side : { Side::left, Side::right })
    {
        auto chain = orderedSide (cfg, side);
        for (size_t i = 0; i < chain.size(); ++i)
        {
            auto& ch = *chain[i];
            Decision d;
            d.channelId = ch.id;

            // High pass
            if (i == 0)
            {
                d.hpHz = hasSub ? 80.0 : std::max (safeHighPassHz (ch, cfg.safety), 35.0);
                if (ch.role == DriverRole::midbass && ch.driver.sizeInches >= 8.0 && hasSub)
                    d.hpHz = 60.0;
                d.hpHz = std::max (d.hpHz, safeHighPassHz (ch, cfg.safety));
                d.reasons.add (hasSub ? "Lowest front way: 80 Hz HP lets the sub carry deep bass (process rule)."
                                      : "No sub: HP set just above the driver's safe limit.");
            }
            else
            {
                auto& lower = *chain[i - 1];
                const double floorHz = safeHighPassHz (ch, cfg.safety);
                const double ceilHz  = dispersionLimitHz (lower);
                double fc = preferredCrossover (lower, ch);

                if (fc < floorHz)
                {
                    fc = floorHz;
                    d.reasons.add ("Raised to protect " + ch.label + " (>= " + juce::String (juce::roundToInt (floorHz)) + " Hz).");
                }
                if (fc > ceilHz && floorHz <= ceilHz)
                {
                    fc = std::max (floorHz, ceilHz);
                    d.reasons.add ("Lowered so " + lower.label + " still disperses widely (beaming above ~"
                                   + juce::String (juce::roundToInt (ceilHz)) + " Hz).");
                }
                if (floorHz > ceilHz)
                    d.reasons.add ("Warning: " + lower.label + " beams below the safe HP of " + ch.label
                                   + ". Expect an off-axis dip near the crossover; a midrange or larger tweeter would help.");

                d.hpHz = fc;
            }

            // Low pass (= HP of the next way)
            if (i + 1 < chain.size())
            {
                auto& upper = *chain[i + 1];
                const double floorHz = safeHighPassHz (upper, cfg.safety);
                const double ceilHz  = dispersionLimitHz (ch);
                double fc = preferredCrossover (ch, upper);
                fc = std::max (fc, floorHz);
                if (floorHz <= ceilHz)
                    fc = std::min (fc, std::max (floorHz, ceilHz));
                d.lpHz = fc;
            }

            if (i == 0)
                lowestFrontHp = std::min (lowestFrontHp, d.hpHz);

            out.push_back (d);
        }
    }

    // Subwoofer(s): LP half an octave below the front stage HP.
    for (auto& c : cfg.channels)
    {
        if (c.role != DriverRole::subwoofer)
            continue;
        Decision d;
        d.channelId = c.id;
        d.hpHz = 20.0;
        d.lpHz = (lowestFrontHp < 1.0e8 ? lowestFrontHp : 80.0) / std::sqrt (2.0);
        d.reasons.add ("Sub LP half an octave below the midbass HP so the bass boost doesn't drag the "
                       "crossover upward (keeps bass up front).");
        out.push_back (d);
    }

    return out;
}

std::vector<CrossoverPlanner::Decision> CrossoverPlanner::planFromMeasurements (
    const SystemConfig& cfg, const std::map<juce::String, FrequencyResponse>& responses)
{
    auto decisions = planDefaults (cfg);

    auto findDecision = [&] (const juce::String& id) -> Decision*
    {
        for (auto& d : decisions)
            if (d.channelId == id)
                return &d;
        return nullptr;
    };

    for (auto side : { Side::left, Side::right })
    {
        auto chain = orderedSide (cfg, side);
        for (size_t i = 0; i + 1 < chain.size(); ++i)
        {
            auto& lower = *chain[i];
            auto& upper = *chain[i + 1];
            auto itL = responses.find (lower.id);
            auto itU = responses.find (upper.id);
            auto* dL = findDecision (lower.id);
            auto* dU = findDecision (upper.id);
            if (dL == nullptr || dU == nullptr)
                continue;

            double fc = dL->lpHz;

            // Lower driver: find where its natural top end falls 6 dB below its passband or
            // rises into a breakup peak. We need clean response an octave above fc.
            if (itL != responses.end())
            {
                const auto sm = smoothFractionalOctave (itL->second, 6.0);
                const double ref = passbandLevel (sm, std::max (dL->hpHz * 1.5, 60.0), fc);
                double limit = 20000.0;
                for (size_t k = 0; k < sm.size(); ++k)
                {
                    const double f = sm.freqs[k];
                    if (f < fc)
                        continue;
                    const double dev = sm.magnitudeDb (k) - ref;
                    if (dev < -6.0 || dev > 6.0)
                    {
                        limit = f;
                        break;
                    }
                }
                if (limit / 2.0 < fc)
                {
                    const double newFc = std::max (limit / 2.0, safeHighPassHz (upper, cfg.safety));
                    if (newFc < fc)
                    {
                        dL->reasons.add (lower.label + " is only clean to " + juce::String (juce::roundToInt (limit))
                                         + " Hz; crossover moved down to keep an octave of margin.");
                        fc = newFc;
                    }
                }
            }

            // Upper driver: its usable low end (after removing the protective HP we measured
            // with) must sit an octave below fc.
            if (itU != responses.end())
            {
                const auto protect = upper.settings.highPass.response (itU->second.freqs, cfg.caps.sampleRate);
                FrequencyResponse raw = itU->second;
                for (size_t k = 0; k < raw.size(); ++k)
                {
                    const double pm = std::abs (protect.values[k]);
                    raw.values[k] = pm > magFromDb (-24.0) ? raw.values[k] / pm : cplx (0.0, 0.0);
                }
                const auto sm = smoothFractionalOctave (raw, 6.0);
                const double ref = passbandLevel (sm, fc * 1.5, std::min (fc * 6.0, 16000.0));
                double lowLimit = 0.0;
                for (size_t k = sm.size(); k-- > 0;)
                {
                    const double f = sm.freqs[k];
                    if (f > fc)
                        continue;
                    if (std::abs (sm.values[k]) == 0.0 || sm.magnitudeDb (k) - ref < -6.0)
                    {
                        lowLimit = f;
                        break;
                    }
                }
                if (lowLimit * 2.0 > fc)
                {
                    const double newFc = std::min (lowLimit * 2.0, std::max (dispersionLimitHz (lower) * 1.5, fc));
                    if (newFc > fc)
                    {
                        dU->reasons.add (upper.label + " loses output below " + juce::String (juce::roundToInt (lowLimit))
                                         + " Hz; crossover moved up for an octave of margin.");
                        fc = newFc;
                    }
                }
            }

            dL->lpHz = fc;
            dU->hpHz = fc;
        }
    }

    // Keep the sub relationship after any midbass change.
    double lowestFrontHp = 1.0e9;
    for (auto& d : decisions)
        if (auto* c = cfg.find (d.channelId); c != nullptr && c->role != DriverRole::subwoofer && d.hpHz > 0)
            if (c->role == DriverRole::midbass || c->role == DriverRole::fullrange)
                lowestFrontHp = std::min (lowestFrontHp, d.hpHz);
    for (auto& d : decisions)
        if (auto* c = cfg.find (d.channelId); c != nullptr && c->role == DriverRole::subwoofer && lowestFrontHp < 1.0e8)
            d.lpHz = lowestFrontHp / std::sqrt (2.0);

    return decisions;
}

void CrossoverPlanner::apply (SystemConfig& cfg, const std::vector<Decision>& decisions)
{
    const auto alignment = cfg.caps.hasLinkwitzRiley ? Alignment::linkwitzRiley : Alignment::butterworth;
    for (auto& d : decisions)
    {
        auto* ch = cfg.find (d.channelId);
        if (ch == nullptr)
            continue;

        auto& s = ch->settings;
        if (d.hpHz > 0.0)
        {
            s.highPass = FilterSpec::crossover (FilterType::highPass, snapFrequency (d.hpHz, cfg.caps), 24, alignment);
            if (ch->role == DriverRole::subwoofer)
                s.highPass = FilterSpec::crossover (FilterType::highPass, snapFrequency (d.hpHz, cfg.caps), 12, Alignment::butterworth);
        }
        if (d.lpHz > 0.0)
            s.lowPass = FilterSpec::crossover (FilterType::lowPass, snapFrequency (d.lpHz, cfg.caps), 24, alignment);
        else
            s.lowPass.enabled = false;

        if (! ch->hasHighPass())
            s.highPass.enabled = ch->role == DriverRole::subwoofer; // subsonic only
    }
}
} // namespace fa
