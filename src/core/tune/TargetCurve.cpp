#include "TargetCurve.h"

namespace fa
{
double TargetCurve::levelAt (double f) const
{
    if (! points.empty())
    {
        if (f <= points.front().first) return points.front().second;
        if (f >= points.back().first)  return points.back().second;
        for (size_t i = 1; i < points.size(); ++i)
        {
            if (f <= points[i].first)
            {
                const auto& a = points[i - 1];
                const auto& b = points[i];
                const double t = std::log (f / a.first) / std::log (b.first / a.first);
                return a.second + (b.second - a.second) * t;
            }
        }
        return points.back().second;
    }

    // Bass: smooth (raised-cosine in log f) transition from +boost to 0 dB.
    double level = 0.0;
    if (f <= transitionLowHz)
        level = bassBoostDb;
    else if (f < transitionHighHz)
    {
        const double t = std::log (f / transitionLowHz) / std::log (transitionHighHz / transitionLowHz);
        level = bassBoostDb * (0.5 + 0.5 * std::cos (pi * t));
    }

    // Treble: linear-in-log tilt from tiltStartHz to 20 kHz.
    if (f > tiltStartHz)
        level += trebleTiltDb * std::log (f / tiltStartHz) / std::log (20000.0 / tiltStartHz);

    // Infrasonic: 2nd-order style roll-off below infrasonicRollHz.
    if (infrasonicRollHz > 0.0 && f < infrasonicRollHz)
        level -= 12.0 * std::log2 (infrasonicRollHz / f);

    return level;
}

std::vector<double> TargetCurve::levels (const std::vector<double>& freqs) const
{
    std::vector<double> out (freqs.size());
    for (size_t i = 0; i < freqs.size(); ++i)
        out[i] = levelAt (freqs[i]);
    return out;
}

TargetCurve TargetCurve::carReference()
{
    return {};
}

TargetCurve TargetCurve::harmanLike()
{
    TargetCurve t;
    t.name = "Harman-style Car";
    t.bassBoostDb = 8.0;
    t.transitionLowHz = 70.0;
    t.transitionHighHz = 200.0;
    t.trebleTiltDb = -4.0;
    t.tiltStartHz = 500.0;
    return t;
}

TargetCurve TargetCurve::flat()
{
    TargetCurve t;
    t.name = "Flat";
    t.bassBoostDb = 0.0;
    t.trebleTiltDb = 0.0;
    t.infrasonicRollHz = 0.0;
    return t;
}

TargetCurve TargetCurve::parse (const juce::String& text, const juce::String& name)
{
    return parse (text, name, true);
}

TargetCurve TargetCurve::parse (const juce::String& text, const juce::String& name, bool normaliseAt1k)
{
    TargetCurve t;
    t.name = name;

    juce::StringArray lines;
    lines.addLines (text);
    for (auto line : lines)
    {
        line = line.trim();
        if (line.isEmpty() || ! (juce::CharacterFunctions::isDigit (line[0]) || line[0] == '.'))
            continue;

        juce::StringArray tok;
        tok.addTokens (line.replaceCharacters (",;\t", "   "), " ", "\"");
        tok.removeEmptyStrings();
        if (tok.size() >= 2 && tok[0].getDoubleValue() > 0.0)
            t.points.emplace_back (tok[0].getDoubleValue(), tok[1].getDoubleValue());
    }
    std::sort (t.points.begin(), t.points.end());

    // Normalise so 1 kHz sits at 0 dB, like the process PDF expects.
    if (normaliseAt1k && ! t.points.empty())
    {
        const double ref = t.levelAt (1000.0);
        for (auto& p : t.points)
            p.second -= ref;
    }
    return t;
}

juce::var TargetCurve::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("name", name);
    o->setProperty ("bassBoost", bassBoostDb);
    o->setProperty ("transLow", transitionLowHz);
    o->setProperty ("transHigh", transitionHighHz);
    o->setProperty ("trebleTilt", trebleTiltDb);
    o->setProperty ("tiltStart", tiltStartHz);
    o->setProperty ("infraRoll", infrasonicRollHz);

    juce::Array<juce::var> pts;
    for (auto& p : points)
        pts.add (juce::Array<juce::var> { p.first, p.second });
    o->setProperty ("points", pts);
    return juce::var (o);
}

TargetCurve TargetCurve::fromVar (const juce::var& v)
{
    TargetCurve t;
    t.name             = v.getProperty ("name", t.name).toString();
    t.bassBoostDb      = v.getProperty ("bassBoost", t.bassBoostDb);
    t.transitionLowHz  = v.getProperty ("transLow", t.transitionLowHz);
    t.transitionHighHz = v.getProperty ("transHigh", t.transitionHighHz);
    t.trebleTiltDb     = v.getProperty ("trebleTilt", t.trebleTiltDb);
    t.tiltStartHz      = v.getProperty ("tiltStart", t.tiltStartHz);
    t.infrasonicRollHz = v.getProperty ("infraRoll", t.infrasonicRollHz);

    if (auto* pts = v.getProperty ("points", {}).getArray())
        for (auto& p : *pts)
            if (p.isArray() && p.size() >= 2)
                t.points.emplace_back ((double) p[0], (double) p[1]);
    return t;
}
} // namespace fa
