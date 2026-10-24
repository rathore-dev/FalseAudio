#pragma once

#include "../SystemSolver.h"

namespace fa::advanced::detail
{
/** Fast re-implementation of SystemSolver::predict() for the optimiser and the judge.

    The measured response of every driver (with its onset delay re-applied and the DSP settings
    that were active during the measurement divided out, exactly as SystemSolver::predict does)
    is cached once per mic position on a 48 points/octave grid (every 2nd point of
    analysisFrequencies(), 20 Hz..20 kHz). A channel's predicted response is then just
        base[p](f) * gain * polarity * HP(f) * LP(f) * PEQ(f) * e^{-j w delay}
    so a cost evaluation costs a few thousand complex multiplies. */
class Model
{
public:
    Model (const SystemConfig& cfg, const DriverDataMap& data);

    struct Channel
    {
        size_t cfgIndex = 0;          // index into SystemConfig::channels
        Side side = Side::left;
        DriverRole role = DriverRole::midbass;
        std::vector<std::vector<cplx>> base;   // [position][freq]
    };

    const std::vector<double>& freqs() const noexcept         { return f; }
    size_t size() const noexcept                              { return f.size(); }
    int numPositions() const noexcept                         { return positions; }
    const std::vector<Channel>& channels() const noexcept     { return chans; }
    const std::vector<double>& targetDb() const noexcept      { return target; }
    double sampleRate() const noexcept                        { return fs; }

    /** HP * LP * PEQ of a channel's settings on the model grid (gain/polarity/delay excluded). */
    std::vector<cplx> filterPart (const ChannelSettings& s) const;

    /** contributions[k][p] = predicted complex response of model channel k at position p. */
    void contribution (size_t k, const ChannelSettings& s, const std::vector<cplx>& filt,
                       std::vector<std::vector<cplx>>& outPerPosition) const;

    /** Everything the cost function and the judge need, per mic position. */
    struct Field
    {
        std::vector<std::vector<cplx>> frontL, frontR, mono;      // [pos][freq] complex sums
        std::vector<std::vector<double>> magL, magR, magMono;     // [pos][freq] sum of |contribution|
        cplx left  (int p, size_t i) const { return frontL[(size_t) p][i] + mono[(size_t) p][i]; }
        cplx right (int p, size_t i) const { return frontR[(size_t) p][i] + mono[(size_t) p][i]; }
        cplx both  (int p, size_t i) const { return frontL[(size_t) p][i] + frontR[(size_t) p][i] + mono[(size_t) p][i]; }
    };

    /** contributions: [model channel][position][freq]. */
    Field sum (const std::vector<std::vector<std::vector<cplx>>>& contributions) const;
    Field sum (const std::vector<const std::vector<std::vector<cplx>>*>& contributions) const;

    /** Power smoothing over one ERB (clamped to 1/12..1/3 octave). Input and output are powers. */
    std::vector<double> erbSmooth (const std::vector<double>& power) const;

    /** Fractional-octave power smoothing (fraction = 6 -> 1/6 octave). */
    std::vector<double> octaveSmooth (const std::vector<double>& power, double fraction) const;

    /** Position-averaged power of the left (0) / right (1) side, ERB smoothed, in dB. */
    std::vector<double> sideDb (const Field& field, int sideIndex, bool erb = true) const;

    /** Per-position ERB-smoothed dB of one side: [pos][freq]. */
    std::vector<std::vector<double>> sideDbPerPosition (const Field& field, int sideIndex) const;

    /** Perceptual frequency weight: sqrt of ERB density (f / ERB(f)), normalised to mean 1. */
    const std::vector<double>& erbWeights() const noexcept    { return wErb; }

    static double erbHz (double freq) { return 24.7 * (4.37 * freq / 1000.0 + 1.0); }

private:
    std::vector<double> f, target, wErb;
    std::vector<std::pair<size_t, size_t>> erbWin;
    std::vector<Channel> chans;
    int positions = 1;
    double fs = 48000.0;
};

/** Spatial-consistency weight per frequency (1 = positions agree, -> 0 when they spread). */
std::vector<double> spatialWeights (const Model& model, const Model::Field& field, int sideIndex);

/** Level offset that anchors the target to the played level (mean over 200 Hz..2 kHz of both sides). */
double anchorOffset (const Model& model, const std::vector<double>& leftDb, const std::vector<double>& rightDb);
} // namespace fa::advanced::detail
