#include "CalibrationDownloader.h"

namespace fa
{
juce::String CalibrationDownloader::normaliseSerial (const juce::String& typed)
{
    return typed.retainCharacters ("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ").toUpperCase();
}

bool CalibrationDownloader::isValidSerial (const juce::String& s)
{
    if (modelForSerial (s) == Model::umik1)
        return true;
    return s.length() >= 5 && s.length() <= 12;
}

CalibrationDownloader::Model CalibrationDownloader::modelForSerial (const juce::String& s)
{
    return (s.length() == 7 && s.containsOnly ("0123456789") && s[0] == '7') ? Model::umik1 : Model::umik2;
}

juce::String CalibrationDownloader::productPageUrl (Model m)
{
    return m == Model::umik1 ? "https://www.minidsp.com/products/acoustic-measurement/umik-1"
                             : "https://www.minidsp.com/products/acoustic-measurement/umik-2";
}

juce::String CalibrationDownloader::miniDspFileName (const juce::String& s, Angle a)
{
    return a == Angle::deg0 ? s + ".txt" : s + "_90deg.txt";
}

juce::String CalibrationDownloader::storedFileName (const juce::String& s, Angle a)
{
    return s + (a == Angle::deg0 ? "_0.txt" : "_90.txt");
}

juce::File CalibrationDownloader::defaultCalibrationDir()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("FalseAudio").getChildFile ("calibration");
}

juce::File CalibrationDownloader::defaultDownloadsDir()
{
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Downloads");
}

CalibrationDownloader::Angle CalibrationDownloader::angleFromFileName (const juce::String& n)
{
    const auto l = n.toLowerCase();
    return (l.contains ("_90") || l.contains ("90deg") || l.contains ("90-deg") || l.contains ("90 deg")) ? Angle::deg90
                                                                                                          : Angle::deg0;
}

CalibrationDownloader::Imported CalibrationDownloader::importFile (const juce::File& source, const juce::String& hint,
                                                                  const juce::File& dir)
{
    Imported r;
    if (! source.existsAsFile()) { r.error = "File not found: " + source.getFileName(); return r; }

    const auto text = source.loadFileAsString();
    r.cal = MicCalibration::parse (text, source.getFileNameWithoutExtension());
    if (! r.cal.isValid()) { r.error = "No calibration data found in " + source.getFileName(); return r; }

    juce::String serial = normaliseSerial (r.cal.serial);
    if (serial.isEmpty())
    {
        const auto lead = normaliseSerial (source.getFileNameWithoutExtension().initialSectionContainingOnly ("0123456789-"));
        serial = lead.length() >= 5 ? lead : normaliseSerial (hint);
    }
    if (serial.isEmpty()) { r.error = "Could not determine the serial number; type it in first."; return r; }

    r.serial = serial;
    r.angle = angleFromFileName (source.getFileName());
    r.cal.serial = serial;

    dir.createDirectory();
    r.saved = dir.getChildFile (storedFileName (serial, r.angle));
    if (! r.saved.replaceWithText (text)) { r.error = "Could not write " + r.saved.getFullPathName(); return r; }
    r.cal.name = "UMIK " + serial + (r.angle == Angle::deg90 ? " (90 deg)" : " (0 deg)");
    r.ok = true;
    return r;
}

std::vector<CalibrationDownloader::Imported> CalibrationDownloader::importFromFolder (const juce::File& folder,
                                                                                    const juce::String& s,
                                                                                    const juce::File& dir)
{
    std::vector<Imported> out;
    if (s.isEmpty() || ! folder.isDirectory())
        return out;

    // Browsers may append " (1)" to repeated downloads, so match on prefix.
    for (auto& f : folder.findChildFiles (juce::File::findFiles, false, s + "*"))
    {
        const auto ext = f.getFileExtension().toLowerCase();
        if (ext != ".txt" && ext != ".cal")
            continue;
        auto r = importFile (f, s, dir);
        if (r.ok && r.serial == s)
            out.push_back (std::move (r));
    }
    return out;
}

juce::File CalibrationDownloader::findStored (const juce::File& dir, const juce::String& s, Angle a)
{
    return dir.getChildFile (storedFileName (s, a));
}

juce::StringArray CalibrationDownloader::storedSerials (const juce::File& dir)
{
    juce::StringArray out;
    for (auto& f : dir.findChildFiles (juce::File::findFiles, false, "*_*.txt"))
    {
        const auto n = f.getFileNameWithoutExtension();
        if (n.endsWith ("_0") || n.endsWith ("_90"))
            out.addIfNotAlreadyThere (n.upToLastOccurrenceOf ("_", false, false));
    }
    return out;
}

juce::String CalibrationDownloader::serialInDeviceName (const juce::String& name, const juce::File& dir, bool onlyStored)
{
    const auto compact = normaliseSerial (name);
    for (auto& s : storedSerials (dir))
        if (name.contains (s) || compact.contains (s))
            return s;
    if (onlyStored)
        return {};

    for (int i = 0; i + 7 <= name.length(); ++i)
    {
        if (i > 0 && juce::CharacterFunctions::isDigit (name[i - 1])) continue;
        const auto cand = name.substring (i, i + 7);
        if (cand[0] == '7' && cand.containsOnly ("0123456789") && ! juce::CharacterFunctions::isDigit (name[i + 7]))
            return cand;
    }
    return {};
}

bool CalibrationDownloader::looksLikeUncalibratedMeasurementMic (const juce::String& n)
{
    return n.containsIgnoreCase ("ecm8000") || n.containsIgnoreCase ("ucm8000") || n.containsIgnoreCase ("ecm-8000");
}

MicCalibration CalibrationDownloader::genericUncalibrated()
{
    MicCalibration c;
    c.name = "Generic ECM8000 / UCM8000 (uncalibrated, flat)";
    c.freqs = { 20.0, 20000.0 };
    c.gainDb = { 0.0, 0.0 };
    c.phaseDeg = { 0.0, 0.0 };
    return c;
}

juce::String CalibrationDownloader::genericAccuracyNote()
{
    return "No per-unit file exists for the ECM8000 and clones, and no published generic curve was found, so the "
           "response is assumed flat. Accuracy is unknown: trust relative changes and the low/mid range; absolute "
           "SPL and the top octaves are not guaranteed. A calibrated mic (UMIK) is better.";
}

juce::File CalibrationDownloader::writeGenericFile (const juce::File& dir)
{
    dir.createDirectory();
    auto f = dir.getChildFile ("generic_ecm8000_flat.txt");
    f.replaceWithText ("* Generic ECM8000 / UCM8000 - UNCALIBRATED, flat assumed\n20 0\n20000 0\n");
    return f;
}
} // namespace fa
