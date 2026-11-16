#include "GearValidator.h"

#include <cmath>

namespace fa::gear
{
namespace
{
using Level = GearIssue::Level;

struct Ctx
{
    const juce::String& file;
    juce::String id;
    std::vector<GearIssue>& out;
    void add (Level l, const juce::String& field, const juce::String& msg)
    {
        out.push_back ({ l, file, id, field, msg });
    }
};

bool isNumber (const juce::var& v) { return v.isInt() || v.isInt64() || v.isDouble(); }

// Keys that must be > 0 when present: a 0 / negative value is a data-entry error.
const char* const positiveKeys[] = { "nominalOhms", "reOhms", "coilOhms", "fsHz", "qts", "qes", "qms", "sizeInches", "xmaxMm",
                                     "vasLitres", "powerRmsW", "powerPeakW", "mmsGrams", "sdCm2", "bl", "leMh" };

void checkDriver (const juce::NamedValueSet& d, const juce::String& category, Ctx& c)
{
    for (auto* k : positiveKeys)
        if (d.contains (k))
        {
            const auto& v = d[k];
            if (! isNumber (v) || (double) v <= 0.0)
                c.add (Level::error, k, juce::String (k) + " = " + v.toString() + " (must be a positive number; remove the field if unknown)");
        }

    if (d.contains ("qts") && isNumber (d["qts"]) && (double) d["qts"] > 1.5)
        c.add (Level::warning, "qts", "Qts " + d["qts"].toString() + " is above 1.5 (check the source; real for some closed-back/pro drivers)");
    if (d.contains ("powerRmsW") && isNumber (d["powerRmsW"]) && (double) d["powerRmsW"] > 5000.0)
        c.add (Level::warning, "powerRmsW", "RMS " + d["powerRmsW"].toString() + " W exceeds 5000 W (marketing figure?)");
    if (d.contains ("sensitivityDb") && isNumber (d["sensitivityDb"]))
    {
        const double s = d["sensitivityDb"];
        if (s < 70.0 || s > 110.0)
            c.add (Level::warning, "sensitivityDb", "sensitivity " + juce::String (s, 1) + " dB is outside 70-110 dB");
    }
    if (d.contains ("powerPeakW") && d.contains ("powerRmsW") && isNumber (d["powerPeakW"]) && isNumber (d["powerRmsW"])
        && (double) d["powerPeakW"] < (double) d["powerRmsW"])
        c.add (Level::warning, "powerPeakW", "peak power " + d["powerPeakW"].toString() + " W is below RMS " + d["powerRmsW"].toString() + " W");
    if (category == "subwoofer" && d.contains ("sizeInches") && d.contains ("fsHz") && isNumber (d["sizeInches"]) && isNumber (d["fsHz"])
        && (double) d["sizeInches"] <= 6.5 && (double) d["fsHz"] >= 60.0)
        c.add (Level::warning, "fsHz", "subwoofer of " + d["sizeInches"].toString() + " in with Fs " + d["fsHz"].toString() + " Hz looks like a mid-bass driver");
    if (! d.contains ("nominalOhms") && ! d.contains ("coilOhms"))
        c.add (Level::warning, "nominalOhms", "no impedance given (treated as unknown)");
}

void checkAmp (const juce::NamedValueSet& a, Ctx& c)
{
    const int channels = a.contains ("channels") ? (int) a["channels"] : 1;
    if (auto* rms = a["rmsPerChannelW"].getDynamicObject())
        for (auto& nv : rms->getProperties())
        {
            if (! isNumber (nv.value) || (double) nv.value <= 0.0)
                c.add (Level::error, "rmsPerChannelW." + nv.name.toString(), "RMS " + nv.value.toString() + " W (must be positive)");
            else if ((double) nv.value > 5000.0 && channels > 1)
                c.add (Level::warning, "rmsPerChannelW." + nv.name.toString(), juce::String (channels) + "-channel amp rated " + nv.value.toString() + " W per channel");
        }
    if (a.contains ("minStableOhms") && (! isNumber (a["minStableOhms"]) || (double) a["minStableOhms"] < 0.0))
        c.add (Level::error, "minStableOhms", "minStableOhms = " + a["minStableOhms"].toString());
    if (a.contains ("inputSensitivityMinV") && a.contains ("inputSensitivityMaxV") && isNumber (a["inputSensitivityMinV"])
        && isNumber (a["inputSensitivityMaxV"]) && (double) a["inputSensitivityMinV"] > (double) a["inputSensitivityMaxV"])
        c.add (Level::warning, "inputSensitivityMinV", "input sensitivity min is above max");
}

/** Walks the product tree: every "driver" block (also nested under components) and "amp" block is checked. */
void walk (const juce::var& v, const juce::String& category, Ctx& c)
{
    if (auto* o = v.getDynamicObject())
    {
        for (auto& nv : o->getProperties())
        {
            if (nv.name.toString() == "driver" && nv.value.getDynamicObject() != nullptr)
                checkDriver (nv.value.getDynamicObject()->getProperties(), category, c);
            else if (nv.name.toString() == "amp" && nv.value.getDynamicObject() != nullptr)
                checkAmp (nv.value.getDynamicObject()->getProperties(), c);
            else
                walk (nv.value, category, c);
        }
    }
    else if (auto* arr = v.getArray())
        for (auto& e : *arr)
            walk (e, category, c);
}
} // namespace

void validateGearProduct (const juce::var& product, const juce::String& file, std::vector<GearIssue>& out)
{
    Ctx c { file, product.getProperty ("id", "?").toString(), out };
    walk (product, product.getProperty ("category", "").toString(), c);
}

int validateGearFolder (const juce::File& gearRoot, std::vector<GearIssue>& out)
{
    int n = 0;
    for (auto& f : gearRoot.findChildFiles (juce::File::findFiles, true, "*.json"))
    {
        const auto rel = f.getRelativePathFrom (gearRoot).replaceCharacter ('\\', '/');
        const auto parsed = juce::JSON::parse (f);
        auto* products = parsed.getProperty ("products", juce::var()).getArray();
        if (products == nullptr)
            continue;
        for (auto& p : *products)
        {
            ++n;
            validateGearProduct (p, rel, out);
        }
    }
    return n;
}

juce::String formatGearReport (const std::vector<GearIssue>& issues, int productsChecked)
{
    int errors = 0, warnings = 0;
    for (auto& i : issues)
        (i.level == Level::error ? errors : warnings)++;
    juce::String r;
    r << "Gear data validation: " << productsChecked << " products, " << errors << " error(s), " << warnings << " warning(s)\n"
      << "ERROR = impossible value (fails the test). WARNING = implausible, review the source.\n\n";
    for (auto lvl : { Level::error, Level::warning })
        for (auto& i : issues)
            if (i.level == lvl)
                r << (lvl == Level::error ? "ERROR   " : "WARNING ") << i.file << " | " << i.productId << " | " << i.field << " | " << i.message << "\n";
    return r;
}
} // namespace fa::gear
