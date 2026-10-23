#include "Types.h"

namespace fa
{
std::vector<double> makeLogFrequencies (double fLow, double fHigh, int pointsPerOctave)
{
    std::vector<double> f;
    const double octaves = std::log2 (fHigh / fLow);
    const int n = (int) std::ceil (octaves * pointsPerOctave) + 1;
    f.reserve ((size_t) n);

    for (int i = 0; i < n; ++i)
        f.push_back (fLow * std::pow (2.0, (double) i / pointsPerOctave));

    return f;
}

std::vector<double> FrequencyResponse::magnitudeDb() const
{
    std::vector<double> m (values.size());
    for (size_t i = 0; i < values.size(); ++i)
        m[i] = dbFromMag (std::abs (values[i]));
    return m;
}

cplx FrequencyResponse::interpolate (double f) const
{
    if (freqs.empty())
        return { 0.0, 0.0 };

    if (f <= freqs.front()) return values.front();
    if (f >= freqs.back())  return values.back();

    auto it = std::upper_bound (freqs.begin(), freqs.end(), f);
    const size_t i1 = (size_t) std::distance (freqs.begin(), it);
    const size_t i0 = i1 - 1;

    const double t = std::log (f / freqs[i0]) / std::log (freqs[i1] / freqs[i0]);

    const double m0 = std::abs (values[i0]), m1 = std::abs (values[i1]);
    const double p0 = std::arg (values[i0]);
    double p1 = std::arg (values[i1]);

    // take the shortest phase path between adjacent points
    while (p1 - p0 >  pi) p1 -= 2.0 * pi;
    while (p1 - p0 < -pi) p1 += 2.0 * pi;

    const double m = std::exp (std::log (std::max (m0, 1e-15)) * (1.0 - t) + std::log (std::max (m1, 1e-15)) * t);
    return std::polar (m, p0 + (p1 - p0) * t);
}

FrequencyResponse FrequencyResponse::resampled (const std::vector<double>& newFreqs) const
{
    FrequencyResponse r;
    r.freqs = newFreqs;
    r.values.resize (newFreqs.size());
    for (size_t i = 0; i < newFreqs.size(); ++i)
        r.values[i] = interpolate (newFreqs[i]);
    return r;
}

FrequencyResponse& FrequencyResponse::operator*= (const FrequencyResponse& other)
{
    jassert (other.size() == size());
    for (size_t i = 0; i < values.size(); ++i)
        values[i] *= other.values[i];
    return *this;
}

FrequencyResponse& FrequencyResponse::operator+= (const FrequencyResponse& other)
{
    jassert (other.size() == size());
    for (size_t i = 0; i < values.size(); ++i)
        values[i] += other.values[i];
    return *this;
}

FrequencyResponse& FrequencyResponse::scale (cplx factor)
{
    for (auto& v : values)
        v *= factor;
    return *this;
}

FrequencyResponse& FrequencyResponse::applyDelay (double seconds)
{
    for (size_t i = 0; i < values.size(); ++i)
        values[i] *= std::polar (1.0, -2.0 * pi * freqs[i] * seconds);
    return *this;
}

FrequencyResponse FrequencyResponse::unity (const std::vector<double>& f)
{
    FrequencyResponse r;
    r.freqs = f;
    r.values.assign (f.size(), cplx (1.0, 0.0));
    return r;
}

FrequencyResponse operator* (FrequencyResponse a, const FrequencyResponse& b) { a *= b; return a; }
FrequencyResponse operator+ (FrequencyResponse a, const FrequencyResponse& b) { a += b; return a; }

double averageLevelDb (const FrequencyResponse& r, double f1, double f2)
{
    double sum = 0.0;
    int n = 0;
    for (size_t i = 0; i < r.size(); ++i)
    {
        if (r.freqs[i] >= f1 && r.freqs[i] <= f2)
        {
            sum += r.magnitudeDb (i);
            ++n;
        }
    }
    return n > 0 ? sum / n : -120.0;
}

FrequencyResponse powerAverage (const std::vector<FrequencyResponse>& responses)
{
    if (responses.empty())
        return {};

    FrequencyResponse out = responses.front();
    for (size_t i = 0; i < out.size(); ++i)
    {
        double p = 0.0;
        for (auto& r : responses)
            p += std::norm (r.values[i]);
        out.values[i] = std::polar (std::sqrt (p / (double) responses.size()), std::arg (responses.front().values[i]));
    }
    return out;
}
} // namespace fa
