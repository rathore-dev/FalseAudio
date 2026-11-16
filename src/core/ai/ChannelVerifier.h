#pragma once

#include "DspOperatorAgent.h"
#include "../audio/MeasurementSource.h"
#include "../tune/SystemConfig.h"
#include <atomic>
#include <functional>
#include <map>
#include <mutex>

namespace fa
{
/** The app side of the AI's verify_channel tool: "what physically plays now on output N?".

    The DSP (its mutes/gains/polarity/delay, set by the AI through the vendor window or by the
    virtual DSP in Demo) decides what plays; the verifier only plays a short, quiet sweep through the
    measurement source (real audio engine or VirtualRig), captures it with the mic and reports:
      - presence (silent = muted / not routed), level
      - dominant band -> likely driver role (sub / midbass / midrange / tweeter / fullrange), using
        the configured channels and their expected bands
      - left / right / both feed response (the sweep is fed to L only, then R only)
      - versus a stored reference capture of the same output: level change in dB, polarity flip and
        delay change in ms (impulse cross-correlation)

    Safety: hard output-level cap (never above maxLevelDbFS, itself clamped to absoluteMaxLevelDbFS);
    the first sweep always starts at the tweeter-protected HP (tweeter-safe); full-range content is only
    played, at a further reduced level, when the first sweep shows the output is not tweeter-like and
    the configured channel is not a tweeter; any clipped capture, a cancel or the kill switch aborts.

    Blocking: call from a worker thread (the agent's). Thread-safe against concurrent calls (serialised). */
class ChannelVerifier
{
public:
    static constexpr double absoluteMaxLevelDbFS = -12.0;

    struct Options
    {
        double levelDbFS = -32.0;           // requested sweep level
        double maxLevelDbFS = -20.0;        // hard cap, clamped to absoluteMaxLevelDbFS
        double fullRangeOffsetDb = -10.0;   // extra attenuation for the full-range (low frequency) stage
        double fullRangeStartHz = 25.0;
        double sweepSeconds = 1.0;
        double tailSeconds = 0.35;
        bool   detectSide = true;           // extra L-only and R-only sweeps
        double minSnrDb = 18.0;             // below this the output counts as silent
        std::function<bool()> shouldAbort;  // e.g. [&]{ return guard.isKilled(); }
        std::function<void (const juce::String&)> log;
    };

    struct Report
    {
        bool performed = false;
        bool aborted = false;
        bool silent = false;                // nothing audible: muted or not routed
        bool roleKnown = false;
        DriverRole role = DriverRole::midbass;
        bool matches = false;               // responded driver is the claimed output's driver
        juce::String driver, band, side, detail;
        double levelDb = -120.0, centroidHz = 0.0, bandLowHz = 0.0, bandHighHz = 0.0;
        bool heardLeft = false, heardRight = false;
        bool hasReference = false;
        double levelChangeDb = 0.0;
        bool polarityFlipped = false;
        double delayChangeMs = 0.0;
        bool storedReference = false;
    };

    /** One sweep that was played (for logging and tests). */
    struct PlayedSweep { double fStartHz, levelDbFS; Feed feed; };

    using SourceFn = std::function<MeasurementSource&()>;
    using ConfigFn = std::function<const SystemConfig&()>;

    ChannelVerifier (SourceFn source, ConfigFn config, Options options = {});

    /** mode: "tone"/"sweep" identify (and compare to the reference if one exists), "reference" identifies
        and stores the capture as the reference for this output, "compare" is like "tone". output is 0-based. */
    Report verify (int output, const juce::String& mode = "tone");

    bool hasReference (int output) const;
    void clearReferences();
    void setOptions (const Options& o);
    std::vector<PlayedSweep> getPlayLog() const;

    /** Adapter for Session::setVerifyChannelHandler / DspOperatorAgent::Options::verify. */
    DspOperatorAgent::VerifyFn asVerifyFn();

    // ---- pure analysis (public for tests) ------------------------------------------------------
    struct Spectrum { double levelDb = -120, peakDb = -120, centroidHz = 0, lowHz = 0, highHz = 0; };
    static Spectrum summarise (const Measurement& m, double fromHz, double toHz);
    static bool compareImpulses (const Measurement& ref, const Measurement& cur, double& delayMs, bool& inverted);
    static DriverRole classify (double centroidHz, const std::vector<DriverRole>& candidates);

private:
    struct Capture;
    bool sweepOnce (double fStart, double level, Feed feed, Measurement& out, juce::String& err);
    double clampLevel (double dB) const;
    const OutputChannel* channelFor (const SystemConfig& cfg, int output) const;
    double protectedHz (const SystemConfig& cfg) const;

    SourceFn sourceFn;
    ConfigFn configFn;
    Options opts;
    mutable std::mutex m;
    std::map<int, Measurement> references;
    std::map<int, double> referenceFromHz;
    std::vector<PlayedSweep> playLog;
};
} // namespace fa
