#pragma once

#include "Calibration.h"

namespace fa
{
/** Filing miniDSP UMIK-1 / UMIK-2 calibration files.

    VERIFIED (miniDSP support articles 47000681537 and 47001186138): per-serial files come from a form
    ("Unique Calibration File Download") on the UMIK-1 / UMIK-2 product pages. miniDSP documents no direct
    file URL and the site refuses scripted requests (HTTP 403), so this class does NOT guess an endpoint.
    Flow:
      1. open the official product page (UI copies the serial to the clipboard),
      2. the user submits the form; the browser saves "<serial>.txt" (0 deg) and "<serial>_90deg.txt",
      3. importFromFolder() picks them up from Downloads, parses them (Sens Factor etc.) and files them under
         %APPDATA%/FalseAudio/calibration/<serial>_0.txt and <serial>_90.txt.
    importFile() covers a manual file picker. No network access happens here. */
class CalibrationDownloader
{
public:
    enum class Model { umik1, umik2 };
    enum class Angle { deg0, deg90 };

    /** Letters/digits only, upper-cased ("700-1870" -> "7001870"). */
    static juce::String normaliseSerial (const juce::String& typed);
    /** UMIK-1: 7 digits starting with 7 (printed 7xx-xxxx). UMIK-2 format is undocumented: 5-12 alphanumerics. */
    static bool isValidSerial (const juce::String& normalised);
    static Model modelForSerial (const juce::String& normalised);

    /** Official miniDSP product page holding the calibration form. */
    static juce::String productPageUrl (Model model);

    /** File names miniDSP's form delivers for this serial. */
    static juce::String miniDspFileName (const juce::String& normalisedSerial, Angle angle);
    /** Name used in our calibration folder. */
    static juce::String storedFileName (const juce::String& normalisedSerial, Angle angle);
    static juce::File   defaultCalibrationDir();
    static juce::File   defaultDownloadsDir();

    /** 90 deg when the file name says so ("_90", "90deg", "90-deg"). */
    static Angle angleFromFileName (const juce::String& fileName);

    struct Imported
    {
        bool ok = false;
        juce::String error;
        juce::String serial;
        Angle angle = Angle::deg0;
        juce::File saved;
        MicCalibration cal;
    };

    /** Parses `source`, determines serial (header SERNO > file name > hint) and angle, saves it into `dir`. */
    static Imported importFile (const juce::File& source, const juce::String& serialHint, const juce::File& dir);

    /** Looks for "<serial>*.txt" in `folder` and imports every match (0 and 90 deg). */
    static std::vector<Imported> importFromFolder (const juce::File& folder, const juce::String& normalisedSerial,
                                                   const juce::File& dir);

    /** Stored file for serial/angle (may not exist). */
    static juce::File findStored (const juce::File& dir, const juce::String& normalisedSerial, Angle angle);
    static juce::StringArray storedSerials (const juce::File& dir);
    /** If an input device name contains a stored serial, returns it (else empty). With onlyStored=false it also
        returns any standalone 7-digit number starting with 7. */
    static juce::String serialInDeviceName (const juce::String& deviceName, const juce::File& dir, bool onlyStored = true);

    //==========================================================================
    // Behringer ECM8000 and clones (also "UCM8000"): no per-unit file exists.
    static bool looksLikeUncalibratedMeasurementMic (const juce::String& deviceName);
    /** Flat curve (no published generic curve was found), flagged as uncalibrated. */
    static MicCalibration genericUncalibrated();
    static juce::String   genericAccuracyNote();
    /** Writes the flat file into `dir` so it loads like any other calibration; returns it. */
    static juce::File     writeGenericFile (const juce::File& dir);
};
} // namespace fa
