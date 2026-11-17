// SoftwareDspController: the app's own DSP. Filters, mute/solo, polarity and delay are checked on a synthetic loopback
// (the audio is rendered through the controller exactly as AudioEngine renders a sweep).
#include "TestRegistry.h"
#include "core/Session.h"
#include "core/bridge/SoftwareDspController.h"

using namespace fa;
using fa::test::check;

namespace
{
constexpr double kFs = 48000.0;

OutputChannel makeChannel (int out, const juce::String& id = "FL.full")
{
    OutputChannel c;
    c.id = id;
    c.label = id;
    c.dspOutput = out;
    c.role = DriverRole::fullrange;
    c.kind = ChannelKind::fullRange;
    c.settings.highPass.enabled = false;
    c.settings.lowPass.enabled = false;
    return c;
}

/** Steady-state gain (dB) of the controller's output `out` for a sine of `f`, measured after the filters settled. */
double sineGainDb (const SoftwareDspController& dsp, int out, double f)
{
    std::vector<float> in ((size_t) (kFs * 1.0));
    for (size_t i = 0; i < in.size(); ++i)
        in[i] = (float) (0.1 * std::sin (2.0 * juce::MathConstants<double>::pi * f * (double) i / kFs));
    std::vector<float> o;
    if (! dsp.processOutput (out, in, kFs, o))
        return -999.0;
    double a = 0.0, b = 0.0;
    for (size_t i = (size_t) (kFs * 0.5); i < (size_t) (kFs * 0.9); ++i)
    {
        a += (double) in[i] * in[i];
        b += (double) o[i] * o[i];
    }
    return 10.0 * std::log10 (std::max (b, 1e-30) / a);
}

std::vector<float> impulse (size_t n = 4800)
{
    std::vector<float> x (n, 0.0f);
    x[100] = 1.0f;
    return x;
}

size_t peakIndex (const std::vector<float>& x)
{
    size_t k = 0;
    for (size_t i = 0; i < x.size(); ++i)
        if (std::abs (x[i]) > std::abs (x[k])) k = i;
    return k;
}
} // namespace

FA_TEST (softwareDspAppliesFiltersAndGain)
{
    SoftwareDspController dsp;
    juce::String err;
    auto c = makeChannel (0);
    c.settings.highPass = FilterSpec::crossover (FilterType::highPass, 1000.0, 24);   // LR4
    c.settings.lowPass  = FilterSpec::crossover (FilterType::lowPass, 4000.0, 12);
    c.settings.peq.push_back (FilterSpec::peq (2000.0, 2.0, -6.0));
    c.settings.gainDb = -3.0;
    check (dsp.applyChannel (c, err), "applyChannel", err);

    const std::vector<double> fs { 250.0, 1000.0, 2000.0, 4000.0, 8000.0 };
    const auto expected = c.settings.response (fs, kFs, false);
    double worst = 0.0;
    for (size_t i = 0; i < fs.size(); ++i)
    {
        const double got = sineGainDb (dsp, 0, fs[i]);
        worst = std::max (worst, std::abs (got - expected.magnitudeDb (i)));
    }
    check (worst < 0.15, "rendered response matches the FilterSpec/ChannelSettings model (HP LR4 + LP + PEQ + gain)", juce::String (worst, 3) + " dB");
    check (sineGainDb (dsp, 0, 250.0) < -20.0, "HP LR4 1 kHz attenuates 250 Hz by > 20 dB");
    check (std::abs (sineGainDb (dsp, 0, 1000.0) - (-6.0 - 3.0 - 0.0)) < 1.5, "1 kHz sits near the -6 dB corner (plus -3 dB gain)");

    // a disabled filter is bypassed
    c.settings.highPass.enabled = false; c.settings.peq.clear(); c.settings.lowPass.enabled = false; c.settings.gainDb = 0.0;
    dsp.applyChannel (c, err);
    check (std::abs (sineGainDb (dsp, 0, 250.0)) < 0.05, "no filters, 0 dB gain: transparent");

    // an output that was never written is not handled (engine plays it unprocessed)
    std::vector<float> o;
    check (! dsp.processOutput (1, impulse(), kFs, o), "unwritten output is not processed");
}

FA_TEST (softwareDspMutePolarityDelay)
{
    SoftwareDspController dsp;
    juce::String err;
    auto c = makeChannel (0);
    dsp.applyChannel (c, err);
    std::vector<float> o;
    dsp.processOutput (0, impulse(), kFs, o);
    const size_t p0 = peakIndex (o);
    check (p0 == 100 && o[p0] > 0.99f, "transparent: impulse passes at the same position", juce::String ((int) p0));

    // polarity
    c.settings.inverted = true;
    dsp.applyChannel (c, err);
    dsp.processOutput (0, impulse(), kFs, o);
    check (peakIndex (o) == 100 && o[100] < -0.99f, "polarity inverts the signal");
    c.settings.inverted = false;

    // delay: 1 ms = 48 samples (whole) and 0.5 ms + 0.25 sample (fractional)
    c.settings.delayMs = 1.0;
    dsp.applyChannel (c, err);
    dsp.processOutput (0, impulse(), kFs, o);
    check (peakIndex (o) == 148 && o[148] > 0.99f, "1 ms delay = +48 samples", juce::String ((int) peakIndex (o)));
    c.settings.delayMs = (48.5 / kFs) * 1000.0;     // half a sample later than 48
    dsp.applyChannel (c, err);
    dsp.processOutput (0, impulse(), kFs, o);
    double centroid = 0.0, mass = 0.0;
    for (size_t i = 120; i < 180; ++i) { centroid += (double) i * o[i]; mass += o[i]; }
    check (std::abs (centroid / mass - 148.5) < 0.05, "fractional delay (48.5 samples) is interpolated", juce::String (centroid / mass, 3));
    // the fractional-delay kernel keeps the level flat well below Nyquist
    c.settings.delayMs = 0.2083;
    dsp.applyChannel (c, err);
    check (std::abs (sineGainDb (dsp, 0, 3000.0)) < 0.2, "fractional delay keeps 3 kHz level");
    c.settings.delayMs = 0.0;

    // mute: the stored mute and the solo mute both silence the output; solo does not overwrite the stored settings
    dsp.applyChannel (c, err);
    dsp.setMuted (c, true, err);
    dsp.processOutput (0, impulse(), kFs, o);
    double peak = 0.0;
    for (auto v : o) peak = std::max (peak, (double) std::abs (v));
    check (peak == 0.0, "solo-muted output is silent");
    dsp.setMuted (c, false, err);
    dsp.processOutput (0, impulse(), kFs, o);
    check (o[100] > 0.99f, "unmuting restores the signal");
    c.settings.muted = true;
    dsp.applyChannel (c, err);
    dsp.processOutput (0, impulse(), kFs, o);
    peak = 0.0;
    for (auto v : o) peak = std::max (peak, (double) std::abs (v));
    check (peak == 0.0, "stored mute silences the output");

    // soloChannels() through the default implementation mutes the others
    SystemConfig cfg;
    auto a = makeChannel (0, "A"), b = makeChannel (1, "B");
    cfg.channels = { a, b };
    dsp.applyAll (cfg, err);
    check (dsp.soloChannels (cfg, { "B" }, err), "soloChannels");
    ChannelSettings sa, sb;
    check (dsp.getSettings (0, sa) && dsp.getSettings (1, sb) && sa.muted && ! sb.muted, "solo B mutes A only");
}

FA_TEST (softwareDspThroughGuardAndSession)
{
    auto inner = std::make_shared<SoftwareDspController>();
    GuardedDspController guard (inner);
    auto c = makeChannel (0);
    c.settings.gainDb = 6.0;                         // above the guard's 0 dB ceiling
    juce::String err;
    check (guard.applyChannel (c, err), "guarded write", err);
    ChannelSettings s;
    check (inner->getSettings (0, s) && s.gainDb <= 0.0, "guard clamps the gain before the software DSP sees it", juce::String (s.gainDb, 1));
    check (guard.isAutomatic() && guard.getName() == "Built-in (software)", "guard reports the inner controller");

    Session session;
    check (session.useSoftwareDsp(), "Session::useSoftwareDsp");
    check (session.getSoftwareDsp() != nullptr, "session exposes the software DSP");
    session.useManualDsp();
    check (session.getSoftwareDsp() == nullptr, "switching away drops it");
}
