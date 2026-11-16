#include "ChannelVerifier.h"
#include "../tune/CrossoverPlanner.h"
#include <algorithm>
#include <thread>

namespace fa
{
namespace
{
double roleCentreHz (DriverRole r)
{
    switch (r)
    {
        case DriverRole::subwoofer: return 55.0;
        case DriverRole::midbass:   return 250.0;
        case DriverRole::midrange:  return 1000.0;
        case DriverRole::tweeter:   return 7000.0;
        case DriverRole::fullrange: return 600.0;
    }
    return 500.0;
}

juce::String roleBand (DriverRole r)
{
    switch (r)
    {
        case DriverRole::subwoofer: return "low (sub bass)";
        case DriverRole::midbass:   return "low-mid (mid bass)";
        case DriverRole::midrange:  return "mid (midrange)";
        case DriverRole::tweeter:   return "high (tweeter)";
        case DriverRole::fullrange: return "wide (full range)";
    }
    return {};
}

juce::String hzText (double f)
{
    return f >= 1000.0 ? juce::String (f / 1000.0, 1) + " kHz" : juce::String (juce::roundToInt (f)) + " Hz";
}

/** Polls the abort callback while a blocking sweep runs and raises the cancel flag. */
struct Watchdog
{
    Watchdog (std::function<bool()> f, std::atomic<bool>& c) : fn (std::move (f)), cancel (c)
    {
        if (fn)
            th = std::thread ([this]
            {
                while (! stop.load())
                {
                    if (fn())
                        cancel.store (true);
                    std::this_thread::sleep_for (std::chrono::milliseconds (15));
                }
            });
    }
    ~Watchdog()
    {
        stop.store (true);
        if (th.joinable())
            th.join();
    }
    std::function<bool()> fn;
    std::atomic<bool>& cancel;
    std::atomic<bool> stop { false };
    std::thread th;
};
} // namespace

ChannelVerifier::ChannelVerifier (SourceFn s, ConfigFn c, Options o)
    : sourceFn (std::move (s)), configFn (std::move (c)), opts (std::move (o)) {}

void ChannelVerifier::setOptions (const Options& o) { std::lock_guard<std::mutex> l (m); opts = o; }
bool ChannelVerifier::hasReference (int output) const { std::lock_guard<std::mutex> l (m); return references.count (output) > 0; }
void ChannelVerifier::clearReferences() { std::lock_guard<std::mutex> l (m); references.clear(); referenceFromHz.clear(); }
std::vector<ChannelVerifier::PlayedSweep> ChannelVerifier::getPlayLog() const { std::lock_guard<std::mutex> l (m); return playLog; }

double ChannelVerifier::clampLevel (double dB) const
{
    return std::min ({ dB, opts.maxLevelDbFS, absoluteMaxLevelDbFS });
}

const OutputChannel* ChannelVerifier::channelFor (const SystemConfig& cfg, int output) const
{
    for (auto& ch : cfg.channels)
        if (ch.dspOutput == output)
            return &ch;
    return nullptr;
}

double ChannelVerifier::protectedHz (const SystemConfig& cfg) const
{
    double hz = cfg.safety.minTweeterHpHz;
    for (auto& ch : cfg.channels)
        if (ch.role == DriverRole::tweeter)
            hz = std::max (hz, CrossoverPlanner::safeHighPassHz (ch, cfg.safety));
    return hz;
}

bool ChannelVerifier::sweepOnce (double fStart, double level, Feed feed, Measurement& out, juce::String& err)
{
    if (opts.shouldAbort && opts.shouldAbort())
    {
        err = "aborted (kill switch)";
        return false;
    }
    SweepRequest rq;
    rq.sweep.sampleRate = sourceFn().getSampleRate();
    rq.sweep.fStart = fStart;
    rq.sweep.fEnd = 20000.0;
    rq.sweep.seconds = opts.sweepSeconds;
    rq.sweep.tailSeconds = opts.tailSeconds;
    rq.sweep.levelDbFS = clampLevel (level);
    rq.feed = feed;
    rq.label = "verify";
    playLog.push_back ({ fStart, rq.sweep.levelDbFS, feed });

    std::atomic<bool> cancel { false };
    std::vector<Measurement> ms;
    {
        Watchdog wd (opts.shouldAbort, cancel);
        ms = sourceFn().measureSweep (rq, cancel, err);
    }
    if (opts.shouldAbort && opts.shouldAbort())
    {
        err = "aborted (kill switch)";
        return false;
    }
    if (ms.empty())
    {
        if (err.isEmpty())
            err = "no capture";
        return false;
    }
    out = std::move (ms.front());
    if (out.clipped)
    {
        err = "clipping detected in the capture; test aborted for safety";
        return false;
    }
    return true;
}

ChannelVerifier::Spectrum ChannelVerifier::summarise (const Measurement& m, double fromHz, double toHz)
{
    Spectrum s;
    const auto& r = m.response;
    if (r.isEmpty())
        return s;
    std::vector<double> db;
    std::vector<double> fr;
    for (size_t i = 0; i < r.size(); ++i)
        if (r.freqs[i] >= fromHz && r.freqs[i] <= toHz)
        {
            db.push_back (r.magnitudeDb (i));
            fr.push_back (r.freqs[i]);
        }
    if (db.empty())
        return s;
    s.peakDb = *std::max_element (db.begin(), db.end());
    double sum = 0.0, n = 0.0, pw = 0.0, pwl = 0.0;
    s.lowHz = 1.0e9;
    s.highHz = 0.0;
    for (size_t k = 0; k < db.size(); ++k)
    {
        if (db[k] >= s.peakDb - 15.0) { sum += db[k]; n += 1.0; }
        if (db[k] >= s.peakDb - 10.0) { s.lowHz = std::min (s.lowHz, fr[k]); s.highHz = std::max (s.highHz, fr[k]); }
        const double p = std::pow (10.0, (db[k] - s.peakDb) / 10.0);
        pw += p;
        pwl += p * std::log (fr[k]);
    }
    s.levelDb = n > 0 ? sum / n : s.peakDb;
    s.centroidHz = pw > 0 ? std::exp (pwl / pw) : 0.0;
    if (s.highHz == 0.0)
        s.lowHz = 0.0;
    return s;
}

bool ChannelVerifier::compareImpulses (const Measurement& ref, const Measurement& cur, double& delayMs, bool& inverted)
{
    if (ref.ir.empty() || cur.ir.empty())
        return false;
    const double fs = ref.sampleRate;
    const int pk = (int) std::lround (findPeak (ref.ir));
    const int a = std::max (0, pk - (int) (0.002 * fs));
    const int b = std::min ((int) ref.ir.size(), pk + (int) (0.008 * fs));
    const int maxLag = (int) (0.004 * fs);
    double er = 0.0;
    for (int i = a; i < b; ++i)
        er += (double) ref.ir[(size_t) i] * ref.ir[(size_t) i];
    if (er <= 0.0)
        return false;

    std::vector<double> c ((size_t) (2 * maxLag + 1), 0.0);
    double best = 0.0;
    int bestLag = 0;
    for (int lag = -maxLag; lag <= maxLag; ++lag)
    {
        double x = 0.0, ec = 0.0;
        for (int i = a; i < b; ++i)
        {
            const int j = i + lag;
            if (j < 0 || j >= (int) cur.ir.size())
                continue;
            x += (double) ref.ir[(size_t) i] * cur.ir[(size_t) j];
            ec += (double) cur.ir[(size_t) j] * cur.ir[(size_t) j];
        }
        const double v = ec > 0 ? x / std::sqrt (er * ec) : 0.0;
        c[(size_t) (lag + maxLag)] = v;
        if (std::abs (v) > std::abs (best))
        {
            best = v;
            bestLag = lag;
        }
    }
    double frac = 0.0;
    if (bestLag > -maxLag && bestLag < maxLag)
    {
        const double y0 = std::abs (c[(size_t) (bestLag + maxLag - 1)]), y1 = std::abs (best),
                     y2 = std::abs (c[(size_t) (bestLag + maxLag + 1)]);
        const double den = y0 - 2.0 * y1 + y2;
        if (den < 0.0)
            frac = juce::jlimit (-0.5, 0.5, 0.5 * (y0 - y2) / den);
    }
    inverted = best < 0.0;
    delayMs = (bestLag + frac) / fs * 1000.0;
    return std::abs (best) > 0.3;
}

DriverRole ChannelVerifier::classify (double centroidHz, const std::vector<DriverRole>& candidates)
{
    std::vector<DriverRole> c = candidates;
    if (c.empty())
        c = { DriverRole::subwoofer, DriverRole::midbass, DriverRole::midrange, DriverRole::tweeter };
    DriverRole best = c.front();
    double bd = 1.0e9;
    for (auto r : c)
    {
        const double d = std::abs (std::log (std::max (1.0, centroidHz) / roleCentreHz (r)));
        if (d < bd)
        {
            bd = d;
            best = r;
        }
    }
    return best;
}

DspOperatorAgent::VerifyFn ChannelVerifier::asVerifyFn()
{
    return [this] (const DspOperatorAgent::VerifyRequest& rq)
    {
        DspOperatorAgent::VerifyResult out;
        const auto r = verify (rq.output, rq.mode);
        out.performed = r.performed;
        out.matches = r.matches;
        out.respondedDriver = r.silent ? juce::String ("none (silent)") : r.driver;
        out.respondedBand = r.band;
        out.detail = r.detail;
        return out;
    };
}

ChannelVerifier::Report ChannelVerifier::verify (int output, const juce::String& mode)
{
    std::lock_guard<std::mutex> lock (m);
    Report rep;
    const auto& cfg = configFn();
    const auto* claimed = channelFor (cfg, output);

    juce::String err;
    if (! sourceFn().isReady (err))
    {
        rep.detail = "Test not completed: measurement not ready: " + err;
        return rep;
    }

    auto fail = [&] (const juce::String& e) -> Report
    {
        rep.performed = false;
        rep.aborted = e.containsIgnoreCase ("abort") || e.containsIgnoreCase ("clip") || e.containsIgnoreCase ("cancel");
        rep.detail = "Test not completed: " + e;
        return rep;
    };

    // Stage 1: tweeter-safe sweep, starting at the protected HP.
    const double protHz = protectedHz (cfg);
    const double lvl1 = clampLevel (opts.levelDbFS);
    Measurement m1;
    err.clear();
    if (! sweepOnce (protHz, lvl1, Feed::both, m1, err))
        return fail (err);

    const auto sp1 = summarise (m1, protHz, 20000.0);
    const auto spAll = summarise (m1, 20.0, 20000.0);
    const bool present1 = m1.snrDb >= opts.minSnrDb;
    const bool tweeterLike = present1 && sp1.peakDb >= spAll.peakDb - 12.0 && sp1.centroidHz >= 5000.0;

    Measurement fin = m1;
    double finFrom = protHz, finLevel = lvl1;
    Spectrum sp = sp1;
    bool present = present1;

    const bool claimedMayBeTweeter = claimed == nullptr || claimed->role == DriverRole::tweeter;
    if (! tweeterLike && ! claimedMayBeTweeter)
    {
        // Stage 2: not tweeter-like and not claimed to be a tweeter: sweep the low end, quieter.
        const double lvl2 = clampLevel (opts.levelDbFS + opts.fullRangeOffsetDb);
        Measurement m2;
        if (! sweepOnce (opts.fullRangeStartHz, lvl2, Feed::both, m2, err))
            return fail (err);
        fin = m2;
        finFrom = opts.fullRangeStartHz;
        finLevel = lvl2;
        present = m2.snrDb >= opts.minSnrDb;
        sp = summarise (m2, finFrom, 20000.0);
    }

    rep.performed = true;
    rep.silent = ! present;
    rep.levelDb = sp.levelDb;

    if (rep.silent)
    {
        rep.detail = "Silent: nothing audible on the mic (output muted, not routed or level too low).";
        rep.levelDb = -120.0;
    }
    else
    {
        rep.centroidHz = sp.centroidHz;
        rep.bandLowHz = sp.lowHz;
        rep.bandHighHz = sp.highHz;

        std::vector<DriverRole> cands;
        for (auto& ch : cfg.channels)
            if (std::find (cands.begin(), cands.end(), ch.role) == cands.end())
                cands.push_back (ch.role);
        rep.role = classify (sp.centroidHz, cands);
        rep.roleKnown = true;

        if (opts.detectSide)
        {
            Measurement ml, mr;
            if (! sweepOnce (finFrom, finLevel, Feed::left, ml, err))
                return fail (err);
            if (! sweepOnce (finFrom, finLevel, Feed::right, mr, err))
                return fail (err);
            rep.heardLeft = ml.snrDb >= opts.minSnrDb;
            rep.heardRight = mr.snrDb >= opts.minSnrDb;
            rep.side = rep.heardLeft && rep.heardRight ? "left+right" : rep.heardLeft ? "left" : rep.heardRight ? "right" : "none";
        }

        // Driver label: the configured channel with this role (and side when known).
        const OutputChannel* resp = nullptr;
        for (auto& ch : cfg.channels)
        {
            if (ch.role != rep.role)
                continue;
            const bool sideOk = ! opts.detectSide || rep.side == "left+right"
                                || (rep.side == "left" && ch.side == Side::left)
                                || (rep.side == "right" && ch.side == Side::right)
                                || ch.side == Side::mono;
            if (sideOk)
            {
                resp = &ch;
                break;
            }
        }
        rep.driver = resp != nullptr ? resp->label
                                     : toString (rep.role) + (rep.side.isNotEmpty() ? " (" + rep.side + ")" : juce::String());
        rep.band = roleBand (rep.role) + ", " + hzText (sp.lowHz) + " - " + hzText (sp.highHz);

        bool sideMatch = true;
        if (claimed != nullptr && opts.detectSide && claimed->side != Side::mono)
        {
            if (rep.side == "left")
                sideMatch = claimed->side == Side::left;
            else if (rep.side == "right")
                sideMatch = claimed->side == Side::right;
        }
        rep.matches = claimed != nullptr && claimed->role == rep.role && sideMatch;
        rep.detail = "Level " + juce::String (rep.levelDb, 1) + " dB, centre " + hzText (rep.centroidHz)
                     + (rep.side.isNotEmpty() ? ", heard on " + rep.side : juce::String()) + ".";
        if (claimed != nullptr && ! rep.matches)
            rep.detail += " Expected " + claimed->label + " (" + toString (claimed->role) + ").";
    }

    const bool isRef = mode.equalsIgnoreCase ("reference");
    auto it = references.find (output);
    if (! isRef && it != references.end())
    {
        const auto& ref = it->second;
        rep.hasReference = true;
        if (rep.silent)
        {
            rep.levelChangeDb = -100.0;
            rep.detail += " Compared to the reference: muted / no signal.";
        }
        else
        {
            const double from = std::max (referenceFromHz[output], finFrom);
            const auto sref = summarise (ref, from, 20000.0);
            const auto cur = summarise (fin, from, 20000.0);
            std::vector<double> diffs;
            const auto& rr = ref.response;
            const auto& cr = fin.response;
            for (size_t i = 0; i < rr.size() && i < cr.size(); ++i)
                if (rr.freqs[i] >= from && rr.freqs[i] <= 20000.0 && rr.magnitudeDb (i) >= sref.peakDb - 15.0
                    && cr.magnitudeDb (i) >= cur.peakDb - 15.0)
                    diffs.push_back (cr.magnitudeDb (i) - rr.magnitudeDb (i));
            if (! diffs.empty())
            {
                std::nth_element (diffs.begin(), diffs.begin() + (long) (diffs.size() / 2), diffs.end());
                rep.levelChangeDb = diffs[diffs.size() / 2];
            }
            double d = 0.0;
            bool inv = false;
            if (compareImpulses (ref, fin, d, inv))
            {
                rep.polarityFlipped = inv;
                rep.delayChangeMs = d;
            }
            rep.detail += " Versus reference: level " + juce::String (rep.levelChangeDb, 1) + " dB, delay "
                          + juce::String (rep.delayChangeMs, 2) + " ms, polarity "
                          + (rep.polarityFlipped ? "FLIPPED." : "unchanged.");
        }
    }
    if (isRef && ! rep.silent)
    {
        references[output] = fin;
        referenceFromHz[output] = finFrom;
        rep.storedReference = true;
        rep.detail += " Stored as the reference for this output.";
    }
    if (opts.log)
        opts.log ("verify_channel output " + juce::String (output + 1) + ": " + rep.detail);
    return rep;
}
} // namespace fa
