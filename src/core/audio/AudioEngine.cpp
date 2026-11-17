#include "AudioEngine.h"

namespace fa
{
juce::var AudioRouting::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("left", leftOutput);
    o->setProperty ("right", rightOutput);
    juce::Array<juce::var> mics;
    for (auto m : micInputs)
        mics.add (m);
    o->setProperty ("mics", mics);
    o->setProperty ("loopOut", loopbackOutput);
    o->setProperty ("loopIn", loopbackInput);
    return juce::var (o);
}

AudioRouting AudioRouting::fromVar (const juce::var& v)
{
    AudioRouting r;
    r.leftOutput     = v.getProperty ("left", r.leftOutput);
    r.rightOutput    = v.getProperty ("right", r.rightOutput);
    r.loopbackOutput = v.getProperty ("loopOut", r.loopbackOutput);
    r.loopbackInput  = v.getProperty ("loopIn", r.loopbackInput);
    if (auto* m = v.getProperty ("mics", {}).getArray())
    {
        r.micInputs.clear();
        for (auto& x : *m)
            r.micInputs.push_back ((int) x);
    }
    return r;
}

//==============================================================================
AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine()
{
    activePlayback.store (nullptr);
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();
}

juce::String AudioEngine::initialise (const juce::XmlElement* savedState)
{
    // Ask for plenty of channels; JUCE opens what the device actually has.
    auto err = deviceManager.initialise (16, 16, savedState, true);
    deviceManager.addAudioCallback (this);
    return err;
}

std::unique_ptr<juce::XmlElement> AudioEngine::createStateXml() const
{
    return deviceManager.createStateXml();
}

void AudioEngine::setRouting (const AudioRouting& r)
{
    const juce::ScopedLock sl (lock);
    routing = r;
}

AudioRouting AudioEngine::getRouting() const
{
    const juce::ScopedLock sl (lock);
    return routing;
}

void AudioEngine::setCalibration (int micIndex, const MicCalibration& cal)
{
    const juce::ScopedLock sl (lock);
    calibrations[micIndex] = cal;
}

MicCalibration AudioEngine::getCalibration (int micIndex) const
{
    const juce::ScopedLock sl (lock);
    auto it = calibrations.find (micIndex);
    return it != calibrations.end() ? it->second : MicCalibration();
}

bool AudioEngine::isReady (juce::String& whyNot) const
{
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
    {
        whyNot = "No audio device is open.";
        return false;
    }

    const auto r = getRouting();
    if (r.micInputs.empty())
    {
        whyNot = "No measurement microphone input selected.";
        return false;
    }
    for (auto in : r.micInputs)
        if (in < 0 || in >= numIns)
        {
            whyNot = "Microphone input " + juce::String (in + 1) + " is not active on " + device->getName() + ".";
            return false;
        }
    if (r.leftOutput >= numOuts || r.rightOutput >= numOuts)
    {
        whyNot = "Selected outputs are not active on " + device->getName() + ".";
        return false;
    }
    return true;
}

int AudioEngine::numMicrophones() const
{
    return (int) getRouting().micInputs.size();
}

double AudioEngine::getSampleRate() const
{
    return sampleRate;
}

int AudioEngine::getNumInputChannels() const  { return numIns; }
int AudioEngine::getNumOutputChannels() const { return numOuts; }

static juce::StringArray activeNames (const juce::StringArray& all, const juce::BigInteger& active)
{
    juce::StringArray out;
    for (int i = 0; i < all.size(); ++i)
        if (active[i])
            out.add (all[i]);
    return out;
}

juce::StringArray AudioEngine::getInputChannelNames() const
{
    if (auto* d = deviceManager.getCurrentAudioDevice())
        return activeNames (d->getInputChannelNames(), d->getActiveInputChannels());
    return {};
}

juce::StringArray AudioEngine::getOutputChannelNames() const
{
    if (auto* d = deviceManager.getCurrentAudioDevice())
        return activeNames (d->getOutputChannelNames(), d->getActiveOutputChannels());
    return {};
}

//==============================================================================
bool AudioEngine::recordInput (double seconds, std::vector<float>& samples, double& sampleRateOut,
                               std::atomic<bool>& cancelFlag, juce::String& error, double* wallSeconds)
{
    samples.clear();
    if (cancelFlag.load())
    {
        error = "Cancelled.";
        return false;
    }
    if (deviceManager.getCurrentAudioDevice() == nullptr)
    {
        error = "No audio device is open.";
        return false;
    }
    const auto r = getRouting();
    if (r.micInputs.empty() || r.micInputs.front() < 0 || r.micInputs.front() >= numIns)
    {
        error = "No active microphone input selected.";
        return false;
    }
    if (activeCapture.load() != nullptr)
    {
        error = "A measurement is already running.";
        return false;
    }

    auto cap = std::make_unique<Capture>();       // empty stimulus, no outputs: pure silence out
    cap->inputs = { r.micInputs.front() };
    const int total = std::max (1, (int) (seconds * sampleRate));
    cap->recorded.assign (1, std::vector<float> ((size_t) total, 0.0f));
    activeCapture.store (cap.get());

    const auto timeoutMs = (juce::uint32) (1000.0 * total / sampleRate) + 3000;
    const auto start = juce::Time::getMillisecondCounter();
    const double t0 = juce::Time::getMillisecondCounterHiRes();
    while (cap->position.load() < total)
    {
        if (cancelFlag.load())
        {
            error = "Cancelled.";
            break;
        }
        if (juce::Time::getMillisecondCounter() - start > timeoutMs)
        {
            error = "The audio device stopped delivering audio.";
            break;
        }
        juce::Thread::sleep (10);
    }
    const double t1 = juce::Time::getMillisecondCounterHiRes();
    activeCapture.store (nullptr);
    waitForCallbackToFinish();

    if (error.isNotEmpty())
        return false;
    if (wallSeconds != nullptr)
        *wallSeconds = (t1 - t0) / 1000.0;
    sampleRateOut = sampleRate;
    samples = std::move (cap->recorded.front());
    return true;
}

bool AudioEngine::recordSilence (std::vector<float>& samples, double& sampleRateOut,
                                 std::atomic<bool>& cancelFlag, juce::String& error)
{
    return recordInput (1.5, samples, sampleRateOut, cancelFlag, error);
}

juce::String AudioEngine::openInputOnly (const juce::String& deviceName, double rate)
{
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager.getAudioDeviceSetup (setup);
    setup.inputDeviceName = deviceName;
    setup.outputDeviceName = {};
    setup.useDefaultInputChannels = false;
    setup.inputChannels.clear();
    setup.inputChannels.setRange (0, 2, true);   // JUCE keeps only the channels the device has
    setup.useDefaultOutputChannels = false;
    setup.outputChannels.clear();
    setup.sampleRate = rate;                      // 0 = device default
    setup.bufferSize = 0;
    deviceManager.removeAudioCallback (this);
    auto err = deviceManager.setAudioDeviceSetup (setup, true);
    deviceManager.addAudioCallback (this);
    return err;
}

//==============================================================================
std::vector<CaptureAttempt> planCaptureAttempts (CaptureMode mode, const juce::String& currentType,
                                                 const juce::StringArray& availableTypes)
{
    static const juce::String shared ("Windows Audio"), excl ("Windows Audio (Exclusive Mode)");
    std::vector<CaptureAttempt> out;
    const bool isWasapi = currentType == shared || currentType == excl;
    if (! isWasapi)
    {
        out.push_back ({ currentType, false, {} });   // ASIO / DirectSound / CoreAudio ...: used as chosen
        return out;
    }
    const bool haveExcl = availableTypes.contains (excl), haveShared = availableTypes.contains (shared);
    const juce::String noExcl = "WASAPI exclusive mode is not available on this system.";
    const juce::String fallback = "Using shared-mode Windows audio: Windows noise suppression / audio enhancements may be "
                                  "active on the mic and make the noise floor read too low. Results are less trustworthy.";
    switch (mode)
    {
        case CaptureMode::shared:
            if (haveShared) out.push_back ({ shared, false, {} });
            break;
        case CaptureMode::exclusive:
            if (haveExcl) out.push_back ({ excl, true, {} });
            break;
        case CaptureMode::autoPreferExclusive:
            if (haveExcl) out.push_back ({ excl, true, {} });
            if (haveShared) out.push_back ({ shared, false, haveExcl ? fallback : noExcl + " " + fallback });
            break;
    }
    return out;
}

juce::String AudioEngine::beginMeasurementPath (bool* failed)
{
    if (failed != nullptr) *failed = false;
    if (pathActive)
        return pathNote;
    auto* dev = deviceManager.getCurrentAudioDevice();
    if (dev == nullptr)
        return {};

    juce::StringArray types;
    for (auto* t : deviceManager.getAvailableDeviceTypes())
        types.add (t->getTypeName());

    savedType = deviceManager.getCurrentAudioDeviceType();
    deviceManager.getAudioDeviceSetup (savedSetup);
    const auto plan = planCaptureAttempts (captureMode, savedType, types);

    juce::StringArray problems;
    for (const auto& a : plan)
    {
        if (a.typeName == savedType)
        {
            pathActive = true; pathSwitched = false; pathExclusive = a.exclusive;
            pathNote = a.warning.isNotEmpty() ? juce::String (problems.joinIntoString (" ") + " " + a.warning).trim() : juce::String();
            return pathNote;
        }
        deviceManager.removeAudioCallback (this);
        deviceManager.setCurrentAudioDeviceType (a.typeName, true);
        auto setup = savedSetup;
        juce::String err = deviceManager.setAudioDeviceSetup (setup, true);
        if (err.isNotEmpty() && setup.sampleRate != 0.0)
        {
            setup.sampleRate = 0.0;                      // exclusive mode needs a format the device supports natively
            err = deviceManager.setAudioDeviceSetup (setup, true);
        }
        deviceManager.addAudioCallback (this);
        if (err.isEmpty() && deviceManager.getCurrentAudioDevice() != nullptr)
        {
            pathActive = true; pathSwitched = true; pathExclusive = a.exclusive;
            pathNote = a.warning.isNotEmpty() ? juce::String (problems.joinIntoString (" ") + " " + a.warning).trim() : juce::String();
            return pathNote;
        }
        problems.add ((a.exclusive ? "Exclusive mode could not be opened (" : "Shared mode could not be opened (")
                      + (err.isNotEmpty() ? err : juce::String ("device did not start"))
                      + "); the device may be in use by another application (e.g. REW) or not support this format.");
    }

    // Nothing opened: put the original back.
    deviceManager.removeAudioCallback (this);
    deviceManager.setCurrentAudioDeviceType (savedType, true);
    deviceManager.setAudioDeviceSetup (savedSetup, true);
    deviceManager.addAudioCallback (this);
    if (failed != nullptr) *failed = true;
    return problems.joinIntoString (" ");
}

void AudioEngine::endMeasurementPath()
{
    if (! pathActive)
        return;
    pathActive = false;
    pathExclusive = false;
    if (! pathSwitched)
        return;
    pathSwitched = false;
    deviceManager.removeAudioCallback (this);          // the exclusive stream is released, other apps get audio back
    deviceManager.setCurrentAudioDeviceType (savedType, true);
    deviceManager.setAudioDeviceSetup (savedSetup, true);
    deviceManager.addAudioCallback (this);
}

//==============================================================================
std::vector<Measurement> AudioEngine::measureSweep (const SweepRequest& request,
                                                    std::atomic<bool>& cancelFlag,
                                                    juce::String& error)
{
    std::vector<Measurement> results;
    if (! isReady (error))
        return results;

    if (activeCapture.load() != nullptr)
    {
        error = "A measurement is already running.";
        return results;
    }

    const auto r = getRouting();
    auto sweep = request.sweep;
    sweep.sampleRate = sampleRate;
    sweep.fEnd = std::min (sweep.fEnd, sampleRate * 0.48);

    auto cap = std::make_unique<Capture>();
    cap->stimulus = sweep.generate();

    if (request.feed != Feed::right) cap->outputs.push_back (r.leftOutput);
    if (request.feed != Feed::left)  cap->outputs.push_back (r.rightOutput);
    if (r.loopbackOutput >= 0)       cap->outputs.push_back (r.loopbackOutput);

    cap->inputs = r.micInputs;
    const bool useLoopback = r.loopbackInput >= 0 && r.loopbackOutput >= 0
                          && r.loopbackInput < numIns && r.loopbackOutput < numOuts;
    if (useLoopback)
        cap->inputs.push_back (r.loopbackInput);

    // Extra half second (plus the reported round-trip latency) so the end of the response isn't cut.
    int reportedLatency = 0;
    if (auto* device = deviceManager.getCurrentAudioDevice())
        reportedLatency = std::max (0, device->getOutputLatencyInSamples() + device->getInputLatencyInSamples());
    const int total = (int) cap->stimulus.size() + (int) (0.5 * sampleRate) + std::min (reportedLatency, (int) sampleRate);
    cap->recorded.assign (cap->inputs.size(), std::vector<float> ((size_t) total, 0.0f));

    activeCapture.store (cap.get());

    const auto timeoutMs = (juce::uint32) (1000.0 * total / sampleRate) + 3000;
    const auto start = juce::Time::getMillisecondCounter();
    while (cap->position.load() < total)
    {
        if (cancelFlag.load())
        {
            error = "Cancelled.";
            break;
        }
        if (juce::Time::getMillisecondCounter() - start > timeoutMs)
        {
            error = "The audio device stopped delivering audio.";
            break;
        }
        juce::Thread::sleep (20);
    }

    activeCapture.store (nullptr);
    waitForCallbackToFinish(); // the audio thread must be done with `cap` before it is freed

    if (error.isNotEmpty())
        return results;

    auto peakDbOf = [] (const std::vector<float>& x)
    {
        float p = 0.0f;
        for (auto v : x)
            p = std::max (p, std::abs (v));
        return dbFromMag (p);
    };

    juce::StringArray warnings;

    // Timing reference. With a loopback cable the IR's time zero is the true electrical output
    // start. Without one, the internal stimulus is the reference and time zero includes the
    // interface's output + input latency: every arrival shifts by the same amount, which is
    // harmless because delays and phases are only compared between channels measured on the
    // same device - but onsetMs is then not an absolute distance.
    const std::vector<float>* reference = &cap->stimulus;
    if (useLoopback)
    {
        if (peakDbOf (cap->recorded.back()) > sweep.levelDbFS - 40.0)
            reference = &cap->recorded.back();
        else
            warnings.add ("The loopback input is silent - check the loopback cable. Used the internal reference instead "
                          "(timing then includes the interface latency, which is fine for relative delays).");
    }

    // Keep the Farina harmonic impulses (they sit before time zero) for distortion analysis.
    const int preRoll = harmonicPreRoll (sweep);

    for (size_t k = 0; k < r.micInputs.size(); ++k)
    {
        if (peakDbOf (cap->recorded[k]) < -80.0)
            warnings.add ("No signal on microphone " + juce::String ((int) k + 1) + " (input "
                          + juce::String (r.micInputs[k] + 1) + "). Check the cable, phantom power and input gain.");

        Measurement m;
        m.sampleRate = sampleRate;
        m.ir = deconvolve (cap->recorded[k], *reference, sampleRate, sweep.fStart, sweep.fEnd, preRoll);
        m.zeroSample = (int) m.ir.size() - (int) cap->recorded[k].size();
        m.channelId = request.label;
        m.micPosition = (int) k;
        m.name = request.label + (r.micInputs.size() > 1 ? " mic " + juce::String ((int) k + 1) : juce::String());
        m.clipped = cap->clipped.load();

        const auto cal = getCalibration ((int) k);
        m.analyse (cal.isValid() ? &cal : nullptr);
        results.push_back (std::move (m));
    }

    if (cap->clipped.load())
        warnings.insert (0, "Input clipped during the sweep - lower the sweep level or the mic gain.");

    for (auto& m : results)
    {
        if (! m.clipped && m.snrDb < 20.0)
        {
            warnings.add ("Low signal-to-noise (" + juce::String (m.snrDb, 0) + " dB) on " + m.name
                          + ". Raise the volume or sweep level, or reduce background noise.");
            break;
        }
    }

    // Results are still returned with warnings; callers treat a non-empty error with results as advisory.
    error = warnings.joinIntoString ("\n");
    return results;
}

//==============================================================================
void AudioEngine::setGenerator (const GeneratorSettings& g)
{
    std::vector<float> table;
    if (g.signal == GeneratorSettings::Signal::pinkPeriodic)
        table = makePeriodicPinkNoise (1 << 16, sampleRate, 0.0);

    const juce::ScopedLock sl (lock);
    generator = g;
    if (! table.empty())
    {
        pinkTable = std::move (table);
        pinkPos = 0;
    }
}

GeneratorSettings AudioEngine::getGenerator() const
{
    const juce::ScopedLock sl (lock);
    return generator;
}

float AudioEngine::nextGeneratorSample()
{
    const double amp = magFromDb (generator.levelDbFS);
    switch (generator.signal)
    {
        case GeneratorSettings::Signal::off:
            return 0.0f;

        case GeneratorSettings::Signal::pinkPeriodic:
        {
            if (pinkTable.empty())
                return 0.0f;
            const float s = pinkTable[(size_t) pinkPos];
            pinkPos = (pinkPos + 1) % (int) pinkTable.size();
            return (float) (amp * s);
        }

        case GeneratorSettings::Signal::pinkRandom:
        {
            // Paul Kellet's refined pink filter
            const double white = rng.nextDouble() * 2.0 - 1.0;
            pinkB[0] = 0.99886 * pinkB[0] + white * 0.0555179;
            pinkB[1] = 0.99332 * pinkB[1] + white * 0.0750759;
            pinkB[2] = 0.96900 * pinkB[2] + white * 0.1538520;
            pinkB[3] = 0.86650 * pinkB[3] + white * 0.3104856;
            pinkB[4] = 0.55000 * pinkB[4] + white * 0.5329522;
            pinkB[5] = -0.7616 * pinkB[5] - white * 0.0168980;
            const double pink = pinkB[0] + pinkB[1] + pinkB[2] + pinkB[3] + pinkB[4] + pinkB[5] + pinkB[6] + white * 0.5362;
            pinkB[6] = white * 0.115926;
            return (float) (amp * pink * 0.11 * 1.41);
        }

        case GeneratorSettings::Signal::whiteNoise:
            return (float) (amp * (rng.nextDouble() * 2.0 - 1.0) * 1.73);

        case GeneratorSettings::Signal::sine:
        {
            const float s = (float) (amp * std::sqrt (2.0) * std::sin (sinePhase));
            sinePhase += 2.0 * pi * generator.sineHz / sampleRate;
            if (sinePhase > 2.0 * pi)
                sinePhase -= 2.0 * pi;
            return s;
        }
    }
    return 0.0f;
}

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    const juce::ScopedLock sl (lock);
    sampleRate = device->getCurrentSampleRate();
    numIns  = device->getActiveInputChannels().countNumberOfSetBits();
    numOuts = device->getActiveOutputChannels().countNumberOfSetBits();

    inputRings.assign ((size_t) numIns, std::vector<float> ((size_t) ringSize, 0.0f));
    referenceRing.assign ((size_t) ringSize, 0.0f);
    ringWrite = 0;

    numPeakSlots = numIns;
    inputPeaks.reset (new std::atomic<float>[(size_t) std::max (1, numIns)]);
    for (int i = 0; i < std::max (1, numIns); ++i)
        inputPeaks[(size_t) i] = 0.0f;

    if (generator.signal == GeneratorSettings::Signal::pinkPeriodic)
        pinkTable = makePeriodicPinkNoise (1 << 16, sampleRate, 0.0);
}

void AudioEngine::audioDeviceStopped()
{
    const juce::ScopedLock sl (lock);
    numIns = numOuts = 0;
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* in, int numInputChannels,
                                                    float* const* out, int numOutputChannels,
                                                    int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    inCallback.store (true);
    struct ClearFlag { std::atomic<bool>& flag; ~ClearFlag() { flag.store (false); } } clearFlag { inCallback };

    for (int ch = 0; ch < numOutputChannels; ++ch)
        if (out[ch] != nullptr)
            juce::FloatVectorOperations::clear (out[ch], numSamples);

    float outPeak = 0.0f;

    // The sweep capture is lock-free so nothing (UI analysers, settings) can ever glitch it.
    auto* cap = activeCapture.load();
    if (cap != nullptr)
    {
        int pos = cap->position.load();
        const int stimLen = (int) cap->stimulus.size();
        const int recLen = cap->recorded.empty() ? 0 : (int) cap->recorded.front().size();

        for (int i = 0; i < numSamples; ++i, ++pos)
        {
            const float s = pos < stimLen ? cap->stimulus[(size_t) pos] : 0.0f;
            for (auto o : cap->outputs)
                if (o >= 0 && o < numOutputChannels && out[o] != nullptr)
                    out[o][i] = s;
            outPeak = std::max (outPeak, std::abs (s));

            if (pos < recLen)
            {
                for (size_t k = 0; k < cap->inputs.size(); ++k)
                {
                    const int ch = cap->inputs[k];
                    if (ch >= 0 && ch < numInputChannels && in[ch] != nullptr)
                    {
                        const float x = in[ch][i];
                        cap->recorded[k][(size_t) pos] = x;
                        if (std::abs (x) > 0.99f)
                            cap->clipped = true;
                    }
                }
            }
        }
        cap->position.store (pos);
    }

    // Listening-check playback (lock-free; a sweep always takes priority).
    auto* pb = cap == nullptr ? activePlayback.load() : nullptr;
    if (pb != nullptr)
    {
        size_t pos = pb->position.load();
        const size_t len = std::min (pb->left.size(), pb->right.size());
        for (int i = 0; i < numSamples && len > 0; ++i)
        {
            if (pos >= len)
            {
                if (! pb->loop)
                    break;
                pos = 0;
            }
            const float sl = pb->left[pos] * pb->gain, sr = pb->right[pos] * pb->gain;
            if (pb->leftOut >= 0 && pb->leftOut < numOutputChannels && out[pb->leftOut] != nullptr)
                out[pb->leftOut][i] = sl;
            if (pb->rightOut >= 0 && pb->rightOut < numOutputChannels && out[pb->rightOut] != nullptr)
                out[pb->rightOut][i] = sr;
            if (pb->loopOut >= 0 && pb->loopOut < numOutputChannels && out[pb->loopOut] != nullptr)
                out[pb->loopOut][i] = 0.5f * (sl + sr);
            outPeak = std::max (outPeak, std::max (std::abs (sl), std::abs (sr)));
            ++pos;
        }
        pb->position.store (pos);
    }

    const juce::ScopedTryLock tl (lock);
    if (! tl.isLocked() || (int) inputRings.size() < numInputChannels)
        return;

    if (cap == nullptr && pb == nullptr && generator.signal != GeneratorSettings::Signal::off)
    {
        const int w = ringWrite.load();
        for (int i = 0; i < numSamples; ++i)
        {
            const float s = nextGeneratorSample();
            if (generator.feed != Feed::right && routing.leftOutput < numOutputChannels && out[routing.leftOutput] != nullptr)
                out[routing.leftOutput][i] = s;
            if (generator.feed != Feed::left && routing.rightOutput < numOutputChannels && out[routing.rightOutput] != nullptr)
                out[routing.rightOutput][i] = s;
            if (routing.loopbackOutput >= 0 && routing.loopbackOutput < numOutputChannels && out[routing.loopbackOutput] != nullptr)
                out[routing.loopbackOutput][i] = s;
            referenceRing[(size_t) ((w + i) & (ringSize - 1))] = s;
            outPeak = std::max (outPeak, std::abs (s));
        }
    }

    // Live input rings + meters
    const int w = ringWrite.load();
    for (int ch = 0; ch < numInputChannels; ++ch)
    {
        if (in[ch] == nullptr)
            continue;
        auto& ring = inputRings[(size_t) ch];
        float peak = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            ring[(size_t) ((w + i) & (ringSize - 1))] = in[ch][i];
            peak = std::max (peak, std::abs (in[ch][i]));
        }
        if (ch < numPeakSlots)
        {
            auto& slot = inputPeaks[(size_t) ch];
            if (peak > slot.load())
                slot.store (peak);
        }
    }
    ringWrite.store ((w + numSamples) & (ringSize - 1));

    if (outPeak > outputPeak.load())
        outputPeak.store (outPeak);
}

void AudioEngine::waitForCallbackToFinish() const
{
    // seq_cst: a callback that starts after the caller swapped a pointer out sees the new value,
    // so only a callback that is already running can still hold the old one.
    const auto start = juce::Time::getMillisecondCounter();
    while (inCallback.load() && juce::Time::getMillisecondCounter() - start < 2000)
        juce::Thread::yield();
}

void AudioEngine::playStereo (std::vector<float> left, std::vector<float> right, bool loop, double gainDb)
{
    auto pb = std::make_unique<Playback>();
    const size_t len = std::min (left.size(), right.size());
    left.resize (len);
    right.resize (len);
    pb->left = std::move (left);
    pb->right = std::move (right);
    pb->loop = loop;
    pb->gain = (float) magFromDb (gainDb);

    const auto r = getRouting();
    pb->leftOut = r.leftOutput;
    pb->rightOut = r.rightOutput;
    pb->loopOut = r.loopbackOutput;

    activePlayback.store (pb.get());
    waitForCallbackToFinish();
    ownedPlayback = std::move (pb); // frees the previous playback, which the audio thread has let go of
}

void AudioEngine::stopPlayback()
{
    activePlayback.store (nullptr);
    waitForCallbackToFinish();
    ownedPlayback.reset();
}

bool AudioEngine::copyLatestInput (int physicalInput, float* dest, int numSamples) const
{
    const juce::ScopedLock sl (lock);
    if (physicalInput < 0 || physicalInput >= (int) inputRings.size() || numSamples > ringSize)
        return false;

    const auto& ring = inputRings[(size_t) physicalInput];
    const int end = ringWrite.load();
    for (int i = 0; i < numSamples; ++i)
        dest[i] = ring[(size_t) ((end - numSamples + i) & (ringSize - 1))];
    return true;
}

bool AudioEngine::copyLatestReference (float* dest, int numSamples) const
{
    const juce::ScopedLock sl (lock);
    if (referenceRing.empty() || numSamples > ringSize)
        return false;

    // Prefer the physical loopback when wired: it includes the interface's own latency.
    const bool loop = routing.loopbackInput >= 0 && routing.loopbackInput < (int) inputRings.size();
    const auto& ring = loop ? inputRings[(size_t) routing.loopbackInput] : referenceRing;
    const int end = ringWrite.load();
    for (int i = 0; i < numSamples; ++i)
        dest[i] = ring[(size_t) ((end - numSamples + i) & (ringSize - 1))];
    return true;
}

float AudioEngine::getInputPeakDb (int physicalInput)
{
    if (physicalInput < 0 || physicalInput >= numPeakSlots || inputPeaks == nullptr)
        return -120.0f;
    return (float) dbFromMag (inputPeaks[(size_t) physicalInput].exchange (0.0f));
}

float AudioEngine::getOutputPeakDb()
{
    return (float) dbFromMag (outputPeak.exchange (0.0f));
}
} // namespace fa
