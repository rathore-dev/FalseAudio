#include "TargetLibrary.h"
#include "FalseAudioData.h"

namespace fa
{
namespace
{
    struct HouseInfo
    {
        const char* fileStem;    // "<stem>_0db_REW.txt" in the embedded data
        const char* displayName;
        const char* blurb;
    };

    // Order shown in the UI.
    const HouseInfo houseCurves[] =
    {
        { "Audiofrog",      "Audiofrog",      "The target from the Audiofrog stereo tuning process: smooth bass rise, gentle treble roll-off." },
        { "Harman",         "Harman",         "Harman in-car preference research curve: moderate bass shelf, slightly tilted top." },
        { "JBL",            "JBL",            "JBL car-audio house curve." },
        { "JL_Audio",       "JL Audio",       "JL Audio's car-audio target with a strong low-bass rise." },
        { "RAW-Cat",        "RAW-Cat",        "RAW-Cat competition-style curve with a big low-end rise." },
        { "Whitledge",      "Whitledge",      "Whitledge curve: steep bass rise and a pronounced treble tilt (warm)." },
        { "Half_Whitledge", "Half Whitledge", "Half-strength Whitledge: between Whitledge and a flatter car curve." },
        { "Flat",           "Flat (house)",   "Flat reference from the house-curve set (with the set's band split)." },
    };

    struct BandFile { const char* token; bool hasRole; DriverRole role; const char* label; };

    const BandFile bandFiles[] =
    {
        { "_Front_sub_SUM_", true,  DriverRole::subwoofer, "sub" },
        { "_Midbass_SUM_",   true,  DriverRole::midbass,   "midbass" },
        { "_Midrange_SUM_",  true,  DriverRole::midrange,  "midrange" },
        { "_Tweeter_SUM_",   true,  DriverRole::tweeter,   "tweeter" },
        { "_Rear_sub_SUM_",  false, DriverRole::subwoofer, "Rear sub" },
    };

    juce::String resourceText (const char* resourceName)
    {
        int size = 0;
        if (auto* data = FalseAudioData::getNamedResource (resourceName, size))
            return juce::String::fromUTF8 (data, size);
        return {};
    }

    juce::String describeShape (const TargetCurve& c)
    {
        auto fmt = [] (double db) { return (db >= 0.0 ? "+" : "") + juce::String (db, 1) + " dB"; };
        return fmt (c.levelAt (30.0)) + " at 30 Hz, " + fmt (c.levelAt (10000.0)) + " at 10 kHz (re 1 kHz).";
    }

    std::vector<TargetLibrary::Entry> loadBuiltIns()
    {
        std::vector<TargetLibrary::Entry> out;

        // Index the embedded files by their original file names.
        std::map<juce::String, const char*> byFile;
        for (int i = 0; i < FalseAudioData::namedResourceListSize; ++i)
        {
            const char* res = FalseAudioData::namedResourceList[i];
            if (auto* orig = FalseAudioData::getNamedResourceOriginalFilename (res))
                byFile[juce::String (orig)] = res;
        }

        for (auto& h : houseCurves)
        {
            const juce::String stem (h.fileStem);
            auto full = byFile.find (stem + "_0db_REW.txt");
            if (full == byFile.end())
                continue;

            const auto fullText = resourceText (full->second);
            auto raw = TargetCurve::parse (fullText, h.displayName, false);
            if (raw.points.size() < 2)
                continue;

            // One reference for the whole family so the bands keep their relation to the sum.
            const double ref = raw.levelAt (1000.0);

            TargetLibrary::Entry e;
            e.curve = TargetCurve::parse (fullText, h.displayName, true);
            e.family = juce::String (h.displayName).upToFirstOccurrenceOf (" (", false, false);
            e.builtIn = true;

            for (auto& b : bandFiles)
            {
                for (auto& [fileName, res] : byFile)
                {
                    // e.g. "Audiofrog" + "_Midbass_SUM_" + "0db_REW.txt"
                    if (! fileName.startsWith (stem) || ! fileName.substring (stem.length()).startsWith (b.token))
                        continue;

                    auto band = TargetCurve::parse (resourceText (res), juce::String (h.displayName) + " " + b.label, false);
                    if (band.points.size() < 2)
                        continue;
                    for (auto& p : band.points)
                        p.second -= ref;

                    if (b.hasRole)
                        e.bands[b.role] = band;
                    else
                        e.extraBands[b.label] = band;
                }
            }

            e.description = juce::String (h.blurb) + " " + describeShape (e.curve)
                          + (e.bands.empty() ? juce::String() : " Includes per-band targets (sub, midbass, midrange, tweeter).");
            out.push_back (std::move (e));
        }

        // The parametric presets.
        auto addPreset = [&out] (TargetCurve c, const juce::String& blurb)
        {
            TargetLibrary::Entry e;
            e.family = "FalseAudio";
            e.description = blurb + " " + describeShape (c);
            e.curve = std::move (c);
            out.push_back (std::move (e));
        };
        addPreset (TargetCurve::carReference(), "Parametric car reference (Audiofrog-style), adjustable bass and treble.");
        addPreset (TargetCurve::harmanLike(),   "Parametric, slightly warmer with a softer top end.");
        addPreset (TargetCurve::flat(),         "Parametric flat (home/studio style), no bass rise or tilt.");

        return out;
    }

    bool isCurveFile (const juce::File& f)
    {
        return f.hasFileExtension ("txt;csv;frd;tsv;dat");
    }
}

const TargetCurve* TargetLibrary::Entry::band (DriverRole role) const
{
    auto it = bands.find (role);
    return it != bands.end() ? &it->second : nullptr;
}

const std::vector<TargetLibrary::Entry>& TargetLibrary::builtIn()
{
    static const std::vector<Entry> entries = loadBuiltIns();
    return entries;
}

juce::File TargetLibrary::userFolder()
{
    auto f = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                 .getChildFile ("FalseAudio").getChildFile ("Targets");
    f.createDirectory();
    return f;
}

std::vector<TargetLibrary::Entry> TargetLibrary::userCurves()
{
    std::vector<Entry> out;
    auto files = userFolder().findChildFiles (juce::File::findFiles, false);
    files.sort();
    for (auto& f : files)
    {
        if (! isCurveFile (f))
            continue;

        auto c = TargetCurve::parse (f.loadFileAsString(), f.getFileNameWithoutExtension());
        if (c.points.size() < 2)
            continue;

        Entry e;
        e.family = "User";
        e.description = "Imported from " + f.getFileName() + ". " + describeShape (c);
        e.curve = std::move (c);
        e.builtIn = false;
        e.file = f;
        out.push_back (std::move (e));
    }
    return out;
}

bool TargetLibrary::import (const juce::File& file, Entry& out, juce::String& error)
{
    if (! file.existsAsFile())
    {
        error = file.getFullPathName() + " does not exist.";
        return false;
    }

    auto c = TargetCurve::parse (file.loadFileAsString(), file.getFileNameWithoutExtension());
    if (c.points.size() < 2)
    {
        error = "No 'frequency dB' data found in " + file.getFileName() + ".";
        return false;
    }
    if (c.points.front().first > 200.0 || c.points.back().first < 5000.0)
    {
        error = file.getFileName() + " only covers " + juce::String (c.points.front().first, 0) + " - "
              + juce::String (c.points.back().first, 0) + " Hz; a target must span at least 200 Hz - 5 kHz.";
        return false;
    }

    auto folder = userFolder();
    auto dest = folder.getChildFile (file.getFileNameWithoutExtension() + ".txt");
    if (file != dest)
    {
        if (dest.existsAsFile() && dest.loadFileAsString() != file.loadFileAsString())
            dest = dest.getNonexistentSibling (false);
        if (! file.copyFileTo (dest))
        {
            error = "Could not copy the curve to " + dest.getFullPathName();
            return false;
        }
    }

    c.name = dest.getFileNameWithoutExtension();
    out = {};
    out.family = "User";
    out.description = "Imported from " + file.getFileName() + ". " + describeShape (c);
    out.curve = std::move (c);
    out.builtIn = false;
    out.file = dest;
    return true;
}

const TargetLibrary::Entry* TargetLibrary::findByName (const juce::String& name)
{
    for (auto& e : builtIn())
        if (e.curve.name == name)
            return &e;
    return nullptr;
}

std::optional<TargetLibrary::Entry> TargetLibrary::find (const juce::String& name)
{
    if (auto* e = findByName (name))
        return *e;
    for (auto& e : userCurves())
        if (e.curve.name == name)
            return e;
    return std::nullopt;
}

std::vector<TargetLibrary::Entry> TargetLibrary::all()
{
    auto out = builtIn();
    for (auto& e : userCurves())
        out.push_back (std::move (e));
    return out;
}

bool TargetLibrary::removeUserCurve (const juce::String& name)
{
    for (auto& e : userCurves())
        if (e.curve.name == name && e.file.existsAsFile())
            return e.file.deleteFile();
    return false;
}
} // namespace fa
