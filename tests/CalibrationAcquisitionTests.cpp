#include "TestRegistry.h"
#include "../src/core/measure/CalibrationDownloader.h"
#include "../src/core/DspSoftware.h"

using fa::CalibrationDownloader;
using fa::test::check;

namespace
{
const char* kSampleCal =
    "\"Sens Factor =-1.378dB, AGain =18dB, SERNO: 7001870\"\n"
    "20.000 -0.5\n100.000 0.1\n1000.000 0.0\n10000.000 1.2\n20000.000 3.0\n";

struct FakeProbe : fa::DspSoftwareProbe
{
    juce::StringArray files, dirs, names;
    juce::String expandEnv (const juce::String& s) override
    {
        return s.replace ("%ProgramFiles(x86)%", "C:/Program Files (x86)").replace ("%ProgramFiles%", "C:/Program Files");
    }
    bool fileExists (const juce::String& p) override { return files.contains (p); }
    bool dirExists (const juce::String& p) override  { return dirs.contains (p); }
    juce::StringArray uninstallDisplayNames() override { return names; }
};
}

FA_TEST (calibrationParseSensFactorAndAngle)
{
    auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("fa_calacq_test");
    tmp.deleteRecursively();
    tmp.createDirectory();
    auto src = tmp.getChildFile ("downloads");
    src.createDirectory();
    src.getChildFile ("7001870.txt").replaceWithText (kSampleCal);
    src.getChildFile ("7001870_90deg.txt").replaceWithText (juce::String (kSampleCal).replace ("-1.378", "-1.512"));
    auto store = tmp.getChildFile ("store");

    const auto c = fa::MicCalibration::parse (kSampleCal);
    check (c.isValid() && c.freqs.size() == 5, "cal parses 5 points");
    check (std::abs (c.sensitivityDb + 1.378) < 1e-9, "Sens Factor parsed", juce::String (c.sensitivityDb));
    check (c.serial == "7001870", "serial from SERNO header", c.serial);

    auto res = CalibrationDownloader::importFromFolder (src, "7001870", store);
    check (res.size() == 2, "both 0 and 90 deg imported", juce::String ((int) res.size()));
    check (CalibrationDownloader::findStored (store, "7001870", CalibrationDownloader::Angle::deg0).existsAsFile(), "0 deg stored");
    const auto f90 = CalibrationDownloader::findStored (store, "7001870", CalibrationDownloader::Angle::deg90);
    check (f90.existsAsFile(), "90 deg stored");
    check (std::abs (fa::MicCalibration::load (f90).sensitivityDb + 1.512) < 1e-9, "90 deg Sens Factor kept");
    check (CalibrationDownloader::angleFromFileName ("7001870_90deg.txt") == CalibrationDownloader::Angle::deg90
           && CalibrationDownloader::angleFromFileName ("7001870.txt") == CalibrationDownloader::Angle::deg0, "angle from filename");

    check (CalibrationDownloader::serialInDeviceName ("Microphone (UMIK-1 7001870)", store) == "7001870", "device name serial auto-detect (stored)");
    check (CalibrationDownloader::serialInDeviceName ("Realtek Mic", store).isEmpty(), "no false detection");
    check (CalibrationDownloader::serialInDeviceName ("UMIK-1 7001871", store, false) == "7001871", "unstored 7xxxxxx detected");

    const auto g = CalibrationDownloader::genericUncalibrated();
    check (g.isValid() && std::abs (g.deviationDb (1000.0)) < 1e-9, "generic curve is flat");
    check (CalibrationDownloader::looksLikeUncalibratedMeasurementMic ("UCM8000 USB"), "UCM8000 recognised");
    tmp.deleteRecursively();
}

FA_TEST (calibrationUrlAndSerial)
{
    using D = CalibrationDownloader;
    check (D::normaliseSerial ("700-1870 ") == "7001870", "serial normalised");
    check (D::isValidSerial ("7001870") && ! D::isValidSerial ("123"), "serial validity");
    check (D::modelForSerial ("7001870") == D::Model::umik1, "7xxxxxx is UMIK-1");
    check (D::productPageUrl (D::Model::umik1) == "https://www.minidsp.com/products/acoustic-measurement/umik-1", "UMIK-1 page url");
    check (D::productPageUrl (D::Model::umik2) == "https://www.minidsp.com/products/acoustic-measurement/umik-2", "UMIK-2 page url");
    check (D::miniDspFileName ("7001870", D::Angle::deg90) == "7001870_90deg.txt", "90 deg file name");
    check (D::storedFileName ("7001870", D::Angle::deg0) == "7001870_0.txt", "stored file name");
}

FA_TEST (dspVendorsJsonSchema)
{
    const char* candidates[] = { "data/dsp-software/vendors.json", "../data/dsp-software/vendors.json",
                                 "../../data/dsp-software/vendors.json", "../../../data/dsp-software/vendors.json" };
    juce::File f = fa::DspSoftware::findVendorsFile();
    for (auto* c : candidates)
        if (! f.existsAsFile()) f = juce::File::getCurrentWorkingDirectory().getChildFile (c);
    check (f.existsAsFile(), "vendors.json found");
    juce::String err;
    auto v = fa::DspSoftware::parseVendors (f.loadFileAsString(), &err);
    check (! v.empty(), "vendors.json parses", err);

    bool ok = true, https = true, ids = true;
    juce::StringArray seen;
    for (auto& x : v)
    {
        ids = ids && ! seen.contains (x.id);
        seen.add (x.id);
        if (x.officialDownloadPage.isNotEmpty() && ! x.officialDownloadPage.startsWith ("https://"))
            https = false;
        const auto raw = juce::JSON::parse (f.loadFileAsString()).getProperty ("vendors", {});
        (void) raw;
    }
    auto root = juce::JSON::parse (f.loadFileAsString());
    for (auto& e : *root.getProperty ("vendors", {}).getArray())
        ok = ok && e.hasProperty ("officialDownloadPage") && e.getProperty ("install", {}).hasProperty ("paths")
                && e.getProperty ("install", {}).hasProperty ("uninstallNames");
    check (ok, "every vendor has officialDownloadPage + install{}");
    check (https && ids, "download pages are https, ids unique");
    check (fa::DspSoftware::findVendor (v, "helix-dsp-pc-tool") != nullptr, "helix vendor present");
    check (fa::DspSoftware::parseVendors ("{}").empty() && fa::DspSoftware::parseVendors ("garbage").empty(), "bad json rejected");
}

FA_TEST (dspInstallDetectionFake)
{
    fa::DspVendorSoftware v;
    v.id = "x"; v.vendor = "X"; v.officialDownloadPage = "https://example.com/dl";
    v.installPaths.add ("%ProgramFiles(x86)%/Acme/Tool");
    v.exeNames.add ("tool.exe");
    v.uninstallNames.add ("Acme Tool");

    FakeProbe p;
    juce::String opened;
    auto opener = [&] (const juce::String& u) { opened = u; return true; };

    check (fa::DspSoftware::ensureInstalledOrOpenPage (v, p, opener) == fa::DspSoftwareAction::openedDownloadPage
           && opened == "https://example.com/dl", "missing -> opens official page");

    p.dirs.add ("C:/Program Files (x86)/Acme/Tool");
    check (! fa::DspSoftware::detect (v, p).installed, "folder without exe is not installed");
    p.files.add ("C:/Program Files (x86)/Acme/Tool/tool.exe");
    auto st = fa::DspSoftware::detect (v, p);
    check (st.installed && st.how == "exe", "exe found");

    FakeProbe q;
    q.names.add ("Acme Tool 2.1");
    st = fa::DspSoftware::detect (v, q);
    opened.clear();
    check (st.installed && st.how == "registry", "registry DisplayName match");
    check (fa::DspSoftware::ensureInstalledOrOpenPage (v, q, opener) == fa::DspSoftwareAction::alreadyInstalled && opened.isEmpty(),
           "installed -> no browser");

    v.officialDownloadPage = {};
    FakeProbe r;
    check (fa::DspSoftware::ensureInstalledOrOpenPage (v, r, opener) == fa::DspSoftwareAction::noOfficialPage, "no page -> reported");
}
