// Measurement-path selection (exclusive preferred) and Windows capture-processing detection. Pure logic, no hardware.
#include "TestRegistry.h"
#include "core/audio/AudioEngine.h"
#include "core/measure/MicSelfTest.h"

using fa::test::check;
using namespace fa;

FA_TEST (capturePathPlanning)
{
    const juce::StringArray both { "Windows Audio", "Windows Audio (Exclusive Mode)", "DirectSound" };
    const juce::StringArray sharedOnly { "Windows Audio", "DirectSound" };

    auto p = planCaptureAttempts (CaptureMode::autoPreferExclusive, "Windows Audio", both);
    check (p.size() == 2 && p[0].exclusive && p[0].typeName == "Windows Audio (Exclusive Mode)" && p[0].warning.isEmpty(),
           "auto: exclusive first, no warning");
    check (p.size() == 2 && ! p[1].exclusive && p[1].warning.containsIgnoreCase ("shared"), "auto: shared fallback carries a warning");

    p = planCaptureAttempts (CaptureMode::autoPreferExclusive, "Windows Audio (Exclusive Mode)", both);
    check (p.size() == 2 && p[0].exclusive, "auto from an exclusive current type");

    p = planCaptureAttempts (CaptureMode::autoPreferExclusive, "Windows Audio", sharedOnly);
    check (p.size() == 1 && ! p[0].exclusive && p[0].warning.containsIgnoreCase ("not available"), "auto without exclusive: shared + warning");

    p = planCaptureAttempts (CaptureMode::shared, "Windows Audio", both);
    check (p.size() == 1 && p[0].typeName == "Windows Audio" && p[0].warning.isEmpty(), "explicit shared is respected");

    p = planCaptureAttempts (CaptureMode::exclusive, "Windows Audio", both);
    check (p.size() == 1 && p[0].exclusive, "explicit exclusive: no silent fallback");
    p = planCaptureAttempts (CaptureMode::exclusive, "Windows Audio", sharedOnly);
    check (p.empty(), "explicit exclusive but unavailable: nothing to try");

    p = planCaptureAttempts (CaptureMode::autoPreferExclusive, "ASIO", { "ASIO", "Windows Audio" });
    check (p.size() == 1 && p[0].typeName == "ASIO" && p[0].warning.isEmpty(), "ASIO is used as chosen");
}

FA_TEST (captureProcessingDetection)
{
    // measured on a real UMIK-1: shared mode -106.8 dBFS / ~5 dBA (processed); exclusive -69.7 dBFS / 43 dBA (real)
    check (captureProcessingSuspected (-106.8, true, 8.0), "calibrated noise 8 dB SPL Z is flagged");
    check (! captureProcessingSuspected (-69.7, true, 45.0), "calibrated noise 45 dB SPL Z is fine");
    check (! captureProcessingSuspected (-106.8, false, 0.0), "uncalibrated, no probe: digital level alone is not enough");
    check (captureProcessingSuspected (-106.8, false, 0.0, true), "-106.8 dBFS with a probe tone received is flagged");
    check (! captureProcessingSuspected (-95.0, false, 0.0, true), "-95 dBFS with a probe is not flagged");
    check (! captureProcessingSuspected (-200.0, true, 0.0), "digital silence is a dead input, not processing");

    // end to end through analyseMicRecording: very quiet but not dead noise with a calibration
    std::vector<float> x (48000 * 2);
    juce::Random rng (7);
    for (auto& v : x) v = (float) (4e-6 * (rng.nextDouble() * 2.0 - 1.0));   // ~ -112 dBFS rms
    MicCalibration cal;
    cal.sensitivityDb = -1.4;
    cal.freqs = { 10.0, 1000.0, 24000.0 }; cal.gainDb = { 0.0, 0.0, 0.0 }; cal.phaseDeg = { 0.0, 0.0, 0.0 };
    const auto r = analyseMicRecording (x, 48000.0, &cal);
    check (r.processingSuspected && r.verdict == MicSelfTestReport::Verdict::processed, "report verdict is Processed input",
           MicSelfTestReport::verdictName (r.verdict) + " Z=" + juce::String (r.levelZDbSpl, 1));
    check (r.message.containsIgnoreCase ("audio processing appears active"), "report message names Windows audio processing");
}
