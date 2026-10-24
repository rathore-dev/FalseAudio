#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include "MeasurementSource.h"
#include "../measure/Calibration.h"

namespace fa
{
/** How the audio interface is wired to the car. Works with any interface/driver JUCE supports
    (WASAPI, DirectSound, ASIO when enabled): USB mics like the UMIK-1 appear as a normal input
    device, XLR mics via any interface with phantom power. */
struct AudioRouting
{
    int  leftOutput  = 0;            // interface output -> DSP left input
    int  rightOutput = 1;            // interface output -> DSP right input
    std::vector<int> micInputs { 0 };// one entry per measurement mic
    int  loopbackOutput = -1;        // optional timing reference: output wired straight back...
    int  loopbackInput  = -1;        // ...into this input (removes interface latency/jitter)

    juce::var toVar() const;
    static AudioRouting fromVar (const juce::var& v);
};

/** Live signal generator settings. */
struct GeneratorSettings
{
    enum class Signal { off, pinkPeriodic, pinkRandom, sine, whiteNoise };
    Signal signal = Signal::off;
    double levelDbFS = -20.0;
    double sineHz = 1000.0;
    Feed   feed = Feed::both;
};

class AudioEngine : public MeasurementSource,
                    private juce::AudioIODeviceCallback
{
public:
    AudioEngine();
    ~AudioEngine() override;

    /** Opens the default device (or restores `savedState` from createStateXml()). */
    juce::String initialise (const juce::XmlElement* savedState = nullptr);
    std::unique_ptr<juce::XmlElement> createStateXml() const;

    juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }

    void setRouting (const AudioRouting& r);
    AudioRouting getRouting() const;

    /** Calibration per mic (index into routing.micInputs). */
    void setCalibration (int micIndex, const MicCalibration& cal);
    MicCalibration getCalibration (int micIndex) const;

    // MeasurementSource
    bool isReady (juce::String& whyNot) const override;
    int  numMicrophones() const override;
    double getSampleRate() const override;
    std::vector<Measurement> measureSweep (const SweepRequest& request,
                                           std::atomic<bool>& cancelFlag,
                                           juce::String& error) override;

    // Generator (for RTA / listening checks)
    void setGenerator (const GeneratorSettings& g);
    GeneratorSettings getGenerator() const;

    /** Plays a stereo track on the left/right outputs (and the loopback output, as the mono
        sum) - used for the listening checks. Replaces any running playback; the live
        generator is muted while it plays and a sweep measurement always takes priority.
        Lock-free on the audio thread. Call from the message thread. */
    void playStereo (std::vector<float> left, std::vector<float> right, bool loop, double gainDb = 0.0);
    void stopPlayback();
    bool isPlayingBack() const noexcept { return activePlayback.load() != nullptr; }

    /** Copies the most recent `numSamples` of a physical input channel. Returns false if not available. */
    bool copyLatestInput (int physicalInput, float* dest, int numSamples) const;

    /** Copies the most recent generator output (reference for transfer-function measurements). */
    bool copyLatestReference (float* dest, int numSamples) const;

    /** Peak level in dBFS since the last call, per physical input. */
    float getInputPeakDb (int physicalInput);
    float getOutputPeakDb();

    int getNumInputChannels() const;
    int getNumOutputChannels() const;
    juce::StringArray getInputChannelNames() const;
    juce::StringArray getOutputChannelNames() const;

private:
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                           float* const* outputChannelData, int numOutputChannels,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;

    float nextGeneratorSample();

    juce::AudioDeviceManager deviceManager;
    mutable juce::CriticalSection lock;

    AudioRouting routing;
    std::map<int, MicCalibration> calibrations;
    GeneratorSettings generator;

    double sampleRate = 48000.0;
    int numIns = 0, numOuts = 0;

    // Sweep playback/capture (owned by the measuring thread while active)
    struct Capture
    {
        std::vector<float> stimulus;
        std::vector<std::vector<float>> recorded; // per entry in `inputs`
        std::vector<int> inputs;
        std::vector<int> outputs;
        std::atomic<int> position { 0 };
        std::atomic<bool> clipped { false };
    };
    std::atomic<Capture*> activeCapture { nullptr };

    // Stereo track playback (listening checks), handed to the audio thread like the capture.
    struct Playback
    {
        std::vector<float> left, right;
        bool loop = true;
        float gain = 1.0f;
        int leftOut = -1, rightOut = -1, loopOut = -1; // routing captured when playback starts
        std::atomic<size_t> position { 0 };
    };
    std::atomic<Playback*> activePlayback { nullptr };

    // Set while the audio callback runs, so a retired capture/playback is only freed once the
    // audio thread can no longer be using it.
    std::atomic<bool> inCallback { false };
    void waitForCallbackToFinish() const;

    // Live ring buffers (sized when the device starts)
    static constexpr int ringSize = 1 << 17;
    std::vector<std::vector<float>> inputRings;
    std::vector<float> referenceRing;
    std::atomic<int> ringWrite { 0 };
    std::unique_ptr<std::atomic<float>[]> inputPeaks;
    int numPeakSlots = 0;
    std::atomic<float> outputPeak { 0.0f };

    // Generator state (audio thread only)
    std::vector<float> pinkTable;
    int pinkPos = 0;
    double sinePhase = 0.0;
    double pinkB[7] {};
    juce::Random rng;

    std::unique_ptr<Playback> ownedPlayback; // keeps activePlayback alive (message thread)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};
} // namespace fa
