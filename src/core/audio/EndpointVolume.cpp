#include "EndpointVolume.h"

#if JUCE_WINDOWS
 #include <windows.h>
 #include <mmdeviceapi.h>
 #include <endpointvolume.h>
 #include <functiondiscoverykeys_devpkey.h>
 #pragma comment (lib, "ole32.lib")
 #pragma comment (lib, "uuid.lib")
 #pragma comment (lib, "propsys.lib")
#endif

namespace fa
{
#if JUCE_WINDOWS
namespace
{
template <class T> struct Rel { T* p = nullptr; ~Rel() { if (p) p->Release(); } };

// Finds the endpoint and hands its IAudioEndpointVolume to fn.
template <class Fn>
bool withEndpoint (const juce::String& match, juce::String& name, juce::String& err, Fn&& fn)
{
    const HRESULT hrInit = CoInitializeEx (nullptr, COINIT_MULTITHREADED);
    bool ok = false;
    {
        Rel<IMMDeviceEnumerator> en;
        Rel<IMMDeviceCollection> col;
        if (FAILED (CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof (IMMDeviceEnumerator), (void**) &en.p)))
            err = "MMDeviceEnumerator unavailable";
        else if (FAILED (en.p->EnumAudioEndpoints (eCapture, DEVICE_STATE_ACTIVE, &col.p)))
            err = "Could not enumerate capture endpoints";
        else
        {
            UINT n = 0;
            col.p->GetCount (&n);
            err = "No capture endpoint matching '" + match + "'";
            for (UINT i = 0; i < n && ! ok; ++i)
            {
                Rel<IMMDevice> dev;
                if (FAILED (col.p->Item (i, &dev.p))) continue;
                Rel<IPropertyStore> ps;
                if (FAILED (dev.p->OpenPropertyStore (STGM_READ, &ps.p))) continue;
                PROPVARIANT pv; PropVariantInit (&pv);
                juce::String fn2;
                if (SUCCEEDED (ps.p->GetValue (PKEY_Device_FriendlyName, &pv)) && pv.vt == VT_LPWSTR)
                    fn2 = juce::String (pv.pwszVal);
                PropVariantClear (&pv);
                if (! fn2.containsIgnoreCase (match)) continue;
                Rel<IAudioEndpointVolume> vol;
                if (FAILED (dev.p->Activate (__uuidof (IAudioEndpointVolume), CLSCTX_ALL, nullptr, (void**) &vol.p)))
                { err = "Could not activate IAudioEndpointVolume for " + fn2; continue; }
                name = fn2;
                err.clear();
                ok = fn (vol.p, err);
            }
        }
    }
    if (SUCCEEDED (hrInit)) CoUninitialize();
    return ok;
}
}

EndpointVolume readCaptureEndpointVolume (const juce::String& match)
{
    EndpointVolume r;
    juce::String err;
    const bool ok = withEndpoint (match, r.deviceName, err, [&] (IAudioEndpointVolume* v, juce::String& e)
    {
        r.found = true;
        float db = 0, mn = 0, mx = 0, st = 0, sc = 0;
        BOOL mute = FALSE;
        if (FAILED (v->GetMasterVolumeLevel (&db)) || FAILED (v->GetMasterVolumeLevelScalar (&sc)))
        { e = "Could not read the endpoint volume"; return false; }
        v->GetVolumeRange (&mn, &mx, &st);
        v->GetMute (&mute);
        r.levelDb = db; r.scalar = sc; r.minDb = mn; r.maxDb = mx; r.stepDb = st; r.muted = mute != FALSE;
        r.valid = true;
        return true;
    });
    if (! ok) r.error = err;
    return r;
}

bool setCaptureEndpointVolumeScalar (const juce::String& match, double scalar, juce::String& error)
{
    juce::String name;
    return withEndpoint (match, name, error, [&] (IAudioEndpointVolume* v, juce::String& e)
    {
        if (FAILED (v->SetMasterVolumeLevelScalar ((float) juce::jlimit (0.0, 1.0, scalar), nullptr)))
        { e = "SetMasterVolumeLevelScalar failed"; return false; }
        return true;
    });
}

bool setCaptureEndpointVolumeDb (const juce::String& match, double targetDb, juce::String& error)
{
    juce::String name;
    return withEndpoint (match, name, error, [&] (IAudioEndpointVolume* v, juce::String& e)
    {
        float mn = 0, mx = 0, st = 0;
        v->GetVolumeRange (&mn, &mx, &st);
        if (FAILED (v->SetMasterVolumeLevel ((float) juce::jlimit ((double) mn, (double) mx, targetDb), nullptr)))
        { e = "SetMasterVolumeLevel failed"; return false; }
        return true;
    });
}
#else
EndpointVolume readCaptureEndpointVolume (const juce::String&) { EndpointVolume r; r.error = "Windows only"; return r; }
bool setCaptureEndpointVolumeScalar (const juce::String&, double, juce::String& e) { e = "Windows only"; return false; }
bool setCaptureEndpointVolumeDb (const juce::String&, double, juce::String& e) { e = "Windows only"; return false; }
#endif

EndpointVolume endpointVolumeForDevice (const juce::String& deviceName, const std::function<EndpointVolume (const juce::String&)>& reader)
{
    if (deviceName.trim().isEmpty())
    {
        EndpointVolume r;
        r.error = "Input device unknown - Windows mic level not applied";
        return r;
    }
    return reader ? reader (deviceName) : readCaptureEndpointVolume (deviceName);
}
} // namespace fa
