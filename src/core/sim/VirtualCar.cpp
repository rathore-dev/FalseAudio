#include "VirtualCar.h"
#include "../dsp/Spectrum.h"
#include "../dsp/Generators.h"

namespace fa
{
VirtualCar::VirtualCar (const SystemConfig& layout, juce::uint32 s) : seed (s)
{
    juce::Random rng ((juce::int64) seed);

    for (auto& ch : layout.channels)
    {
        Driver d;
        const bool left = ch.side == Side::left;

        // Driver seat: left speakers are closer.
        switch (ch.role)
        {
            case DriverRole::subwoofer:
                d.distanceM = 1.9;
                d.lowRollHz = 28.0; d.highRollHz = 250.0; d.sensitivityDb = 4.0;
                d.resonances = { FilterSpec::peq (45.0, 4.0, 6.0), FilterSpec::peq (110.0, 3.0, -5.0) };
                break;
            case DriverRole::midbass:
                d.distanceM = left ? 0.85 : 1.45;
                d.lowRollHz = 55.0; d.highRollHz = 2500.0; d.sensitivityDb = 1.0;
                d.resonances = { FilterSpec::peq (left ? 160.0 : 190.0, 2.5, left ? 5.0 : 3.0),
                                 FilterSpec::peq (left ? 330.0 : 290.0, 3.0, -4.0) };
                break;
            case DriverRole::midrange:
                d.distanceM = left ? 0.95 : 1.35;
                d.lowRollHz = 180.0; d.highRollHz = 9000.0; d.sensitivityDb = -1.0;
                d.resonances = { FilterSpec::peq (left ? 520.0 : 480.0, 2.0, 7.0),
                                 FilterSpec::peq (1600.0, 8.0, 4.0) };
                break;
            case DriverRole::tweeter:
                d.distanceM = left ? 0.80 : 1.30;
                d.lowRollHz = 1100.0; d.highRollHz = 30000.0; d.sensitivityDb = 3.0;
                d.resonances = { FilterSpec::peq (left ? 9000.0 : 10500.0, 1.5, -4.0),
                                 FilterSpec::peq (14500.0, 1.4, 4.0) };
                break;
            case DriverRole::fullrange:
                d.distanceM = left ? 0.9 : 1.4;
                d.lowRollHz = 90.0; d.highRollHz = 15000.0;
                break;
        }

        d.sensitivityDb += rng.nextDouble() * 2.0 - 1.0;
        drivers[ch.id] = d;

        // Two to four early reflections (dash, glass, door) unique to each driver.
        std::vector<Reflection> refl;
        const int nRefl = 2 + rng.nextInt (3);
        for (int i = 0; i < nRefl; ++i)
            refl.push_back ({ 0.6 + rng.nextDouble() * 3.5, -5.0 - rng.nextDouble() * 7.0 });
        reflections[ch.id] = refl;
    }
}

cplx VirtualCar::acoustic (const juce::String& id, double f, int position, double fs) const
{
    auto it = drivers.find (id);
    if (it == drivers.end())
        return { 0.0, 0.0 };

    const auto& d = it->second;
    cplx h = magFromDb (d.sensitivityDb) * (d.wiredInverted ? -1.0 : 1.0);

    h *= FilterSpec::crossover (FilterType::highPass, d.lowRollHz, 12, Alignment::butterworth).response (f, fs);
    if (d.highRollHz < fs * 0.45)
        h *= FilterSpec::crossover (FilterType::lowPass, d.highRollHz, 12, Alignment::butterworth).response (f, fs);
    for (auto& r : d.resonances)
        h *= r.response (f, fs);

    // Mic position moves the head ~5 cm and changes reflection timing a little.
    const double posShiftM = 0.05 * (position % 3 - 1);
    const double dist = std::max (0.2, d.distanceM + posShiftM * (id.startsWith ("FL") ? 1.0 : -1.0));
    const double t = dist / speedOfSound;

    cplx refl (1.0, 0.0);
    for (auto& r : reflections.at (id))
    {
        const double extra = (r.extraMs + 0.15 * position) * 0.001;
        refl += magFromDb (r.gainDb) * std::polar (1.0, -2.0 * pi * f * extra);
    }

    // 1/r spreading relative to 1 m
    return h * refl * std::polar (1.0 / dist, -2.0 * pi * f * t);
}

Measurement VirtualCar::measure (const std::vector<std::pair<juce::String, ChannelSettings>>& playing,
                                 int position, double fs, double noiseDb) const
{
    // Build the system's spectrum on linear bins, then inverse-FFT to an impulse response.
    const int n = 1 << 15;
    const double binHz = fs / n;
    std::vector<cplx> bins ((size_t) n / 2 + 1, cplx (0.0, 0.0));

    for (size_t k = 1; k < bins.size(); ++k)
    {
        const double f = k * binHz;
        cplx sum (0.0, 0.0);
        for (auto& [id, settings] : playing)
        {
            if (settings.muted)
                continue;
            cplx elec = magFromDb (settings.gainDb) * (settings.inverted ? -1.0 : 1.0);
            elec *= settings.highPass.response (f, fs);
            elec *= settings.lowPass.response (f, fs);
            for (auto& p : settings.peq)
                elec *= p.response (f, fs);
            elec *= std::polar (1.0, -2.0 * pi * f * settings.delayMs * 0.001);
            sum += elec * acoustic (id, f, position, fs);
        }
        bins[k] = sum;
    }

    auto h = inverseRealFFT (bins, n);

    Measurement m;
    m.sampleRate = fs;
    m.zeroSample = deconvolutionPreRoll (fs);
    m.ir.assign ((size_t) (m.zeroSample + n), 0.0f);

    juce::Random rng ((juce::int64) (seed + 7919u * (juce::uint32) position));
    const double noise = magFromDb (noiseDb);
    for (size_t i = 0; i < m.ir.size(); ++i)
    {
        const double s = i >= (size_t) m.zeroSample ? h[i - (size_t) m.zeroSample] : 0.0;
        m.ir[i] = (float) (s + noise * (rng.nextDouble() * 2.0 - 1.0));
    }

    juce::StringArray ids;
    for (auto& [id, s] : playing)
        ids.add (id);
    m.channelId = ids.joinIntoString ("+");
    m.name = m.channelId + " @pos" + juce::String (position + 1);
    m.micPosition = position;
    m.analyse (nullptr);
    return m;
}
} // namespace fa
