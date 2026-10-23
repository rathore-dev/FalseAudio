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
    const bool useLoopback = r.loopbackInput >= 0 && r.loopbackOutput >= 0 && r.loopbackInput < numIns;
    if (useLoopback)
        cap->inputs.push_back (r.loopbackInput);

    // Extra half second to catch the device's round-trip latency.
    const int total = (int) cap->stimulus.size() + (int) (0.5 * sampleRate);
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
    juce::Thread::sleep (50); // let any in-flight callback finish with the capture

    if (error.isNotEmpty())
        return results;

    const auto& reference = useLoopback ? cap->recorded.back() : cap->stimulus;

    for (size_t k = 0; k < r.micInputs.size(); ++k)
    {
        Measurement m;
        m.sampleRate = sampleRate;
        m.ir = deconvolve (cap->recorded[k], reference, sampleRate, sweep.fStart, sweep.fEnd);
        m.zeroSample = deconvolutionPreRoll (sampleRate);
        m.channelId = request.label;
        m.micPosition = (int) k;
        m.name = request.label + (r.micInputs.size() > 1 ? " mic " + juce::String ((int) k + 1) : juce::String());
        m.clipped = cap->clipped.load();

        const auto cal = getCalibration ((int) k);
        m.analyse (cal.isValid() ? &cal : nullptr);
        results.push_back (std::move (m));
    }

    if (cap->clipped.load())
        error = "Input clipped during the sweep - lower the sweep level or the mic gain.";

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

    const juce::ScopedTryLock tl (lock);
    if (! tl.isLocked() || (int) inputRings.size() < numInputChannels)
        return;

    if (cap == nullptr && generator.signal != GeneratorSettings::Signal::off)
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
