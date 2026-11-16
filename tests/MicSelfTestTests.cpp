// Mic self-test maths (synthetic signals) and the gated live UMIK-1 input-only check (FA_LIVE_MIC=1).

#include "TestRegistry.h"
#include "core/measure/MicSelfTest.h"
#include "core/audio/AudioEngine.h"
#include "core/audio/EndpointVolume.h"
#include <iostream>

using namespace fa;

namespace
{
std::vector<float> tone (double hz, double rms, double sr, double seconds, double phase = 0.0)
{
    std::vector<float> x ((size_t) (sr * seconds));
    const double a = rms * std::sqrt (2.0);
    for (size_t i = 0; i < x.size(); ++i)
        x[i] = (float) (a * std::sin (2.0 * juce::MathConstants<double>::pi * hz * (double) i / sr + phase));
    return x;
}

void addNoise (std::vector<float>& x, double rms, int seed)
{
    juce::Random rng (seed);
    for (auto& v : x)
        v += (float) (rms * 1.7320508 * (rng.nextDouble() * 2.0 - 1.0));
}

MicCalibration flatCal (double sens)
{
    MicCalibration c;
    c.sensitivityDb = sens;
    c.freqs = { 10.0, 1000.0, 24000.0 };
    c.gainDb = { 0.0, 0.0, 0.0 };
    c.phaseDeg = { 0.0, 0.0, 0.0 };
    return c;
}
}

FA_TEST (micSelfTestToneLevel)
{
    // 1 kHz tone, RMS -40 dBFS, sens -1.268 dB: SPL = -40 + 124.0206 + 1.268 = 85.289 dB (REW convention); A-weighted equals Z at 1 kHz.
    auto x = tone (1000.0, 0.01, 48000.0, 5.0);
    const auto cal = flatCal (-1.268);
    const auto r = analyseMicRecording (x, 48000.0, &cal);
    fa::test::check (std::abs (r.rmsDbFS + 40.0) < 0.05, "RMS dBFS of a known tone", juce::String (r.rmsDbFS, 3));
    fa::test::check (r.absolute, "Calibration with sens factor gives absolute levels");
    fa::test::check (std::abs (r.levelZDbSpl - 85.289) < 0.15, "Z level = dBFS + 124.02 - sens", juce::String (r.levelZDbSpl, 3));
    fa::test::check (std::abs (r.levelADbSpl - 85.289) < 0.15, "A level at 1 kHz equals Z", juce::String (r.levelADbSpl, 3));
    fa::test::check (std::abs (r.peakDbFS + 37.0) < 0.1, "Peak of a sine is RMS + 3 dB", juce::String (r.peakDbFS, 2));
    fa::test::check (std::abs (r.dcOffset) < 1e-4 && r.clippedSamples == 0, "No DC, no clipping");
    // The 1/3-octave band holding the tone carries all of the power.
    double best = -999.0, centre = 0.0;
    for (size_t i = 0; i < r.bandDbSpl.size(); ++i)
        if (r.bandDbSpl[i] > best) { best = r.bandDbSpl[i]; centre = r.bandCentreHz[i]; }
    fa::test::check (std::abs (centre - 1000.0) < 1.0 && std::abs (best - 85.289) < 0.15, "1/3-octave band at 1 kHz",
                     juce::String (centre, 0) + " Hz " + juce::String (best, 2));
}

FA_TEST (micSelfTestMatchesRew)
{
    // REW 5.40, UMIK-1 serial 7165149, 90 deg file (sens -1.268), endpoint 0 dB: input-cal.dBFSAt94dBSPL = -28.2577,
    // REW dBFS is sine-referenced (full-scale sine = 0 dBFS = -3.01 dB mean square).
    // A 94 dB SPL tone therefore reads -28.2577 dBFS (sine) = -31.2680 dBFS RMS.
    const double rewDbfsAt94 = -28.2577;
    auto x = tone (1000.0, std::pow (10.0, (rewDbfsAt94 - 3.0103) / 20.0), 48000.0, 5.0);
    const auto cal = flatCal (-1.268);
    const auto r = analyseMicRecording (x, 48000.0, &cal);
    fa::test::check (std::abs (r.levelZDbSpl - 94.0) < 0.1, "REW's 94 dB SPL reference tone reads 94 dB", juce::String (r.levelZDbSpl, 3));
    // Windows endpoint volume +6 dB amplifies the signal; the same acoustic level must still read 94 dB.
    EndpointVolume ep; ep.found = ep.valid = true; ep.levelDb = 6.0; ep.scalar = 0.7;
    auto y = x;
    for (auto& v : y) v *= 2.0f;   // +6.02 dB
    const auto r2 = analyseMicRecording (y, 48000.0, &cal, &ep);
    fa::test::check (std::abs (r2.levelZDbSpl - 94.0) < 0.1 + 0.02, "Endpoint volume is compensated", juce::String (r2.levelZDbSpl, 3));
    fa::test::check (r2.volumeWarning && r2.volumeMessage.isNotEmpty() && r2.endpointKnown, "Level off the recommended value raises a warning");
    EndpointVolume ep0; ep0.found = ep0.valid = true; ep0.levelDb = 0.0; ep0.scalar = 0.537;
    const auto r3 = analyseMicRecording (x, 48000.0, &cal, &ep0);
    fa::test::check (! r3.volumeWarning && r3.volumeMessage.isEmpty(), "Recommended level: no warning");
    fa::test::check (std::abs (r3.fullScaleDbSpl - 122.29) < 0.05, "Full-scale sine headroom", juce::String (r3.fullScaleDbSpl, 2));
}

FA_TEST (micSelfTestCalibrationCurve)
{
    // The mic reads +6 dB at 1 kHz; the corrected level must come down 6 dB.
    MicCalibration cal = flatCal (0.0);
    cal.sensitivityDb = -2.0;
    cal.gainDb = { 6.0, 6.0, 6.0 };
    auto x = tone (1000.0, 0.01, 48000.0, 3.0);
    const auto r = analyseMicRecording (x, 48000.0, &cal);
    fa::test::check (std::abs (r.levelZDbSpl - (-40.0 + 124.0206 + 2.0 - 6.0)) < 0.15, "Calibration curve is removed",
                     juce::String (r.levelZDbSpl, 2));
}

FA_TEST (micSelfTestAWeighting)
{
    fa::test::check (std::abs (aWeightingDb (1000.0)) < 1e-9, "A-weighting is 0 dB at 1 kHz");
    fa::test::check (std::abs (aWeightingDb (100.0) + 19.1) < 0.2, "A-weighting at 100 Hz is about -19.1 dB", juce::String (aWeightingDb (100.0), 2));
    fa::test::check (std::abs (aWeightingDb (10000.0) + 2.5) < 0.2, "A-weighting at 10 kHz is about -2.5 dB", juce::String (aWeightingDb (10000.0), 2));
    // A 100 Hz tone must read ~19 dB lower in dBA than in dB Z.
    const auto cal = flatCal (-1.0);
    auto x = tone (100.0, 0.01, 48000.0, 3.0);
    const auto r = analyseMicRecording (x, 48000.0, &cal);
    fa::test::check (std::abs ((r.levelZDbSpl - r.levelADbSpl) - 19.1) < 0.3, "A-weighted spectrum applied to a 100 Hz tone",
                     juce::String (r.levelZDbSpl - r.levelADbSpl, 2));
}

FA_TEST (micSelfTestHumDetection)
{
    const auto cal = flatCal (-1.268);
    for (double mains : { 50.0, 60.0 })
    {
        auto x = tone (mains, 0.0003, 48000.0, 5.0);
        auto h2 = tone (mains * 2.0, 0.0002, 48000.0, 5.0);
        for (size_t i = 0; i < x.size(); ++i) x[i] += h2[i];
        addNoise (x, 0.0003, 7);
        const auto r = analyseMicRecording (x, 48000.0, &cal);
        fa::test::check (r.humDetected && r.humFundamentalHz == mains && r.verdict == MicSelfTestReport::Verdict::hum,
                         "Hum detected at " + juce::String ((int) mains) + " Hz", r.humDetail);
    }

    auto clean = std::vector<float> (48000 * 5, 0.0f);
    addNoise (clean, 0.0003, 11);
    const auto r = analyseMicRecording (clean, 48000.0, &cal);
    fa::test::check (! r.humDetected && r.verdict == MicSelfTestReport::Verdict::ok, "Plain noise: no hum, verdict OK",
                     MicSelfTestReport::verdictName (r.verdict) + " " + r.humDetail);
}

FA_TEST (micSelfTestClippingAndSilence)
{
    const auto cal = flatCal (-1.268);
    auto x = tone (1000.0, 0.9, 48000.0, 3.0);   // peak 1.27 -> hard clip
    for (auto& v : x) v = juce::jlimit (-1.0f, 1.0f, v);
    auto r = analyseMicRecording (x, 48000.0, &cal);
    fa::test::check (r.clipping && r.verdict == MicSelfTestReport::Verdict::clipping, "Clipping detected",
                     juce::String (r.clippedSamples) + " samples");

    std::vector<float> silent (48000 * 3, 0.0f);
    r = analyseMicRecording (silent, 48000.0, &cal);
    fa::test::check (r.verdict == MicSelfTestReport::Verdict::tooQuiet, "Digital silence = too quiet");

    std::vector<float> dc (48000 * 3, 0.01f);
    addNoise (dc, 0.001, 3);
    r = analyseMicRecording (dc, 48000.0, &cal);
    fa::test::check (std::abs (r.dcOffset - 0.01) < 1e-4, "DC offset measured", juce::String (r.dcOffset, 5));

    r = analyseMicRecording ({}, 48000.0, &cal);
    fa::test::check (r.verdict == MicSelfTestReport::Verdict::noData, "Empty buffer = no data");
}

//==============================================================================
// Live, input-only. Off unless FA_LIVE_MIC=1. Never opens an output device.
FA_TEST (micSelfTestLiveUmik)
{
    if (juce::SystemStats::getEnvironmentVariable ("FA_LIVE_MIC", {}) != "1")
        return;

    std::cout << "  [LIVE] FA_LIVE_MIC=1: real UMIK-1 input-only self test\n";
    juce::ScopedJuceInitialiser_GUI gui;
    AudioEngine engine;
    auto& dm = engine.getDeviceManager();

    juce::String devName, typeName;
    const auto envType = juce::SystemStats::getEnvironmentVariable ("FA_LIVE_TYPE", {});   // e.g. "Windows Audio (Exclusive Mode)"
    const juce::String preferredType = envType.isNotEmpty() ? envType : juce::String ("Windows Audio");
    for (auto* type : dm.getAvailableDeviceTypes())
    {
        type->scanForDevices();
        for (auto& n : type->getDeviceNames (true))
            std::cout << "    [" << type->getTypeName() << "] input: " << n << "\n";
        if (devName.isEmpty() && type->getTypeName() == preferredType)
            for (auto& n : type->getDeviceNames (true))
                if (n.containsIgnoreCase ("umik"))
                    { devName = n; typeName = type->getTypeName(); }
    }
    if (devName.isEmpty())
        for (auto* type : dm.getAvailableDeviceTypes())
            for (auto& n : type->getDeviceNames (true))
                if (devName.isEmpty() && n.containsIgnoreCase ("umik"))
                    { devName = n; typeName = type->getTypeName(); }
    fa::test::check (devName.isNotEmpty(), "UMIK-1 is present", devName);
    if (devName.isEmpty())
        return;

    dm.setCurrentAudioDeviceType (typeName, true);
    const auto err = engine.openInputOnly (devName);
    fa::test::check (err.isEmpty(), "Opened " + devName + " via " + typeName + " (input only)", err);
    if (err.isNotEmpty())
        return;
    fa::test::check (engine.getNumOutputChannels() == 0, "No output channels opened", juce::String (engine.getNumOutputChannels()));

    juce::File calFile = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                             .getChildFile ("FalseAudio/calibration/7165149_90.txt");
    auto cal = MicCalibration::load (calFile);
    fa::test::check (cal.isValid(), "90 degree calibration loaded", juce::String (cal.sensitivityDb, 3) + " dB, " + juce::String ((int) cal.freqs.size()) + " pts");
    engine.setCalibration (0, cal);
    AudioRouting rt;
    rt.micInputs = { 0 };
    engine.setRouting (rt);

    std::cout << "    channels: " << engine.getInputChannelNames().joinIntoString (", ") << ", device rate "
              << engine.getSampleRate() << "\n";

    juce::Thread::sleep (500);   // let the stream settle
    std::atomic<bool> cancel { false };
    std::vector<float> x;
    double sr = 0.0, wall = 0.0;
    juce::String rerr;
    const bool ok = engine.recordInput (5.0, x, sr, cancel, rerr, &wall);
    fa::test::check (ok, "Recorded ~5 s", rerr);
    if (! ok)
        return;

    const auto ep = readCaptureEndpointVolume (devName);
    std::cout << "    endpoint volume: " << ep.levelDb << " dB (scalar " << ep.scalar << ", valid " << ep.valid << ")" << std::endl;
    auto rep = analyseMicRecording (x, sr, &cal, ep.valid ? &ep : nullptr);
    rep.measuredRate = (double) x.size() / wall;
    std::cout << rep.toText();
    fa::test::check (rep.verdict != MicSelfTestReport::Verdict::noData, "Self test produced a verdict",
                     MicSelfTestReport::verdictName (rep.verdict));
}

FA_TEST (endpointVolumeLiveUmik)
{
    if (juce::SystemStats::getEnvironmentVariable ("FA_LIVE_MIC", {}) != "1")
        return;
    const auto v = readCaptureEndpointVolume ("umik");
    std::cout << "  [LIVE] endpoint '" << v.deviceName << "' level " << v.levelDb << " dB, scalar " << v.scalar
              << ", range " << v.minDb << ".." << v.maxDb << " step " << v.stepDb << ", muted " << v.muted << " " << v.error << std::endl;
    fa::test::check (v.valid, "Read UMIK endpoint volume", v.error);
}
