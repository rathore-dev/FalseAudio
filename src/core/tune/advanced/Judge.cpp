#include "Optimizer.h"
#include "PredictionModel.h"

namespace fa::advanced
{
/*  Simulated sound-quality judge.

    Competition sheets (IASCA SQC, EMMA, MECA SQ, USACi) all score the same handful of things
    from the driver's seat: tonal accuracy, bass (extension, quality, "up front"), sound stage
    (width/height/depth), centre image focus, imaging consistency across the spectrum,
    linearity/smoothness and absence of noise. Every category here is computed from the
    predicted responses at all mic positions (power-averaged, ERB smoothed, target anchored at
    200 Hz..2 kHz like SystemSolver) and starts at 100 with penalties that grow linearly with
    the defect above a "transparent" threshold (clamped 0..100):

    tonal (25%)       E = mean over L/R of RMS(side - target) 30 Hz..16 kHz (ERB-weighted);
                      100 - 14 (E - 0.5)
    bass (15%)        extension: mean shortfall below target-1 dB in 25..40 Hz, 6/dB;
                      up front: coherent-sum loss of sub vs each front side around the
                      sub/front crossover (or L+R midbass, 40..150 Hz, without a sub), 10/dB;
                      sub level more than 3 dB above target (bass pulled to the rear), 3/dB
    stage (10%)       HF extension shortfall 8..14 kHz beyond 2 dB, 5/dB; mono sub leaking above
                      150 Hz (more than -20 dB re side), 1.5/dB; broadband L/R level offset
                      500 Hz..8 kHz beyond 0.5 dB (stage leans), 8/dB
    centre (20%)      L/R magnitude match RMS 200 Hz..4 kHz beyond 0.3 dB, 12/dB;
                      L+R coherence loss 200 Hz..4 kHz beyond 1 dB, 8/dB
    imaging (10%)     1/3-oct bands 250 Hz..6.3 kHz: std of L-R level beyond 0.3 dB, 10/dB;
                      inter-channel time (arg of the position-averaged cross spectrum) 250 Hz..2 kHz:
                      std across bands beyond 0.03 ms, 150/ms; mean offset beyond 0.05 ms, 50/ms
    linearity (10%)   ripple RMS(1/6 oct - 1 oct) 60 Hz..12 kHz beyond 0.7 dB, 12/dB; worst peak
                      above target beyond 3 dB, 4/dB; worst crossover summation hole beyond 3 dB, 3/dB
    noise (10%)       proxy from gain structure: any channel's gain + largest EQ boost beyond
                      +6 dB, 3/dB; tweeter gain above 0 dB (hiss), 4/dB; total EQ boost beyond
                      25 dB, 0.4/dB
*/
JudgeScore judge (const SystemConfig& config, const DriverDataMap& data)
{
    JudgeScore js;
    detail::Model model (config, data);
    if (model.channels().empty())
    {
        js.comments.add ("Nothing measured - cannot judge.");
        return js;
    }

    const auto& f = model.freqs();
    const auto& t = model.targetDb();
    const auto& we = model.erbWeights();
    const auto N = model.size();
    const int P = model.numPositions();

    std::vector<std::vector<std::vector<cplx>>> contrib (model.channels().size());
    for (size_t k = 0; k < model.channels().size(); ++k)
    {
        const auto& s = config.channels[model.channels()[k].cfgIndex].settings;
        model.contribution (k, s, model.filterPart (s), contrib[k]);
    }
    const auto fl = model.sum (contrib);
    const auto L = model.sideDb (fl, 0), R = model.sideDb (fl, 1);
    const double off = detail::anchorOffset (model, L, R);

    auto clampScore = [] (double v) { return juce::jlimit (0.0, 100.0, v); };
    auto over = [] (double v, double thr) { return std::max (0.0, v - thr); };

    auto weightedRms = [&] (auto fn, double f1, double f2)
    {
        double acc = 0.0, w = 0.0;
        for (size_t i = 0; i < N; ++i)
            if (f[i] >= f1 && f[i] <= f2)
            {
                const double e = fn (i);
                acc += we[i] * e * e;
                w += we[i];
            }
        return w > 0.0 ? std::sqrt (acc / w) : 0.0;
    };
    auto mean = [&] (auto fn, double f1, double f2)
    {
        double acc = 0.0;
        int n = 0;
        for (size_t i = 0; i < N; ++i)
            if (f[i] >= f1 && f[i] <= f2)
            {
                acc += fn (i);
                ++n;
            }
        return n > 0 ? acc / n : 0.0;
    };

    /** Coherent-summation loss in dB (ERB smoothed, averaged over [f1, f2]). */
    auto coherenceLoss = [&] (auto idealMag, auto actual, double f1, double f2, double* worst = nullptr)
    {
        std::vector<double> id (N, 0.0), ac (N, 0.0);
        for (int p = 0; p < P; ++p)
            for (size_t i = 0; i < N; ++i)
            {
                const double m = idealMag (p, i);
                id[i] += m * m;
                ac[i] += std::norm (actual (p, i));
            }
        id = model.erbSmooth (id);
        ac = model.erbSmooth (ac);
        double acc = 0.0;
        int n = 0;
        for (size_t i = 0; i < N; ++i)
            if (f[i] >= f1 && f[i] <= f2 && id[i] > 1e-20)
            {
                const double d = std::max (0.0, dbFromPower (id[i]) - dbFromPower (ac[i]));
                acc += d;
                ++n;
                if (worst != nullptr)
                    *worst = std::max (*worst, d);
            }
        return n > 0 ? acc / n : 0.0;
    };

    //==========================================================================
    // Tonal balance
    const double eL = weightedRms ([&] (size_t i) { return L[i] - t[i] - off; }, 30.0, 16000.0);
    const double eR = weightedRms ([&] (size_t i) { return R[i] - t[i] - off; }, 30.0, 16000.0);
    const double tonalErr = 0.5 * (eL + eR);
    const double tonal = clampScore (100.0 - 14.0 * over (tonalErr, 0.5));
    if (tonal < 85.0)
        js.comments.add ("Tonal balance: " + juce::String (tonalErr, 1) + " dB RMS from the target (L "
                         + juce::String (eL, 1) + ", R " + juce::String (eR, 1) + ").");

    //==========================================================================
    // Bass: extension, integration (up front), level
    const auto& chans = model.channels();
    double fSub = 0.0, fFront = 1.0e9;
    for (auto& mc : chans)
    {
        const auto& s = config.channels[mc.cfgIndex].settings;
        if (mc.role == DriverRole::subwoofer)
            fSub = std::max (fSub, s.lowPass.enabled ? s.lowPass.freq : 80.0);
        else if (s.highPass.enabled)
            fFront = std::min (fFront, s.highPass.freq);
    }
    const bool hasSub = fSub > 0.0;

    const double extension = 0.5 * (mean ([&] (size_t i) { return over (t[i] + off - L[i], 1.0); }, 25.0, 40.0)
                                   + mean ([&] (size_t i) { return over (t[i] + off - R[i], 1.0); }, 25.0, 40.0));
    double integration = 0.0;
    if (hasSub)
    {
        const double f1 = std::min (fSub, fFront) / 1.4, f2 = std::max (fSub, fFront) * 1.4;
        integration = 0.5 * (coherenceLoss ([&] (int p, size_t i) { return std::abs (fl.mono[(size_t) p][i]) + std::abs (fl.frontL[(size_t) p][i]); },
                                            [&] (int p, size_t i) { return fl.left (p, i); }, f1, f2)
                           + coherenceLoss ([&] (int p, size_t i) { return std::abs (fl.mono[(size_t) p][i]) + std::abs (fl.frontR[(size_t) p][i]); },
                                            [&] (int p, size_t i) { return fl.right (p, i); }, f1, f2));
    }
    else
    {
        integration = coherenceLoss ([&] (int p, size_t i) { return std::abs (fl.frontL[(size_t) p][i]) + std::abs (fl.frontR[(size_t) p][i]); },
                                     [&] (int p, size_t i) { return fl.both (p, i); }, 40.0, 150.0);
    }
    const double subExcess = hasSub ? 0.5 * (mean ([&] (size_t i) { return L[i] - t[i] - off; }, 30.0, fSub)
                                           + mean ([&] (size_t i) { return R[i] - t[i] - off; }, 30.0, fSub)) : 0.0;
    const double bass = clampScore (100.0 - 6.0 * extension - 10.0 * integration - 3.0 * over (subExcess, 3.0));
    if (bass < 85.0)
        js.comments.add ("Bass: extension shortfall " + juce::String (extension, 1) + " dB, sub/front summing loss "
                         + juce::String (integration, 1) + " dB" + (integration > 1.0 ? " (bass will not sound up front)" : "") + ".");

    //==========================================================================
    // Stage width / height proxies
    const double hfShort = 0.5 * (mean ([&] (size_t i) { return over (t[i] + off - L[i], 2.0); }, 8000.0, 14000.0)
                                + mean ([&] (size_t i) { return over (t[i] + off - R[i], 2.0); }, 8000.0, 14000.0));
    double leak = -100.0;
    if (hasSub)
    {
        std::vector<double> m (N, 0.0), s (N, 0.0);
        for (int p = 0; p < P; ++p)
            for (size_t i = 0; i < N; ++i)
            {
                m[i] += std::norm (fl.mono[(size_t) p][i]);
                s[i] += 0.5 * (std::norm (fl.left (p, i)) + std::norm (fl.right (p, i)));
            }
        leak = mean ([&] (size_t i) { return dbFromPower (m[i]) - dbFromPower (s[i]); }, 150.0, 400.0);
    }
    const double lean = std::abs (mean ([&] (size_t i) { return L[i] - R[i]; }, 500.0, 8000.0));
    const double stage = clampScore (100.0 - 5.0 * hfShort - std::min (30.0, 1.5 * over (leak, -20.0)) - 8.0 * over (lean, 0.5));
    if (stage < 85.0)
        js.comments.add ("Stage: HF shortfall " + juce::String (hfShort, 1) + " dB, sub leakage "
                         + juce::String (leak, 0) + " dB re front, L/R lean " + juce::String (lean, 1) + " dB.");

    //==========================================================================
    // Centre focus
    const double match = weightedRms ([&] (size_t i) { return L[i] - R[i]; }, 200.0, 4000.0);
    const double lrLoss = coherenceLoss ([&] (int p, size_t i)
                                         {
                                             return std::abs (fl.frontL[(size_t) p][i]) + std::abs (fl.frontR[(size_t) p][i])
                                                  + std::abs (fl.mono[(size_t) p][i]);
                                         },
                                         [&] (int p, size_t i) { return fl.both (p, i); }, 200.0, 4000.0);
    const double centre = clampScore (100.0 - 12.0 * over (match, 0.3) - 8.0 * over (lrLoss, 1.0));
    if (centre < 85.0)
        js.comments.add ("Centre image: L/R mismatch " + juce::String (match, 2) + " dB RMS, L+R summing loss "
                         + juce::String (lrLoss, 1) + " dB in 200 Hz-4 kHz.");

    //==========================================================================
    // Imaging consistency across bands
    std::vector<double> levelDiff, timeDiff;
    for (double fc = 250.0; fc <= 6400.0; fc *= std::pow (2.0, 1.0 / 3.0))
    {
        const double lo = fc / std::pow (2.0, 1.0 / 6.0), hi = fc * std::pow (2.0, 1.0 / 6.0);
        levelDiff.push_back (mean ([&] (size_t i) { return L[i] - R[i]; }, lo, hi));
        if (fc <= 2000.0)
        {
            cplx cross (0.0, 0.0);
            for (int p = 0; p < P; ++p)
                for (size_t i = 0; i < N; ++i)
                    if (f[i] >= lo && f[i] <= hi)
                    {
                        // Normalise by the centre frequency phase rate so bands combine coherently.
                        cross += fl.left (p, i) * std::conj (fl.right (p, i)) / (std::abs (fl.left (p, i)) * std::abs (fl.right (p, i)) + 1e-30);
                    }
            timeDiff.push_back (-std::arg (cross) / (2.0 * pi * fc) * 1000.0);
        }
    }
    auto stdOf = [] (const std::vector<double>& v)
    {
        if (v.size() < 2) return 0.0;
        double m = 0.0, s = 0.0;
        for (auto x : v) m += x;
        m /= (double) v.size();
        for (auto x : v) s += (x - m) * (x - m);
        return std::sqrt (s / (double) v.size());
    };
    double meanTime = 0.0;
    for (auto x : timeDiff) meanTime += x;
    meanTime /= std::max<size_t> (1, timeDiff.size());
    const double lvlStd = stdOf (levelDiff), timeStd = stdOf (timeDiff);
    const double imaging = clampScore (100.0 - 10.0 * over (lvlStd, 0.3) - 150.0 * over (timeStd, 0.03)
                                       - 50.0 * over (std::abs (meanTime), 0.05));
    if (imaging < 85.0)
        js.comments.add ("Imaging: L/R level varies " + juce::String (lvlStd, 2) + " dB and arrival "
                         + juce::String (timeStd, 3) + " ms between bands (instruments wander with pitch); mean offset "
                         + juce::String (meanTime, 3) + " ms.");

    //==========================================================================
    // Linearity / smoothness
    double ripple = 0.0, peak = 0.0;
    for (int side = 0; side < 2; ++side)
    {
        std::vector<double> pw (N, 0.0);
        for (int p = 0; p < P; ++p)
            for (size_t i = 0; i < N; ++i)
                pw[i] += std::norm (side == 0 ? fl.left (p, i) : fl.right (p, i));
        const auto fine = model.octaveSmooth (pw, 6.0), coarse = model.octaveSmooth (pw, 1.0);
        ripple += 0.5 * weightedRms ([&] (size_t i) { return dbFromPower (fine[i]) - dbFromPower (coarse[i]); }, 60.0, 12000.0);
        const auto& d = side == 0 ? L : R;
        for (size_t i = 0; i < N; ++i)
            if (f[i] >= 30.0 && f[i] <= 16000.0)
                peak = std::max (peak, d[i] - t[i] - off);
    }
    double hole = 0.0;
    coherenceLoss ([&] (int p, size_t i) { return fl.magL[(size_t) p][i]; }, [&] (int p, size_t i) { return fl.left (p, i); }, 40.0, 10000.0, &hole);
    coherenceLoss ([&] (int p, size_t i) { return fl.magR[(size_t) p][i]; }, [&] (int p, size_t i) { return fl.right (p, i); }, 40.0, 10000.0, &hole);
    const double linearity = clampScore (100.0 - 12.0 * over (ripple, 0.7) - 4.0 * over (peak, 3.0) - 3.0 * over (hole, 3.0));
    if (linearity < 85.0)
        js.comments.add ("Linearity: ripple " + juce::String (ripple, 2) + " dB, worst peak +" + juce::String (peak, 1)
                         + " dB, worst crossover hole " + juce::String (hole, 1) + " dB.");

    //==========================================================================
    // Noise / cleanliness proxy (gain structure)
    double worstNet = 0.0, tweeterGain = 0.0, totalBoost = 0.0;
    for (auto& mc : chans)
    {
        const auto& ch = config.channels[mc.cfgIndex];
        double maxBoost = 0.0;
        for (auto& b : ch.settings.peq)
            if (b.enabled && b.gainDb > 0.0 && b.type != FilterType::allPass)
            {
                maxBoost = std::max (maxBoost, b.gainDb);
                totalBoost += b.gainDb;
            }
        worstNet = std::max (worstNet, ch.settings.gainDb + maxBoost);
        if (ch.role == DriverRole::tweeter)
            tweeterGain = std::max (tweeterGain, ch.settings.gainDb);
    }
    const double noise = clampScore (100.0 - 3.0 * over (worstNet, 6.0) - 4.0 * over (tweeterGain, 0.0) - 0.4 * over (totalBoost, 25.0));
    if (noise < 85.0)
        js.comments.add ("Noise: gain structure adds up to +" + juce::String (worstNet, 1) + " dB on one channel, "
                         + juce::String (totalBoost, 0) + " dB of EQ boost in total (raises hiss).");

    js.categories["Tonal balance"] = tonal;
    js.categories["Bass extension & up front"] = bass;
    js.categories["Stage width"] = stage;
    js.categories["Centre focus"] = centre;
    js.categories["Imaging consistency"] = imaging;
    js.categories["Linearity"] = linearity;
    js.categories["Noise"] = noise;

    js.total = 0.25 * tonal + 0.15 * bass + 0.10 * stage + 0.20 * centre + 0.10 * imaging + 0.10 * linearity + 0.10 * noise;
    if (js.comments.isEmpty())
        js.comments.add ("Competition-ready: no category below 85.");
    return js;
}
} // namespace fa::advanced
