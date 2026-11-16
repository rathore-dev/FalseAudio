#pragma once

#include <juce_core/juce_core.h>
#include <functional>
#include <memory>
#include <vector>

namespace fa
{
/** DSP vendor software: official download page + "is it installed?" detection.
    Data comes from data/dsp-software/vendors.json (fields officialDownloadPage, install{}).
    The app only ever OPENS the official page in the browser; it never downloads or runs installers. */
struct DspVendorSoftware
{
    juce::String id, vendor, software;
    juce::String officialDownloadPage;           // empty = none verified
    juce::StringArray installPaths;              // may contain %ENV% variables
    juce::StringArray exeNames;
    juce::StringArray uninstallNames;            // substrings of the Uninstall DisplayName
};

/** System access, injectable so tests need no real filesystem/registry. */
class DspSoftwareProbe
{
public:
    virtual ~DspSoftwareProbe() = default;
    virtual juce::String expandEnv (const juce::String& s) = 0;
    virtual bool fileExists (const juce::String& path) = 0;
    virtual bool dirExists (const juce::String& path) = 0;
    /** DisplayName values under HKLM/HKCU ...\Uninstall (and WOW6432Node). */
    virtual juce::StringArray uninstallDisplayNames() = 0;
    /** Real Windows implementation. */
    static std::unique_ptr<DspSoftwareProbe> createSystem();
};

struct DspInstallStatus
{
    bool installed = false;
    juce::String how;        // "exe", "folder", "registry"
    juce::String where;      // path or registry DisplayName
};

enum class DspSoftwareAction { alreadyInstalled, openedDownloadPage, noOfficialPage, openFailed };

class DspSoftware
{
public:
    /** Parses vendors.json text. Returns an empty vector (and fills error) on invalid input. */
    static std::vector<DspVendorSoftware> parseVendors (const juce::String& json, juce::String* error = nullptr);
    /** Looks for data/dsp-software/vendors.json next to the exe or in parent folders. */
    static juce::File findVendorsFile();
    static std::vector<DspVendorSoftware> loadVendors();
    static const DspVendorSoftware* findVendor (const std::vector<DspVendorSoftware>&, const juce::String& id);

    /** Known install folders / exe names first, then the Uninstall registry keys. */
    static DspInstallStatus detect (const DspVendorSoftware&, DspSoftwareProbe&);
    static DspInstallStatus detect (const DspVendorSoftware& v) { return detect (v, *DspSoftwareProbe::createSystem()); }

    /** If installed: alreadyInstalled. Else opens the official page via `opener` (default: default browser).
        The user must download and run the installer themselves (Windows UAC). */
    static DspSoftwareAction ensureInstalledOrOpenPage (const DspVendorSoftware&, DspSoftwareProbe&,
                                                         const std::function<bool (const juce::String&)>& opener = {});
    static DspSoftwareAction ensureInstalledOrOpenPage (const DspVendorSoftware& v);
};
} // namespace fa
