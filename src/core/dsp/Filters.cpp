#include "Filters.h"

namespace fa
{
//==============================================================================
// RBJ audio-EQ-cookbook biquads.

cplx BiquadCoeffs::response (double freq, double fs) const
{
    const double w = 2.0 * pi * freq / fs;
    const cplx z1 = std::polar (1.0, -w);
    const cplx z2 = z1 * z1;
    return (b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2);
}

static BiquadCoeffs normalise (double b0, double b1, double b2, double a0, double a1, double a2)
{
    BiquadCoeffs c;
    c.b0 = b0 / a0; c.b1 = b1 / a0; c.b2 = b2 / a0;
    c.a1 = a1 / a0; c.a2 = a2 / a0;
    return c;
}

BiquadCoeffs BiquadCoeffs::peaking (double f, double q, double gainDb, double fs)
{
    const double A = std::pow (10.0, gainDb / 40.0);
    const double w0 = 2.0 * pi * f / fs;
    const double alpha = std::sin (w0) / (2.0 * q);
    const double c = std::cos (w0);
    return normalise (1 + alpha * A, -2 * c, 1 - alpha * A,
                      1 + alpha / A, -2 * c, 1 - alpha / A);
}

BiquadCoeffs BiquadCoeffs::lowShelf (double f, double q, double gainDb, double fs)
{
    const double A = std::pow (10.0, gainDb / 40.0);
    const double w0 = 2.0 * pi * f / fs;
    const double alpha = std::sin (w0) / (2.0 * q);
    const double c = std::cos (w0);
    const double s = 2.0 * std::sqrt (A) * alpha;
    return normalise (A * ((A + 1) - (A - 1) * c + s),
                      2 * A * ((A - 1) - (A + 1) * c),
                      A * ((A + 1) - (A - 1) * c - s),
                      (A + 1) + (A - 1) * c + s,
                      -2 * ((A - 1) + (A + 1) * c),
                      (A + 1) + (A - 1) * c - s);
}

BiquadCoeffs BiquadCoeffs::highShelf (double f, double q, double gainDb, double fs)
{
    const double A = std::pow (10.0, gainDb / 40.0);
    const double w0 = 2.0 * pi * f / fs;
    const double alpha = std::sin (w0) / (2.0 * q);
    const double c = std::cos (w0);
    const double s = 2.0 * std::sqrt (A) * alpha;
    return normalise (A * ((A + 1) + (A - 1) * c + s),
                      -2 * A * ((A - 1) + (A + 1) * c),
                      A * ((A + 1) + (A - 1) * c - s),
                      (A + 1) - (A - 1) * c + s,
                      2 * ((A - 1) - (A + 1) * c),
                      (A + 1) - (A - 1) * c - s);
}

BiquadCoeffs BiquadCoeffs::lowPass2 (double f, double q, double fs)
{
    const double w0 = 2.0 * pi * f / fs;
    const double alpha = std::sin (w0) / (2.0 * q);
    const double c = std::cos (w0);
    return normalise ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + alpha, -2 * c, 1 - alpha);
}

BiquadCoeffs BiquadCoeffs::highPass2 (double f, double q, double fs)
{
    const double w0 = 2.0 * pi * f / fs;
    const double alpha = std::sin (w0) / (2.0 * q);
    const double c = std::cos (w0);
    return normalise ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + alpha, -2 * c, 1 - alpha);
}

BiquadCoeffs BiquadCoeffs::lowPass1 (double f, double fs)
{
    const double k = std::tan (pi * f / fs);
    return normalise (k, k, 0, k + 1, k - 1, 0);
}

BiquadCoeffs BiquadCoeffs::highPass1 (double f, double fs)
{
    const double k = std::tan (pi * f / fs);
    return normalise (1, -1, 0, k + 1, k - 1, 0);
}

BiquadCoeffs BiquadCoeffs::allPass2 (double f, double q, double fs)
{
    const double w0 = 2.0 * pi * f / fs;
    const double alpha = std::sin (w0) / (2.0 * q);
    const double c = std::cos (w0);
    return normalise (1 - alpha, -2 * c, 1 + alpha, 1 + alpha, -2 * c, 1 - alpha);
}

BiquadCoeffs BiquadCoeffs::allPass1 (double f, double fs)
{
    const double k = std::tan (pi * f / fs);
    return normalise (k - 1, k + 1, 0, k + 1, k - 1, 0);
}

//==============================================================================
juce::String toString (FilterType t)
{
    switch (t)
    {
        case FilterType::peaking:   return "PEQ";
        case FilterType::lowShelf:  return "Low Shelf";
        case FilterType::highShelf: return "High Shelf";
        case FilterType::highPass:  return "High Pass";
        case FilterType::lowPass:   return "Low Pass";
        case FilterType::allPass:   return "All Pass";
    }
    return {};
}

juce::String toString (Alignment a)
{
    switch (a)
    {
        case Alignment::butterworth:   return "Butterworth";
        case Alignment::linkwitzRiley: return "Linkwitz-Riley";
        case Alignment::bessel:        return "Bessel";
    }
    return {};
}

namespace
{
    struct Section { bool firstOrder; double q; double freqScale; };

    std::vector<Section> butterworthSections (int order)
    {
        std::vector<Section> s;
        for (int k = 0; k < order / 2; ++k)
        {
            const double theta = pi * (2.0 * k + 1.0) / (2.0 * order);
            s.push_back ({ false, 1.0 / (2.0 * std::sin (theta)), 1.0 });
        }
        if (order % 2 == 1)
            s.push_back ({ true, 0.0, 1.0 });
        return s;
    }

    // -3 dB normalised Bessel lowpass sections (freq scale applies to low-pass; invert for high-pass).
    std::vector<Section> besselSections (int order)
    {
        switch (order)
        {
            case 1:  return { { true,  0.0,     1.0 } };
            case 2:  return { { false, 0.57735, 1.27201 } };
            case 3:  return { { true,  0.0,     1.32700 }, { false, 0.69105, 1.45240 } };
            default: return { { false, 0.52193, 1.41917 }, { false, 0.80554, 1.59102 } };
        }
    }
}

std::vector<BiquadCoeffs> FilterSpec::toBiquads (double fs) const
{
    std::vector<BiquadCoeffs> out;
    const double f = juce::jlimit (1.0, fs * 0.49, freq);

    switch (type)
    {
        case FilterType::peaking:   out.push_back (BiquadCoeffs::peaking   (f, q, gainDb, fs)); break;
        case FilterType::lowShelf:  out.push_back (BiquadCoeffs::lowShelf  (f, q, gainDb, fs)); break;
        case FilterType::highShelf: out.push_back (BiquadCoeffs::highShelf (f, q, gainDb, fs)); break;
        case FilterType::allPass:
            out.push_back (slopeDbPerOct <= 6 ? BiquadCoeffs::allPass1 (f, fs) : BiquadCoeffs::allPass2 (f, q, fs));
            break;

        case FilterType::highPass:
        case FilterType::lowPass:
        {
            const bool hp = (type == FilterType::highPass);
            std::vector<Section> sections;

            if (alignment == Alignment::linkwitzRiley)
            {
                // LR(2n) = Butterworth(n) squared. Odd orders are not LR; fall back to Butterworth.
                if (order() % 2 == 0)
                {
                    auto bw = butterworthSections (order() / 2);
                    sections.insert (sections.end(), bw.begin(), bw.end());
                    sections.insert (sections.end(), bw.begin(), bw.end());
                }
                else
                {
                    sections = butterworthSections (order());
                }
            }
            else if (alignment == Alignment::bessel)
            {
                sections = besselSections (std::min (order(), 4));
            }
            else
            {
                sections = butterworthSections (order());
            }

            for (auto& s : sections)
            {
                const double fc = hp ? f / s.freqScale : f * s.freqScale;
                if (s.firstOrder)
                    out.push_back (hp ? BiquadCoeffs::highPass1 (fc, fs) : BiquadCoeffs::lowPass1 (fc, fs));
                else
                    out.push_back (hp ? BiquadCoeffs::highPass2 (fc, s.q, fs) : BiquadCoeffs::lowPass2 (fc, s.q, fs));
            }
            break;
        }
    }

    return out;
}

cplx FilterSpec::response (double f, double fs) const
{
    if (! enabled)
        return { 1.0, 0.0 };

    cplx h (1.0, 0.0);
    for (auto& bq : toBiquads (fs))
        h *= bq.response (f, fs);
    return h;
}

FrequencyResponse FilterSpec::response (const std::vector<double>& freqs, double fs) const
{
    FrequencyResponse r = FrequencyResponse::unity (freqs);
    if (! enabled)
        return r;

    const auto biquads = toBiquads (fs);
    for (size_t i = 0; i < freqs.size(); ++i)
        for (auto& bq : biquads)
            r.values[i] *= bq.response (freqs[i], fs);
    return r;
}

juce::String FilterSpec::describe() const
{
    switch (type)
    {
        case FilterType::highPass:
        case FilterType::lowPass:
            return toString (type) + " " + juce::String (freq, 0) + " Hz " + juce::String (slopeDbPerOct)
                   + " dB/oct " + toString (alignment);
        case FilterType::allPass:
            return "All Pass " + juce::String (freq, 0) + " Hz Q " + juce::String (q, 2);
        default:
            return toString (type) + " " + juce::String (freq, 1) + " Hz  " + juce::String (gainDb, 1)
                   + " dB  Q " + juce::String (q, 2);
    }
}

juce::var FilterSpec::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("type", (int) type);
    o->setProperty ("freq", freq);
    o->setProperty ("q", q);
    o->setProperty ("gain", gainDb);
    o->setProperty ("slope", slopeDbPerOct);
    o->setProperty ("alignment", (int) alignment);
    o->setProperty ("enabled", enabled);
    return juce::var (o);
}

FilterSpec FilterSpec::fromVar (const juce::var& v)
{
    FilterSpec s;
    s.type          = (FilterType) (int) v.getProperty ("type", 0);
    s.freq          = v.getProperty ("freq", 1000.0);
    s.q             = v.getProperty ("q", 0.707);
    s.gainDb        = v.getProperty ("gain", 0.0);
    s.slopeDbPerOct = v.getProperty ("slope", 24);
    s.alignment     = (Alignment) (int) v.getProperty ("alignment", 1);
    s.enabled       = v.getProperty ("enabled", true);
    return s;
}

FrequencyResponse chainResponse (const std::vector<FilterSpec>& chain,
                                 const std::vector<double>& freqs,
                                 double fs)
{
    FrequencyResponse r = FrequencyResponse::unity (freqs);
    for (auto& f : chain)
        r *= f.response (freqs, fs);
    return r;
}
} // namespace fa
