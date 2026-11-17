#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include "audio/AudioEngine.h"
#include "measure/MicSelfTest.h"
#include "audio/ListeningTracks.h"
#include <optional>
#include "audio/LiveAnalyzer.h"
#include "ai/ChannelVerifier.h"
#include "bridge/AiDspController.h"
#include "bridge/DspController.h"
#include "bridge/GuardedDspController.h"
#include "bridge/Exporters.h"
#include "bridge/UIAutomationController.h"
#include "bridge/native/MiniDspController.h"
#include "bridge/native/Dsp408Controller.h"
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
    enum class DspKind { virtualDemo, manual, uiAutomation, nativeMiniDsp, nativeDsp408, aiOperator };

    Session();
    ~Session() override;

    /** Opens the audio device and restores the last session/settings. */
    void initialise();

    // ---- Mode -------------------------------------------------------------
    Mode getMode() const noexcept { return mode; }
    void setMode (Mode m);                    // demo = virtual car, live = real hardware

    // ---- System -----------------------------------------------------------
    const SystemConfig& getConfig() const noexcept { return config; }
    /** True while an Auto Tune is running: config edits are refused (they would race the tune and be overwritten).
        The edit calls below return false in that case; the UI should disable its controls and tell the user. */
    bool isConfigLocked() const noexcept { return autoTune.isRunning(); }
    static const char* configLockedMessage() noexcept { return "Auto Tune is running - wait for it to finish (or cancel it) before changing the system."; }
    bool setConfig (const SystemConfig& c);
    bool setLayout (int waysPerSide, bool withSub, bool stereoSub = false);
    bool setTarget (const TargetCurve& t);
    bool updateChannel (const OutputChannel& ch);

    // ---- Hardware ---------------------------------------------------------
    AudioEngine&  getAudio() noexcept        { return audio; }
    LiveAnalyzer& getAnalyzer() noexcept     { return analyzer; }
    void setRouting (const AudioRouting& r);
    bool loadCalibration (int micIndex, const juce::File& file, juce::String& error);
    MicCalibration getCalibration (int micIndex) const { return audio.getCalibration (micIndex); }

    /** Input-only mic check (nothing is played): records `seconds` from mic 1 and analyses it with
        its calibration. Blocking - call from a worker thread. On failure the verdict is noData. */
    MicSelfTestReport runMicSelfTest (double seconds, std::atomic<bool>& cancelFlag);

    /** Latest warning about the measurement path (shared-mode fallback, exclusive open failure, Windows capture
        processing detected). Empty when fine. Refreshed by startAutoTune() and runMicSelfTest(). */
    juce::String getCaptureWarning() const { const juce::ScopedLock sl (warnLock); return captureWarning; }
    void setCaptureMode (CaptureMode m);   // persisted; the user's explicit device-type choice
    CaptureMode getCaptureMode() const { return audio.getCaptureMode(); }

    // ---- DSP --------------------------------------------------------------
    DspKind getDspKind() const noexcept { return mode == Mode::demo ? DspKind::virtualDemo : liveDspKind; }
    /** DSP switches return false (and change nothing) while an Auto Tune is running. A controller that a worker
        (background push, Auto Tune, AI learn) still uses is never destroyed under it: workers hold shared ownership
        (via the guard), and a replaced AI controller is killed and kept alive until the session ends. Never blocks. */
    bool useManualDsp();
    bool setDspController (std::unique_ptr<DspController> controller, DspKind kind);

    /** Drive the vendor's DSP software with a taught profile. The window provider returns the
        HWND of the DSP app (e.g. DspWindowHost::getAttached().handle). */
    void useUIAutomationDsp (const DspProfile& profile, std::function<void*()> windowProvider);

    /** Direct USB control (no UI clicking). Both connect read-only and back up the device's
        current state before the first write. Return false with an error if the unit isn't found. */
    bool useNativeMiniDsp (const MiniDspController::Options& options, juce::String& error);
    bool useNativeDsp408 (const Dsp408Controller::Options& options, juce::String& error);
    /** AI operator: Claude works out any vendor DSP window (no teaching); the learned map is replayed
        without API calls. `options.outputs` / `options.caps` default to the current config, and
        `options.agent.verify` to the handler set with setVerifyChannelHandler(). Connects (attach
        window + load saved map) but does NOT learn: if getAiOperator()->hasMap() is false, call
        getAiOperator()->learn(error) on a worker thread after the user consented. */
    bool useAiOperator (AiDspController::Options options, juce::String& error);
    AiDspController* getAiOperator() noexcept;
    /** The app side of verify_channel: solo-play a tone/sweep on a DSP output and report which driver responded. */
    void setVerifyChannelHandler (DspOperatorAgent::VerifyFn fn) { verifyChannelHandler = std::move (fn); }
    DspController& getDsp();
    std::shared_ptr<DspController> getDspShared();
    /** getDsp() wrapped in a GuardedDspController: use for EVERY write (tweeter HP floor, gain <= 0 dB, step cap). */
    DspController& getGuardedDsp();
    std::shared_ptr<GuardedDspController> getGuardedDspShared();
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

    // ---- Listening checks & imaging (process steps 8-10) ------------------
    /** Plays a listening-check track on the interface's L/R outputs (loops until stopped).
        Stopped automatically when a sweep / auto tune starts. */
    void playListeningTrack (ListeningTrack track, double levelDbFS = -20.0);
    void stopListeningTrack();
    bool isPlayingListeningTrack() const noexcept { return audio.isPlayingBack(); }
    std::optional<ListeningTrack> getCurrentListeningTrack() const noexcept
    {
        return audio.isPlayingBack() ? currentTrack : std::nullopt;
    }

    /** Moves the stereo image with levels, never delays (process rule): db > 0 moves the image
        to the RIGHT by lowering every left-side channel, db < 0 moves it LEFT by lowering the
        right side. Nudging back first restores the lowered side, so only one side is ever cut.
        Automatic DSPs are updated in the background; returns the changes as readable steps
        (for a manual DSP the user types these in). */
    juce::StringArray nudgeImage (double db);

    /** Accumulated image offset in dB (> 0: left side lowered, < 0: right side lowered). */
    double getImageBalanceDb() const noexcept { return imageBalanceDb; }

    /** Writes the whole current config (crossovers, PEQ, delay, gain, polarity) to the DSP.
        Automatic DSPs (and the Demo virtual car) are updated in the background and `automatic`
        is set; for a manual DSP nothing is sent and the returned steps are what the user has
        to type in. Returns empty while an Auto Tune is running. */
    juce::StringArray pushConfigToDsp (bool& automatic);

private:
    void autoTuneMeasurement (const Measurement& m) override;
    void autoTuneFinished (const AutoTuneEngine::Result& r) override;
    void rebuildDemoRig();
    void retireDsp();
    void applyGuardLimits();
    void saveSettings();
    void loadSettings();

    Mode mode = Mode::demo;
    DspKind liveDspKind = DspKind::manual;
    SystemConfig config;

    AudioEngine audio;
    LiveAnalyzer analyzer { audio };
    std::shared_ptr<VirtualRig> demoRig;
    std::shared_ptr<DspController> liveDsp;
    std::shared_ptr<GuardedDspController> activeGuard;
    std::shared_ptr<GuardedDspController> tuneGuard;     // held while an Auto Tune runs (it keeps the DSP + rig alive)
    std::shared_ptr<VirtualRig> tuneRig;
    std::vector<std::shared_ptr<DspController>> retiredAi;   // killed AI controllers; a learn thread may still be inside one
    std::vector<std::shared_ptr<ChannelVerifier>> retiredVerifiers;
    unsigned configRevision = 0, tuneStartRevision = 0;
    mutable juce::CriticalSection warnLock;
    juce::String captureWarning;
    void setCaptureWarning (const juce::String& w) { const juce::ScopedLock sl (warnLock); captureWarning = w; }
    bool tunePathOpen = false;   // measurement path opened by startAutoTune, restored in autoTuneFinished
    DspOperatorAgent::VerifyFn verifyChannelHandler;
    std::shared_ptr<ChannelVerifier> channelVerifier;   // default verify_channel for DspKind::aiOperator

    AutoTuneEngine autoTune;
    AutoTuneEngine::Options autoTuneOptions;
    AutoTuneEngine::Result tuneResult;
    bool hasResult = false;

    std::vector<Measurement> measurements;
    std::atomic<bool> measuringNow { false };
    std::atomic<bool> measureCancel { false };
    std::unique_ptr<juce::Thread> measureThread;

    std::unique_ptr<juce::PropertiesFile> settings;

    // Listening checks / imaging
    std::optional<ListeningTrack> currentTrack;
    double imageBalanceDb = 0.0;

    // Channel writes to an automatic DSP outside the auto tune (e.g. image nudges), done on a
    // background thread because DSP controllers may block.
    void applyChannelsInBackground (const std::vector<OutputChannel>& channels);
    void finishDspApply (bool wait = false); // drops queued writes and orphans the worker (never blocks unless wait)
    juce::CriticalSection dspApplyLock;
    std::map<juce::String, OutputChannel> dspApplyQueue;
    bool dspApplyBusy = false;               // guarded by dspApplyLock
    std::unique_ptr<juce::Thread> dspApplyThread;
    std::shared_ptr<std::atomic<bool>> dspApplyStale = std::make_shared<std::atomic<bool>> (false);
    std::vector<std::unique_ptr<juce::Thread>> orphanedApplyThreads;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Session)
};
} // namespace fa
