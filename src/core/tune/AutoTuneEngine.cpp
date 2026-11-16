#include "AutoTuneEngine.h"
#include "GraphicEQ.h"
#include "../dsp/Spectrum.h"
#include "TargetLibrary.h"
#include "PassiveNotch.h"
#include "MethodologyStages.h"

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
        case Stage::sumCheck:   return "L+R check";
        case Stage::imaging:    return "Imaging";
        case Stage::apply:      return "Apply";
        case Stage::verify:     return "Verify";
        case Stage::noise:      return "Noise";
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
double AutoTuneEngine::sweepLevelForRole (DriverRole role, double requestedDbFS, const SafetyLimits& safety)
{
    return method::sweepLevelFor (role, requestedDbFS, safety.sweepLevelDbFS);
}

double AutoTuneEngine::levelForChannel (const OutputChannel& c) const
{
    return sweepLevelForRole (c.role, options.sweepLevelDbFS, config.safety);
}

double AutoTuneEngine::levelForIds (const juce::StringArray& ids) const
{
    double level = std::min (0.0, options.sweepLevelDbFS);
    for (auto& c : config.channels)
        if (ids.contains (c.id))
            level = std::min (level, levelForChannel (c));
    return level;
}

std::vector<Measurement> AutoTuneEngine::sweep (Feed feed, const juce::String& label, juce::String& error, double levelDbFS)
{
    SweepRequest req;
    req.sweep.seconds = options.sweepSeconds;
    req.sweep.levelDbFS = levelDbFS;
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

    auto ms = sweep (feed, label, error, levelForIds (ids));
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
        if (sm.freqs[i] >= evalLowHz && sm.freqs[i] <= 12000.0)
            { acc += std::pow (sm.magnitudeDb (i) - targetDb[i] - ref, 2.0); ++n; }
    return std::sqrt (acc / std::max (1, n));
}

//==============================================================================
/** Everything the stages hand to each other during one run. */
struct AutoTuneEngine::RunState
{
    explicit RunState (Result& r) : result (r) {}

    Result& result;
    juce::StringArray allIds;
    SystemConfig protectedConfig;
    juce::StringArray topologyNotes, crossoverNotes, measureWarnings;
    DriverDataMap data;
    std::unique_ptr<SystemSolver> solver;
    advanced::Outcome refined;
    std::vector<double> targetDb;
    juce::String error;

    bool fail (const juce::String& e) { result.error = e; return false; }
};

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
    RunState rs (result);
    for (auto& c : config.channels)
        rs.allIds.add (c.id);

    auto cancelled = [this] { return cancelFlag.load() || threadShouldExit(); };

    if (! stagePreflight (rs))  return false;
    if (cancelled()) return false;
    if (! stageProtect (rs))    return false;
    if (cancelled()) return false;
    if (! stageMeasure (rs))    return false;
    if (cancelled()) return false;
    if (! stageCrossovers (rs)) return false;
    if (cancelled()) return false;
    if (! stageSolve (rs))      return false;
    if (cancelled()) return false;
    if (! stageSumCheck (rs))   return false;
    if (cancelled()) return false;
    if (! stageImaging (rs))    return false;
    if (cancelled()) return false;
    if (! stageApply (rs))      return false;
    if (cancelled()) return false;
    if (! stageVerify (rs))     return false;
    if (cancelled()) return false;
    if (! stageNoise (rs))      return false;
    if (cancelled()) return false;
    return stageReport (rs);
}

//==============================================================================
bool AutoTuneEngine::stagePreflight (RunState& rs)
{
    setStage (Stage::preflight);
    progress (0.0, "Checking audio device");
    if (! source->isReady (rs.error))
        return rs.fail (rs.error);

    progress (0.4, "Connecting to " + dsp->getName());
    if (! dsp->connect (rs.error))
        return rs.fail ("DSP: " + rs.error);

    config.caps = dsp->getCapabilities();
    config.caps.sampleRate = config.caps.sampleRate > 0 ? config.caps.sampleRate : source->getSampleRate();
    progress (1.0, "Ready");
    return true;
}

bool AutoTuneEngine::stageProtect (RunState& rs)
{
    setStage (Stage::protect);
    progress (0.0, "Applying protective crossovers (tweeters safe)");
    for (auto& c : config.channels)
        c.settings = {};
    const auto safeDecisions = CrossoverPlanner::planDefaults (config);
    CrossoverPlanner::apply (config, safeDecisions);

    if (! dsp->applyAll (config, rs.error))
        return rs.fail ("Could not write protective settings: " + rs.error);

    rs.protectedConfig = config;

    // Topology notes shown at the top of the report (what is skipped and why).
    int passiveSets = 0, fullRanges = 0, active = 0;
    for (auto& c : config.channels)
    {
        if (c.isPassiveSet()) ++passiveSets;
        else if (c.role == DriverRole::fullrange) ++fullRanges;
        else if (c.role != DriverRole::subwoofer) ++active;
    }
    if (passiveSets > 0)
        rs.topologyNotes.add ("Topology: " + juce::String (passiveSets) + " passive set(s) (woofer + tweeter behind a passive crossover). "
                              "Skipped for them: crossover points, tweeter delay, per-driver levels. Tuned instead: woofer-protecting HP, "
                              "delay, level, polarity and EQ of the whole set.");
    if (fullRanges > 0)
        rs.topologyNotes.add ("Topology: full-range driver(s). No crossover stage; the HP comes from the usable range of the driver.");
    if (! config.isStereo())
        rs.topologyNotes.add ("Topology: single side / single driver. Left-right polarity, matching, centre image and L+R sum checks are skipped.");
    if (passiveSets > 0 && active > 0)
        rs.topologyNotes.add ("Mixed system: active drivers keep the full crossover and phase optimisation.");
    if (! config.hasSubwoofer())
        rs.topologyNotes.add ("No subwoofer: bass below the lowest high-pass is not scored or boosted.");

    progress (1.0, "Protected");
    return true;
}

//==============================================================================
/** Signal check on a safe driver: enough SNR, no clipping. The level can climb only up to the
    probe's role cap and the safety ceiling (SafetyLimits::sweepLevelDbFS), never beyond. */
bool AutoTuneEngine::levelCheck (RunState& rs)
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
        const double level = levelForChannel (*probe);
        const double ceiling = std::min (method::sweepCapDbFS (probe->role), config.safety.sweepLevelDbFS);
        progress (0.0, "Level check (" + juce::String (juce::roundToInt (level)) + " dBFS)");
        if (! dsp->soloChannels (config, { probe->id }, rs.error))
            return rs.fail (rs.error);

        rs.error.clear();
        auto ms = sweep (probe->side == Side::right ? Feed::right : Feed::left, "Level check", rs.error, level);
        if (cancelFlag.load() || threadShouldExit()) return false;
        if (ms.empty())
            return rs.fail (rs.error);

        const auto& m = ms.front();
        lastClipped = m.clipped;
        lastSnr = m.snrDb;
        for (auto& other : ms)
        {
            lastClipped = lastClipped || other.clipped;
            lastSnr = std::min (lastSnr, other.snrDb);
        }
        if (lastClipped && level > -40.0)
        {
            options.sweepLevelDbFS = std::min (options.sweepLevelDbFS, level) - 6.0;
            continue;
        }
        if (lastSnr < 45.0 && level < ceiling - 1e-9)
        {
            options.sweepLevelDbFS = std::min (ceiling, level + std::min (12.0, 45.0 - lastSnr));
            continue;
        }
        break;
    }

    const int shown = juce::roundToInt (probe != nullptr ? levelForChannel (*probe) : options.sweepLevelDbFS);

    // Don't tune on data that can't be trusted.
    if (lastClipped)
        return rs.fail ("The microphone input still clips at " + juce::String (shown)
                        + " dBFS. Lower the mic preamp gain (or the amp gain) and try again.");
    if (lastSnr < 15.0)
        return rs.fail ("Almost no signal reached the microphone (signal-to-noise " + juce::String (juce::roundToInt (lastSnr))
                        + " dB). Check the mic input, the DSP input routing, that the amp is on and the volume is up.");
    return true;
}

bool AutoTuneEngine::measureDrivers (RunState& rs)
{
    auto& result = rs.result;
    const int mics = std::max (1, source->numMicrophones());
    const int passes = std::max (1, (options.micPositions + mics - 1) / mics);
    const int total = passes * (int) config.channels.size();
    int done = 0;

    for (auto& c : config.channels)
        rs.data[c.id].measuredWith = c.settings;

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
            if (cancelFlag.load() || threadShouldExit()) return false;

            progress ((double) done / total, "Measuring " + c.label + (passes > 1 ? " @ position " + juce::String (pass + 1) : juce::String()));
            if (! dsp->soloChannels (config, { c.id }, rs.error))
                return rs.fail (rs.error);

            rs.error.clear();
            const Feed feed = c.side == Side::left ? Feed::left : c.side == Side::right ? Feed::right : Feed::both;
            auto ms = sweep (feed, c.id, rs.error, levelForChannel (c));
            if (ms.empty())
                return rs.fail (c.label + ": " + rs.error);

            for (auto& m : ms)
            {
                if (m.snrDb < 25.0)
                    rs.measureWarnings.add (c.label + ": low signal-to-noise (" + juce::String (juce::roundToInt (m.snrDb))
                                            + " dB). Is the speaker connected and the amp on?");
                if (m.clipped)
                    rs.measureWarnings.add (c.label + ": the mic input clipped during this sweep - its response may be wrong.");
                rs.data[c.id].add (m);
                result.measurements.push_back (m);
            }
            ++done;
        }
    }
    progress (1.0, "All drivers measured");
    return true;
}

bool AutoTuneEngine::stageMeasure (RunState& rs)
{
    setStage (Stage::measure);
    if (! levelCheck (rs))
        return false;
    if (! measureDrivers (rs))
        return false;

    // "Before" curves: what the system sounded like with only protective crossovers.
    SystemConfig before = rs.protectedConfig;
    SystemSolver predictor (before, rs.data, {});
    std::vector<FrequencyResponse> l, r;
    for (int p = 0; p < predictor.numPositions(); ++p)
    {
        l.push_back (predictor.predictSum (predictor.sideChannels (Side::left), p));
        r.push_back (predictor.predictSum (predictor.sideChannels (Side::right), p));
    }
    rs.result.leftBefore = powerAverage (l);
    rs.result.rightBefore = powerAverage (r);
    return true;
}

//==============================================================================
bool AutoTuneEngine::stageCrossovers (RunState& rs)
{
    setStage (Stage::crossovers);
    if (options.refineCrossovers)
    {
        progress (0.2, "Checking each driver's usable bandwidth");
        std::map<juce::String, FrequencyResponse> responses;
        for (auto& [id, dd] : rs.data)
            responses[id] = powerAverage (dd.positions);

        const auto decisions = CrossoverPlanner::planFromMeasurements (config, responses);
        CrossoverPlanner::apply (config, decisions);
        for (auto& d : decisions)
            for (auto& r : d.reasons)
                rs.crossoverNotes.add (d.channelId + ": " + r);
    }
    for (auto& c : config.channels)
        if (c.coversWholeBand())
        {
            juce::String why = "one driver covers the whole range";
            if (c.isPassiveSet())
                why = "the internal passive crossover" + (c.passive.crossoverHz > 0.0 ? " (~" + juce::String (juce::roundToInt (c.passive.crossoverHz)) + " Hz)" : juce::String())
                      + " cannot be changed from the DSP";
            rs.crossoverNotes.add (c.id + ": crossover stage skipped, " + why);
        }
    progress (1.0, "Crossovers set (LR 24 dB/oct)");
    return true;
}

//==============================================================================
/** Methodology order: polarity, delay, crossover phase, levels, left to target, right matched to left.
    The optimiser then refines crossover frequencies and left EQ inside the same rules (sub LP = HP/sqrt 2,
    equal L/R crossovers, no delay/polarity changes), and the right side is re-matched to the left. */
bool AutoTuneEngine::stageSolve (RunState& rs)
{
    setStage (Stage::solve);
    SystemSolver::Options so;
    so.matchRightToLeft = options.matchRightToLeft;
    if (options.useBandTargets)
        if (auto* entry = TargetLibrary::findByName (config.target.name))
            so.bandTargets = entry->bands;
    rs.solver = std::make_unique<SystemSolver> (config, rs.data, so);
    auto& solver = *rs.solver;
    {
        double lowestHp = 0.0;
        for (auto& c : config.channels)
            if (c.role != DriverRole::subwoofer && c.settings.highPass.enabled)
                lowestHp = lowestHp > 0.0 ? std::min (lowestHp, c.settings.highPass.freq) : c.settings.highPass.freq;
        evalLowHz = config.hasSubwoofer() ? 40.0 : std::max (40.0, lowestHp * 1.25);
    }

    auto cancelled = [this] { return cancelFlag.load() || threadShouldExit(); };
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

    if (options.competitionRefine)
    {
        progress (0.80, "Refining crossover points and left EQ (within the methodology rules)");
        advanced::Options ao;
        ao.maxIterations = std::max (20, options.refineIterations);
        ao.allowAllPass = false;
        ao.constrained = true;
        rs.refined = advanced::refine (config, rs.data, ao);   // restores config itself if there is no gain
        if (cancelled()) return false;

        // EQ was optimised on the left: re-derive the right by matching it to the left again.
        if (options.matchRightToLeft && config.isStereo())
        {
            progress (0.92, "Matching right to the refined left");
            solver.rematchRightToLeft();
        }
    }
    rs.result.refineCostBefore = rs.refined.costBefore;
    rs.result.refineCostAfter = rs.refined.costAfter;
    progress (1.0, "Solved");
    return true;
}

bool AutoTuneEngine::stageSumCheck (RunState& rs)
{
    setStage (Stage::sumCheck);
    auto& solver = *rs.solver;
    progress (0.2, "Checking the L+R sum");
    const auto sc = method::correctSumCancellation (config, solver);
    solver.analyseSum();   // report on the final settings

    auto& result = rs.result;
    result.report = solver.getReport();
    for (auto& w : rs.measureWarnings)
        result.report.warnings.add (w);
    for (auto& n : rs.crossoverNotes)
        result.report.notes.insert (0, n);
    for (int i = rs.topologyNotes.size(); --i >= 0;)
        result.report.notes.insert (0, rs.topologyNotes[i]);
    for (auto& n : rs.refined.notes)
        result.report.notes.add (n);
    for (auto& n : sc.notes)
        result.report.notes.add (n);
    progress (1.0, sc.corrected ? "Cancellation corrected" : "L+R checked");
    return true;
}

bool AutoTuneEngine::stageImaging (RunState& rs)
{
    setStage (Stage::imaging);
    progress (0.2, "Centering the image (level only)");
    const auto im = method::balanceImagingByLevel (config, *rs.solver, rs.data);
    auto& result = rs.result;
    for (auto& n : im.notes)
        result.report.notes.add (n);

    if (im.changed)
        rs.solver->findSumDips (&result.report.leftRightMatchDb);   // the L/R match changed with the levels

    result.sqScore = advanced::judge (config, rs.data);
    result.report.notes.add ("Simulated SQ judge: " + juce::String (result.sqScore.total, 1) + " / 100");
    progress (1.0, im.changed ? "Image balanced" : "Image checked");
    return true;
}

bool AutoTuneEngine::stageApply (RunState& rs)
{
    setStage (Stage::apply);
    progress (0.1, "Writing to " + dsp->getName());
    if (! dsp->applyAll (config, rs.error))
        return rs.fail ("Could not apply the tune: " + rs.error);
    if (! dsp->soloChannels (config, rs.allIds, rs.error))
        return rs.fail (rs.error);
    progress (1.0, "Applied");
    return true;
}

//==============================================================================
juce::StringArray AutoTuneEngine::idsFor (Side side) const
{
    juce::StringArray ids;
    for (auto& c : config.channels)
        if (c.side == side || c.side == Side::mono)
            ids.add (c.id);
    return ids;
}

/** Closed loop: fit a few broad corrections to what was actually measured and give each band to the
    driver that dominates that frequency (if it has a free EQ slot). Returns true if anything changed. */
bool AutoTuneEngine::correctResiduals (const FrequencyResponse& leftMeasured, const FrequencyResponse& rightMeasured,
                                       const std::vector<double>& targetDb)
{
    const bool stereo = config.isStereo();
    bool changed = false;
    for (auto side : { Side::left, Side::right })
    {
        if (side == Side::right && ! stereo)
            continue;
        const auto& measured = side == Side::left ? leftMeasured : rightMeasured;
        const auto sideTarget = (side == Side::right && options.matchRightToLeft)
                                    ? smoothFractionalOctave (leftMeasured, 3.0).magnitudeDb() : targetDb;

        AutoEQ::Settings s;
        s.sampleRate = config.caps.sampleRate;
        s.fLow = std::max (60.0, evalLowHz * 1.5);
        s.fHigh = 10000.0;
        if (config.hasPassiveSets())
        {
            // Notches from the passive network cannot be fixed from the DSP: keep EQ out of them.
            double xo = 0.0;
            for (auto& c : config.channels)
                if (c.isPassiveSet() && c.side == side)
                    xo = c.passive.crossoverHz;
            s.noBoostRanges = passive::ranges (passive::findNotches (measured, xo));
        }
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
            changed = correctGraphic (side, measured, rel, s) || changed;
            continue;
        }

        const auto fix = AutoEQ::solve (measured, rel, s);
        for (auto& band : fix.filters)
        {
            OutputChannel* owner = nullptr;
            for (auto& c : config.channels)
            {
                if (! (c.side == side || (c.side == Side::mono && (c.role != DriverRole::subwoofer ? side == Side::left : band.freq < 100.0))))
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
    return changed;
}

/** Graphic EQ: no bands can be added - fit a small correction on the same fixed centres and add it to the
    existing gains of the driver that owns each centre (or of every channel on the side when the GEQ is
    shared per side). */
bool AutoTuneEngine::correctGraphic (Side side, const FrequencyResponse& measured, const std::vector<double>& rel,
                                     const AutoEQ::Settings& s)
{
    bool changed = false;
    GraphicEQ::Settings gs;
    gs.fLow = s.fLow;
    gs.fHigh = s.fHigh;
    gs.maxBoostDb = s.maxBoostDb;
    gs.maxCutDb = s.maxCutDb;
    gs.smoothness = 0.03;
    std::vector<double> gw;
    if (! s.noBoostRanges.empty())
    {
        gw.assign (measured.freqs.size(), 1.0);
        for (size_t q = 0; q < gw.size(); ++q)
            for (auto& r : s.noBoostRanges)
                if (measured.freqs[q] >= r.first && measured.freqs[q] <= r.second)
                    gw[q] = 0.0;
    }
    const auto fix = GraphicEQ::solve (measured, rel, config.caps, gs, gw);
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
            if (! (c.side == side || (c.side == Side::mono && (c.role != DriverRole::subwoofer ? side == Side::left : centres[j] < 100.0))))
                continue;
            const double lo = (c.settings.highPass.enabled && c.role != DriverRole::subwoofer) ? c.settings.highPass.freq : 0.0;
            const double hi = c.settings.lowPass.enabled ? c.settings.lowPass.freq : 1.0e6;
            if (centres[j] >= lo && centres[j] < hi)
                owner = &c;
        }
        if (owner != nullptr)
            adjust (*owner, j, fix.gainsDb[j]);
    }
    return changed;
}

bool AutoTuneEngine::stageVerify (RunState& rs)
{
    setStage (Stage::verify);
    auto& result = rs.result;
    auto& targetDb = rs.targetDb;
    const bool stereo = config.isStereo();

    for (int pass = 0; pass <= options.maxVerifyPasses; ++pass)
    {
        progress (0.1 + 0.25 * pass, stereo ? "Verifying left side" : "Verifying output");
        result.leftAfter = measureSide (stereo ? Feed::left : Feed::both, idsFor (Side::left), stereo ? "Verify L" : "Verify",
                                        result.measurements, rs.error);
        if (result.leftAfter.isEmpty()) return rs.fail (rs.error);
        if (cancelFlag.load() || threadShouldExit()) return false;

        if (stereo)
        {
            progress (0.2 + 0.25 * pass, "Verifying right side");
            result.rightAfter = measureSide (Feed::right, idsFor (Side::right), "Verify R", result.measurements, rs.error);
            if (result.rightAfter.isEmpty()) return rs.fail (rs.error);
            if (cancelFlag.load() || threadShouldExit()) return false;
        }
        else
        {
            result.rightAfter = result.leftAfter;   // nothing to compare against
        }

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

        progress (0.3 + 0.25 * pass, "Correcting residual error (pass " + juce::String (pass + 1) + ")");
        if (! correctResiduals (result.leftAfter, result.rightAfter, targetDb))
            break;
        if (! dsp->applyAll (config, rs.error) || ! dsp->soloChannels (config, rs.allIds, rs.error))
            return rs.fail (rs.error);
    }

    if (stereo)
    {
        progress (0.9, "Measuring both sides together (centre image)");
        result.bothAfter = measureSide (Feed::both, rs.allIds, "Verify L+R", result.measurements, rs.error);
        if (! dsp->soloChannels (config, rs.allIds, rs.error))
            return rs.fail (rs.error);
    }
    else
    {
        result.bothAfter = result.leftAfter;
    }
    progress (1.0, "Verified");
    return true;
}

//==============================================================================
/** Final noise check: with silence playing, record the mic with every output muted (baseline), then with
    each channel's output enabled. A channel that adds noise is flagged as ground-loop hum or as hiss. */
bool AutoTuneEngine::stageNoise (RunState& rs)
{
    setStage (Stage::noise);
    auto& notes = rs.result.report.notes;
    auto* probe = dynamic_cast<method::NoiseProbe*> (source);
    if (probe == nullptr)
    {
        notes.add ("Noise check: skipped, this measurement source cannot record silence.");
        progress (1.0, "Skipped");
        return true;
    }
    if (! dsp->isAutomatic())
    {
        notes.add ("Noise check: skipped, the DSP is changed by hand (it would need a mute/unmute prompt per channel).");
        progress (1.0, "Skipped");
        return true;
    }

    auto record = [&] (const juce::StringArray& ids, method::NoiseReading& out) -> bool
    {
        if (! dsp->soloChannels (config, ids, rs.error))
            return false;
        std::vector<float> x;
        double sr = 48000.0;
        juce::String err;
        if (! probe->recordSilence (x, sr, cancelFlag, err))
        {
            if (! cancelFlag.load())
                rs.error = err;
            return false;
        }
        out = method::analyseNoise (x, sr);
        return true;
    };

    method::NoiseReading baseline;
    progress (0.05, "Recording the silent floor");
    if (! record ({}, baseline))
    {
        const bool wasCancelled = cancelFlag.load();
        juce::String ignore;
        dsp->soloChannels (config, rs.allIds, ignore);
        if (wasCancelled)
            return false;
        notes.add ("Noise check: skipped (" + rs.error + ")");
        return true;
    }

    std::vector<method::NamedNoise> readings;
    int i = 0;
    for (auto& c : config.channels)
    {
        progress (0.1 + 0.85 * (double) i++ / std::max<size_t> (1, config.channels.size()), "Noise check: " + c.label);
        method::NamedNoise nn { c.id, c.label, {} };
        if (! record ({ c.id }, nn.reading))
        {
            const bool wasCancelled = cancelFlag.load();
            juce::String ignore;
            dsp->soloChannels (config, rs.allIds, ignore);
            if (wasCancelled)
                return false;
            notes.add ("Noise check: stopped at " + c.label + " (" + rs.error + ")");
            return true;
        }
        readings.push_back (std::move (nn));
    }

    if (! dsp->soloChannels (config, rs.allIds, rs.error))
        return rs.fail (rs.error);

    const auto findings = method::assessNoise (baseline, readings);
    notes.add ("Noise check: silent floor " + juce::String (baseline.rmsDbFS, 0) + " dBFS at the mic, "
               + juce::String ((int) findings.size()) + " channel(s) add noise.");
    for (auto& f : findings)
        rs.result.report.warnings.add (f.message);
    progress (1.0, findings.empty() ? "Quiet" : "Noise found");
    return true;
}

//==============================================================================
bool AutoTuneEngine::stageReport (RunState& rs)
{
    setStage (Stage::report);
    auto& result = rs.result;
    const auto& targetDb = rs.targetDb;

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

    // Score error: 1/6-oct below 1 kHz, 1/3-oct above (comb/reflection ripple up high is not tonal error).
    auto scoreError = [&] (const FrequencyResponse& r)
    {
        if (r.isEmpty())
            return 99.0;
        const auto lo = smoothFractionalOctave (r, 6.0);
        const auto hi = smoothFractionalOctave (r, 3.0);
        double ref = 0.0, acc = 0.0;
        int n = 0;
        for (size_t i = 0; i < lo.size(); ++i)
            if (lo.freqs[i] >= 200.0 && lo.freqs[i] <= 2000.0)
                { ref += (lo.freqs[i] < 1000.0 ? lo : hi).magnitudeDb (i) - targetDb[i]; ++n; }
        ref /= std::max (1, n);
        n = 0;
        for (size_t i = 0; i < lo.size(); ++i)
            if (lo.freqs[i] >= evalLowHz && lo.freqs[i] <= 12000.0)
                { acc += std::pow ((lo.freqs[i] < 1000.0 ? lo : hi).magnitudeDb (i) - targetDb[i] - ref, 2.0); ++n; }
        return std::sqrt (acc / std::max (1, n));
    };
    const double avgErr = 0.5 * (scoreError (result.leftAfter) + scoreError (result.rightAfter));
    const double measuredScore = 100.0 - 9.0 * std::max (0.0, avgErr - 0.5)
                                       - 8.0 * std::max (0.0, result.matchDb - 0.5)
                                       - 5.0 * bigDips
                                       - 1.0 * result.report.warnings.size();
    // Blend with the simulated SQ judge so the headline number and the judge agree.
    const double blended = result.sqScore.total > 0.0 ? 0.5 * measuredScore + 0.5 * result.sqScore.total : measuredScore;
    result.score = juce::jlimit (0, 100, (int) std::round (blended));
    result.targetDb = targetDb;
    result.tunedConfig = config;
    progress (1.0, "Score " + juce::String (result.score) + " / 100");
    return true;
}
} // namespace fa
