#pragma once

#include "../tune/SystemConfig.h"
#include "../measure/Measurement.h"
#include "../dsp/Generators.h"
#include <map>

namespace fa
{
/** A simulated car: imperfect drivers at different distances, cabin reflections that change
    with mic position, and optionally a driver wired in reverse. Produces real impulse
    responses so the whole measurement + tuning chain can run without hardware (tests and
    the app's demo mode). */
class VirtualCar
{
public:
    struct Driver
    {
        double distanceM = 1.0;
        double lowRollHz = 50.0;             // natural acoustic HP (2nd order)
        double highRollHz = 20000.0;         // natural acoustic LP (2nd order)
        double sensitivityDb = 0.0;
        bool   wiredInverted = false;
        std::vector<FilterSpec> resonances;  // breakup, cabin modes, etc.

        /** Passive 2-way set on one output: the fields above describe the woofer; the tweeter is
            summed through a simulated 12 dB/oct passive network at passiveXoverHz, mounted
            tweeterOffsetM further from the listener than the woofer (it nulls the crossover region). */
        bool   passiveSet = false;
        double passiveXoverHz = 2500.0;
        double tweeterOffsetM = 0.068;   // ~half a wavelength at 2.5 kHz: a deep crossover null
        double tweeterSensitivityDb = 0.5;
        bool   tweeterWiredInverted = true;  // normal for a 12 dB network (keeps woofer and tweeter in phase at the crossover)
        std::vector<FilterSpec> tweeterResonances;

        /** Static nonlinearity at the driver input (Hammerstein model), only used by
            measureSwept(): y = x + h2 * x^2 + h3 * x^3, x in full-scale units. For a sine of
            amplitude A this gives H2 = h2*A/2 and H3 = h3*A^2/4 relative to the fundamental. */
        double nonlinearH2 = 0.0;
        double nonlinearH3 = 0.0;
        bool isNonlinear() const { return nonlinearH2 != 0.0 || nonlinearH3 != 0.0; }
    };

    struct Reflection { double extraMs, gainDb; };

    VirtualCar (const SystemConfig& layout, juce::uint32 seed = 42);

    Driver& driver (const juce::String& channelId) { return drivers[channelId]; }

    /** Simulates a sweep measurement of the given channels playing together with the given
        DSP settings, at mic position `position` (0..n). */
    Measurement measure (const std::vector<std::pair<juce::String, ChannelSettings>>& playing,
                         int position, double sampleRate = 48000.0, double noiseDb = -90.0) const;

    /** Full time-domain simulation of a real sweep measurement: plays `sweep` through each
        channel's DSP settings, the driver's nonlinearity and the acoustic path, adds noise and
        deconvolves. Keeps the harmonic pre-roll (harmonicPreRoll) so analysis::harmonicDistortion
        works on the result. Slower than measure(). */
    Measurement measureSwept (const std::vector<std::pair<juce::String, ChannelSettings>>& playing,
                              int position, const LogSweep& sweep, double noiseDb = -90.0) const;

private:
    std::map<juce::String, Driver> drivers;
    std::map<juce::String, std::vector<Reflection>> reflections;
    juce::uint32 seed;

    cplx acoustic (const juce::String& id, double f, int position, double fs) const;
};
} // namespace fa
