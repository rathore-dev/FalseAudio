#pragma once

#include <juce_events/juce_events.h>
#include "SystemSolver.h"
#include "CrossoverPlanner.h"
#include "advanced/Optimizer.h"
#include "../audio/MeasurementSource.h"
#include "../bridge/DspController.h"

namespace fa
{
/** The one-click tuner. Runs the whole process on a background thread:

      1 Preflight     device, DSP link, signal-to-noise and clipping check
      2 Protect       safe crossovers, flat EQ, zero delay (tweeters can't be hurt)
      3 Measure       every driver alone, at every mic position
      4 Crossovers    refine points from the measured drivers
      5 Solve         polarity, time alignment, crossover phase, levels, EQ (left, then right to left)
      6 Apply         write everything to the DSP
      7 Verify        measure left, right and both; correct residual error (closed loop)
      8 Report        scores, warnings, before/after curves

    Listener callbacks always arrive on the message thread. */
class AutoTuneEngine : private juce::Thread
{
public:
    enum class Stage { idle, preflight, protect, measure, crossovers, solve, apply, verify, report, done, failed, cancelled };
    static juce::String stageName (Stage s);
    static constexpr int numWorkStages = 8;

    struct Options
    {
        int    micPositions = 3;          // total head positions (several mics capture several at once)
        double sweepSeconds = 3.0;
        double sweepLevelDbFS = -18.0;
        bool   matchRightToLeft = true;
        bool   refineCrossovers = true;
        int    maxVerifyPasses = 2;
        double acceptableErrorDb = 1.5;   // stop correcting once the side error is below this
        bool   useBandTargets = false;    // level-match bands to the house curve's per-band targets
                                          // (when config.target is a TargetLibrary built-in with bands)
        bool   competitionRefine = true;  // joint perceptual optimisation after the solver (advanced::refine)
        int    refineIterations = 200;    // ~90 % of the gain of 400 in half the time
    };

    struct UserAction
    {
        enum class Kind { moveMic, manualDsp, confirm };
        Kind kind = Kind::confirm;
        juce::String title;
        juce::String message;
        juce::StringArray steps;
    };

    struct Result
    {
        bool success = false;
        juce::String error;
        SystemConfig tunedConfig;
        SystemSolver::Report report;
        std::vector<Measurement> measurements;               // everything captured
        FrequencyResponse leftBefore, rightBefore;           // predicted with protective settings
        FrequencyResponse leftAfter, rightAfter, bothAfter;  // verified (measured) after tuning
        std::vector<double> targetDb;                        // anchored target on analysisFrequencies()
        double leftErrorDb = 0.0, rightErrorDb = 0.0, matchDb = 0.0;
        int score = 0;                                       // 0..100
        double seconds = 0.0;
        advanced::JudgeScore sqScore;                        // simulated SQ judge on the final settings
        double refineCostBefore = 0.0, refineCostAfter = 0.0;
    };

    struct Listener
    {
        virtual ~Listener() = default;
        virtual void autoTuneStageChanged (Stage) {}
        virtual void autoTuneProgress (Stage, double /*0..1 within stage*/, const juce::String& /*message*/) {}
        virtual void autoTuneMeasurement (const Measurement&) {}
        virtual void autoTuneNeedsUser (const UserAction&) {}
        virtual void autoTuneFinished (const Result&) {}
    };

    AutoTuneEngine();
    ~AutoTuneEngine() override;

    void addListener (Listener* l)    { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }

    /** Starts tuning `config` using the given source and DSP. Both must outlive the run. */
    bool start (const SystemConfig& config, MeasurementSource& source, DspController& dsp, const Options& options);
    void cancel();
    bool isRunning() const { return isThreadRunning(); }
    Stage getStage() const { return stage.load(); }

    /** Answer to autoTuneNeedsUser(): proceed (true) or abort (false). */
    void respondToUser (bool proceed);

    /** Blocking prompt usable from the tuning thread (e.g. by ManualDspController). */
    bool askUser (const UserAction& action);

    /** Optional hook called when the user is asked to move the mic (demo rig moves it itself). */
    std::function<void (int position)> onMicPositionChanged;

    Result getLastResult() const;

private:
    void run() override;
    bool runStages (Result& result);
    void setStage (Stage s);
    void progress (double fraction, const juce::String& message);
    void finish (Result result);

    std::vector<Measurement> sweep (Feed feed, const juce::String& label, juce::String& error);
    FrequencyResponse measureSide (Feed feed, const juce::StringArray& ids, const juce::String& label,
                                   std::vector<Measurement>& sink, juce::String& error);
    double sideError (const FrequencyResponse& r, const std::vector<double>& targetDb) const;

    /** Posts f to the message thread unless this engine has been destroyed by then. */
    void postToListeners (std::function<void (Listener&)> f);

    juce::ListenerList<Listener> listeners;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    std::atomic<Stage> stage { Stage::idle };
    std::atomic<bool> cancelFlag { false };

    SystemConfig config;
    MeasurementSource* source = nullptr;
    DspController* dsp = nullptr;
    Options options;

    juce::WaitableEvent userEvent;
    std::atomic<bool> userAnswer { false };

    mutable juce::CriticalSection resultLock;
    Result lastResult;

    JUCE_DECLARE_NON_COPYABLE (AutoTuneEngine)
};
} // namespace fa
