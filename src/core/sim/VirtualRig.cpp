#include "VirtualRig.h"

namespace fa
{
VirtualRig::VirtualRig (const SystemConfig& layout, int mics, juce::uint32 seed)
    : car (layout, seed), caps (layout.caps), numMics (std::max (1, mics))
{
    for (auto& ch : layout.channels)
    {
        auto copy = ch;
        copy.settings = {}; // a factory-fresh DSP: everything flat and full range
        dspState[ch.id] = copy;
    }
}

bool VirtualRig::applyChannel (const OutputChannel& channel, juce::String&)
{
    const juce::ScopedLock sl (lock);
    const bool wasMuted = dspState[channel.id].settings.muted;
    dspState[channel.id] = channel;
    dspState[channel.id].settings.muted = wasMuted; // mutes are driven by setMuted()/soloChannels()
    return true;
}

bool VirtualRig::setMuted (const OutputChannel& channel, bool muted, juce::String&)
{
    const juce::ScopedLock sl (lock);
    dspState[channel.id].settings.muted = muted;
    return true;
}

void VirtualRig::setChannelNoise (const juce::String& channelId, double dbFS, bool hum)
{
    const juce::ScopedLock sl (lock);
    injectedNoise[channelId] = { dbFS, hum };
}

bool VirtualRig::recordSilence (std::vector<float>& samples, double& sampleRate, std::atomic<bool>& cancelFlag, juce::String& error)
{
    if (cancelFlag.load())
    {
        error = "Cancelled.";
        return false;
    }

    sampleRate = 48000.0;
    const size_t n = (size_t) (1.5 * sampleRate);
    samples.assign (n, 0.0f);

    std::vector<InjectedNoise> active;
    {
        const juce::ScopedLock sl (lock);
        for (auto& [id, ch] : dspState)
            if (! ch.settings.muted)
                if (auto it = injectedNoise.find (id); it != injectedNoise.end())
                    active.push_back (it->second);
    }

    juce::Random rng (noiseSeed++);
    const double floorAmp = magFromDb (-90.0);   // the quiet cabin + mic floor
    const double twoPi = juce::MathConstants<double>::twoPi;
    for (size_t i = 0; i < n; ++i)
    {
        double v = floorAmp * (rng.nextDouble() * 2.0 - 1.0);
        for (auto& a : active)
        {
            const double amp = magFromDb (a.dbFS);
            if (a.hum)
            {
                const double t = (double) i / sampleRate;
                v += amp * 1.2 * (std::sin (twoPi * 50.0 * t) + 0.5 * std::sin (twoPi * 100.0 * t));
            }
            else
                v += amp * 1.7320508 * (rng.nextDouble() * 2.0 - 1.0);   // uniform noise with RMS = amp
        }
        samples[i] = (float) v;
    }
    return true;
}

std::vector<Measurement> VirtualRig::measureSweep (const SweepRequest& request, std::atomic<bool>& cancelFlag,
                                                   juce::String& error)
{
    std::vector<std::pair<juce::String, ChannelSettings>> playing;
    {
        const juce::ScopedLock sl (lock);
        for (auto& [id, ch] : dspState)
        {
            if (ch.settings.muted)
                continue;
            const bool fed = request.feed == Feed::both
                          || ch.side == Side::mono
                          || (request.feed == Feed::left && ch.side == Side::left)
                          || (request.feed == Feed::right && ch.side == Side::right);
            if (fed)
                playing.emplace_back (id, ch.settings);
        }
    }

    const auto start = juce::Time::getMillisecondCounter();
    while ((int) (juce::Time::getMillisecondCounter() - start) < sweepMs)
    {
        if (cancelFlag.load())
        {
            error = "Cancelled.";
            return {};
        }
        juce::Thread::sleep (10);
    }

    std::vector<Measurement> out;
    for (int m = 0; m < numMics; ++m)
    {
        // Several mics = several head positions captured in one sweep.
        auto meas = car.measure (playing, micPosition * numMics + m);
        meas.channelId = request.label;
        meas.micPosition = micPosition * numMics + m;
        meas.name = request.label + " @pos" + juce::String (meas.micPosition + 1);
        out.push_back (std::move (meas));
    }
    return out;
}
} // namespace fa
