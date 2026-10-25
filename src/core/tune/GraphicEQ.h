#pragma once

#include "SystemConfig.h"

namespace fa
{
/** Graphic EQ support: DSPs (and head units) whose EQ has fixed centre frequencies, a fixed or
    proportional Q and a fixed gain step - AudioControl DM-608/810, Audison bit Ten, Hertz H8,
    JL FiX, Rockford DSR1, Pioneer/Kenwood/JVC 13-band head units, ...

    Representation: a graphic EQ setting is stored as one peaking FilterSpec per centre (in
    centre order, 0 dB bands included) in ChannelSettings::peq, so prediction, the optimiser,
    the judge and every exporter keep working without knowing about graphic EQs.

    Q model (DspCapabilities::graphicQ):
     - graphicQ > 0: constant-Q bands, every band uses that Q at every gain.
     - graphicQ == 0: proportional-Q, the usual behaviour of analog-style graphic EQs (and of the
       head-unit/DSP implementations that emulate them): the bandwidth narrows as the slider
       moves away from 0 dB. Nominal Q is the constant-Q value whose bandwidth equals the band
       spacing (1/3 oct -> 4.32, 2/3 oct -> 2.14, 1 oct -> 1.41); it is reached at +-12 dB and
       the band is 3x broader near 0 dB:
           Q(g) = Qnominal * (1/3 + 2/3 * min (1, |g| / 12 dB))
       so a 1/3-octave band at +-3 dB has Q ~ 2.2, at +-6 dB ~ 2.9 and at +-12 dB 4.32.

    Fitting (solve): all band gains are solved jointly (adjacent bands overlap, so solving them
    one by one would make neighbours fight). Damped Gauss-Newton on the weighted dB error with a
    numerical Jacobian (so the gain-dependent proportional Q is handled exactly), projected onto
    the boost/cut limits, with three regularisers: a boost penalty, a small ridge on every gain
    and a neighbour-difference (smoothness) term that stops the +/- zig-zag a least-squares fit
    of overlapping bands otherwise produces. Peaks are cut with full weight, dips are filled
    with reduced weight and narrow deep dips (cancellations) are ignored, exactly like AutoEQ.
    The continuous solution is then quantised to the gain step and polished by a coordinate
    search on the step grid (+-1 step per band until nothing improves). */
class GraphicEQ
{
public:
    struct Settings
    {
        double fLow  = 20.0;
        double fHigh = 16000.0;
        double maxBoostDb = 6.0;             // further limited by caps.graphicMaxBoostDb
        double maxCutDb   = -12.0;           // further limited by caps.graphicMaxCutDb
        double dipWeight  = 0.4;
        double lfBoostLimitBelowHz = 0.0;
        double lfBoostLimitDb = 3.0;
        double boostPenalty = 0.02;          // per dB^2 of boost
        double smoothness   = 0.01;          // per dB^2 of neighbour difference
    };

    struct Result
    {
        std::vector<double> gainsDb;         // one per centre (caps.graphicCentres())
        std::vector<FilterSpec> filters;     // makeGraphic (caps, gainsDb)
        double rmsErrorBeforeDb = 0.0;
        double rmsErrorAfterDb  = 0.0;
        std::vector<double> freqs, measuredDb, correctedDb, targetDb;
    };

    /** Nominal Q of band `index` from its spacing to the neighbouring centres. */
    static double nominalQ (const DspCapabilities& caps, size_t index);

    /** Q the DSP actually uses for band `index` at `gainDb` (constant or proportional, see above). */
    static double bandQ (const DspCapabilities& caps, size_t index, double gainDb);

    /** One peaking filter per centre (all of them, 0 dB included) with the model's Q. */
    static std::vector<FilterSpec> makeGraphic (const DspCapabilities& caps, const std::vector<double>& gainsDb);

    /** Reads the gains back from a peq list (matching by centre; 0 dB where a centre is missing). */
    static std::vector<double> gainsOf (const DspCapabilities& caps, const std::vector<FilterSpec>& peq);

    /** Rounds to the gain step, never past the [cutLimit, boostLimit] range. */
    static double quantise (const DspCapabilities& caps, double gainDb, double cutLimitDb, double boostLimitDb);

    /** Fits the band gains (see class comment). measured: complex response, smoothed internally;
        targetDb on measured.freqs; weights optional (same size, 0..1). */
    static Result solve (const FrequencyResponse& measured,
                         const std::vector<double>& targetDb,
                         const DspCapabilities& caps,
                         const Settings& settings,
                         const std::vector<double>& weights = {});
};
} // namespace fa
