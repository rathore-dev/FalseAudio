#include "DspSoftware.h"

#if JUCE_WINDOWS
 #define WIN32_LEAN_AND_MEAN
 #define NOMINMAX
 #include <windows.h>
 #pragma comment(lib, "advapi32.lib")
#endif

namespace fa
{
namespace
{
#if JUCE_WINDOWS
    class SystemProbe : public DspSoftwareProbe
    {
    public:
        juce::String expandEnv (const juce::String& s) override
        {
            wchar_t buf[2048];
            const auto n = ExpandEnvironmentStringsW (s.toWideCharPointer(), buf, 2048);
            return n > 0 && n <= 2048 ? juce::String (buf) : s;
        }
        bool fileExists (const juce::String& p) override { return juce::File (p).existsAsFile(); }
        bool dirExists (const juce::String& p) override  { return juce::File (p).isDirectory(); }

        juce::StringArray uninstallDisplayNames() override
        {
            juce::StringArray out;
            const wchar_t* subs[] = { L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
                                      L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall" };
            for (HKEY root : { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER })
                for (auto* sub : subs)
                {
                    HKEY key = nullptr;
                    if (RegOpenKeyExW (root, sub, 0, KEY_READ, &key) != ERROR_SUCCESS)
                        continue;
                    for (DWORD i = 0;; ++i)
                    {
                        wchar_t name[256];
                        DWORD len = 256;
                        if (RegEnumKeyExW (key, i, name, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
                            break;
                        HKEY sk = nullptr;
                        if (RegOpenKeyExW (key, name, 0, KEY_READ, &sk) != ERROR_SUCCESS)
                            continue;
                        wchar_t val[512];
                        DWORD vl = sizeof (val) - sizeof (wchar_t), type = 0;
                        if (RegQueryValueExW (sk, L"DisplayName", nullptr, &type, (LPBYTE) val, &vl) == ERROR_SUCCESS
                            && type == REG_SZ)
                        {
                            val[vl / sizeof (wchar_t)] = 0;
                            out.add (juce::String (val));
                        }
                        RegCloseKey (sk);
                    }
                    RegCloseKey (key);
                }
            return out;
        }
    };
#endif
}

std::unique_ptr<DspSoftwareProbe> DspSoftwareProbe::createSystem()
{
#if JUCE_WINDOWS
    return std::make_unique<SystemProbe>();
#else
    return nullptr;
#endif
}

//==============================================================================
static juce::StringArray toStrings (const juce::var& v)
{
    juce::StringArray s;
    if (auto* a = v.getArray())
        for (auto& x : *a) s.add (x.toString());
    return s;
}

std::vector<DspVendorSoftware> DspSoftware::parseVendors (const juce::String& json, juce::String* error)
{
    std::vector<DspVendorSoftware> out;
    auto fail = [&] (const juce::String& m) { if (error) *error = m; out.clear(); return out; };

    juce::var root;
    if (juce::JSON::parse (json, root).failed())
        return fail ("Invalid JSON");
    auto* arr = root.getProperty ("vendors", {}).getArray();
    if (arr == nullptr)
        return fail ("Missing \"vendors\" array");

    for (auto& v : *arr)
    {
        DspVendorSoftware d;
        d.id       = v.getProperty ("id", {}).toString();
        d.vendor   = v.getProperty ("vendor", {}).toString();
        d.software = v.getProperty ("software", {}).toString();
        if (d.id.isEmpty() || d.vendor.isEmpty())
            return fail ("Vendor entry without id/vendor");
        d.officialDownloadPage = v.getProperty ("officialDownloadPage", {}).toString();
        auto inst = v.getProperty ("install", {});
        d.installPaths    = toStrings (inst.getProperty ("paths", {}));
        d.exeNames        = toStrings (inst.getProperty ("exeNames", {}));
        d.uninstallNames  = toStrings (inst.getProperty ("uninstallNames", {}));
        out.push_back (std::move (d));
    }
    return out;
}

juce::File DspSoftware::findVendorsFile()
{
    auto dir = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
    for (int i = 0; i < 6; ++i, dir = dir.getParentDirectory())
    {
        auto f = dir.getChildFile ("data/dsp-software/vendors.json");
        if (f.existsAsFile()) return f;
    }
    return {};
}

std::vector<DspVendorSoftware> DspSoftware::loadVendors()
{
    const auto f = findVendorsFile();
    return f.existsAsFile() ? parseVendors (f.loadFileAsString()) : std::vector<DspVendorSoftware>{};
}

const DspVendorSoftware* DspSoftware::findVendor (const std::vector<DspVendorSoftware>& v, const juce::String& id)
{
    for (auto& x : v) if (x.id == id) return &x;
    return nullptr;
}

DspInstallStatus DspSoftware::detect (const DspVendorSoftware& v, DspSoftwareProbe& p)
{
    DspInstallStatus st;
    for (auto& raw : v.installPaths)
    {
        const auto dir = p.expandEnv (raw).replaceCharacter ('\\', '/').trimCharactersAtEnd ("/");
        if (! p.dirExists (dir))
            continue;
        for (auto& exe : v.exeNames)
            if (p.fileExists (dir + "/" + exe))
                return { true, "exe", dir + "/" + exe };
        if (v.exeNames.isEmpty())
            return { true, "folder", dir };
    }
    if (! v.uninstallNames.isEmpty())
        for (auto& dn : p.uninstallDisplayNames())
            for (auto& needle : v.uninstallNames)
                if (needle.isNotEmpty() && dn.containsIgnoreCase (needle))
                    return { true, "registry", dn };
    return st;
}

DspSoftwareAction DspSoftware::ensureInstalledOrOpenPage (const DspVendorSoftware& v, DspSoftwareProbe& p,
                                                           const std::function<bool (const juce::String&)>& opener)
{
    if (detect (v, p).installed)
        return DspSoftwareAction::alreadyInstalled;
    if (v.officialDownloadPage.isEmpty())
        return DspSoftwareAction::noOfficialPage;
    const bool ok = opener ? opener (v.officialDownloadPage) : juce::URL (v.officialDownloadPage).launchInDefaultBrowser();
    return ok ? DspSoftwareAction::openedDownloadPage : DspSoftwareAction::openFailed;
}

DspSoftwareAction DspSoftware::ensureInstalledOrOpenPage (const DspVendorSoftware& v)
{
    auto p = DspSoftwareProbe::createSystem();
    return p ? ensureInstalledOrOpenPage (v, *p) : DspSoftwareAction::noOfficialPage;
}
} // namespace fa
