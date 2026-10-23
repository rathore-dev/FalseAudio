#include "Measurement.h"

namespace fa
{
const std::vector<double>& analysisFrequencies()
{
    static const std::vector<double> f = makeLogFrequencies (10.0, 24000.0, 96);
    return f;
}

double findPeak (const std::vector<float>& ir, int searchStart, int searchEnd)
{
    if (ir.empty())
        return 0.0;

    if (searchEnd < 0 || searchEnd > (int) ir.size())
        searchEnd = (int) ir.size();
    searchStart = juce::jlimit (0, searchEnd - 1, searchStart);

    int best = searchStart;
    float bestAbs = 0.0f;
    for (int i = searchStart; i < searchEnd; ++i)
    {
        const float a = std::abs (ir[(size_t) i]);
        if (a > bestAbs)
        {
            bestAbs = a;
            best = i;
        }
    }

    if (best > 0 && best + 1 < (int) ir.size())
    {
        const double y0 = std::abs (ir[(size_t) best - 1]), y1 = bestAbs, y2 = std::abs (ir[(size_t) best + 1]);
        const double d = y0 - 2.0 * y1 + y2;
        if (std::abs (d) > 1e-12)
            return best + 0.5 * (y0 - y2) / d;
    }
    return best;
}

double findOnset (const std::vector<float>& ir, double peak, double fs, double thresholdDb)
{
    const int p = (int) peak;
    if (p <= 0 || p >= (int) ir.size())
        return peak;

    const double thr = std::abs (ir[(size_t) p]) * magFromDb (thresholdDb);
    const int start = std::max (0, p - (int) (0.02 * fs));

    for (int i = start; i <= p; ++i)
        if (std::abs (ir[(size_t) i]) >= thr)
            return i;

    return peak;
}

void Measurement::analyse (const MicCalibration* cal, const IRWindow& window)
{
    if (ir.empty())
        return;

    const double peak = findPeak (ir, zeroSample);
    peakSample = (int) std::round (peak);
    const double onset = findOnset (ir, peak, sampleRate);
    onsetMs = (onset - zeroSample) / sampleRate * 1000.0;

    const float peakAbs = std::abs (ir[(size_t) peakSample]);
    peakDbFS = dbFromMag (peakAbs);

    // Noise floor estimated from the pre-roll region (before time zero).
    double noise = 0.0;
    int nNoise = 0;
    for (int i = 0; i < std::max (1, zeroSample - (int) (0.005 * sampleRate)); ++i)
    {
        noise += (double) ir[(size_t) i] * ir[(size_t) i];
        ++nNoise;
    }
    snrDb = nNoise > 0 ? dbFromMag (peakAbs) - dbFromPower (noise / nNoise) : 0.0;

    const int ref = (int) std::round (onset);
    response = impulseToResponse (ir, sampleRate, analysisFrequencies(), ref, window);
    if (cal != nullptr)
        response = cal->corrected (response);
}

FrequencyResponse Measurement::absoluteResponse() const
{
    auto r = response;
    r.applyDelay (onsetMs * 0.001);
    return r;
}

ArrivalEstimate estimateArrival (const FrequencyResponse& response, double fLow, double fHigh)
{
    ArrivalEstimate est;
    if (response.isEmpty())
        return est;

    const auto minPhase = minimumPhaseOf (response);

    // Excess phase = measured / minimum phase. For a delayed (possibly inverted) minimum-phase
    // system it is a pure linear phase: e^{j(phi0 - 2*pi*f*tau)}, phi0 = 0 or pi.
    // Instead of unwrapping (fragile with cabin reflections), search the delay tau that makes the
    // de-rotated excess-phase vectors add most coherently. Reflections decorrelate and average out.
    std::vector<double> f;
    std::vector<cplx> unitExcess;
    std::vector<double> w;
    for (size_t i = 0; i < response.size(); ++i)
    {
        if (response.freqs[i] < fLow || response.freqs[i] > fHigh)
            continue;
        const cplx e = response.values[i] / (minPhase.values[i] + cplx (1e-15, 0.0));
        f.push_back (response.freqs[i]);
        unitExcess.push_back (e / std::max (std::abs (e), 1e-15));
        // log-frequency grid already gives equal weight per octave; weight by level so
        // deep (reflection-dominated) notches count less.
        w.push_back (std::abs (response.values[i]));
    }
    if (f.size() < 8)
        return est;

    double wsum = 0.0;
    for (auto x : w)
        wsum += x;

    auto coherentSum = [&] (double tauS)
    {
        cplx acc (0.0, 0.0);
        for (size_t i = 0; i < f.size(); ++i)
            acc += w[i] * unitExcess[i] * std::polar (1.0, 2.0 * pi * f[i] * tauS);
        return acc;
    };

    // Search +/- 10 ms (a sub behind an LR4 low-pass has its onset several ms after the true
    // arrival) with a step that keeps the phase error below ~20 deg at fHigh.
    const double range = 0.010;
    const double step = 1.0 / (18.0 * fHigh);
    double bestTau = 0.0, bestMag = -1.0;
    for (double tau = -range; tau <= range; tau += step)
    {
        const double m = std::abs (coherentSum (tau));
        if (m > bestMag)
        {
            bestMag = m;
            bestTau = tau;
        }
    }

    // Parabolic refinement around the best step.
    const double m0 = std::abs (coherentSum (bestTau - step));
    const double m2 = std::abs (coherentSum (bestTau + step));
    const double d = m0 - 2.0 * bestMag + m2;
    if (std::abs (d) > 1e-12)
        bestTau += juce::jlimit (-0.5, 0.5, 0.5 * (m0 - m2) / d) * step;

    const cplx s = coherentSum (bestTau);
    const double phi0 = std::arg (s);

    est.delayMs = bestTau * 1000.0;
    est.inverted = std::abs (phi0) > pi / 2.0;
    est.confidence = juce::jlimit (0.0, 1.0, std::abs (s) / std::max (wsum, 1e-15))
                   * juce::jlimit (0.0, 1.0, std::abs (std::cos (phi0)) * 1.5);
    return est;
}

juce::var Measurement::toVar (bool includeIR) const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("id", id);
    o->setProperty ("name", name);
    o->setProperty ("channel", channelId);
    o->setProperty ("micPosition", micPosition);
    o->setProperty ("time", timestamp.toMilliseconds());
    o->setProperty ("sampleRate", sampleRate);
    o->setProperty ("zeroSample", zeroSample);

    if (includeIR)
    {
        juce::MemoryBlock mb (ir.data(), ir.size() * sizeof (float));
        o->setProperty ("ir", mb.toBase64Encoding());
    }
    return juce::var (o);
}

Measurement Measurement::fromVar (const juce::var& v)
{
    Measurement m;
    m.id          = v.getProperty ("id", m.id).toString();
    m.name        = v.getProperty ("name", {}).toString();
    m.channelId   = v.getProperty ("channel", {}).toString();
    m.micPosition = v.getProperty ("micPosition", 0);
    m.timestamp   = juce::Time ((juce::int64) v.getProperty ("time", 0));
    m.sampleRate  = v.getProperty ("sampleRate", 48000.0);
    m.zeroSample  = v.getProperty ("zeroSample", 0);

    juce::MemoryBlock mb;
    if (mb.fromBase64Encoding (v.getProperty ("ir", {}).toString()))
    {
        m.ir.resize (mb.getSize() / sizeof (float));
        std::memcpy (m.ir.data(), mb.getData(), m.ir.size() * sizeof (float));
    }
    return m;
}
} // namespace fa
