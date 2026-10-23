#pragma once

#include "../dsp/Filters.h"
#include "TargetCurve.h"

namespace fa
{
enum class DriverRole { subwoofer, midbass, midrange, tweeter, fullrange };
enum class Side       { left, right, mono };

juce::String toString (DriverRole r);
juce::String toString (Side s);

/** What we know about a physical driver. Everything is optional; sensible defaults are
    derived from the role and size when a value is zero. */
struct DriverInfo
{
    juce::String model;
    double sizeInches  = 0.0;
    double fsHz        = 0.0;     // free-air resonance
    double powerRmsW   = 0.0;
    double xmaxMm      = 0.0;

    double effectiveDiameterM (DriverRole role) const;
};

/** Settings that live in the DSP for one output channel. */
struct ChannelSettings
{
    double gainDb   = 0.0;
    double delayMs  = 0.0;
    bool   inverted = false;
    bool   muted    = false;
    FilterSpec highPass = FilterSpec::crossover (FilterType::highPass, 20.0);
    FilterSpec lowPass  = FilterSpec::crossover (FilterType::lowPass, 20000.0);
    std::vector<FilterSpec> peq;

    /** Complete electrical transfer function of this channel (gain, polarity, delay, filters). */
    FrequencyResponse response (const std::vector<double>& freqs, double sampleRate, bool includeDelay = true) const;

    juce::var toVar() const;
    static ChannelSettings fromVar (const juce::var& v);
};

/** One DSP output channel feeding one driver (or a mono sub). */
struct OutputChannel
{
    juce::String id;            // e.g. "FL.tweeter"
    juce::String label;         // e.g. "Front Left Tweeter"
    Side         side = Side::left;
    DriverRole   role = DriverRole::midbass;
    int          dspOutput = 0; // 0-based output index on the DSP
    DriverInfo   driver;
    ChannelSettings settings;

    bool hasHighPass() const { return role != DriverRole::subwoofer; }
    bool hasLowPass()  const { return role != DriverRole::tweeter && role != DriverRole::fullrange; }
};

/** What the target DSP can do; the optimiser quantises its results to these limits. */
struct DspCapabilities
{
    double sampleRate      = 48000.0;
    int    peqBandsPerChannel = 10;
    double peqMinQ         = 0.4;
    double peqMaxQ         = 15.0;
    double peqMaxGainDb    = 12.0;
    double peqMinGainDb    = -15.0;
    double delayStepMs     = 0.01;
    double maxDelayMs      = 20.0;
    double gainStepDb      = 0.1;
    double minChannelGainDb = -40.0;
    double maxChannelGainDb = 12.0;
    double freqStepHz      = 0.0;    // 0 = continuous; otherwise snaps to ISO 1/24 oct style grid
    std::vector<int> availableSlopes { 6, 12, 18, 24, 36, 48 };
    bool   hasLinkwitzRiley = true;
    bool   hasBessel        = true;
    bool   hasAllPass       = false;

    juce::var toVar() const;
    static DspCapabilities fromVar (const juce::var& v);
};

/** Safety limits used by every automatic step so cheap drivers and amps survive. */
struct SafetyLimits
{
    double maxBoostDb          = 6.0;    // never boost an EQ band more than this
    double maxLowFreqBoostDb   = 3.0;    // stricter boost limit below 2x the HP of small drivers
    double sweepLevelDbFS      = -18.0;  // measurement level
    double tweeterMinHpFactor  = 1.6;    // HP >= this x Fs
    double minTweeterHpHz      = 1800.0; // when Fs unknown
    double minMidrangeHpHz     = 250.0;
    double minMidbassHpHz      = 45.0;
};

struct SystemConfig
{
    juce::String name = "My Car";
    std::vector<OutputChannel> channels;
    DspCapabilities caps;
    SafetyLimits safety;
    TargetCurve target = TargetCurve::carReference();
    int listeningSeat = 0;     // 0 = driver, 1 = passenger, 2 = centre

    OutputChannel*       find (const juce::String& id);
    const OutputChannel* find (const juce::String& id) const;
    std::vector<OutputChannel*> channelsFor (Side side);
    std::vector<const OutputChannel*> channelsFor (Side side) const;
    bool hasSubwoofer() const;

    /** Ready-made layouts: ways = 2, 3 or 4 per side, optional sub (mono or stereo). */
    static SystemConfig makeLayout (int waysPerSide, bool withSub, bool stereoSub = false);

    juce::var toVar() const;
    static SystemConfig fromVar (const juce::var& v);
};
} // namespace fa
