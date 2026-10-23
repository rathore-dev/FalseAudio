#pragma once

#include "../Types.h"
#include <array>

namespace fa
{
struct BiquadCoeffs
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; // a0 normalised to 1

    cplx response (double freq, double sampleRate) const;

    static BiquadCoeffs peaking   (double f, double q, double gainDb, double fs);
    static BiquadCoeffs lowShelf  (double f, double q, double gainDb, double fs);
    static BiquadCoeffs highShelf (double f, double q, double gainDb, double fs);
    static BiquadCoeffs lowPass2  (double f, double q, double fs);
    static BiquadCoeffs highPass2 (double f, double q, double fs);
    static BiquadCoeffs lowPass1  (double f, double fs);
    static BiquadCoeffs highPass1 (double f, double fs);
    static BiquadCoeffs allPass2  (double f, double q, double fs);
    static BiquadCoeffs allPass1  (double f, double fs);
};

enum class FilterType
{
    peaking,
    lowShelf,
    highShelf,
    highPass,
    lowPass,
    allPass
};

enum class Alignment
{
    butterworth,
    linkwitzRiley,
    bessel
};

juce::String toString (FilterType t);
juce::String toString (Alignment a);

/** One user-level filter, as it would be entered into a DSP. */
struct FilterSpec
{
    FilterType type      = FilterType::peaking;
    double     freq      = 1000.0;
    double     q         = 0.707;
    double     gainDb    = 0.0;
    int        slopeDbPerOct = 24;          // for highPass / lowPass
    Alignment  alignment = Alignment::linkwitzRiley;
    bool       enabled   = true;

    int order() const noexcept { return juce::jlimit (1, 8, slopeDbPerOct / 6); }

    /** Expands into cascaded biquad sections at the given sample rate. */
    std::vector<BiquadCoeffs> toBiquads (double sampleRate) const;

    cplx response (double freq, double sampleRate) const;

    FrequencyResponse response (const std::vector<double>& freqs, double sampleRate) const;

    juce::String describe() const;

    juce::var toVar() const;
    static FilterSpec fromVar (const juce::var& v);

    static FilterSpec peq (double f, double q, double gainDb)
    {
        FilterSpec s;
        s.type = FilterType::peaking; s.freq = f; s.q = q; s.gainDb = gainDb;
        return s;
    }

    static FilterSpec crossover (FilterType hpOrLp, double f, int slope = 24, Alignment a = Alignment::linkwitzRiley)
    {
        FilterSpec s;
        s.type = hpOrLp; s.freq = f; s.slopeDbPerOct = slope; s.alignment = a;
        return s;
    }
};

/** Combined response of a filter chain. */
FrequencyResponse chainResponse (const std::vector<FilterSpec>& chain,
                                 const std::vector<double>& freqs,
                                 double sampleRate);
} // namespace fa
