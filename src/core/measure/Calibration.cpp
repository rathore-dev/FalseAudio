#include "Calibration.h"

namespace fa
{
MicCalibration MicCalibration::parse (const juce::String& text, const juce::String& name)
{
    MicCalibration cal;
    cal.name = name;

    juce::StringArray lines;
    lines.addLines (text);

    struct Row { double f, g, p; bool hasP; };
    std::vector<Row> rows;

    for (auto line : lines)
    {
        line = line.trim();
        if (line.isEmpty())
            continue;

        if (line.containsIgnoreCase ("sens factor"))
        {
            const auto value = line.fromFirstOccurrenceOf ("=", false, false).trim();
            cal.sensitivityDb = value.getDoubleValue();
            if (line.containsIgnoreCase ("SERNO"))
                cal.serial = line.fromFirstOccurrenceOf ("SERNO:", false, true).trim()
                                 .upToFirstOccurrenceOf (" ", false, false).removeCharacters ("\"'");
            continue;
        }

        if (! (juce::CharacterFunctions::isDigit (line[0]) || line[0] == '.' || line[0] == '-' || line[0] == '+'))
            continue; // header or comment line

        juce::StringArray tokens;
        tokens.addTokens (line.replaceCharacters (",;\t", "   "), " ", "\"");
        tokens.removeEmptyStrings();
        if (tokens.size() < 2)
            continue;

        Row r { tokens[0].getDoubleValue(), tokens[1].getDoubleValue(), 0.0, tokens.size() >= 3 };
        if (r.hasP)
            r.p = tokens[2].getDoubleValue();
        if (r.f > 0.0)
            rows.push_back (r);
    }

    std::sort (rows.begin(), rows.end(), [] (const Row& a, const Row& b) { return a.f < b.f; });

    cal.hasPhase = ! rows.empty();
    for (auto& r : rows)
    {
        cal.freqs.push_back (r.f);
        cal.gainDb.push_back (r.g);
        cal.phaseDeg.push_back (r.p);
        cal.hasPhase = cal.hasPhase && r.hasP;
    }
    return cal;
}

MicCalibration MicCalibration::load (const juce::File& file)
{
    return parse (file.loadFileAsString(), file.getFileNameWithoutExtension());
}

namespace
{
    double interpLog (const std::vector<double>& xs, const std::vector<double>& ys, double x)
    {
        if (xs.empty()) return 0.0;
        if (x <= xs.front()) return ys.front();
        if (x >= xs.back())  return ys.back();
        auto it = std::upper_bound (xs.begin(), xs.end(), x);
        const size_t i1 = (size_t) std::distance (xs.begin(), it), i0 = i1 - 1;
        const double t = std::log (x / xs[i0]) / std::log (xs[i1] / xs[i0]);
        return ys[i0] + (ys[i1] - ys[i0]) * t;
    }
}

double MicCalibration::deviationDb (double f) const       { return interpLog (freqs, gainDb, f); }
double MicCalibration::deviationPhaseDeg (double f) const { return hasPhase ? interpLog (freqs, phaseDeg, f) : 0.0; }

FrequencyResponse MicCalibration::corrected (const FrequencyResponse& measured) const
{
    if (! isValid())
        return measured;

    FrequencyResponse out = measured;
    for (size_t i = 0; i < out.size(); ++i)
    {
        const double f = out.freqs[i];
        out.values[i] /= std::polar (magFromDb (deviationDb (f)), deviationPhaseDeg (f) * pi / 180.0);
    }
    return out;
}

juce::var MicCalibration::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("name", name);
    o->setProperty ("serial", serial);
    o->setProperty ("sensitivity", sensitivityDb);
    o->setProperty ("hasPhase", hasPhase);

    juce::Array<juce::var> f, g, p;
    for (size_t i = 0; i < freqs.size(); ++i)
    {
        f.add (freqs[i]);
        g.add (gainDb[i]);
        p.add (phaseDeg[i]);
    }
    o->setProperty ("freqs", f);
    o->setProperty ("gain", g);
    o->setProperty ("phase", p);
    return juce::var (o);
}

MicCalibration MicCalibration::fromVar (const juce::var& v)
{
    MicCalibration c;
    c.name          = v.getProperty ("name", {}).toString();
    c.serial        = v.getProperty ("serial", {}).toString();
    c.sensitivityDb = v.getProperty ("sensitivity", 0.0);
    c.hasPhase      = v.getProperty ("hasPhase", false);

    if (auto* f = v.getProperty ("freqs", {}).getArray())
        for (auto& x : *f) c.freqs.push_back (x);
    if (auto* g = v.getProperty ("gain", {}).getArray())
        for (auto& x : *g) c.gainDb.push_back (x);
    if (auto* p = v.getProperty ("phase", {}).getArray())
        for (auto& x : *p) c.phaseDeg.push_back (x);

    c.phaseDeg.resize (c.freqs.size(), 0.0);
    c.gainDb.resize (c.freqs.size(), 0.0);
    return c;
}
} // namespace fa
