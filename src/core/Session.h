#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include "audio/AudioEngine.h"
#include "audio/LiveAnalyzer.h"
#include "bridge/DspController.h"
#include "bridge/Exporters.h"
#include "bridge/UIAutomationController.h"
#include "sim/VirtualRig.h"
#include "tune/AutoTuneEngine.h"

namespace fa
{
/** Everything the UI talks to. Owns the system configuration, the audio engine, the DSP
    controller, the measurement library and the auto-tuner.

    Change notifications (ChangeBroadcaster) are sent on the message thread whenever the config,
    mode, measurement library or tune result changes. */
class Session : public juce::ChangeBroadcaster,
                private AutoTuneEngine::Listener
{
public:
    enum class Mode { demo, live };
    enum class DspKind { virtualDemo, manual, uiAutomation };

    Session();
    ~Session() override;

    /** Opens the audio device and restores the last session/settings. */
    void initialise();

    // ---- Mode -------------------------------------------------------------
    Mode getMode() const noexcept { return mode; }
    void setMode (Mode m);                    // demo = virtual car, live = real hardware

    // ---- System -----------------------------------------------------------
    const SystemConfig& getConfig() const noexcept { return config; }
    void setConfig (const SystemConfig& c);
    void setLayout (int waysPerSide, bool withSub, bool stereoSub = false);
    void setTarget (const TargetCurve& t);
    void updateChannel (const OutputChannel& ch);

    // ---- Hardware ---------------------------------------------------------
    AudioEngine&  getAudio() noexcept        { return audio; }
    LiveAnalyzer& getAnalyzer() noexcept     { return analyzer; }
    void setRouting (const AudioRouting& r);
    bool loadCalibration (int micIndex, const juce::File& file, juce::String& error);
    MicCalibration getCalibration (int micIndex) const { return audio.getCalibration (micIndex); }

    // ---- DSP --------------------------------------------------------------
    DspKind getDspKind() const noexcept { return mode == Mode::demo ? DspKind::virtualDemo : liveDspKind; }
    void useManualDsp();
    void setDspController (std::unique_ptr<DspController> controller, DspKind kind);

    /** Drive the vendor's DSP software with a taught profile. The window provider returns the
        HWND of the DSP app (e.g. DspWindowHost::getAttached().handle). */
    void useUIAutomationDsp (const DspProfile& profile, std::function<void*()> windowProvider);
    DspController& getDsp();
    MeasurementSource& getSource();

    // ---- Auto tune --------------------------------------------------------
    AutoTuneEngine& getAutoTune() noexcept { return autoTune; }
    AutoTuneEngine::Options& getAutoTuneOptions() noexcept { return autoTuneOptions; }
    bool startAutoTune (juce::String& error);
    bool hasTuneResult() const noexcept { return hasResult; }
    const AutoTuneEngine::Result& getTuneResult() const noexcept { return tuneResult; }

    // ---- Measurement library ---------------------------------------------
    const std::vector<Measurement>& getMeasurements() const noexcept { return measurements; }
    void addMeasurement (const Measurement& m);
    void removeMeasurement (const juce::String& id);
    void clearMeasurements();

    /** Runs a single sweep in the background (manual measuring). onDone is called on the message thread. */
    void measureOnce (Feed feed, const juce::String& label,
                      std::function<void (const std::vector<Measurement>&, const juce::String& error)> onDone);
    bool isMeasuring() const noexcept { return measuringNow.load(); }

    // ---- Files ------------------------------------------------------------
    bool saveSession (const juce::File& f, juce::String& error) const;
    bool loadSession (const juce::File& f, juce::String& error);
    juce::File getAppDataFolder() const;

private:
    void autoTuneMeasurement (const Measurement& m) override;
    void autoTuneFinished (const AutoTuneEngine::Result& r) override;
    void rebuildDemoRig();
    void saveSettings();
    void loadSettings();

    Mode mode = Mode::demo;
    DspKind liveDspKind = DspKind::manual;
    SystemConfig config;

    AudioEngine audio;
    LiveAnalyzer analyzer { audio };
    std::unique_ptr<VirtualRig> demoRig;
    std::unique_ptr<DspController> liveDsp;

    AutoTuneEngine autoTune;
    AutoTuneEngine::Options autoTuneOptions;
    AutoTuneEngine::Result tuneResult;
    bool hasResult = false;

    std::vector<Measurement> measurements;
    std::atomic<bool> measuringNow { false };
    std::atomic<bool> measureCancel { false };
    std::unique_ptr<juce::Thread> measureThread;

    std::unique_ptr<juce::PropertiesFile> settings;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Session)
};
} // namespace fa
