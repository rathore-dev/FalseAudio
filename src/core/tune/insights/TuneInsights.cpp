#include "TuneInsights.h"
#include "../../measure/Analysis.h"
#include "../../dsp/Spectrum.h"

namespace fa::insights
{
namespace
{
    const std::vector<double>& thirdOctaveCentres()
    {
        static const std::vector<double> c { 20, 25, 31.5, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500,
                                             630, 800, 1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300,
                                             8000, 10000, 12500, 16000, 20000 };
        return c;
    }

    /** Level of a response at f (dB), or NaN when the response is empty or f is outside it. */
    double levelAt (const FrequencyResponse& r, double f)
    {
        if (r.isEmpty() || f < r.freqs.front() * 0.999 || f > r.freqs.back() * 1.001)
            return std::numeric_limits<double>::quiet_NaN();
        return dbFromMag (std::abs (r.interpolate (f)));
    }

    /** Interpolates a dB curve given on `freqs` at f (log-frequency). */
    double curveAt (const std::vector<double>& freqs, const std::vector<double>& db, double f)
    {
        if (freqs.empty() || db.size() != freqs.size())
            return std::numeric_limits<double>::quiet_NaN();
        if (f <= freqs.front()) return db.front();
        if (f >= freqs.back())  return db.back();
        const auto it = std::lower_bound (freqs.begin(), freqs.end(), f);
        const size_t i = (size_t) std::distance (freqs.begin(), it);
        const double t = std::log (f / freqs[i - 1]) / std::log (freqs[i] / freqs[i - 1]);
        return db[i - 1] + t * (db[i] - db[i - 1]);
    }

    double nanTo0 (double v) { return std::isfinite (v) ? v : 0.0; }

    juce::String hz (double f)
    {
        if (f < 1000.0)
            return juce::String (juce::roundToInt (f)) + " Hz";
        const double k = std::round (f / 100.0) / 10.0;
        return (std::abs (k - std::round (k)) < 0.05 ? juce::String (juce::roundToInt (k)) : juce::String (k, 1)) + " kHz";
    }

    juce::String driverName (const OutputChannel& c)
    {
        if (c.label.isNotEmpty())
            return c.label;
        return toString (c.side) + " " + toString (c.role);
    }

    /** Allowed THD before it is worth mentioning, by role. */
    double thdLimit (DriverRole role)
    {
        switch (role)
        {
            case DriverRole::subwoofer: return 10.0;
            case DriverRole::midbass:   return 5.0;
            case DriverRole::midrange:  return 2.0;
            case DriverRole::tweeter:   return 1.0;
            case DriverRole::fullrange: return 3.0;
        }
        return 3.0;
    }

    /** RMS of (a - b) after removing the mean difference, over rows within [f1, f2]. */
    template <typename GetA, typename GetB>
    double offsetFreeRms (const std::vector<BandRow>& rows, double f1, double f2, GetA a, GetB b)
    {
        std::vector<double> d;
        for (auto& r : rows)
            if (r.freqHz >= f1 && r.freqHz <= f2)
            {
                const double v = a (r) - b (r);
                if (std::isfinite (v))
                    d.push_back (v);
            }
        if (d.empty())
            return 0.0;
        double mean = 0.0;
        for (auto v : d) mean += v;
        mean /= (double) d.size();
        double acc = 0.0;
        for (auto v : d) acc += (v - mean) * (v - mean);
        return std::sqrt (acc / (double) d.size());
    }
} // namespace

//==============================================================================
Insights compute (const AutoTuneEngine::Result& result, double sweepSeconds, double fStart, double fEnd)
{
    Insights in;
    const auto& cfg = result.tunedConfig;
    const double sr = cfg.caps.sampleRate > 0.0 ? cfg.caps.sampleRate : 48000.0;
    const auto& grid = analysisFrequencies();

    //==========================================================================
    // Band table (responses smoothed to 1/3 octave, read at the nominal centres).
    {
        auto smooth = [] (const FrequencyResponse& r) { return r.isEmpty() ? r : smoothFractionalOctave (r, 3.0); };
        const auto lb = smooth (result.leftBefore), rb = smooth (result.rightBefore);
        const auto la = smooth (result.leftAfter), ra = smooth (result.rightAfter), ba = smooth (result.bothAfter);

        // Net electrical EQ per side: complex sum of every audible channel's DSP transfer function
        // (gain, polarity, crossovers, PEQ; delay excluded so the sum shows the filters, not the alignment).
        const auto& centres = thirdOctaveCentres();
        auto netEq = [&] (Side side)
        {
            FrequencyResponse sum;
            sum.freqs = centres;
            sum.values.assign (centres.size(), cplx (0.0, 0.0));
            bool any = false;
            for (auto& c : cfg.channels)
                if ((c.side == side || c.side == Side::mono) && ! c.settings.muted)
                {
                    sum += c.settings.response (centres, sr, false);
                    any = true;
                }
            return any ? sum : FrequencyResponse::unity (centres);
        };
        const auto eqL = netEq (Side::left), eqR = netEq (Side::right);

        for (size_t i = 0; i < centres.size(); ++i)
        {
            const double f = centres[i];
            if (f >= sr * 0.5)
                break;
            BandRow row;
            row.freqHz = f;
            row.targetDb = nanTo0 (curveAt (grid, result.targetDb, f));
            row.leftBeforeDb = nanTo0 (levelAt (lb, f));
            row.rightBeforeDb = nanTo0 (levelAt (rb, f));
            row.leftAfterDb = nanTo0 (levelAt (la, f));
            row.rightAfterDb = nanTo0 (levelAt (ra, f));
            row.sumAfterDb = nanTo0 (levelAt (ba, f));
            row.eqAppliedLeftDb = eqL.magnitudeDb (i);
            row.eqAppliedRightDb = eqR.magnitudeDb (i);
            in.bands.push_back (row);
        }
    }

    //==========================================================================
    // Per-driver diagnostics from the solo measurements.
    std::vector<double> cleanPenalties;
    for (auto& ch : cfg.channels)
    {
        std::vector<const Measurement*> ms;
        for (auto& m : result.measurements)
            if (m.channelId == ch.id && ! m.response.isEmpty())
                ms.push_back (&m);
        if (ms.empty())
            continue;

        DriverDiagnostics d;
        d.channelId = ch.id;
        const auto name = driverName (ch);

        std::vector<FrequencyResponse> rs;
        double snr = 0.0, onset = 0.0;
        for (auto* m : ms)
        {
            rs.push_back (m->response);
            snr += m->snrDb;
            onset += m->onsetMs;
        }
        d.snrDb = snr / (double) ms.size();
        const auto avg = powerAverage (rs);

        // Usable band: contiguous region around the maximum of the 1-octave-smoothed response
        // (solo measurements are taken with the protective crossovers in place, so these edges
        // can include those filters - hence the generous margins in the crossover findings)
        // that stays within 6 dB of that maximum.
        const auto trend1 = smoothFractionalOctave (avg, 1.0);
        const auto t1 = trend1.magnitudeDb();
        if (! t1.empty())
        {
            size_t best = 0;
            for (size_t i = 0; i < t1.size(); ++i)
                if (trend1.freqs[i] >= 15.0 && trend1.freqs[i] <= 22000.0 && t1[i] > t1[best])
                    best = i;
            size_t lo = best, hi = best;
            while (lo > 0 && t1[lo - 1] >= t1[best] - 6.0) --lo;
            while (hi + 1 < t1.size() && t1[hi + 1] >= t1[best] - 6.0) ++hi;
            d.usableLowHz = trend1.freqs[lo];
            d.usableHighHz = trend1.freqs[hi];
        }

        // Resonances: 1/12-octave detail more than 3 dB above the 1/3-octave trend, inside the
        // usable band, merged within 1/3 octave (strongest kept).
        {
            const auto fine = smoothFractionalOctave (avg, 12.0);
            const auto trend = smoothFractionalOctave (avg, 3.0);
            const auto fd = fine.magnitudeDb(), td = trend.magnitudeDb();
            std::vector<std::pair<double, double>> peaks;
            for (size_t i = 1; i + 1 < fd.size(); ++i)
            {
                const double f = fine.freqs[i];
                if (f < d.usableLowHz || f > d.usableHighHz)
                    continue;
                const double ex = fd[i] - td[i];
                if (ex > 3.0 && fd[i] >= fd[i - 1] && fd[i] >= fd[i + 1])
                {
                    if (! peaks.empty() && f / peaks.back().first < std::pow (2.0, 1.0 / 3.0))
                    {
                        if (ex > peaks.back().second)
                            peaks.back() = { f, ex };
                    }
                    else
                        peaks.emplace_back (f, ex);
                }
            }
            std::sort (peaks.begin(), peaks.end(), [] (auto& a, auto& b) { return a.second > b.second; });
            if (peaks.size() > 5)
                peaks.resize (5);
            std::sort (peaks.begin(), peaks.end());
            d.resonances = peaks;
        }

        d.arrivalMs = result.report.arrivalMs.count (ch.id) ? result.report.arrivalMs.at (ch.id) : onset / (double) ms.size();
        d.polarityReversed = result.report.wiringInverted.count (ch.id) && result.report.wiringInverted.at (ch.id);

        // Distortion (first position) within the driver's working band: its crossover passband
        // intersected with its usable range.
        const double passLo = std::max (d.usableLowHz, ch.hasHighPass() ? ch.settings.highPass.freq : 0.0);
        const double passHi = std::min (d.usableHighHz, ch.hasLowPass() ? ch.settings.lowPass.freq : 1.0e9);
        double peakThdFreq = 0.0;
        {
            const auto dist = analysis::harmonicDistortion (*ms.front(), sweepSeconds, fStart, fEnd);
            if (dist.valid)
            {
                for (size_t i = 0; i < dist.freqs.size() && i < dist.thdPercent.size(); ++i)
                {
                    const double f = dist.freqs[i];
                    if (f < passLo || f > passHi || ! std::isfinite (dist.thdPercent[i]))
                        continue;
                    d.thdCurve.emplace_back (f, dist.thdPercent[i]);
                    if (dist.thdPercent[i] > d.peakThdPercent)
                    {
                        d.peakThdPercent = dist.thdPercent[i];
                        peakThdFreq = f;
                    }
                }
                if (! dist.freqs.empty() && 100.0 >= dist.freqs.front() && 100.0 <= dist.freqs.back())
                    d.thdAt100HzPercent = std::max (0.0, curveAt (dist.freqs, dist.thdPercent, 100.0));
            }
        }

        //----------------------------------------------------------------------
        // Findings.
        for (auto& [f, db] : d.resonances)
        {
            bool corrected = false;
            for (auto& p : ch.settings.peq)
                if (p.enabled && p.type == FilterType::peaking && p.gainDb < -1.0
                    && std::abs (std::log2 (p.freq / f)) <= 1.0 / 3.0)
                    corrected = true;
            const bool panel = (ch.role == DriverRole::midbass || ch.role == DriverRole::subwoofer) && f >= 70.0 && f <= 300.0;
            juce::String note = name + ": " + (db >= 5.0 ? "strong " : "") + hz (f) + (panel ? " door/panel resonance" : " resonance")
                              + " (+" + juce::String (db, 1) + " dB)";
            if (corrected)
                note << " - corrected by EQ";
            else if (f < passLo || f > passHi)
                note << " - outside the crossover band, filtered out";
            else
                note << " - not EQ'd" << (panel ? " (sound-deadening the door would help)" : "");
            d.findings.add (note);
        }

        if (d.peakThdPercent > thdLimit (ch.role))
        {
            juce::String note = name + ": THD " + juce::String (d.peakThdPercent, d.peakThdPercent < 10.0 ? 1 : 0) + " % at " + hz (peakThdFreq);
            const bool nearHp = ch.hasHighPass() && peakThdFreq < ch.settings.highPass.freq * 2.0;
            if (nearHp)
                note << " - consider raising the high-pass (now " << hz (ch.settings.highPass.freq) << ")";
            else
                note << " - reduce the level or check for rattles/a damaged driver";
            d.findings.add (note);
        }

        if (d.polarityReversed)
            d.findings.add (name + ": wired with reversed polarity - inverted in the DSP (or swap its + and - wires)");

        if (d.snrDb < 30.0)
            d.findings.add (name + ": low signal-to-noise (" + juce::String (juce::roundToInt (d.snrDb)) + " dB) - measure louder or in a quieter place");

        if (ch.hasHighPass() && d.usableLowHz > 0.0 && ch.settings.highPass.freq < d.usableLowHz * 0.5)
            d.findings.add (name + ": high-pass at " + hz (ch.settings.highPass.freq) + " is below the driver's usable range (from "
                            + hz (d.usableLowHz) + ")");
        if (ch.hasLowPass() && d.usableHighHz > 0.0 && ch.settings.lowPass.freq > d.usableHighHz * 2.0)
            d.findings.add (name + ": low-pass at " + hz (ch.settings.lowPass.freq) + " is above the driver's usable range (up to "
                            + hz (d.usableHighHz) + ")");

        // Cleanliness penalty for this driver (see cleanlinessScore below).
        const double thdPen = d.thdCurve.empty() ? 0.0 : 8.0 * std::max (0.0, d.peakThdPercent / thdLimit (ch.role) - 1.0);
        const double snrPen = 1.5 * std::max (0.0, 45.0 - d.snrDb);
        cleanPenalties.push_back (std::min (100.0, thdPen + snrPen));

        in.drivers.push_back (std::move (d));
    }

    //==========================================================================
    // Scores (all 0..100, documented formulas):
    //   tonal       = 100 - 12 x RMS(after - target), both sides, 1/3-oct bands 40 Hz..16 kHz,
    //                 mean offset removed (level doesn't count, shape does).
    //   imaging     = 100 - 15 x RMS(left after - right after) over 200 Hz..8 kHz (offset removed)
    //                       - min(30, sum of L+R cancellation dip depths in dB between 80 Hz and 5 kHz).
    //   cleanliness = 100 - mean over drivers of [8 x max(0, peakTHD/limit - 1) + 1.5 x max(0, 45 - SNR)]
    //                 with THD limits sub 10 %, midbass 5 %, mid 2 %, tweeter 1 %, full-range 3 %.
    {
        const bool hasAfter = ! result.leftAfter.isEmpty() && ! result.targetDb.empty();
        if (hasAfter)
        {
            const double tl = offsetFreeRms (in.bands, 40.0, 16000.0, [] (auto& r) { return r.leftAfterDb; }, [] (auto& r) { return r.targetDb; });
            const double tr = result.rightAfter.isEmpty() ? tl
                            : offsetFreeRms (in.bands, 40.0, 16000.0, [] (auto& r) { return r.rightAfterDb; }, [] (auto& r) { return r.targetDb; });
            in.tonalBalanceScore = juce::jlimit (0.0, 100.0, 100.0 - 12.0 * 0.5 * (tl + tr));
        }

        if (! result.leftAfter.isEmpty() && ! result.rightAfter.isEmpty())
        {
            const double lr = offsetFreeRms (in.bands, 200.0, 8000.0, [] (auto& r) { return r.leftAfterDb; }, [] (auto& r) { return r.rightAfterDb; });
            double dips = 0.0;
            for (auto& [f, depth] : result.report.sumDips)
                if (f >= 80.0 && f <= 5000.0)
                    dips += std::abs (depth);
            in.imagingScore = juce::jlimit (0.0, 100.0, 100.0 - 15.0 * lr - std::min (30.0, dips));
        }

        double pen = 0.0;
        for (auto p : cleanPenalties)
            pen += p;
        in.cleanlinessScore = cleanPenalties.empty() ? 0.0 : juce::jlimit (0.0, 100.0, 100.0 - pen / (double) cleanPenalties.size());
    }

    //==========================================================================
    in.summary.add ("Tonal balance " + juce::String (juce::roundToInt (in.tonalBalanceScore)) + "/100, imaging "
                    + juce::String (juce::roundToInt (in.imagingScore)) + "/100, cleanliness "
                    + juce::String (juce::roundToInt (in.cleanlinessScore)) + "/100.");
    if (! result.leftAfter.isEmpty())
        in.summary.add ("Left is within " + juce::String (result.leftErrorDb, 1) + " dB and right within "
                        + juce::String (result.rightErrorDb, 1) + " dB (RMS) of the target.");
    {
        double maxCut = 0.0, maxBoost = 0.0, cutF = 0.0, boostF = 0.0;
        for (auto& r : in.bands)
            for (double v : { r.eqAppliedLeftDb, r.eqAppliedRightDb })
            {
                if (v < maxCut)   { maxCut = v;   cutF = r.freqHz; }
                if (v > maxBoost) { maxBoost = v; boostF = r.freqHz; }
            }
        if (maxCut < -3.0)
            in.summary.add ("Biggest EQ cut " + juce::String (maxCut, 1) + " dB at " + hz (cutF) + ".");
        if (maxBoost > 3.0)
            in.summary.add ("Biggest EQ boost +" + juce::String (maxBoost, 1) + " dB at " + hz (boostF) + ".");
    }
    int inverted = 0, findings = 0;
    for (auto& d : in.drivers)
    {
        inverted += d.polarityReversed ? 1 : 0;
        findings += d.findings.size();
    }
    if (inverted > 0)
        in.summary.add (juce::String (inverted) + (inverted == 1 ? " driver was" : " drivers were") + " wired in reverse polarity (fixed in the DSP).");
    for (auto& [f, depth] : result.report.sumDips)
        in.summary.add ("L+R cancellation dip of " + juce::String (depth, 1) + " dB at " + hz (f) + ".");
    if (findings > 0)
        in.summary.add (juce::String (findings) + " driver note" + (findings == 1 ? "" : "s") + " - see the driver list.");
    return in;
}

//==============================================================================
juce::String bandsToCsv (const Insights& in)
{
    juce::String csv = "Frequency (Hz),Target (dB),Left before (dB),Left after (dB),Right before (dB),Right after (dB),"
                       "L+R after (dB),EQ left (dB),EQ right (dB)\n";
    auto n = [] (double v) { return juce::String (v, 2); };
    for (auto& r : in.bands)
        csv << juce::String (r.freqHz, r.freqHz < 100.0 ? 1 : 0) << "," << n (r.targetDb) << "," << n (r.leftBeforeDb) << ","
            << n (r.leftAfterDb) << "," << n (r.rightBeforeDb) << "," << n (r.rightAfterDb) << "," << n (r.sumAfterDb) << ","
            << n (r.eqAppliedLeftDb) << "," << n (r.eqAppliedRightDb) << "\n";
    return csv;
}
} // namespace fa::insights
