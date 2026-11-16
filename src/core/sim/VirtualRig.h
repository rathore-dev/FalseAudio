#pragma once

#include "VirtualCar.h"
#include "../bridge/DspController.h"
#include "../audio/MeasurementSource.h"
#include "../tune/MethodologyStages.h"

namespace fa
{
/** Demo/test rig: a simulated DSP (that really applies settings) in a simulated car, measured by
    simulated microphones. Implements both the DSP and the measurement interfaces so the
    auto-tuner runs unchanged. */
class VirtualRig : public DspController,
                   public MeasurementSource,
                   public method::NoiseProbe
{
public:
    explicit VirtualRig (const SystemConfig& layout, int numMics = 1, juce::uint32 seed = 42);

    VirtualCar& getCar() noexcept { return car; }

    /** Where the (virtual) mic currently is; the auto-tuner asks the user to move it. */
    void setMicPosition (int p) { micPosition = p; }

    /** Simulated sweep duration so the UI shows realistic progress (0 = instant). */
    void setSimulatedSweepMs (int ms) { sweepMs = ms; }

    /** Test hook: this channel injects noise (hiss, or 50 Hz hum + harmonics) at `dbFS` RMS whenever its output is enabled. */
    void setChannelNoise (const juce::String& channelId, double dbFS, bool hum = false);

    // method::NoiseProbe
    bool recordSilence (std::vector<float>& samples, double& sampleRate, std::atomic<bool>& cancelFlag, juce::String& error) override;

    // DspController
    juce::String getName() const override { return "Virtual DSP (demo car)"; }
    bool isAutomatic() const override     { return true; }
    DspCapabilities getCapabilities() const override { return caps; }
    bool connect (juce::String&) override { return true; }
    bool isConnected() const override     { return true; }
    bool applyChannel (const OutputChannel& channel, juce::String& error) override;
    bool setMuted (const OutputChannel& channel, bool muted, juce::String& error) override;

    // MeasurementSource
    bool isReady (juce::String&) const override { return true; }
    int  numMicrophones() const override        { return numMics; }
    double getSampleRate() const override       { return 48000.0; }
    std::vector<Measurement> measureSweep (const SweepRequest& request, std::atomic<bool>& cancelFlag,
                                           juce::String& error) override;

private:
    VirtualCar car;
    DspCapabilities caps;
    juce::CriticalSection lock;
    std::map<juce::String, OutputChannel> dspState;
    int numMics = 1;
    int micPosition = 0;
    int sweepMs = 400;
    struct InjectedNoise { double dbFS = -120.0; bool hum = false; };
    std::map<juce::String, InjectedNoise> injectedNoise;
    juce::uint32 noiseSeed = 7;
};
} // namespace fa
