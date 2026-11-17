// FIRST real acoustic measurement through the app's measurement chain (gated: FA_LIVE_SWEEP=1, off by default).
//
//   Output: laptop speakers only ("Speakers (Realtek(R) Audio)").  Input: UMIK-1 ("Microphone (Umik-1  Gain: 18dB)").
//   - 1 kHz sine level probe, starting at -30 dBFS and stepping up <= 6 dB (hard ceiling -10 dBFS) until the mic sees a
//     comfortable level;
//   - 2 log sweeps (AudioEngine::measureSweep -> deconvolve -> Measurement::analyse -> calibration);
//   - silent noise floor, THD (Farina), latency, sweep-to-sweep repeatability, PNG plot + CSV.
// Env: FA_LIVE_SWEEP=1, FA_SWEEP_OUT=<dir>, FA_SWEEP_OUT_DEVICE / FA_SWEEP_IN_DEVICE to override the device names,
//      FA_SWEEP_START_DB (default -30), FA_SWEEP_TARGET_SPL (default 70).
#include "TestRegistry.h"
#include "core/audio/AudioEngine.h"
#include "core/audio/EndpointVolume.h"
#include "core/dsp/Spectrum.h"
#include "core/measure/Analysis.h"
#include "core/measure/MicSelfTest.h"

#include <juce_graphics/juce_graphics.h>

using fa::test::check;
using namespace fa;

namespace
{
juce::String env (const char* n, const juce::String& def = {})
{
    auto v = juce::SystemStats::getEnvironmentVariable (n, {});
    return v.isEmpty() ? def : v;
}

double rmsDb (const std::vector<float>& x, size_t from = 0, size_t to = 0)
{
    if (to == 0 || to > x.size()) to = x.size();
    double s = 0.0;
    for (size_t i = from; i < to; ++i) s += (double) x[i] * x[i];
    return 10.0 * std::log10 (std::max (s / (double) std::max<size_t> (1, to - from), 1e-20));
}

double peakDbOf (const std::vector<float>& x)
{
    float p = 0.0f;
    for (auto v : x) p = std::max (p, std::abs (v));
    return 20.0 * std::log10 (std::max ((double) p, 1e-9));
}

struct Plot
{
    juce::Image img { juce::Image::ARGB, 1500, 1100, true, juce::SoftwareImageType() };
    juce::Graphics g { img };
    juce::Rectangle<float> r;
    double xLo = 20, xHi = 20000, yLo = 0, yHi = 1;

    float X (double f) const { return r.getX() + (float) (std::log (f / xLo) / std::log (xHi / xLo)) * r.getWidth(); }
    float Y (double v) const { return r.getBottom() - (float) ((v - yLo) / (yHi - yLo)) * r.getHeight(); }

    void frame (juce::Rectangle<float> area, double y0, double y1, double ystep, const juce::String& title, const juce::String& unit)
    {
        r = area; yLo = y0; yHi = y1;
        g.setColour (juce::Colours::white);
        g.fillRect (r);
        g.setFont (13.0f);
        for (double f : { 20.0, 30.0, 40.0, 50.0, 60.0, 70.0, 80.0, 90.0, 100.0, 200.0, 300.0, 400.0, 500.0, 600.0, 700.0, 800.0, 900.0,
                          1000.0, 2000.0, 3000.0, 4000.0, 5000.0, 6000.0, 7000.0, 8000.0, 9000.0, 10000.0, 20000.0 })
        {
            const bool major = f == 20 || f == 100 || f == 1000 || f == 10000 || f == 20000;
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
        g.setFont (13.0f);
        g.drawText (unit, (int) r.getRight() - 160, (int) r.getY() - 22, 160, 20, juce::Justification::right);
    }

    void line (const std::vector<double>& f, const std::vector<double>& v, juce::Colour c, float thick)
    {
        juce::Path p;
        bool started = false;
        for (size_t i = 0; i < f.size(); ++i)
        {
            if (f[i] < xLo || f[i] > xHi || ! std::isfinite (v[i])) continue;
            const float y = juce::jlimit (r.getY(), r.getBottom(), Y (v[i]));
            if (! started) { p.startNewSubPath (X (f[i]), y); started = true; } else p.lineTo (X (f[i]), y);
        }
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (thick));
    }
};

double dbAt (const std::vector<double>& fs, const std::vector<double>& db, double f)
{
    if (f <= fs.front()) return db.front();
    for (size_t i = 1; i < fs.size(); ++i)
        if (fs[i] >= f)
        {
            const double t = std::log (f / fs[i - 1]) / std::log (fs[i] / fs[i - 1]);
            return db[i - 1] + t * (db[i] - db[i - 1]);
        }
    return db.back();
}
} // namespace

FA_TEST (liveSweepFirstMeasurement)
{
    if (env ("FA_LIVE_SWEEP") != "1")
    {
        std::cout << "  (gated off: set FA_LIVE_SWEEP=1)\n";
        return;
    }

    const juce::String outName = env ("FA_SWEEP_OUT_DEVICE", "Speakers (Realtek(R) Audio)");
    const juce::String inName  = env ("FA_SWEEP_IN_DEVICE", "Microphone (Umik-1  Gain: 18dB)");
    const juce::File outDir = juce::File (env ("FA_SWEEP_OUT", juce::File::getSpecialLocation (juce::File::tempDirectory).getFullPathName() + "\\sweep1"));
    outDir.createDirectory();
    const juce::String tag = env ("FA_SWEEP_TAG", "sweep2");
    juce::FileLogger log (outDir.getChildFile (tag + ".log"), "live sweep", 0);
    auto say = [&] (const juce::String& s) { std::cout << "  " << s << "\n"; log.logMessage (s); };

    // ---- devices -------------------------------------------------------------------------------------------------------
    AudioEngine engine;
    engine.initialise();
    auto& dm = engine.getDeviceManager();
    dm.setCurrentAudioDeviceType ("Windows Audio", true);
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    dm.getAudioDeviceSetup (setup);
    setup.inputDeviceName = inName;
    setup.outputDeviceName = outName;
    setup.sampleRate = 48000.0;
    setup.bufferSize = 0;
    setup.useDefaultInputChannels = false;
    setup.useDefaultOutputChannels = false;
    setup.inputChannels.clear();  setup.inputChannels.setRange (0, 1, true);   // mono mic: channel 0 only
    setup.outputChannels.clear(); setup.outputChannels.setRange (0, 2, true);  // stereo speakers
    const auto err = dm.setAudioDeviceSetup (setup, true);
    check (err.isEmpty(), "devices opened", err);
    if (err.isNotEmpty() || dm.getCurrentAudioDevice() == nullptr)
        return;
    auto* dev = dm.getCurrentAudioDevice();
    say ("type=" + dev->getTypeName() + " name=" + dev->getName() + " sr=" + juce::String (dev->getCurrentSampleRate())
         + " buf=" + juce::String (dev->getCurrentBufferSizeSamples()) + " inCh=" + dev->getActiveInputChannels().toString (2)
         + " outCh=" + dev->getActiveOutputChannels().toString (2)
         + " latency in/out=" + juce::String (dev->getInputLatencyInSamples()) + "/" + juce::String (dev->getOutputLatencyInSamples()));
    {
        auto s = dm.getAudioDeviceSetup();
        say ("setup in='" + s.inputDeviceName + "' out='" + s.outputDeviceName + "'");
        check (s.inputDeviceName == inName && s.outputDeviceName == outName, "exactly the requested devices are open");
    }
    check (dev->getCurrentSampleRate() == 48000.0, "sample rate 48 kHz", juce::String (dev->getCurrentSampleRate()));

    AudioRouting routing;
    routing.leftOutput = 0; routing.rightOutput = 1; routing.micInputs = { 0 };
    engine.setRouting (routing);

    const auto cal = MicCalibration::load (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                                               .getChildFile ("FalseAudio/calibration/7165149_0.txt"));
    check (cal.isValid() && cal.sensitivityDb != 0.0, "calibration loaded", juce::String (cal.sensitivityDb, 3));
    engine.setCalibration (0, cal);

    const auto ep = readCaptureEndpointVolume ("umik");
    const double epDb = ep.valid ? ep.levelDb : 0.0;
    say ("UMIK endpoint: found=" + juce::String ((int) ep.found) + " valid=" + juce::String ((int) ep.valid) + " " + juce::String (epDb, 2) + " dB");
    const double splOffset = micSplOffsetDb (cal.sensitivityDb, epDb);   // + 10log(ms) -> dB SPL

    // Measurement path: exclusive WASAPI preferred (FA_SWEEP_MODE=shared|exclusive|auto overrides), restored at the end.
    const auto modeEnv = env ("FA_SWEEP_MODE", "auto");
    engine.setCaptureMode (modeEnv == "shared" ? CaptureMode::shared : modeEnv == "exclusive" ? CaptureMode::exclusive : CaptureMode::autoPreferExclusive);
    bool pathFailed = false;
    const auto pathNote = engine.beginMeasurementPath (&pathFailed);
    struct PathGuard { AudioEngine& e; ~PathGuard() { e.endMeasurementPath(); } } pathGuard { engine };
    check (! pathFailed, "measurement path opened", pathNote);
    if (pathFailed) return;
    dev = dm.getCurrentAudioDevice();
    say ("MEASUREMENT PATH: type=" + dm.getCurrentAudioDeviceType() + " exclusive=" + juce::String ((int) engine.isMeasurementPathExclusive())
         + " sr=" + juce::String (dev->getCurrentSampleRate()) + " buf=" + juce::String (dev->getCurrentBufferSizeSamples())
         + " latency in/out=" + juce::String (dev->getInputLatencyInSamples()) + "/" + juce::String (dev->getOutputLatencyInSamples())
         + " note='" + pathNote + "'");
    {
        auto s = dm.getAudioDeviceSetup();
        check (s.inputDeviceName == inName && s.outputDeviceName == outName, "same devices after the path switch", s.inputDeviceName + " / " + s.outputDeviceName);
    }
    const double fs = dev->getCurrentSampleRate();
    std::atomic<bool> cancel { false };

    // ---- silent noise floor (nothing is played) -------------------------------------------------------------------------
    std::vector<float> silence; double srRec = 0; juce::String e2;
    check (engine.recordInput (3.0, silence, srRec, cancel, e2), "silent noise recording", e2);
    const auto noiseRep = analyseMicRecording (silence, srRec, &cal, &ep);
 
    say ("PROCESSING suspected=" + juce::String ((int) noiseRep.processingSuspected) + " verdict=" + MicSelfTestReport::verdictName (noiseRep.verdict));
    check (! noiseRep.processingSuspected, "no Windows capture processing detected on the measurement path", noiseRep.message);
    say ("NOISE: Z " + juce::String (noiseRep.levelZDbSpl, 1) + " dB SPL, A " + juce::String (noiseRep.levelADbSpl, 1)
         + " dB(A) SPL, peak " + juce::String (noiseRep.peakDbFS, 1) + " dBFS, rms " + juce::String (noiseRep.rmsDbFS, 1)
         + " dBFS, hum=" + juce::String ((int) noiseRep.humDetected));

    // ---- level probe: 1 kHz sine, <= 1 s, from -30 dBFS in <= 6 dB steps, ceiling -10 dBFS -------------------------------
    const double target = env ("FA_SWEEP_TARGET_SPL", "70").getDoubleValue();
    double sweepDb = env ("FA_SWEEP_START_DB", "-30").getDoubleValue();
    const double ceilingDb = -10.0;
    bool stop = false;
    const bool noProbe = env ("FA_SWEEP_NOPROBE") == "1";
    for (int probe = 0; probe < 5 && ! stop && ! noProbe; ++probe)
    {
        sweepDb = std::min (sweepDb, ceilingDb);
        GeneratorSettings gs;
        gs.signal = GeneratorSettings::Signal::sine;
        gs.sineHz = 1000.0;
        gs.levelDbFS = sweepDb - 3.0103;                    // RMS level of a sine whose peak equals the sweep peak
        gs.feed = Feed::both;
        engine.getInputPeakDb (0); engine.getOutputPeakDb();
        engine.setGenerator (gs);
        juce::Thread::sleep (700);
        std::vector<float> buf (24000);
        engine.copyLatestInput (0, buf.data(), (int) buf.size());   // last 0.5 s
        const double inPk = engine.getInputPeakDb (0), outPk = engine.getOutputPeakDb();
        gs.signal = GeneratorSettings::Signal::off;
        engine.setGenerator (gs);
        // 1 kHz level through the calibration (deviation at 1 kHz) in dB SPL
        const double spl = rmsDb (buf) + splOffset - cal.deviationDb (1000.0);
        say ("PROBE " + juce::String (probe + 1) + ": sweep/peak level " + juce::String (sweepDb, 1) + " dBFS (sine rms "
             + juce::String (gs.levelDbFS, 1) + "), out peak " + juce::String (outPk, 1) + " dBFS, mic peak " + juce::String (inPk, 1)
             + " dBFS, mic 1 kHz = " + juce::String (spl, 1) + " dB SPL");
        check (inPk < -1.0 && outPk < -9.5, "probe: no clipping, within the output ceiling");
        if (inPk >= -1.0) { stop = true; break; }
        if (spl >= target - 5.0 || sweepDb >= ceilingDb) break;
        const double need = target - spl;
        sweepDb += juce::jlimit (1.0, 6.0, need);
        juce::Thread::sleep (300);
    }
    if (stop) return;
    say ("SWEEP LEVEL chosen: " + juce::String (sweepDb, 1) + " dBFS");

    // ---- 2 sweeps ---------------------------------------------------------------------------------------------------------
    SweepRequest req;
    req.sweep.sampleRate = fs;
    req.sweep.fStart = 20.0;
    req.sweep.fEnd = 20000.0;
    req.sweep.seconds = 6.0;
    req.sweep.tailSeconds = 1.0;
    req.sweep.levelDbFS = sweepDb;
    req.feed = Feed::both;
    req.label = "laptop";

    std::vector<Measurement> sweeps;
    const int nSweeps = env ("FA_SWEEP_COUNT", "2").getIntValue();
    for (int i = 0; i < nSweeps; ++i)
    {
        check (sweepDb <= ceilingDb + 1e-9, "sweep level within the -10 dBFS ceiling");
        if (sweepDb > ceilingDb) return;
        juce::String e;
        auto res = engine.measureSweep (req, cancel, e);
        say ("sweep " + juce::String (i + 1) + ": results=" + juce::String ((int) res.size()) + " msg='" + e.replace ("\n", " | ") + "'");
        check (! res.empty(), "sweep produced a measurement");
        if (res.empty()) return;
        check (! res[0].clipped, "sweep did not clip");
        sweeps.push_back (std::move (res[0]));
        juce::Thread::sleep (800);
    }

    // ---- analysis ---------------------------------------------------------------------------------------------------------
    // dB SPL of a sine of peak level `sweepDb` through |H|: SPL = sweepDb + H + 124.02 - 3.01 - sens - endpoint
    const double fsOffset = sweepDb + splOffset - 3.0103;
    std::vector<std::vector<double>> rawSpl, smSpl;
    for (auto& m : sweeps)
    {
        auto raw = m.response.magnitudeDb();
        for (auto& v : raw) v += fsOffset;
        auto sm = smoothFractionalOctave (m.response, 6.0).magnitudeDb();
        for (auto& v : sm) v += fsOffset;
        rawSpl.push_back (raw); smSpl.push_back (sm);
    }
    const auto F = sweeps[0].response.freqs;
    const size_t nNew = sweeps.size();
    if (env ("FA_SWEEP_PREV").isNotEmpty())   // earlier run (same grid): raw1,raw2,smooth1,smooth2 columns become extra curves
    {
        juce::StringArray lines; juce::File (env ("FA_SWEEP_PREV")).readLines (lines);
        std::vector<double> a[4];
        for (int li = 1; li < lines.size(); ++li)
        {
            juce::StringArray c; c.addTokens (lines[li], ",", "");
            if (c.size() >= 5) for (int k = 0; k < 4; ++k) a[k].push_back (c[k + 1].getDoubleValue());
        }
        if (a[0].size() == F.size()) { rawSpl.push_back (a[0]); rawSpl.push_back (a[1]); smSpl.push_back (a[2]); smSpl.push_back (a[3]); }
    }

    for (size_t i = 0; i < sweeps.size(); ++i)
    {
        auto& m = sweeps[i];
        say ("sweep " + juce::String ((int) i + 1) + ": IR peak " + juce::String (m.peakDbFS, 1) + " dBFS at "
             + juce::String ((m.peakSample - m.zeroSample) * 1000.0 / fs, 2) + " ms after output start (zeroSample "
             + juce::String (m.zeroSample) + ", peakSample " + juce::String (m.peakSample) + "), onset "
             + juce::String (m.onsetMs, 2) + " ms, SNR " + juce::String (m.snrDb, 1) + " dB, IR len " + juce::String ((int) m.ir.size()));
    }
    const double latencyMs = (sweeps[0].peakSample - sweeps[0].zeroSample) * 1000.0 / fs;
    check (latencyMs > 0.5 && latencyMs < 500.0, "latency plausible", juce::String (latencyMs, 2) + " ms");
    check (sweeps.size() < 2 || std::abs (sweeps[0].peakSample - sweeps[1].peakSample) <= 48, "latency repeatable (+-1 ms; no loopback, shared-mode start jitter)",
           juce::String (sweeps[0].peakSample) + " / " + juce::String (sweeps[1].peakSample));

    // reference level: smoothed FR around 1 kHz vs the sine probe
    say ("FR @1k smoothed: " + juce::String (dbAt (F, smSpl[0], 1000.0), 1) + " / " + juce::String (dbAt (F, smSpl[smSpl.size() - 1], 1000.0), 1) + " dB SPL");

    // -6 dB points (re the 1-5 kHz band mean level) on sweep 1 smoothed
    double ref = 0.0; int nref = 0;
    for (size_t i = 0; i < F.size(); ++i) if (F[i] >= 1000 && F[i] <= 5000) { ref += smSpl[0][i]; ++nref; }
    ref /= std::max (1, nref);
    double fLo6 = 0, fHi6 = 0;
    for (size_t i = 0; i < F.size(); ++i) if (F[i] >= 100 && smSpl[0][i] >= ref - 6.0) { fLo6 = F[i]; break; }
    for (size_t i = F.size(); i-- > 0;) if (F[i] <= 20000 && smSpl[0][i] >= ref - 6.0) { fHi6 = F[i]; break; }
    double pkF = 0, pkV = -1e9, dipF = 0, dipV = 1e9;
    for (size_t i = 0; i < F.size(); ++i)
        if (F[i] >= 300 && F[i] <= 15000)
        {
            if (smSpl[0][i] > pkV) { pkV = smSpl[0][i]; pkF = F[i]; }
            if (smSpl[0][i] < dipV) { dipV = smSpl[0][i]; dipF = F[i]; }
        }
    say ("FR: 1-5 kHz mean " + juce::String (ref, 1) + " dB SPL; -6 dB points " + juce::String (fLo6, 0) + " Hz .. " + juce::String (fHi6, 0)
         + " Hz; max " + juce::String (pkV, 1) + " dB @ " + juce::String (pkF, 0) + " Hz; min(300-15k) " + juce::String (dipV, 1) + " dB @ " + juce::String (dipF, 0) + " Hz");

    // repeatability 200 Hz - 10 kHz
    double repSm = 0.0;
    for (size_t a = 0; a < nNew; ++a)
        for (size_t b = a + 1; b < nNew; ++b)
        {
            double acc = 0.0, accr = 0.0; int na = 0;
            for (size_t i = 0; i < F.size(); ++i)
                if (F[i] >= 200 && F[i] <= 10000)
                {
                    acc += std::pow (smSpl[a][i] - smSpl[b][i], 2.0);
                    accr += std::pow (rawSpl[a][i] - rawSpl[b][i], 2.0);
                    ++na;
                }
            const double s = std::sqrt (acc / std::max (1, na)), r = std::sqrt (accr / std::max (1, na));
            repSm = std::max (repSm, s);
            say ("REPEATABILITY 200 Hz-10 kHz, sweep " + juce::String ((int) a + 1) + " vs " + juce::String ((int) b + 1) + ": 1/6-oct smoothed "
                 + juce::String (s, 2) + " dB rms, raw " + juce::String (r, 2) + " dB rms");
        }
    if (smSpl.size() >= nNew + 2)   // vs the earlier (shared-mode) run
        for (size_t k = nNew; k < smSpl.size(); ++k)
        {
            double acc = 0.0, mean = 0.0; int na = 0;
            for (size_t i = 0; i < F.size(); ++i)
                if (F[i] >= 200 && F[i] <= 10000) { const double d = smSpl[0][i] - smSpl[k][i]; acc += d * d; mean += d; ++na; }
            say ("COMPARE vs previous run curve " + juce::String ((int) (k - nNew) + 1) + ": 200 Hz-10 kHz 1/6-oct rms diff "
                 + juce::String (std::sqrt (acc / std::max (1, na)), 2) + " dB, mean offset " + juce::String (mean / std::max (1, na), 2) + " dB");
        }
    check (repSm < 3.5, "sweep-to-sweep repeatability < 3.5 dB rms", juce::String (repSm, 2));

    // THD
    std::vector<analysis::Distortion> thd;
    for (auto& m : sweeps)
        thd.push_back (analysis::harmonicDistortion (m, req.sweep.seconds, req.sweep.fStart, std::min (req.sweep.fEnd, fs * 0.48)));
    check (thd[0].valid, "THD analysis valid");
    if (thd[0].valid)
    {
        juce::String s = "THD% (sweep1): ";
        for (double f : { 100.0, 200.0, 300.0, 500.0, 1000.0, 2000.0, 3000.0, 5000.0, 8000.0 })
            if (f >= thd[0].freqs.front() && f <= thd[0].freqs.back())
                s << juce::String (f, 0) << "Hz=" << juce::String (dbAt (thd[0].freqs, thd[0].thdPercent, f), 2) << "  ";
        say (s);
    }

    // ---- CSV + PNG --------------------------------------------------------------------------------------------------------
    {
        juce::String csv = "freq_hz,raw1_db_spl,raw2_db_spl,smooth1_db_spl,smooth2_db_spl,phase1_deg\n";
        for (size_t i = 0; i < F.size(); ++i)
            csv << juce::String (F[i], 2) << "," << juce::String (rawSpl[0][i], 2) << "," << juce::String (rawSpl[1][i], 2) << ","
                << juce::String (smSpl[0][i], 2) << "," << juce::String (smSpl[1][i], 2) << "," << juce::String (sweeps[0].response.phaseDeg (i), 1) << "\n";
        outDir.getChildFile ("fr.csv").replaceWithText (csv);
    }
    {
        Plot p;
        p.g.fillAll (juce::Colour (0xfff4f4f4));
        double top = -1e9;
        for (size_t i = 0; i < F.size(); ++i) if (F[i] >= 100) top = std::max (top, smSpl[0][i]);
        const double yTop = std::ceil ((top + 6.0) / 10.0) * 10.0;
        p.frame ({ 70, 50, 1380, 560 }, yTop - 80.0, yTop, 10.0,
                 "Laptop speakers @ ~30 cm, UMIK-1 (cal 0 deg): calibrated FR, sweep level " + juce::String (sweepDb, 0) + " dBFS peak", "dB SPL (sine-equivalent)");
        p.line (F, rawSpl[0], juce::Colour (0x66808080), 1.0f);
        for (size_t k = 1; k < smSpl.size(); ++k) p.line (F, smSpl[k], k == 1 ? juce::Colour (0xffe08a00) : juce::Colour (0xff20a040), 1.5f);
        p.line (F, smSpl[0], juce::Colour (0xff1060d0), 2.5f);
        if (! noiseRep.bandCentreHz.empty())
            p.line (noiseRep.bandCentreHz, noiseRep.bandDbSpl, juce::Colour (0xffc02020), 1.5f);
        p.g.setFont (13.0f);
        p.g.setColour (juce::Colours::black);
        p.g.drawText ("grey = raw sweep 1, blue = 1/6 oct sweep 1, orange = sweep 2, green = earlier shared-mode run (1/6 oct), red = noise floor 1/3 oct (dB SPL per band)",
                      70, 640, 1380, 16, juce::Justification::left);

        // THD
        p.frame ({ 70, 700, 1380, 310 }, -80.0, 0.0, 10.0, "THD (Farina, harmonics 2-6), dB re fundamental", "dB (0 dB = 100 %)");
        if (thd[0].valid)
        {
            std::vector<double> t0, t1;
            for (auto v : thd[0].thdPercent) t0.push_back (20.0 * std::log10 (std::max (v, 1e-6) / 100.0));
            if (thd.size() > 1) for (auto v : thd[1].thdPercent) t1.push_back (20.0 * std::log10 (std::max (v, 1e-6) / 100.0));
            p.line (thd[0].freqs, t0, juce::Colour (0xffb01040), 2.0f);
            if (thd.size() > 1 && thd[1].valid) p.line (thd[1].freqs, t1, juce::Colour (0xffe08a00), 1.5f);
            // -20 dB = 10 %, -40 dB = 1 %
            p.g.setColour (juce::Colours::black);
            p.g.drawText ("-20 dB = 10 %, -40 dB = 1 %, -60 dB = 0.1 %", 80, 1040, 600, 16, juce::Justification::left);
        }
        auto png = outDir.getChildFile (tag + ".png");
        png.deleteFile();
        juce::FileOutputStream fo (png);
        juce::PNGImageFormat fmt;
        check (fo.openedOk() && fmt.writeImageToStream (p.img, fo), "PNG written", png.getFullPathName());
    }
    say ("done");
}

