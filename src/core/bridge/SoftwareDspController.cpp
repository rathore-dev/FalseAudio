#include "SoftwareDspController.h"
#include <cmath>

namespace fa
{
namespace
{
/** Transposed direct form II, double precision. */
struct BiquadState
{
    BiquadCoeffs c;
    double z1 = 0.0, z2 = 0.0;
    double tick (double x) noexcept
    {
        const double y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return y;
    }
};

void addFilter (std::vector<BiquadState>& chain, const FilterSpec& f, double fs)
{
    if (! f.enabled)
        return;
    for (auto& bq : f.toBiquads (fs))
        chain.push_back ({ bq, 0.0, 0.0 });
}

/** Delays `y` by `d` samples (fractional allowed) with a Blackman-windowed sinc (35 taps); exact shift when d is whole. */
std::vector<float> delayed (const std::vector<double>& y, double d, size_t outLen)
{
    std::vector<float> out (outLen, 0.0f);
    const long whole = (long) std::floor (d);
    const double frac = d - (double) whole;
    if (frac < 1e-4 || frac > 1.0 - 1e-4)
    {
        const long sh = (long) std::llround (d);
        for (size_t i = 0; i < y.size(); ++i)
            if ((long) i + sh >= 0 && (size_t) ((long) i + sh) < outLen)
                out[(size_t) ((long) i + sh)] = (float) y[i];
        return out;
    }

    constexpr int W = 17;                           // half width in samples
    std::vector<double> h (2 * W + 1);
    double sum = 0.0;
    const double pi = juce::MathConstants<double>::pi;
    for (int j = 0; j < (int) h.size(); ++j)
    {
        const long k = whole - W + 1 + j;           // tap at input lag k: output[n] += h * y[n - k]
        const double t = (double) k - d;
        const double sinc = std::abs (t) < 1e-12 ? 1.0 : std::sin (pi * t) / (pi * t);
        const double w = std::abs (t) <= W ? 0.42 + 0.5 * std::cos (pi * t / W) + 0.08 * std::cos (2.0 * pi * t / W) : 0.0;
        h[(size_t) j] = sinc * w;
        sum += h[(size_t) j];
    }
    for (auto& v : h) v /= sum;

    for (size_t n = 0; n < outLen; ++n)
    {
        double acc = 0.0;
        for (int j = 0; j < (int) h.size(); ++j)
        {
            const long idx = (long) n - (whole - W + 1 + j);
            if (idx >= 0 && idx < (long) y.size())
                acc += h[(size_t) j] * y[(size_t) idx];
        }
        out[n] = (float) acc;
    }
    return out;
}
} // namespace

DspCapabilities SoftwareDspController::defaultCapabilities()
{
    DspCapabilities c;
    c.sampleRate = 48000.0;
    c.peqBandsPerChannel = 12;
    c.maxDelayMs = 20.0;
    c.delayStepMs = 0.01;
    c.maxChannelGainDb = 0.0;      // software gain never boosts (the guard enforces the same)
    return c;
}

SoftwareDspController::SoftwareDspController (DspCapabilities c) : caps (std::move (c)) {}

bool SoftwareDspController::applyChannel (const OutputChannel& channel, juce::String& error)
{
    const auto& s = channel.settings;
    if (! std::isfinite (s.gainDb) || ! std::isfinite (s.delayMs))
    {
        error = channel.label + ": gain/delay is not finite";
        return false;
    }
    std::lock_guard<std::mutex> l (m);
    outputs[channel.dspOutput].settings = s;    // the solo mute is separate and stays as it was
    return true;
}

bool SoftwareDspController::setMuted (const OutputChannel& channel, bool muted, juce::String&)
{
    std::lock_guard<std::mutex> l (m);
    outputs[channel.dspOutput].soloMuted = muted;
    return true;
}

bool SoftwareDspController::getSettings (int dspOutput, ChannelSettings& out) const
{
    std::lock_guard<std::mutex> l (m);
    auto it = outputs.find (dspOutput);
    if (it == outputs.end())
        return false;
    out = it->second.settings;
    out.muted = out.muted || it->second.soloMuted;
    return true;
}

std::vector<float> SoftwareDspController::render (const ChannelSettings& s, const std::vector<float>& in, double fs)
{
    const double d = juce::jlimit (0.0, 100000.0, s.delayMs * 0.001 * fs);
    const size_t tail = 4096;                        // filter ringing after the stimulus ends
    const size_t outLen = in.size() + (size_t) std::ceil (d) + tail;
    if (s.muted)
        return std::vector<float> (outLen, 0.0f);

    std::vector<BiquadState> chain;
    addFilter (chain, s.highPass, fs);
    addFilter (chain, s.lowPass, fs);
    for (auto& f : s.peq)
        addFilter (chain, f, fs);

    const double g = magFromDb (s.gainDb) * (s.inverted ? -1.0 : 1.0);
    std::vector<double> y (in.size() + tail, 0.0);
    for (size_t i = 0; i < y.size(); ++i)
    {
        double x = i < in.size() ? (double) in[i] * g : 0.0;
        for (auto& b : chain)
            x = b.tick (x);
        y[i] = x;
    }
    return delayed (y, d, outLen);
}

bool SoftwareDspController::processOutput (int dspOutput, const std::vector<float>& in, double fs, std::vector<float>& out) const
{
    ChannelSettings s;
    {
        std::lock_guard<std::mutex> l (m);
        auto it = outputs.find (dspOutput);
        if (it == outputs.end())
            return false;
        s = it->second.settings;
        s.muted = s.muted || it->second.soloMuted;
    }
    out = render (s, in, fs);
    return true;
}
} // namespace fa
