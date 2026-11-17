#pragma once

#include "DspController.h"
#include "../audio/OutputProcessor.h"
#include <map>
#include <mutex>

namespace fa
{
/** The app's own DSP: a real DspController whose settings are applied by the AudioEngine to everything it plays on that
    output (measurement sweeps and app playback). For testing the whole auto-tune loop without a hardware DSP, or for PC
    playback (export the result to Equalizer APO afterwards).

    Per output it stores ChannelSettings (gain, delay, polarity, mute, HP/LP via FilterSpec -> biquads, PEQ). Solo/mute
    from soloChannels()/setMuted() is kept apart from the stored mute, like a hardware DSP's mute button. */
class SoftwareDspController : public DspController, public OutputProcessor
{
public:
    explicit SoftwareDspController (DspCapabilities caps = defaultCapabilities());

    static DspCapabilities defaultCapabilities();

    juce::String getName() const override        { return "Built-in (software)"; }
    bool isAutomatic() const override            { return true; }
    DspCapabilities getCapabilities() const override { return caps; }
    bool connect (juce::String&) override        { return true; }
    bool isConnected() const override            { return true; }

    bool applyChannel (const OutputChannel& channel, juce::String& error) override;
    bool setMuted (const OutputChannel& channel, bool muted, juce::String& error) override;

    /** The settings currently stored for a DSP output (false if none were written yet). `muted` includes the solo mute. */
    bool getSettings (int dspOutput, ChannelSettings& out) const;

    bool processOutput (int dspOutput, const std::vector<float>& in, double sampleRate, std::vector<float>& out) const override;

    /** Offline reference rendering of one chain (used by processOutput and the tests). */
    static std::vector<float> render (const ChannelSettings& s, const std::vector<float>& in, double sampleRate);

private:
    struct State { ChannelSettings settings; bool soloMuted = false; };
    DspCapabilities caps;
    mutable std::mutex m;
    std::map<int, State> outputs;
};
} // namespace fa
