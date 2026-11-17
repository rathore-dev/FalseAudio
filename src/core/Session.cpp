#include "Session.h"

namespace fa
{
Session::Session()
{
    config = SystemConfig::makeLayout (3, true);
    autoTune.addListener (this);

    juce::PropertiesFile::Options o;
    o.applicationName = "FalseAudio";
    o.filenameSuffix = ".settings";
    o.folderName = "FalseAudio";
    o.osxLibrarySubFolder = "Application Support";
    settings = std::make_unique<juce::PropertiesFile> (o);
}

Session::~Session()
{
    autoTune.removeListener (this);
    autoTune.cancel();
    measureCancel = true;
    if (measureThread != nullptr)
        measureThread->stopThread (5000);
    finishDspApply (true);
    for (auto& t : orphanedApplyThreads)
        t->stopThread (10000);
    audio.stopPlayback();
    saveSettings();
}

juce::File Session::getAppDataFolder() const
{
    auto f = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("FalseAudio");
    f.createDirectory();
    return f;
}

void Session::initialise()
{
    std::unique_ptr<juce::XmlElement> deviceState;
    if (settings != nullptr)
        deviceState = settings->getXmlValue ("audioDevice");
    audio.initialise (deviceState.get());

    loadSettings();
    rebuildDemoRig();
    sendChangeMessage();
}

void Session::loadSettings()
{
    if (settings == nullptr)
        return;

    mode = settings->getBoolValue ("liveMode", false) ? Mode::live : Mode::demo;

    const auto cfgJson = settings->getValue ("config");
    SystemConfig loaded;
    if (cfgJson.isNotEmpty() && exporters::fromJSON (cfgJson, loaded))
        config = loaded;

    audio.setCaptureMode ((CaptureMode) juce::jlimit (0, 2, settings->getIntValue ("captureMode", 0)));
    const auto routing = juce::JSON::parse (settings->getValue ("routing"));
    if (routing.isObject())
        audio.setRouting (AudioRouting::fromVar (routing));

    for (int i = 0; i < 8; ++i)
    {
        const auto cal = juce::JSON::parse (settings->getValue ("cal" + juce::String (i)));
        if (cal.isObject())
            audio.setCalibration (i, MicCalibration::fromVar (cal));
    }

    if (mode == Mode::live)
        useManualDsp();
}

void Session::saveSettings()
{
    if (settings == nullptr)
        return;

    settings->setValue ("liveMode", mode == Mode::live);
    settings->setValue ("config", exporters::toJSON (config));
    settings->setValue ("captureMode", (int) audio.getCaptureMode());
    settings->setValue ("routing", juce::JSON::toString (audio.getRouting().toVar()));
    for (int i = 0; i < 8; ++i)
    {
        const auto cal = audio.getCalibration (i);
        if (cal.isValid())
            settings->setValue ("cal" + juce::String (i), juce::JSON::toString (cal.toVar()));
    }
    if (auto xml = audio.createStateXml())
        settings->setValue ("audioDevice", xml.get());
    settings->saveIfNeeded();
}

void Session::setCaptureMode (CaptureMode m)
{
    audio.setCaptureMode (m);
    saveSettings();
}

MicSelfTestReport Session::runMicSelfTest (double seconds, std::atomic<bool>& cancelFlag)
{
    std::vector<float> x;
    double sr = 0.0, wall = 0.0;
    juce::String err;
    // Measurement path: exclusive WASAPI when possible (bypasses Windows capture processing), restored afterwards.
    const bool pathWasOpen = audio.isMeasurementPathActive();
    bool pathFailed = false;
    const auto pathNote = audio.beginMeasurementPath (&pathFailed);
    struct Restore { AudioEngine& a; bool keep; ~Restore() { if (! keep) a.endMeasurementPath(); } } restore { audio, pathWasOpen };
    const bool ok = ! pathFailed && audio.recordInput (seconds, x, sr, cancelFlag, err, &wall);
    if (! ok)
    {
        MicSelfTestReport r;
        r.message = pathFailed ? pathNote : err;
        setCaptureWarning (pathNote);
        return r;
    }
    const auto cal = audio.getCalibration (0);
    juce::String devName;
    if (auto* dev = audio.getDeviceManager().getCurrentAudioDevice())
        devName = dev->getName();
    const auto ep = endpointVolumeForDevice (devName);
    // Never fall back to the UMIK's endpoint for another device: its Windows level is not this mic's.
    auto r = analyseMicRecording (x, sr, cal.isValid() ? &cal : nullptr, ep.valid ? &ep : nullptr);
    if (wall > 0.0)
        r.measuredRate = (double) x.size() / wall;
    juce::String warn = pathNote;
    if (r.processingSuspected)
        warn = (warn + " " + kProcessingWarning).trim();
    setCaptureWarning (warn);
    if (pathNote.isNotEmpty() && r.verdict != MicSelfTestReport::Verdict::noData)
        r.message = (r.message + " " + pathNote).trim();
    return r;
}

//==============================================================================
void Session::setMode (Mode m)
{
    if (m == mode || autoTune.isRunning())
        return;

    mode = m;
    if (mode == Mode::live && liveDsp == nullptr)
        useManualDsp();
    if (mode == Mode::demo)
        rebuildDemoRig();
    saveSettings();
    sendChangeMessage();
}

void Session::rebuildDemoRig()
{
    finishDspApply();
    demoRig = std::make_shared<VirtualRig> (config, 1);
    // A realistic installer mistake for the demo to find.
    for (auto& c : config.channels)
        if (c.side == Side::right && c.role == DriverRole::midrange)
            demoRig->getCar().driver (c.id).wiredInverted = true;
}

bool Session::setConfig (const SystemConfig& c)
{
    if (isConfigLocked())
        return false;
    ++configRevision;
    config = c;
    imageBalanceDb = 0.0;
    rebuildDemoRig();
    saveSettings();
    sendChangeMessage();
    return true;
}

bool Session::setLayout (int ways, bool withSub, bool stereoSub)
{
    auto fresh = SystemConfig::makeLayout (ways, withSub, stereoSub);
    fresh.name = config.name;
    fresh.caps = config.caps;
    fresh.safety = config.safety;
    fresh.target = config.target;
    return setConfig (fresh);
}

bool Session::setTarget (const TargetCurve& t)
{
    if (isConfigLocked())
        return false;
    ++configRevision;
    config.target = t;
    saveSettings();
    sendChangeMessage();
    return true;
}

bool Session::updateChannel (const OutputChannel& ch)
{
    if (isConfigLocked())
        return false;
    if (auto* c = config.find (ch.id))
    {
        ++configRevision;
        *c = ch;
        saveSettings();
        sendChangeMessage();
        return true;
    }
    return false;
}

void Session::setRouting (const AudioRouting& r)
{
    audio.setRouting (r);
    saveSettings();
    sendChangeMessage();
}

bool Session::loadCalibration (int micIndex, const juce::File& file, juce::String& error)
{
    auto cal = MicCalibration::load (file);
    if (! cal.isValid())
    {
        error = "No calibration data found in " + file.getFileName();
        return false;
    }
    audio.setCalibration (micIndex, cal);
    saveSettings();
    sendChangeMessage();
    return true;
}

//==============================================================================
bool Session::useManualDsp()
{
    if (autoTune.isRunning())
        return false;
    finishDspApply();
    retireDsp();
    liveDsp = std::make_shared<ManualDspController> (config.caps, [this] (const juce::String& title, const juce::StringArray& steps)
    {
        AutoTuneEngine::UserAction a;
        a.kind = AutoTuneEngine::UserAction::Kind::manualDsp;
        a.title = title;
        a.message = "Enter these values in your DSP software, then press Done.";
        a.steps = steps;
        return autoTune.askUser (a);
    });
    liveDspKind = DspKind::manual;
    sendChangeMessage();
    return true;
}

bool Session::useSoftwareDsp()
{
    if (autoTune.isRunning())
        return false;
    auto caps = SoftwareDspController::defaultCapabilities();
    if (const double fs = audio.getSampleRate(); fs > 0.0)
        caps.sampleRate = fs;
    auto c = std::make_shared<SoftwareDspController> (caps);
    finishDspApply();
    retireDsp();
    liveDsp = c;
    liveDspKind = DspKind::softwareDsp;
    audio.setOutputProcessor (c);
    config.caps = c->getCapabilities();
    sendChangeMessage();
    return true;
}

SoftwareDspController* Session::getSoftwareDsp() noexcept
{
    return liveDspKind == DspKind::softwareDsp ? dynamic_cast<SoftwareDspController*> (liveDsp.get()) : nullptr;
}

bool Session::useNativeMiniDsp (const MiniDspController::Options& options, juce::String& error)
{
    if (autoTune.isRunning()) { error = configLockedMessage(); return false; }
    auto c = std::make_unique<MiniDspController> (options);
    if (! c->connect (error))
        return false;
    config.caps = c->getCapabilities();
    return setDspController (std::move (c), DspKind::nativeMiniDsp);
}

bool Session::useNativeDsp408 (const Dsp408Controller::Options& options, juce::String& error)
{
    if (autoTune.isRunning()) { error = configLockedMessage(); return false; }
    auto c = std::make_unique<Dsp408Controller> (options);
    if (! c->connect (error))
        return false;
    config.caps = c->getCapabilities();
    return setDspController (std::move (c), DspKind::nativeDsp408);
}

bool Session::useAiOperator (AiDspController::Options options, juce::String& error)
{
    if (autoTune.isRunning()) { error = configLockedMessage(); return false; }
    if (options.outputs.empty())
        options.outputs = config.channels;
    if (options.agent.verify == nullptr)
        options.agent.verify = verifyChannelHandler;
    if (options.agent.verify == nullptr)
    {
        // Default: the real verifier on the current measurement source (Demo: the virtual rig).
        ChannelVerifier::Options vo;
        vo.shouldAbort = [this] { auto* ai = getAiOperator(); return ai != nullptr && ai->getGuard().isKilled(); };
        // The previous verifier stays alive: an old agent's verify lambda may still point at it.
        if (channelVerifier != nullptr)
            retiredVerifiers.push_back (channelVerifier);
        channelVerifier = std::make_shared<ChannelVerifier> ([this]() -> MeasurementSource& { return getSource(); },
                                                             [this]() -> const SystemConfig& { return config; }, vo);
        options.agent.verify = channelVerifier->asVerifyFn();
    }
    auto c = std::make_unique<AiDspController> (options);
    if (! c->connect (error))
        return false;
    config.caps = c->getCapabilities();
    return setDspController (std::move (c), DspKind::aiOperator);
}

AiDspController* Session::getAiOperator() noexcept
{
    return liveDspKind == DspKind::aiOperator ? dynamic_cast<AiDspController*> (liveDsp.get()) : nullptr;
}

void Session::useUIAutomationDsp (const DspProfile& profile, std::function<void*()> windowProvider)
{
    auto controller = std::make_unique<UIAutomationController> (profile, std::move (windowProvider),
        [this] (const juce::String& title, const juce::StringArray& steps)
        {
            AutoTuneEngine::UserAction a;
            a.kind = AutoTuneEngine::UserAction::Kind::manualDsp;
            a.title = title;
            a.message = "These controls haven't been taught yet. Set them in your DSP software, then press Done.";
            a.steps = steps;
            return autoTune.askUser (a);
        });
    config.caps = profile.caps;
    setDspController (std::move (controller), DspKind::uiAutomation);
}

void Session::retireDsp()
{
    audio.setOutputProcessor (nullptr);   // the built-in DSP (if any) stops shaping what the engine plays
    // An AI controller may be inside a learn thread (HTTP call): stop it and keep it alive for the session.
    if (auto ai = std::dynamic_pointer_cast<AiDspController> (liveDsp))
    {
        ai->kill ("DSP mode changed");
        retiredAi.push_back (std::move (liveDsp));
    }
    liveDsp.reset();   // anything else: workers hold it through the guard's shared ownership
}

bool Session::setDspController (std::unique_ptr<DspController> controller, DspKind kind)
{
    if (autoTune.isRunning())
        return false;
    finishDspApply();
    retireDsp();
    liveDsp = std::shared_ptr<DspController> (std::move (controller));
    liveDspKind = kind;
    sendChangeMessage();
    return true;
}

std::shared_ptr<DspController> Session::getDspShared()
{
    if (mode == Mode::demo || liveDsp == nullptr)
    {
        if (demoRig == nullptr)
            rebuildDemoRig();
        return demoRig;
    }
    return liveDsp;
}

DspController& Session::getDsp()
{
    if (mode == Mode::demo || liveDsp == nullptr)
    {
        if (demoRig == nullptr)
            rebuildDemoRig();
        return *demoRig;
    }
    return *liveDsp;
}

DspController& Session::getGuardedDsp()
{
    return *getGuardedDspShared();
}

std::shared_ptr<GuardedDspController> Session::getGuardedDspShared()
{
    auto inner = getDspShared();
    if (activeGuard == nullptr || &activeGuard->getInner() != inner.get())
    {
        // A worker holding the old guard keeps it (and the controller inside) alive.
        GuardedDspController::Options o;
        o.planner = config.safety;
        activeGuard = std::make_shared<GuardedDspController> (inner, o);
    }
    activeGuard->setPlannerLimits (config.safety);   // always the session's current limits
    return activeGuard;
}

MeasurementSource& Session::getSource()
{
    if (mode == Mode::demo)
    {
        if (demoRig == nullptr)
            rebuildDemoRig();
        return *demoRig;
    }
    return audio;
}

//==============================================================================
bool Session::startAutoTune (juce::String& error)
{
    if (autoTune.isRunning())
    {
        error = "Auto tune is already running.";
        return false;
    }
    if (measuringNow.load())
    {
        error = "Wait for the current measurement to finish.";
        return false;
    }

    stopListeningTrack();

    // Live: open the measurement path (exclusive when possible) and check the mic is not being processed by Windows.
    setCaptureWarning ({});
    tunePathOpen = false;
    if (mode == Mode::live && audio.getDeviceManager().getCurrentAudioDevice() != nullptr)
    {
        bool failed = false;
        auto note = audio.beginMeasurementPath (&failed);
        if (failed)
        {
            setCaptureWarning (note);
            error = "Could not open the audio devices for measurement. " + note;
            return false;
        }
        tunePathOpen = true;
        std::atomic<bool> noCancel { false };
        std::vector<float> quiet; double sr = 0.0; juce::String e2;
        if (audio.recordInput (1.0, quiet, sr, noCancel, e2))
        {
            const auto cal = audio.getCalibration (0);
            juce::String devName;
            if (auto* dev = audio.getDeviceManager().getCurrentAudioDevice())
                devName = dev->getName();
            const auto ep = endpointVolumeForDevice (devName);
            const auto rep = analyseMicRecording (quiet, sr, cal.isValid() ? &cal : nullptr, ep.valid ? &ep : nullptr);
            if (rep.processingSuspected)
                note = (note + " " + kProcessingWarning).trim();
        }
        setCaptureWarning (note);
    }

    if (mode == Mode::demo)
    {
        rebuildDemoRig();
        autoTune.onMicPositionChanged = [rig = demoRig.get()] (int p) { rig->setMicPosition (p); };
    }
    else
    {
        autoTune.onMicPositionChanged = nullptr;
    }

    // The worker uses the guard (-> DSP) and, in Demo, the rig as source: hold them until it finishes.
    tuneGuard = getGuardedDspShared();
    tuneRig = mode == Mode::demo ? demoRig : nullptr;
    tuneStartRevision = configRevision;
    if (! autoTune.start (config, getSource(), *tuneGuard, autoTuneOptions))
    {
        tuneGuard.reset();
        tuneRig.reset();
        if (tunePathOpen) { audio.endMeasurementPath(); tunePathOpen = false; }
        error = "Could not start the auto tune.";
        return false;
    }
    sendChangeMessage();   // lets the UI lock its config controls
    return true;
}

void Session::autoTuneMeasurement (const Measurement& m)
{
    addMeasurement (m);
}

void Session::autoTuneFinished (const AutoTuneEngine::Result& r)
{
    tuneResult = r;
    hasResult = r.success;
    // Edits are refused while a tune runs, so the revision only differs if something bypassed that:
    // then the user's config wins and the result stays available in tuneResult only.
    if (r.success && configRevision == tuneStartRevision)
    {
        config = r.tunedConfig;
        ++configRevision;
        imageBalanceDb = 0.0;
        saveSettings();
    }
    // The worker is past its last DSP/source use when it reports its result.
    tuneGuard.reset();
    tuneRig.reset();
    if (tunePathOpen)
    {
        // Restore the normal device type/setup (hands exclusive devices back to other applications).
        tunePathOpen = false;
        audio.endMeasurementPath();
    }
    sendChangeMessage();
}

//==============================================================================
void Session::addMeasurement (const Measurement& m)
{
    measurements.push_back (m);
    // Keep the library bounded (IRs are large).
    if (measurements.size() > 400)
        measurements.erase (measurements.begin());
    sendChangeMessage();
}

void Session::removeMeasurement (const juce::String& id)
{
    measurements.erase (std::remove_if (measurements.begin(), measurements.end(),
                                        [&] (const Measurement& m) { return m.id == id; }),
                        measurements.end());
    sendChangeMessage();
}

void Session::clearMeasurements()
{
    measurements.clear();
    sendChangeMessage();
}

namespace
{
    class OneShotThread : public juce::Thread
    {
    public:
        explicit OneShotThread (std::function<void()> f) : juce::Thread ("FalseAudio measure"), fn (std::move (f)) {}
        void run() override { fn(); }
    private:
        std::function<void()> fn;
    };
}

void Session::measureOnce (Feed feed, const juce::String& label,
                           std::function<void (const std::vector<Measurement>&, const juce::String&)> onDone)
{
    if (measuringNow.load() || autoTune.isRunning())
    {
        if (onDone != nullptr)
            onDone ({}, "Busy.");
        return;
    }

    if (measureThread != nullptr)
        measureThread->stopThread (2000);

    stopListeningTrack();
    measuringNow = true;
    measureCancel = false;

    auto* source = &getSource();
    const auto sampleRate = source->getSampleRate();
    std::shared_ptr<VirtualRig> rigKeep = mode == Mode::demo ? demoRig : nullptr;   // keeps the demo source alive

    measureThread = std::make_unique<OneShotThread> ([this, source, sampleRate, feed, label, onDone, rigKeep]
    {
        SweepRequest req;
        req.sweep.sampleRate = sampleRate;
        req.sweep.seconds = autoTuneOptions.sweepSeconds;
        req.sweep.levelDbFS = autoTuneOptions.sweepLevelDbFS;
        req.sweep.fEnd = std::min (24000.0, sampleRate * 0.48);
        req.feed = feed;
        req.label = label;

        juce::String err;
        const bool live = source == &audio;
        const bool pathWasOpen = audio.isMeasurementPathActive();
        bool pathFailed = false;
        juce::String pathNote;
        if (live)
            pathNote = audio.beginMeasurementPath (&pathFailed);
        setCaptureWarning (pathNote);
        std::vector<Measurement> ms;
        if (pathFailed)
            err = "Could not open the audio devices for measurement. " + pathNote;
        else
            ms = source->measureSweep (req, measureCancel, err);
        if (live && ! pathWasOpen)
            audio.endMeasurementPath();

        juce::MessageManager::callAsync ([this, ms, err, onDone]
        {
            for (auto& m : ms)
                addMeasurement (m);
            measuringNow = false;
            if (onDone != nullptr)
                onDone (ms, err);
        });
    });
    measureThread->startThread();
}

//==============================================================================
void Session::playListeningTrack (ListeningTrack track, double levelDbFS)
{
    if (autoTune.isRunning() || measuringNow.load())
        return;

    auto t = ListeningTracks::make (track, audio.getSampleRate(), levelDbFS);
    audio.playStereo (std::move (t.left), std::move (t.right), t.loop);
    currentTrack = track;
    sendChangeMessage();
}

void Session::stopListeningTrack()
{
    const bool wasPlaying = audio.isPlayingBack();
    audio.stopPlayback();
    currentTrack.reset();
    if (wasPlaying)
        sendChangeMessage();
}

juce::StringArray Session::nudgeImage (double db)
{
    juce::StringArray steps;
    if (autoTune.isRunning() || std::abs (db) < 1.0e-6)
        return steps;

    // Only one side is ever lowered; nudging back restores it before cutting the other side.
    const double before = imageBalanceDb;
    imageBalanceDb = juce::jlimit (-20.0, 20.0, before + db);
    const double leftChange  = std::max (before, 0.0) - std::max (imageBalanceDb, 0.0);
    const double rightChange = std::max (-before, 0.0) - std::max (-imageBalanceDb, 0.0);

    std::vector<OutputChannel> changed;
    for (auto& c : config.channels)
    {
        const double d = c.side == Side::left ? leftChange : c.side == Side::right ? rightChange : 0.0;
        if (std::abs (d) < 1.0e-9)
            continue;

        const double old = c.settings.gainDb;
        c.settings.gainDb = juce::jlimit (config.caps.minChannelGainDb, config.caps.maxChannelGainDb, old + d);
        steps.add (c.label + ": gain " + juce::String (old, 1) + " dB -> " + juce::String (c.settings.gainDb, 1) + " dB");
        changed.push_back (c);
    }

    if (! changed.empty())
    {
        if (getDsp().isAutomatic())
            applyChannelsInBackground (changed);
        saveSettings();
        sendChangeMessage();
    }
    return steps;
}

juce::StringArray Session::pushConfigToDsp (bool& automatic)
{
    juce::StringArray steps;
    automatic = false;
    if (autoTune.isRunning())
        return steps;

    auto& dsp = getDsp();
    if (dsp.isAutomatic())
    {
        automatic = true;
        applyChannelsInBackground (config.channels);
        return steps;
    }

    const auto caps = dsp.getCapabilities();
    for (auto& ch : config.channels)
    {
        steps.add ("--- " + ch.label + " (output " + juce::String (ch.dspOutput + 1) + ")");
        steps.addArray (ManualDspController::describeChannel (ch, &caps));
    }
    return steps;
}

void Session::finishDspApply (bool wait)
{
    {
        const juce::ScopedLock sl (dspApplyLock);
        dspApplyQueue.clear();
        dspApplyStale->store (true);          // a running worker stops after its current write
        dspApplyBusy = false;
        dspApplyStale = std::make_shared<std::atomic<bool>> (false);
    }
    if (dspApplyThread != nullptr)
    {
        if (wait)
            dspApplyThread->stopThread (10000);
        else if (dspApplyThread->isThreadRunning())
            orphanedApplyThreads.push_back (std::move (dspApplyThread));   // never block the message thread
        dspApplyThread.reset();
    }
    orphanedApplyThreads.erase (std::remove_if (orphanedApplyThreads.begin(), orphanedApplyThreads.end(),
                                                [] (auto& t) { return ! t->isThreadRunning(); }),
                                orphanedApplyThreads.end());
}

void Session::applyChannelsInBackground (const std::vector<OutputChannel>& channels)
{
    const juce::ScopedLock sl (dspApplyLock);
    for (auto& c : channels)
        dspApplyQueue[c.id] = c;

    if (dspApplyBusy)
        return; // the running worker picks the new values up

    if (dspApplyThread != nullptr)
        dspApplyThread->waitForThreadToExit (2000); // it has already drained the queue and is returning

    dspApplyBusy = true;
    auto dsp = getGuardedDspShared();               // the worker owns the guard (and the DSP inside it)
    auto stale = dspApplyStale;
    dspApplyThread = std::make_unique<OneShotThread> ([this, dsp, stale]
    {
        for (;;)
        {
            std::map<juce::String, OutputChannel> work;
            {
                const juce::ScopedLock lock (dspApplyLock);
                if (stale->load())
                    return;                          // the DSP was replaced: queue and busy flag belong to a newer worker
                if (dspApplyQueue.empty())
                {
                    dspApplyBusy = false;
                    return;
                }
                work.swap (dspApplyQueue);
            }
            for (auto& [id, ch] : work)
            {
                if (stale->load())
                    return;
                juce::String err;
                if (! dsp->applyChannel (ch, err))
                {
                    DBG ("DSP write failed for " + id + ": " + err);
                }
            }
        }
    });
    dspApplyThread->startThread();
}

//==============================================================================
bool Session::saveSession (const juce::File& f, juce::String& error) const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("format", "FalseAudio session 1");
    o->setProperty ("config", config.toVar());
    o->setProperty ("routing", audio.getRouting().toVar());

    juce::Array<juce::var> ms;
    for (auto& m : measurements)
        ms.add (m.toVar (true));
    o->setProperty ("measurements", ms);

    if (! f.replaceWithText (juce::JSON::toString (juce::var (o), true)))
    {
        error = "Could not write " + f.getFullPathName();
        return false;
    }
    return true;
}

bool Session::loadSession (const juce::File& f, juce::String& error)
{
    if (autoTune.isRunning() || measuringNow.load())
    {
        error = autoTune.isRunning() ? juce::String (configLockedMessage()) : juce::String ("Wait for the current measurement to finish.");
        return false;
    }
    const auto v = juce::JSON::parse (f.loadFileAsString());
    if (! v.isObject() || ! v.getProperty ("format", {}).toString().startsWith ("FalseAudio session"))
    {
        error = f.getFileName() + " is not a FalseAudio session.";
        return false;
    }

    ++configRevision;
    config = SystemConfig::fromVar (v["config"]);
    audio.setRouting (AudioRouting::fromVar (v["routing"]));

    measurements.clear();
    if (auto* ms = v["measurements"].getArray())
    {
        for (auto& mv : *ms)
        {
            auto m = Measurement::fromVar (mv);
            const auto cal = audio.getCalibration (m.micPosition);
            m.analyse (cal.isValid() ? &cal : nullptr);
            measurements.push_back (std::move (m));
        }
    }

    rebuildDemoRig();
    saveSettings();
    sendChangeMessage();
    return true;
}
} // namespace fa
