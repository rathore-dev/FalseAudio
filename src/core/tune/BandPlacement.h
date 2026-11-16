#pragma once

#include <cmath>
#include <vector>

namespace fa
{
/** Frequency for a newly added EQ band: the first band goes to `preferredHz`; every later one goes to the
    1/3-octave grid point (40 Hz..16 kHz) farthest (in octaves) from all existing bands, so repeated
    "Add band" clicks never stack bands on top of each other. Ties prefer the point nearest `preferredHz`. */
inline double nextBandFrequency (const std::vector<double>& existingHz, double preferredHz = 1000.0)
{
    if (existingHz.empty())
        return preferredHz;
    double best = preferredHz, bestDist = -1.0, bestTie = 1.0e9;
    for (int i = 0; i <= 28; ++i)
    {
        const double f = 40.0 * std::pow (2.0, i / 3.0);   // 40 Hz .. ~20 kHz
        if (f > 16000.0) break;
        double d = 1.0e9;
        for (double e : existingHz)
            d = std::min (d, std::abs (std::log2 (f / std::max (1.0, e))));
        const double tie = std::abs (std::log2 (f / preferredHz));
        if (d > bestDist + 1.0e-9 || (std::abs (d - bestDist) <= 1.0e-9 && tie < bestTie))
        {
            bestDist = d; best = f; bestTie = tie;
        }
    }
    return best;
}
} // namespace fa
