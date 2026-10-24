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
    finishDspApply();
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
    demoRig = std::make_unique<VirtualRig> (config, 1);
    // A realistic installer mistake for the demo to find.
    for (auto& c : config.channels)
        if (c.side == Side::right && c.role == DriverRole::midrange)
            demoRig->getCar().driver (c.id).wiredInverted = true;
}

void Session::setConfig (const SystemConfig& c)
{
    config = c;
    imageBalanceDb = 0.0;
    rebuildDemoRig();
    saveSettings();
    sendChangeMessage();
}

void Session::setLayout (int ways, bool withSub, bool stereoSub)
{
    auto fresh = SystemConfig::makeLayout (ways, withSub, stereoSub);
    fresh.name = config.name;
    fresh.caps = config.caps;
    fresh.safety = config.safety;
    fresh.target = config.target;
    setConfig (fresh);
}

void Session::setTarget (const TargetCurve& t)
{
    config.target = t;
    saveSettings();
    sendChangeMessage();
}

void Session::updateChannel (const OutputChannel& ch)
{
    if (auto* c = config.find (ch.id))
    {
        *c = ch;
        saveSettings();
        sendChangeMessage();
    }
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
void Session::useManualDsp()
{
    finishDspApply();
    liveDsp = std::make_unique<ManualDspController> (config.caps, [this] (const juce::String& title, const juce::StringArray& steps)
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

void Session::setDspController (std::unique_ptr<DspController> controller, DspKind kind)
{
    finishDspApply();
    liveDsp = std::move (controller);
    liveDspKind = kind;
    sendChangeMessage();
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

    if (mode == Mode::demo)
    {
        rebuildDemoRig();
        autoTune.onMicPositionChanged = [rig = demoRig.get()] (int p) { rig->setMicPosition (p); };
    }
    else
    {
        autoTune.onMicPositionChanged = nullptr;
    }

    if (! autoTune.start (config, getSource(), getDsp(), autoTuneOptions))
    {
        error = "Could not start the auto tune.";
        return false;
    }
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
    if (r.success)
    {
        config = r.tunedConfig;
        imageBalanceDb = 0.0;
        saveSettings();
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

    measureThread = std::make_unique<OneShotThread> ([this, source, sampleRate, feed, label, onDone]
    {
        SweepRequest req;
        req.sweep.sampleRate = sampleRate;
        req.sweep.seconds = autoTuneOptions.sweepSeconds;
        req.sweep.levelDbFS = autoTuneOptions.sweepLevelDbFS;
        req.sweep.fEnd = std::min (24000.0, sampleRate * 0.48);
        req.feed = feed;
        req.label = label;

        juce::String err;
        auto ms = source->measureSweep (req, measureCancel, err);

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

void Session::finishDspApply()
{
    {
        const juce::ScopedLock sl (dspApplyLock);
        dspApplyQueue.clear();
    }
    if (dspApplyThread != nullptr)
        dspApplyThread->waitForThreadToExit (10000);
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
    auto* dsp = &getDsp();
    dspApplyThread = std::make_unique<OneShotThread> ([this, dsp]
    {
        for (;;)
        {
            std::map<juce::String, OutputChannel> work;
            {
                const juce::ScopedLock lock (dspApplyLock);
                if (dspApplyQueue.empty())
                {
                    dspApplyBusy = false;
                    return;
                }
                work.swap (dspApplyQueue);
            }
            for (auto& [id, ch] : work)
            {
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
    const auto v = juce::JSON::parse (f.loadFileAsString());
    if (! v.isObject() || ! v.getProperty ("format", {}).toString().startsWith ("FalseAudio session"))
    {
        error = f.getFileName() + " is not a FalseAudio session.";
        return false;
    }

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
