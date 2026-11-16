#pragma once

#include "../dsp/Filters.h"
#include "TargetCurve.h"

namespace fa
{
enum class DriverRole { subwoofer, midbass, midrange, tweeter, fullrange };
enum class Side       { left, right, mono };

/** How an output channel is wired to its speaker(s).
      activeDriver  one driver per DSP output (the classic fully active layout)
      passiveSet    woofer + tweeter behind a passive crossover on ONE output (component set or coax):
                    the DSP can only shape the whole set (HP, delay, level, polarity, PEQ)
      fullRange     one full-range driver on the output (pair, or a single mono driver in a test box)
      sub           subwoofer output */
enum class ChannelKind { activeDriver, passiveSet, fullRange, sub };

juce::String toString (DriverRole r);
juce::String toString (ChannelKind k);
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

/** What we know about the passive network of a passiveSet channel (all optional). */
struct PassiveSetInfo
{
    double crossoverHz = 0.0;     // 0 = unknown
    int    slopeDbPerOct = 12;
    bool   coaxial = false;       // tweeter mounted in front of the woofer (no big offset)
    juce::String tweeterModel;
    double tweeterSizeInches = 1.0;
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
    DriverInfo   driver;        // for a passiveSet: the woofer
    ChannelSettings settings;
    ChannelKind  kind = ChannelKind::activeDriver;
    PassiveSetInfo passive;     // only meaningful when kind == passiveSet

    bool isPassiveSet() const { return kind == ChannelKind::passiveSet; }
    /** True when one output carries more than one driver (or one driver over its whole range):
        no crossover between channels exists to tune, and the DSP cannot touch what is inside. */
    bool coversWholeBand() const { return kind == ChannelKind::passiveSet || role == DriverRole::fullrange; }

    /** Keeps kind and role consistent (call after editing either). */
    void normaliseKind();
    static ChannelKind defaultKindFor (DriverRole r);

    bool hasHighPass() const { return role != DriverRole::subwoofer; }
    bool hasLowPass()  const { return (role != DriverRole::tweeter && role != DriverRole::fullrange) || isPassiveSet(); }
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

    /** Graphic-EQ DSPs (fixed centres, fixed or proportional Q, fixed gain step) instead of
        parametric bands. In graphic mode ChannelSettings::peq holds one peaking FilterSpec per
        centre (see GraphicEQ), so prediction and exports keep working unchanged. */
    enum class EqType { parametric, graphic };
    EqType eqType = EqType::parametric;
    std::vector<double> graphicBandsHz;   // centres; empty = standardGraphicBands (31)
    double graphicQ           = 0.0;      // fixed Q, or 0 = proportional-Q model (GraphicEQ::bandQ)
    double graphicGainStepDb  = 0.5;
    double graphicMaxBoostDb  = 12.0;
    double graphicMaxCutDb    = -12.0;
    bool   graphicPerChannel  = true;     // each output has its own GEQ; false = one GEQ per side

    bool isGraphic() const noexcept { return eqType == EqType::graphic; }

    /** The graphic centres in use (graphicBandsHz, or the 31-band ISO layout when empty). */
    std::vector<double> graphicCentres() const;

    /** Nominal centres for a band count: 31 = ISO 1/3 oct 20 Hz..20 kHz, 30 = 25 Hz..20 kHz,
        15 = ISO 2/3 oct 25 Hz..16 kHz, 13 = car head-unit 50 Hz..12.5 kHz, 10 = ISO octaves
        31.5 Hz..16 kHz, 20/21 = 1/2 oct; other counts are log-spaced 40 Hz..16 kHz. */
    static std::vector<double> standardGraphicBands (int count);

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

    /** True when a non-sub channel exists on the right side (otherwise there is nothing to match
        left to right: a single mono driver, or a one-sided test). */
    bool isStereo() const;
    bool hasPassiveSets() const;
    /** True when the channel belongs to a side's front chain (a mono non-sub driver counts as left). */
    static bool inFrontChain (const OutputChannel& c, Side side)
    {
        return c.role != DriverRole::subwoofer && (c.side == side || (c.side == Side::mono && side == Side::left));
    }

    /** Ready-made topologies (SystemPage presets). */
    enum class Preset { active3WaySub, active2WaySub, passive2WaySub, passiveFront, fullRangePair, singleDriver };
    static SystemConfig makePreset (Preset p);
    static juce::String presetName (Preset p);

    /** Ready-made layouts: ways = 2, 3 or 4 per side, optional sub (mono or stereo). */
    static SystemConfig makeLayout (int waysPerSide, bool withSub, bool stereoSub = false);

    juce::var toVar() const;
    static SystemConfig fromVar (const juce::var& v);
};
} // namespace fa
