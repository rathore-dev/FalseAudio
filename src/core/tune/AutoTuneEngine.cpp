#include "AutoTuneEngine.h"
#include "GraphicEQ.h"
#include "../dsp/Spectrum.h"
#include "TargetLibrary.h"

namespace fa
{
juce::String AutoTuneEngine::stageName (Stage s)
{
    switch (s)
    {
        case Stage::idle:       return "Ready";
        case Stage::preflight:  return "Preflight";
        case Stage::protect:    return "Protect";
        case Stage::measure:    return "Measure";
        case Stage::crossovers: return "Crossovers";
        case Stage::solve:      return "Solve";
        case Stage::apply:      return "Apply";
        case Stage::verify:     return "Verify";
        case Stage::report:     return "Report";
        case Stage::done:       return "Done";
        case Stage::failed:     return "Failed";
        case Stage::cancelled:  return "Cancelled";
    }
    return {};
}

AutoTuneEngine::AutoTuneEngine() : juce::Thread ("FalseAudio AutoTune") {}

AutoTuneEngine::~AutoTuneEngine()
{
    *alive = false;
    cancel();
    stopThread (10000);
}

void AutoTuneEngine::postToListeners (std::function<void (Listener&)> f)
{
    juce::MessageManager::callAsync ([this, flag = alive, f = std::move (f)]
    {
        if (*flag)
            listeners.call (f);
    });
}

bool AutoTuneEngine::start (const SystemConfig& cfg, MeasurementSource& src, DspController& d, const Options& o)
{
    if (isThreadRunning())
        return false;

    config = cfg;
    source = &src;
    dsp = &d;
    options = o;
    cancelFlag = false;
    userEvent.reset();
    return startThread();
}

void AutoTuneEngine::cancel()
{
    cancelFlag = true;
    userAnswer = false;
    userEvent.signal();
}

void AutoTuneEngine::respondToUser (bool proceed)
{
    userAnswer = proceed;
    userEvent.signal();
}

AutoTuneEngine::Result AutoTuneEngine::getLastResult() const
{
    const juce::ScopedLock sl (resultLock);
    return lastResult;
}

void AutoTuneEngine::setStage (Stage s)
{
    stage = s;
    postToListeners ([s] (Listener& l) { l.autoTuneStageChanged (s); });
}

void AutoTuneEngine::progress (double fraction, const juce::String& message)
{
    const auto s = stage.load();
    postToListeners ([s, fraction, message] (Listener& l) { l.autoTuneProgress (s, fraction, message); });
}

bool AutoTuneEngine::askUser (const UserAction& action)
{
    userEvent.reset();
    userAnswer = false;

    postToListeners ([action] (Listener& l) { l.autoTuneNeedsUser (action); });

    while (! userEvent.wait (100))
        if (cancelFlag.load() || threadShouldExit())
            return false;

    return userAnswer.load() && ! cancelFlag.load();
}

void AutoTuneEngine::finish (Result result)
{
    {
        const juce::ScopedLock sl (resultLock);
        lastResult = result;
    }

    postToListeners ([result] (Listener& l) { l.autoTuneFinished (result); });
}

//==============================================================================
std::vector<Measurement> AutoTuneEngine::sweep (Feed feed, const juce::String& label, juce::String& error)
{
    SweepRequest req;
    req.sweep.seconds = options.sweepSeconds;
    req.sweep.levelDbFS = options.sweepLevelDbFS;
    req.sweep.sampleRate = source->getSampleRate();
    req.sweep.fStart = 10.0;
    req.sweep.fEnd = std::min (24000.0, source->getSampleRate() * 0.48);
    req.feed = feed;
    req.label = label;

    juce::String err;
    auto ms = source->measureSweep (req, cancelFlag, err);
    if (ms.empty())
        error = err.isNotEmpty() ? err : juce::String ("Measurement failed.");

    for (auto& m : ms)
        postToListeners ([m] (Listener& l) { l.autoTuneMeasurement (m); });
    return ms;
}

FrequencyResponse AutoTuneEngine::measureSide (Feed feed, const juce::StringArray& ids, const juce::String& label,
                                                std::vector<Measurement>& sink, juce::String& error)
{
    if (! dsp->soloChannels (config, ids, error))
        return {};

    auto ms = sweep (feed, label, error);
    if (ms.empty())
        return {};

    std::vector<FrequencyResponse> rs;
    for (auto& m : ms)
    {
        rs.push_back (m.response);
        sink.push_back (m);
    }
    return powerAverage (rs);
}

double AutoTuneEngine::sideError (const FrequencyResponse& r, const std::vector<double>& targetDb) const
{
    if (r.isEmpty())
        return 99.0;

    const auto sm = smoothFractionalOctave (r, 6.0);
    double ref = 0.0, acc = 0.0;
    int n = 0;
    for (size_t i = 0; i < sm.size(); ++i)
        if (sm.freqs[i] >= 200.0 && sm.freqs[i] <= 2000.0)
            { ref += sm.magnitudeDb (i) - targetDb[i]; ++n; }
    ref /= std::max (1, n);
    n = 0;
    for (size_t i = 0; i < sm.size(); ++i)
        if (sm.freqs[i] >= 40.0 && sm.freqs[i] <= 12000.0)
            { acc += std::pow (sm.magnitudeDb (i) - targetDb[i] - ref, 2.0); ++n; }
    return std::sqrt (acc / std::max (1, n));
}

void AutoTuneEngine::run()
{
    Result result;
    const auto startTime = juce::Time::getMillisecondCounterHiRes();

    const bool ok = runStages (result);
    result.seconds = (juce::Time::getMillisecondCounterHiRes() - startTime) / 1000.0;
    result.success = ok;

    if (! ok && cancelFlag.load())
    {
        result.error = "Cancelled.";
        setStage (Stage::cancelled);
    }
    else
    {
        setStage (ok ? Stage::done : Stage::failed);
    }

    finish (std::move (result));
}

bool AutoTuneEngine::runStages (Result& result)
{
    auto fail = [&result] (const juce::String& e) { result.error = e; return false; };
    auto cancelled = [this] { return cancelFlag.load() || threadShouldExit(); };
    juce::String error;

    juce::StringArray allIds;
    for (auto& c : config.channels)
        allIds.add (c.id);

    //==========================================================================
    setStage (Stage::preflight);
    progress (0.0, "Checking audio device");
    if (! source->isReady (error))
        return fail (error);

    progress (0.4, "Connecting to " + dsp->getName());
    if (! dsp->connect (error))
        return fail ("DSP: " + error);

    config.caps = dsp->getCapabilities();
    config.caps.sampleRate = config.caps.sampleRate > 0 ? config.caps.sampleRate : source->getSampleRate();
    progress (1.0, "Ready");
    if (cancelled()) return false;

    //==========================================================================
    setStage (Stage::protect);
    progress (0.0, "Applying protective crossovers (tweeters safe)");
    for (auto& c : config.channels)
    {
        c.settings = {};
    }
    const auto safeDecisions = CrossoverPlanner::planDefaults (config);
    CrossoverPlanner::apply (config, safeDecisions);

    if (! dsp->applyAll (config, error))
        return fail ("Could not write protective settings: " + error);

    const SystemConfig protectedConfig = config;
    progress (1.0, "Protected");
    if (cancelled()) return false;

    //==========================================================================
    setStage (Stage::measure);

    // Signal check on a safe driver: enough SNR, no clipping.
    {
        const OutputChannel* probe = nullptr;
        for (auto& c : config.channels)
            if (c.role == DriverRole::midbass || c.role == DriverRole::midrange || c.role == DriverRole::fullrange)
                { probe = &c; break; }
        if (probe == nullptr && ! config.channels.empty())
            probe = &config.channels.front();

        bool lastClipped = false;
        double lastSnr = 100.0;
        for (int attempt = 0; attempt < 4 && probe != nullptr; ++attempt)
        {
            progress (0.0, "Level check (" + juce::String (options.sweepLevelDbFS, 0) + " dBFS)");
            if (! dsp->soloChannels (config, { probe->id }, error))
                return fail (error);

            error.clear();
            auto ms = sweep (probe->side == Side::right ? Feed::right : Feed::left, "Level check", error);
            if (cancelled()) return false;
            if (ms.empty())
                return fail (error);

            const auto& m = ms.front();
            lastClipped = m.clipped;
            lastSnr = m.snrDb;
            for (auto& other : ms)
            {
                lastClipped = lastClipped || other.clipped;
                lastSnr = std::min (lastSnr, other.snrDb);
            }
            if (lastClipped && options.sweepLevelDbFS > -40.0)
            {
                options.sweepLevelDbFS -= 6.0;
                continue;
            }
            if (lastSnr < 45.0 && options.sweepLevelDbFS < -6.0)
            {
                options.sweepLevelDbFS = std::min (-6.0, options.sweepLevelDbFS + std::min (12.0, 45.0 - lastSnr));
                continue;
            }
            break;
        }

        // Don't tune on data that can't be trusted.
        if (lastClipped)
            return fail ("The microphone input still clips at " + juce::String (options.sweepLevelDbFS, 0)
                         + " dBFS. Lower the mic preamp gain (or the amp gain) and try again.");
        if (lastSnr < 15.0)
            return fail ("Almost no signal reached the microphone (signal-to-noise " + juce::String (lastSnr, 0)
                         + " dB). Check the mic input, the DSP input routing, that the amp is on and the volume is up.");
    }

    const int mics = std::max (1, source->numMicrophones());
    const int passes = std::max (1, (options.micPositions + mics - 1) / mics);
    const int total = passes * (int) config.channels.size();
    int done = 0;

    DriverDataMap data;
    for (auto& c : config.channels)
        data[c.id].measuredWith = c.settings;

    for (int pass = 0; pass < passes; ++pass)
    {
        if (pass > 0)
        {
            UserAction a;
            a.kind = UserAction::Kind::moveMic;
            a.title = "Move the microphone";
            a.message = "Move the mic to position " + juce::String (pass * mics + 1) + " of " + juce::String (passes * mics)
                      + ": shift it about 10 cm (left/right/forward) around the listener's head, ear height, pointing at the roof.";
            if (onMicPositionChanged == nullptr && ! askUser (a))
                return false;
            if (onMicPositionChanged != nullptr)
                onMicPositionChanged (pass);
        }

        for (auto& c : config.channels)
        {
            if (cancelled()) return false;

            progress ((double) done / total, "Measuring " + c.label + (passes > 1 ? " @ position " + juce::String (pass + 1) : juce::String()));
            if (! dsp->soloChannels (config, { c.id }, error))
                return fail (error);

            error.clear();
            const Feed feed = c.side == Side::left ? Feed::left : c.side == Side::right ? Feed::right : Feed::both;
            auto ms = sweep (feed, c.id, error);
            if (ms.empty())
                return fail (c.label + ": " + error);

            for (auto& m : ms)
            {
                if (m.snrDb < 25.0)
                    result.report.warnings.add (c.label + ": low signal-to-noise (" + juce::String (m.snrDb, 0)
                                                + " dB). Is the speaker connected and the amp on?");
                if (m.clipped)
                    result.report.warnings.add (c.label + ": the mic input clipped during this sweep - its response may be wrong.");
                data[c.id].add (m);
                result.measurements.push_back (m);
            }
            ++done;
        }
    }
    progress (1.0, "All drivers measured");

    // "Before" curves: what the system sounded like with only protective crossovers.
    {
        SystemConfig before = protectedConfig;
        SystemSolver predictor (before, data, {});
        std::vector<FrequencyResponse> l, r;
        for (int p = 0; p < predictor.numPositions(); ++p)
        {
            l.push_back (predictor.predictSum (predictor.sideChannels (Side::left), p));
            r.push_back (predictor.predictSum (predictor.sideChannels (Side::right), p));
        }
        result.leftBefore = powerAverage (l);
        result.rightBefore = powerAverage (r);
    }

    //==========================================================================
    setStage (Stage::crossovers);
    juce::StringArray crossoverNotes;
    if (options.refineCrossovers)
    {
        progress (0.2, "Checking each driver's usable bandwidth");
        std::map<juce::String, FrequencyResponse> responses;
        for (auto& [id, dd] : data)
            responses[id] = powerAverage (dd.positions);

        const auto decisions = CrossoverPlanner::planFromMeasurements (config, responses);
        CrossoverPlanner::apply (config, decisions);
        for (auto& d : decisions)
            for (auto& r : d.reasons)
                crossoverNotes.add (d.channelId + ": " + r);
    }
    progress (1.0, "Crossovers set (LR 24 dB/oct)");
    if (cancelled()) return false;

    //==========================================================================
    setStage (Stage::solve);
    SystemSolver::Options so;
    so.matchRightToLeft = options.matchRightToLeft;
    if (options.useBandTargets)
        if (auto* entry = TargetLibrary::findByName (config.target.name))
            so.bandTargets = entry->bands;
    SystemSolver solver (config, data, so);

    progress (0.05, "Checking polarity");                 solver.checkPolarity();
    if (cancelled()) return false;
    progress (0.20, "Time alignment");                    solver.alignTime();
    if (cancelled()) return false;
    progress (0.35, "Crossover phase optimisation");      solver.refineCrossoverPhase();
    if (cancelled()) return false;
    progress (0.55, "Level matching");                    solver.matchLevels();
    if (cancelled()) return false;
    progress (0.70, "Equalising left, then right to left"); solver.equalise();
    if (cancelled()) return false;

    // Refine before analyseSum() so the sum-dip check reports on the refined settings.
    advanced::Outcome refined;
    if (options.competitionRefine)
    {
        progress (0.80, "Competition refinement (phase, levels, crossovers, EQ)");
        advanced::Options ao;
        ao.maxIterations = std::max (20, options.refineIterations);
        ao.allowAllPass = config.caps.hasAllPass;
        refined = advanced::refine (config, data, ao);   // restores config itself if there is no gain
        if (cancelled()) return false;
    }

    progress (0.95, "Checking the L+R sum");              solver.analyseSum();
    result.sqScore = advanced::judge (config, data);
    result.refineCostBefore = refined.costBefore;
    result.refineCostAfter = refined.costAfter;

    result.report = solver.getReport();
    for (auto& n : crossoverNotes)
        result.report.notes.insert (0, n);
    for (auto& n : refined.notes)
        result.report.notes.add (n);
    result.report.notes.add ("Simulated SQ judge: " + juce::String (result.sqScore.total, 1) + " / 100");
    progress (1.0, "Solved");

    //==========================================================================
    setStage (Stage::apply);
    progress (0.1, "Writing to " + dsp->getName());
    if (! dsp->applyAll (config, error))
        return fail ("Could not apply the tune: " + error);
    if (! dsp->soloChannels (config, allIds, error))
        return fail (error);
    progress (1.0, "Applied");
    if (cancelled()) return false;

    //==========================================================================
    setStage (Stage::verify);
    auto idsFor = [this] (Side side)
    {
        juce::StringArray ids;
        for (auto& c : config.channels)
            if (c.side == side || c.side == Side::mono)
                ids.add (c.id);
        return ids;
    };

    std::vector<double> targetDb;
    for (int pass = 0; pass <= options.maxVerifyPasses; ++pass)
    {
        progress (0.1 + 0.25 * pass, "Verifying left side");
        result.leftAfter = measureSide (Feed::left, idsFor (Side::left), "Verify L", result.measurements, error);
        if (result.leftAfter.isEmpty()) return fail (error);
        if (cancelled()) return false;

        progress (0.2 + 0.25 * pass, "Verifying right side");
        result.rightAfter = measureSide (Feed::right, idsFor (Side::right), "Verify R", result.measurements, error);
        if (result.rightAfter.isEmpty()) return fail (error);
        if (cancelled()) return false;

        // Target anchored to the measured left level.
        targetDb = config.target.levels (analysisFrequencies());
        {
            const auto sm = smoothFractionalOctave (result.leftAfter, 3.0);
            double off = 0.0;
            int n = 0;
            for (size_t i = 0; i < sm.size(); ++i)
                if (sm.freqs[i] >= 200.0 && sm.freqs[i] <= 2000.0)
                    { off += sm.magnitudeDb (i) - targetDb[i]; ++n; }
            off /= std::max (1, n);
            for (auto& t : targetDb)
                t += off;
        }

        result.leftErrorDb = sideError (result.leftAfter, targetDb);
        result.rightErrorDb = sideError (result.rightAfter, targetDb);

        if (pass == options.maxVerifyPasses
            || (result.leftErrorDb <= options.acceptableErrorDb && result.rightErrorDb <= options.acceptableErrorDb))
            break;

        // Closed loop: fit a few broad corrections to what was actually measured and give each
        // band to the driver that dominates that frequency (if it has a free EQ slot).
        progress (0.3 + 0.25 * pass, "Correcting residual error (pass " + juce::String (pass + 1) + ")");
        bool changed = false;
        for (auto side : { Side::left, Side::right })
        {
            const auto& measured = side == Side::left ? result.leftAfter : result.rightAfter;
            const auto sideTarget = (side == Side::right && options.matchRightToLeft)
                                        ? smoothFractionalOctave (result.leftAfter, 3.0).magnitudeDb() : targetDb;

            AutoEQ::Settings s;
            s.sampleRate = config.caps.sampleRate;
            s.fLow = 60.0;
            s.fHigh = 10000.0;
            s.maxBands = 3;
            s.maxBoostDb = 3.0;
            s.maxCutDb = -6.0;
            s.minQ = std::max (0.7, config.caps.peqMinQ);
            s.maxQ = std::min (4.0, config.caps.peqMaxQ);
            s.gainStepDb = config.caps.gainStepDb;

            // Fit relative to the anchored target, ignoring the absolute level.
            auto rel = sideTarget;
            const auto sm = smoothFractionalOctave (measured, 3.0);
            double off = 0.0;
            int n = 0;
            for (size_t i = 0; i < sm.size(); ++i)
                if (sm.freqs[i] >= 200.0 && sm.freqs[i] <= 2000.0)
                    { off += sm.magnitudeDb (i) - rel[i]; ++n; }
            off /= std::max (1, n);
            for (auto& t : rel)
                t += off;

            if (config.caps.isGraphic())
            {
                // Graphic EQ: no bands can be added - fit a small correction on the same fixed
                // centres and add it to the existing gains of the driver that owns each centre
                // (or of every channel on the side when the GEQ is shared per side).
                GraphicEQ::Settings gs;
                gs.fLow = s.fLow;
                gs.fHigh = s.fHigh;
                gs.maxBoostDb = s.maxBoostDb;
                gs.maxCutDb = s.maxCutDb;
                gs.smoothness = 0.03;
                const auto fix = GraphicEQ::solve (measured, rel, config.caps, gs);
                const auto centres = config.caps.graphicCentres();
                const double maxBoost = std::min (config.safety.maxBoostDb, config.caps.graphicMaxBoostDb);

                auto adjust = [&] (OutputChannel& c, size_t j, double delta)
                {
                    if (c.settings.peq.size() != centres.size())
                        return;
                    auto g = GraphicEQ::gainsOf (config.caps, c.settings.peq);
                    const double ng = GraphicEQ::quantise (config.caps, g[j] + delta, config.caps.graphicMaxCutDb, maxBoost);
                    if (std::abs (ng - g[j]) < 1e-9)
                        return;
                    g[j] = ng;
                    c.settings.peq = GraphicEQ::makeGraphic (config.caps, g);
                    changed = true;
                };

                for (size_t j = 0; j < centres.size() && j < fix.gainsDb.size(); ++j)
                {
                    if (std::abs (fix.gainsDb[j]) < 1e-9)
                        continue;
                    if (! config.caps.graphicPerChannel)
                    {
                        for (auto& c : config.channels)
                            if (c.side == side)
                                adjust (c, j, fix.gainsDb[j]);
                        continue;
                    }
                    OutputChannel* owner = nullptr;
                    for (auto& c : config.channels)
                    {
                        if (! (c.side == side || (c.side == Side::mono && centres[j] < 100.0)))
                            continue;
                        const double lo = (c.settings.highPass.enabled && c.role != DriverRole::subwoofer) ? c.settings.highPass.freq : 0.0;
                        const double hi = c.settings.lowPass.enabled ? c.settings.lowPass.freq : 1.0e6;
                        if (centres[j] >= lo && centres[j] < hi)
                            owner = &c;
                    }
                    if (owner != nullptr)
                        adjust (*owner, j, fix.gainsDb[j]);
                }
                continue;
            }

            const auto fix = AutoEQ::solve (measured, rel, s);
            for (auto& band : fix.filters)
            {
                OutputChannel* owner = nullptr;
                for (auto& c : config.channels)
                {
                    if (! (c.side == side || (c.side == Side::mono && band.freq < 100.0)))
                        continue;
                    const double lo = (c.settings.highPass.enabled && c.role != DriverRole::subwoofer) ? c.settings.highPass.freq : 0.0;
                    const double hi = c.settings.lowPass.enabled ? c.settings.lowPass.freq : 1.0e6;
                    if (band.freq >= lo && band.freq < hi)
                        owner = &c;
                }
                if (owner != nullptr && (int) owner->settings.peq.size() < config.caps.peqBandsPerChannel)
                {
                    owner->settings.peq.push_back (band);
                    changed = true;
                }
            }
        }

        if (! changed)
            break;
        if (! dsp->applyAll (config, error) || ! dsp->soloChannels (config, allIds, error))
            return fail (error);
    }

    progress (0.9, "Measuring both sides together (centre image)");
    result.bothAfter = measureSide (Feed::both, allIds, "Verify L+R", result.measurements, error);
    if (! dsp->soloChannels (config, allIds, error))
        return fail (error);
    progress (1.0, "Verified");

    //==========================================================================
    setStage (Stage::report);
    {
        const auto l = smoothFractionalOctave (result.leftAfter, 3.0);
        const auto r = smoothFractionalOctave (result.rightAfter, 3.0);
        double acc = 0.0;
        int n = 0;
        for (size_t i = 0; i < l.size(); ++i)
            if (l.freqs[i] >= 100.0 && l.freqs[i] <= 10000.0)
                { acc += std::pow (l.magnitudeDb (i) - r.magnitudeDb (i), 2.0); ++n; }
        result.matchDb = std::sqrt (acc / std::max (1, n));
    }

    int bigDips = 0;
    for (auto& [f, d] : result.report.sumDips)
        if (d >= 9.0)
            ++bigDips;

    const double avgErr = 0.5 * (result.leftErrorDb + result.rightErrorDb);
    result.score = juce::jlimit (0, 100, (int) std::round (100.0 - 9.0 * std::max (0.0, avgErr - 0.5)
                                                                 - 8.0 * std::max (0.0, result.matchDb - 0.5)
                                                                 - 5.0 * bigDips
                                                                 - 1.0 * result.report.warnings.size()));
    result.targetDb = targetDb;
    result.tunedConfig = config;
    progress (1.0, "Score " + juce::String (result.score) + " / 100");
    return true;
}
} // namespace fa
