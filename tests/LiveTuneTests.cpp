// First full Auto Tune on REAL hardware (gated: FA_LIVE_TUNE=1, off by default), through the real Session/AutoTuneEngine path.
//
//   Output: the laptop's own speakers ("Speakers (Realtek(R) Audio)") as a "Full-range pair" (OUT1 = Left, OUT2 = Right),
//   no sub. Input: the UMIK-1 with its calibration file. DSP: the app's built-in software DSP (Session::useSoftwareDsp()),
//   driven through the guard like any other controller. Target: the Audiofrog house curve.
// Safety (all enforced by this test, on top of the engine's own caps): laptop speakers only; full-range HP >= 150 Hz at all
//   times (polled; the run is cancelled otherwise); output peak <= -10 dBFS (measured at the engine); <= 20 sweeps (cancelled
//   beyond that). Writes <out>/ log, PNG, CSV, Equalizer APO + text exports.
// Env: FA_LIVE_TUNE=1, FA_TUNE_OUT=<dir>, FA_TUNE_OUT_DEVICE / FA_TUNE_IN_DEVICE, FA_TUNE_FS (driver Fs, default 150), FA_TUNE_TARGET.
#include "TestRegistry.h"
#include "core/Session.h"
#include "core/bridge/Exporters.h"
#include "core/dsp/Spectrum.h"
#include "core/tune/TargetLibrary.h"
#include "core/tune/CrossoverPlanner.h"

#include <juce_graphics/juce_graphics.h>
#ifndef NOMINMAX
 #define NOMINMAX
#endif
#include <windows.h>

using fa::test::check;
using namespace fa;

namespace
{
juce::String env (const char* n, const juce::String& def = {})
{
    auto v = juce::SystemStats::getEnvironmentVariable (n, {});
    return v.isEmpty() ? def : v;
}

/** Pumps the message thread (the engine reports to its listeners with callAsync) for about `ms`. */
void pump (int ms)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
    do
    {
        MSG msg;
        while (PeekMessage (&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage (&msg);
            DispatchMessage (&msg);
        }
        juce::Thread::sleep (5);
    } while (juce::Time::getMillisecondCounter() < end);
}

bool processRunning (const juce::String& needle)
{
    juce::ChildProcess p;
    if (! p.start ("tasklist /FO CSV /NH"))
        return false;
    return p.readAllProcessOutput().containsIgnoreCase (needle);
}

struct Plot
{
    juce::Image img { juce::Image::ARGB, 1500, 1500, true, juce::SoftwareImageType() };
    juce::Graphics g { img };
    juce::Rectangle<float> r;
    double xLo = 50, xHi = 20000, yLo = 0, yHi = 1;

    float X (double f) const { return r.getX() + (float) (std::log (f / xLo) / std::log (xHi / xLo)) * r.getWidth(); }
    float Y (double v) const { return r.getBottom() - (float) ((v - yLo) / (yHi - yLo)) * r.getHeight(); }

    void frame (juce::Rectangle<float> area, double y0, double y1, double ystep, const juce::String& title)
    {
        r = area; yLo = y0; yHi = y1;
        g.setColour (juce::Colours::white);
        g.fillRect (r);
        g.setFont (13.0f);
        for (double f : { 50.0, 60.0, 70.0, 80.0, 90.0, 100.0, 200.0, 300.0, 400.0, 500.0, 600.0, 700.0, 800.0, 900.0, 1000.0, 2000.0,
                          3000.0, 4000.0, 5000.0, 6000.0, 7000.0, 8000.0, 9000.0, 10000.0, 20000.0 })
        {
            const bool major = f == 50 || f == 100 || f == 1000 || f == 10000 || f == 20000 || f == 500 || f == 5000;
            g.setColour (major ? juce::Colour (0xffbbbbbb) : juce::Colour (0xffe6e6e6));
            g.drawVerticalLine ((int) X (f), r.getY(), r.getBottom());
            if (major)
            {
                g.setColour (juce::Colours::black);
                g.drawText (f >= 1000 ? juce::String (f / 1000, 0) + "k" : juce::String (f, 0), (int) X (f) - 20, (int) r.getBottom() + 2, 40, 16, juce::Justification::centred);
            }
        }
        for (double v = y0; v <= y1 + 1e-9; v += ystep)
        {
            g.setColour (juce::Colour (0xffdddddd));
            g.drawHorizontalLine ((int) Y (v), r.getX(), r.getRight());
            g.setColour (juce::Colours::black);
            g.drawText (juce::String (v, 0), (int) r.getX() - 46, (int) Y (v) - 8, 42, 16, juce::Justification::right);
        }
        g.setColour (juce::Colours::black);
        g.drawRect (r, 1.0f);
        g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        g.drawText (title, (int) r.getX(), (int) r.getY() - 22, (int) r.getWidth(), 20, juce::Justification::left);
    }

    void line (const std::vector<double>& f, const std::vector<double>& v, juce::Colour c, float thick)
    {
        juce::Path p;
        bool started = false;
        for (size_t i = 0; i < f.size() && i < v.size(); ++i)
        {
            if (f[i] < xLo || f[i] > xHi || ! std::isfinite (v[i])) continue;
            const float y = juce::jlimit (r.getY(), r.getBottom(), Y (v[i]));
            if (! started) { p.startNewSubPath (X (f[i]), y); started = true; } else p.lineTo (X (f[i]), y);
        }
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (thick));
    }
};

/** Same maths as AutoTuneEngine::sideError: RMS deviation from the target (1/6 oct), level anchored on 200 Hz - 2 kHz. */
double errorVsTarget (const FrequencyResponse& r, const std::vector<double>& targetDb, double lowHz, double* offsetOut = nullptr)
{
    if (r.isEmpty()) return 99.0;
    const auto sm = smoothFractionalOctave (r, 6.0);
    double ref = 0.0, acc = 0.0; int n = 0;
    for (size_t i = 0; i < sm.size(); ++i)
        if (sm.freqs[i] >= 200.0 && sm.freqs[i] <= 2000.0) { ref += sm.magnitudeDb (i) - targetDb[i]; ++n; }
    ref /= std::max (1, n);
    if (offsetOut) *offsetOut = ref;
    n = 0;
    for (size_t i = 0; i < sm.size(); ++i)
        if (sm.freqs[i] >= lowHz && sm.freqs[i] <= 12000.0) { acc += std::pow (sm.magnitudeDb (i) - targetDb[i] - ref, 2.0); ++n; }
    return std::sqrt (acc / std::max (1, n));
}

std::vector<double> smoothedDb (const FrequencyResponse& r, double fraction = 6.0)
{
    return smoothFractionalOctave (r, fraction).magnitudeDb();
}

struct Recorder : AutoTuneEngine::Listener
{
    std::function<void (const juce::String&)> say;
    AudioEngine* audio = nullptr;
    juce::File dumpDir;
    int sweeps = 0;
    bool finished = false;
    AutoTuneEngine::Stage lastStage = AutoTuneEngine::Stage::idle;
    juce::String lastMsg;
    std::vector<juce::String> sweepLog;

    void autoTuneStageChanged (AutoTuneEngine::Stage s) override
    {
        lastStage = s;
        say ("STAGE " + AutoTuneEngine::stageName (s));
    }
    void autoTuneProgress (AutoTuneEngine::Stage s, double f, const juce::String& msg) override
    {
        if (msg != lastMsg)
            say ("  [" + AutoTuneEngine::stageName (s) + " " + juce::String (juce::roundToInt (f * 100.0)) + "%] " + msg);
        lastMsg = msg;
    }
    void autoTuneMeasurement (const Measurement& m) override
    {
        ++sweeps;
        if (dumpDir != juce::File())   // raw impulse response (float32) + its zero sample, for offline analysis
        {
            auto f = dumpDir.getChildFile ("ir_" + juce::String (sweeps) + "_" + m.name.replaceCharacters (" .", "__") + ".f32");
            f.replaceWithData (m.ir.data(), m.ir.size() * sizeof (float));
            dumpDir.getChildFile ("ir_" + juce::String (sweeps) + ".txt").replaceWithText ("zeroSample " + juce::String (m.zeroSample) + " peakSample " + juce::String (m.peakSample) + " sr " + juce::String (m.sampleRate));
        }
        say ("  SWEEP " + juce::String (sweeps) + ": '" + m.name + "' snr " + juce::String (m.snrDb, 1) + " dB, clipped=" + juce::String ((int) m.clipped)
             + ", arrival(onset) " + juce::String (m.onsetMs, 2) + " ms, IR peak " + juce::String (m.peakDbFS, 1) + " dBFS @ "
             + juce::String ((m.peakSample - m.zeroSample) * 1000.0 / m.sampleRate, 2) + " ms"
             + (audio != nullptr && audio->getDeviceManager().getCurrentAudioDevice() != nullptr
                    ? ", device xruns " + juce::String (audio->getDeviceManager().getCurrentAudioDevice()->getXRunCount()) : juce::String()));
    }
    void autoTuneNeedsUser (const AutoTuneEngine::UserAction& a) override { say ("  NEEDS USER: " + a.title); }
    void autoTuneFinished (const AutoTuneEngine::Result&) override { finished = true; }
};

struct SettingsBackup
{
    juce::File file, bak;
    bool had = false;
    SettingsBackup()
    {
        file = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("FalseAudio/FalseAudio.settings");
        bak = file.withFileExtension ("settings.livetune.bak");
        had = file.existsAsFile();
        if (had) file.copyFileTo (bak);
    }
    ~SettingsBackup()
    {
        if (had) { bak.copyFileTo (file); bak.deleteFile(); }
        else file.deleteFile();
    }
};
} // namespace

FA_TEST (liveFullAutoTune)
{
    if (env ("FA_LIVE_TUNE") != "1")
    {
        std::cout << "  (gated off: set FA_LIVE_TUNE=1)\n";
        return;
    }
    if (processRunning ("roomeqwizard"))
    {
        check (false, "REW (roomeqwizard) is running - close it and rerun");
        return;
    }

    const juce::String outName = env ("FA_TUNE_OUT_DEVICE", "Speakers (Realtek(R) Audio)");
    const juce::String inName  = env ("FA_TUNE_IN_DEVICE", "Microphone (Umik-1  Gain: 18dB)");
    const juce::File outDir = juce::File (env ("FA_TUNE_OUT", juce::File::getSpecialLocation (juce::File::tempDirectory).getFullPathName() + "\\live_tune1"));
    outDir.createDirectory();
    juce::FileLogger log (outDir.getChildFile ("live_tune1.log"), "live tune", 0);
    auto say = [&] (const juce::String& s) { std::cout << "  " << s << std::endl; log.logMessage (s); };

    SettingsBackup settingsBackup;   // Session writes the user's settings file; restored at the end

    {
        Session session;
        auto& audio = session.getAudio();
        audio.initialise();
        auto& dm = audio.getDeviceManager();
        dm.setCurrentAudioDeviceType ("Windows Audio", true);
        juce::AudioDeviceManager::AudioDeviceSetup setup;
        dm.getAudioDeviceSetup (setup);
        setup.inputDeviceName = inName;
        setup.outputDeviceName = outName;
        setup.sampleRate = 48000.0;
        setup.bufferSize = 0;
        setup.useDefaultInputChannels = false;
        setup.useDefaultOutputChannels = false;
        setup.inputChannels.clear();  setup.inputChannels.setRange (0, 1, true);
        setup.outputChannels.clear(); setup.outputChannels.setRange (0, 2, true);
        const auto err = dm.setAudioDeviceSetup (setup, true);
        check (err.isEmpty() && dm.getCurrentAudioDevice() != nullptr, "devices opened", err);
        if (err.isNotEmpty() || dm.getCurrentAudioDevice() == nullptr)
            return;
        {
            auto s = dm.getAudioDeviceSetup();
            check (s.inputDeviceName == inName && s.outputDeviceName == outName, "exactly the laptop speakers + UMIK-1 are open", s.outputDeviceName);
            if (s.outputDeviceName != outName || s.inputDeviceName != inName) return;
        }

        AudioRouting routing;
        routing.leftOutput = 0; routing.rightOutput = 1; routing.micInputs = { 0 };
        audio.setRouting (routing);
        const auto cal = MicCalibration::load (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                                                   .getChildFile ("FalseAudio/calibration/7165149_0.txt"));
        check (cal.isValid(), "UMIK calibration loaded");
        audio.setCalibration (0, cal);

        // ---- system: Full-range pair, laptop drivers, no sub, Audiofrog house curve --------------------------------------
        auto cfg = SystemConfig::makePreset (SystemConfig::Preset::fullRangePair);
        const double fsHz = env ("FA_TUNE_FS", "150").getDoubleValue();
        check (cfg.channels.size() == 2 && ! cfg.hasSubwoofer(), "preset: 2 full-range outputs, no sub");
        for (auto& c : cfg.channels)
        {
            c.driver.model = "Laptop speaker";
            c.driver.sizeInches = 1.5;
            c.driver.fsHz = fsHz;           // measured: -6 dB at ~170 Hz; 400 Hz would be a mid-range guess, 150 Hz is what we saw
            c.label = c.side == Side::left ? "OUT1 Laptop Left" : "OUT2 Laptop Right";
        }
        const auto* te = TargetLibrary::findByName (env ("FA_TUNE_TARGET", "Audiofrog"));
        check (te != nullptr, "house curve found");
        if (te == nullptr) return;
        cfg.target = te->curve;
        say ("target: " + cfg.target.name + "; drivers: 1.5\" Fs " + juce::String (fsHz, 0) + " Hz");

        session.setMode (Session::Mode::live);
        check (session.useSoftwareDsp(), "built-in software DSP selected");
        check (session.setConfig (cfg), "config set");
        check (session.getSoftwareDsp() != nullptr, "software DSP active");

        auto& opt = session.getAutoTuneOptions();
        opt.micPositions = 1;
        opt.sweepSeconds = 4.0;
        opt.sweepLevelDbFS = -18.0;
        opt.maxVerifyPasses = 2;

        // ---- one measured "before" sweep with both speakers (the tune measures L and R separately), protective HP only -------------
        FrequencyResponse lrBeforeMeasured;
        {
            bool pathFailed = false;
            audio.setCaptureMode (CaptureMode::autoPreferExclusive);
            audio.beginMeasurementPath (&pathFailed);
            check (! pathFailed, "measurement path for the before L+R sweep");
            if (pathFailed) return;
            auto protectedCfg = session.getConfig();
            for (auto& c : protectedCfg.channels) c.settings = {};
            CrossoverPlanner::apply (protectedCfg, CrossoverPlanner::planDefaults (protectedCfg));
            juce::String e;
            session.getGuardedDsp().applyAll (protectedCfg, e);
            session.getGuardedDsp().soloChannels (protectedCfg, { "FL.fullrange", "FR.fullrange" }, e);
            SweepRequest req;
            req.sweep.seconds = 4.0; req.sweep.levelDbFS = -18.0; req.sweep.fStart = 10.0; req.sweep.fEnd = 24000.0;
            req.sweep.sampleRate = audio.getSampleRate();
            req.feed = Feed::both; req.label = "Before L+R";
            std::atomic<bool> noCancel { false };
            auto ms = audio.measureSweep (req, noCancel, e);
            audio.endMeasurementPath();
            check (! ms.empty(), "before L+R sweep", e);
            if (ms.empty()) return;
            lrBeforeMeasured = ms[0].response;
            say ("before L+R sweep: snr " + juce::String (ms[0].snrDb, 1) + " dB, IR peak " + juce::String (ms[0].peakDbFS, 1) + " dBFS");
        }

        Recorder rec;
        rec.sweeps = 1;                 // the before sweep counts against the 20-sweep budget
        rec.say = say;
        rec.audio = &audio;
        rec.dumpDir = outDir.getChildFile ("ir");
        rec.dumpDir.createDirectory();
        session.getAutoTune().addListener (&rec);

        juce::String error;
        const bool started = session.startAutoTune (error);
        say ("capture warning: '" + session.getCaptureWarning() + "'");
        check (started, "auto tune started", error);
        if (! started) { session.getAutoTune().removeListener (&rec); return; }

        // ---- run, monitoring the safety limits ---------------------------------------------------------------------------
        const auto t0 = juce::Time::getMillisecondCounter();
        double maxOutPeak = -200.0, minHp = 1e9;
        bool cancelledForSafety = false;
        juce::String safetyReason;
        while (! rec.finished)
        {
            pump (40);
            maxOutPeak = std::max (maxOutPeak, (double) audio.getOutputPeakDb());
            if (auto* sw = session.getSoftwareDsp())
                for (int o = 0; o < 2; ++o)
                {
                    ChannelSettings s;
                    if (sw->getSettings (o, s))
                    {
                        const double hp = s.highPass.enabled ? s.highPass.freq : 0.0;
                        minHp = std::min (minHp, hp);
                        if (hp < 150.0 && ! cancelledForSafety)
                        {
                            cancelledForSafety = true;
                            safetyReason = "full-range HP below 150 Hz on output " + juce::String (o + 1);
                            session.getAutoTune().cancel();
                        }
                    }
                }
            if (maxOutPeak > -9.9 && ! cancelledForSafety)
            {
                cancelledForSafety = true;
                safetyReason = "output peak " + juce::String (maxOutPeak, 1) + " dBFS above -10";
                session.getAutoTune().cancel();
            }
            if (rec.sweeps >= 19 && ! cancelledForSafety)
            {
                cancelledForSafety = true;
                safetyReason = "sweep budget (20) reached";
                session.getAutoTune().cancel();
            }
            if (juce::Time::getMillisecondCounter() - t0 > 12 * 60 * 1000 && ! cancelledForSafety)
            {
                cancelledForSafety = true;
                safetyReason = "time limit";
                session.getAutoTune().cancel();
            }
        }
        session.getAutoTune().removeListener (&rec);
        for (int i = 0; i < 20; ++i) pump (20);

        say ("SAFETY: sweeps=" + juce::String (rec.sweeps) + " (<=20), max output peak " + juce::String (maxOutPeak, 1) + " dBFS (<= -10), min HP seen "
             + juce::String (minHp, 0) + " Hz (>= 150)" + (cancelledForSafety ? "  CANCELLED: " + safetyReason : juce::String()));
        check (! cancelledForSafety, "no safety limit hit", safetyReason);
        check (rec.sweeps <= 20, "<= 20 sweeps");
        check (maxOutPeak <= -9.9, "output peak <= -10 dBFS");
        check (minHp >= 150.0, "HP >= 150 Hz at all times");
        check (session.hasTuneResult(), "tune finished with a result", session.getTuneResult().error);
        if (! session.hasTuneResult()) return;

        const auto& R = session.getTuneResult();
        const auto& tc = R.tunedConfig;
        const double lowHz = tc.scoredLowHz (40.0);

        // ---- before / after ------------------------------------------------------------------------------------------------
        FrequencyResponse lrBefore = lrBeforeMeasured;            // measured both-speaker sweep, protective settings only
        FrequencyResponse lrBeforePredicted = R.leftBefore;
        lrBeforePredicted += R.rightBefore;
        {
            double o = 0;
            say ("L+R before: measured error " + juce::String (errorVsTarget (lrBefore, R.targetDb, tc.scoredLowHz (40.0), &o), 2)
                 + " dB, sum-of-individual-measurements prediction " + juce::String (errorVsTarget (lrBeforePredicted, R.targetDb, tc.scoredLowHz (40.0), &o), 2) + " dB");
        }
        const auto& tgt = R.targetDb;
        double offLb = 0, offRb = 0, offLRb = 0, offLa = 0, offRa = 0, offLRa = 0;
        const double eLb = errorVsTarget (R.leftBefore, tgt, lowHz, &offLb), eRb = errorVsTarget (R.rightBefore, tgt, lowHz, &offRb);
        const double eLRb = errorVsTarget (lrBefore, tgt, lowHz, &offLRb);
        const double eLa = errorVsTarget (R.leftAfter, tgt, lowHz, &offLa), eRa = errorVsTarget (R.rightAfter, tgt, lowHz, &offRa);
        const double eLRa = errorVsTarget (R.bothAfter, tgt, lowHz, &offLRa);
        say ("scored band: " + juce::String (lowHz, 0) + " Hz - 12 kHz, 1/6 oct, level anchored on 200 Hz - 2 kHz");
        say ("ERROR vs target (dB rms)   before -> after:  L " + juce::String (eLb, 2) + " -> " + juce::String (eLa, 2)
             + "   R " + juce::String (eRb, 2) + " -> " + juce::String (eRa, 2)
             + "   L+R " + juce::String (eLRb, 2) + " -> " + juce::String (eLRa, 2));
        say ("engine verified errors: L " + juce::String (R.leftErrorDb, 2) + ", R " + juce::String (R.rightErrorDb, 2) + ", L/R match " + juce::String (R.matchDb, 2) + " dB rms");
        say ("SCORES: tune " + juce::String (R.score) + " / 100, SQ judge " + juce::String (R.sqScore.total, 1) + " / 100");
        for (auto& [k, v] : R.sqScore.categories)
            say ("   judge " + k + ": " + juce::String (v, 1));
        for (auto& c : R.sqScore.comments) say ("   judge note: " + c);
        say ("tune seconds " + juce::String (R.seconds, 1) + ", measurements " + juce::String ((int) R.measurements.size()));
        for (auto& n : R.report.notes) say ("NOTE: " + n);
        for (auto& w : R.report.warnings) say ("WARNING: " + w);
        for (auto& [id, ms] : R.report.arrivalMs) say ("arrival " + id + ": " + juce::String (ms, 3) + " ms");
        for (auto& [f, d] : R.report.sumDips) say ("L+R sum dip: " + juce::String (f, 0) + " Hz, " + juce::String (d, 1) + " dB");
        if (auto* guard = session.getGuardedDspShared().get())
            for (auto& n : guard->takeNotes()) say ("GUARD: " + n);

        // ---- does the measured "after" match what the solver predicted from its own "before" + the written EQ? --------------------
        {
            auto predicted = [&] (const FrequencyResponse& before, Side side)
            {
                FrequencyResponse r = before;
                for (auto& c : tc.channels)
                    if (c.side == side)
                    {
                        auto eq = chainResponse (c.settings.peq, r.freqs, tc.caps.sampleRate);
                        r.scale (magFromDb (c.settings.gainDb));
                        r *= eq;
                    }
                return r;
            };
            const auto pl = predicted (R.leftBefore, Side::left), pr = predicted (R.rightBefore, Side::right);
            auto rmsDiff = [&] (const FrequencyResponse& meas, const FrequencyResponse& pred)
            {
                const auto m = smoothFractionalOctave (meas, 6.0), q = smoothFractionalOctave (pred, 6.0);
                double mean = 0.0, acc = 0.0; int n = 0;
                for (size_t i = 0; i < m.size(); ++i)
                    if (m.freqs[i] >= 235.0 && m.freqs[i] <= 12000.0) { mean += m.magnitudeDb (i) - q.magnitudeDb (i); ++n; }
                mean /= std::max (1, n);
                for (size_t i = 0; i < m.size(); ++i)
                    if (m.freqs[i] >= 235.0 && m.freqs[i] <= 12000.0) acc += std::pow (m.magnitudeDb (i) - q.magnitudeDb (i) - mean, 2.0);
                return std::pair<double, double> { std::sqrt (acc / std::max (1, n)), mean };
            };
            const auto dl = rmsDiff (R.leftAfter, pl), dr = rmsDiff (R.rightAfter, pr);
            say ("PREDICTION CHECK (measured after vs before + written chain, 235 Hz-12 kHz): L rms " + juce::String (dl.first, 2) + " dB (mean offset "
                 + juce::String (dl.second, 2) + "), R rms " + juce::String (dr.first, 2) + " dB (mean offset " + juce::String (dr.second, 2) + ")");
        }

        // ---- final settings + sanity ----------------------------------------------------------------------------------------
        bool boostOk = true, noNarrowLow = true;
        for (auto& c : tc.channels)
        {
            const auto& s = c.settings;
            say ("FINAL " + c.id + " (" + c.label + ", DSP out " + juce::String (c.dspOutput + 1) + "): gain " + juce::String (s.gainDb, 2) + " dB, delay "
                 + juce::String (s.delayMs, 3) + " ms (" + juce::String (msToMetres (s.delayMs) * 100.0, 1) + " cm), polarity " + (s.inverted ? "INVERTED" : "normal")
                 + ", HP " + (s.highPass.enabled ? s.highPass.describe() : juce::String ("off")) + ", LP " + (s.lowPass.enabled ? s.lowPass.describe() : juce::String ("off")));
            for (auto& b : s.peq)
            {
                say ("      PEQ " + b.describe());
                if (b.gainDb > tc.safety.maxBoostDb + 1e-6) boostOk = false;
                if (b.freq < 150.0 && b.q > 3.0) noNarrowLow = false;
            }
        }
        check (boostOk, "no EQ boost above the safety limit (" + juce::String (tc.safety.maxBoostDb, 1) + " dB)");
        check (noNarrowLow, "no narrow (Q > 3) bands below 150 Hz (fan-noise region)");

        // ---- files ---------------------------------------------------------------------------------------------------------
        const auto freqs = R.leftAfter.freqs;
        {
            juce::String csv = "freq_hz,target_db,L_before,R_before,LR_before,L_after,R_after,LR_after\n";
            const auto a = smoothedDb (R.leftBefore), b = smoothedDb (R.rightBefore), c = smoothedDb (lrBefore);
            const auto d = smoothedDb (R.leftAfter), e = smoothedDb (R.rightAfter), f = smoothedDb (R.bothAfter);
            for (size_t i = 0; i < freqs.size(); ++i)
                csv << juce::String (freqs[i], 2) << "," << juce::String (tgt[i], 2) << "," << juce::String (a[i], 2) << "," << juce::String (b[i], 2) << ","
                    << juce::String (c[i], 2) << "," << juce::String (d[i], 2) << "," << juce::String (e[i], 2) << "," << juce::String (f[i], 2) << "\n";
            outDir.getChildFile ("fr.csv").replaceWithText (csv);
        }
        outDir.getChildFile ("settings_eqapo.txt").replaceWithText (exporters::toEqualizerAPO (tc));
        outDir.getChildFile ("settings_sheet.txt").replaceWithText (exporters::toText (tc));
        outDir.getChildFile ("preset.json").replaceWithText (exporters::toJSON (tc));

        {
            Plot p;
            p.g.fillAll (juce::Colour (0xfff4f4f4));
            struct Panel { const char* title; const FrequencyResponse *before, *after; double offB, offA; };
            const Panel panels[] = { { "Left speaker (OUT1)", &R.leftBefore, &R.leftAfter, offLb, offLa },
                                     { "Right speaker (OUT2)", &R.rightBefore, &R.rightAfter, offRb, offRa },
                                     { "L+R together", &lrBefore, &R.bothAfter, offLRb, offLRa } };
            int row = 0;
            double t1k = 0.0;
            for (size_t i = 0; i < freqs.size(); ++i)
                if (freqs[i] >= 1000.0) { t1k = tgt[i]; break; }
            auto tgtRel = tgt;
            for (auto& v : tgtRel) v -= t1k;
            for (auto& pn : panels)
            {
                // every curve is shown level-anchored to the target (200 Hz - 2 kHz mean = target), so the SHAPE error is what you see
                auto b = smoothedDb (*pn.before), a = smoothedDb (*pn.after);
                for (auto& v : b) v -= pn.offB + t1k;
                for (auto& v : a) v -= pn.offA + t1k;
                const double top = 20.0;
                const double e0 = pn.before == &R.leftBefore ? eLb : pn.before == &R.rightBefore ? eRb : eLRb;
                const double e1 = pn.after == &R.leftAfter ? eLa : pn.after == &R.rightAfter ? eRa : eLRa;
                p.frame ({ 70.f, (float) (50 + row * 480), 1380.f, 400.f }, -30.0, top, 5.0,
                         juce::String (pn.title) + "  -  error vs target (rms, " + juce::String (lowHz, 0) + " Hz-12 kHz): before " + juce::String (e0, 2)
                             + " dB -> after " + juce::String (e1, 2) + " dB");
                p.line (freqs, tgtRel, juce::Colour (0xff202020), 2.5f);
                p.line (freqs, b, juce::Colour (0xffe08a00), 2.0f);
                p.line (freqs, a, juce::Colour (0xff1060d0), 2.5f);
                ++row;
            }
            p.g.setFont (14.0f);
            p.g.setColour (juce::Colours::black);
            p.g.drawText ("black = target (" + cfg.target.name + "), orange = before (measured drivers, protective HP only), blue = after (verified measurement, software DSP active). 1/6 oct, level-anchored 200 Hz-2 kHz, dB relative.",
                          70, 1480 - 22, 1380, 16, juce::Justification::left);
            auto png = outDir.getChildFile ("live_tune1.png");
            png.deleteFile();
            juce::FileOutputStream fo (png);
            juce::PNGImageFormat fmt;
            check (fo.openedOk() && fmt.writeImageToStream (p.img, fo), "PNG written", png.getFullPathName());
        }
        check (R.success, "tune reported success", R.error);
        say ("done");
    }
}
