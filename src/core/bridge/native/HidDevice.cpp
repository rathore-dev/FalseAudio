#include "HidDevice.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <setupapi.h>
 #include <hidsdi.h>
 #include <hidpi.h>
 #pragma comment (lib, "setupapi.lib")
 #pragma comment (lib, "hid.lib")
#endif

namespace fa::hid
{
#if JUCE_WINDOWS
namespace
{
juce::String lastErrorText (const char* what)
{
    const auto code = GetLastError();
    wchar_t buf[512] = {};
    FormatMessageW (FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code,
                    0, buf, (DWORD) std::size (buf), nullptr);
    return juce::String (what) + " failed (" + juce::String ((int) code) + "): " + juce::String (buf).trim();
}

juce::String hidString (HANDLE h, BOOLEAN (__stdcall *fn) (HANDLE, PVOID, ULONG))
{
    wchar_t buf[256] = {};
    if (fn (h, buf, sizeof (buf) - sizeof (wchar_t)))
        return juce::String (buf).trim();
    return {};
}

void fillInfo (HANDLE h, DeviceInfo& info)
{
    HIDD_ATTRIBUTES attr {};
    attr.Size = sizeof (attr);
    if (HidD_GetAttributes (h, &attr))
    {
        info.vendorId  = attr.VendorID;
        info.productId = attr.ProductID;
        info.version   = attr.VersionNumber;
    }
    info.manufacturer = hidString (h, HidD_GetManufacturerString);
    info.product      = hidString (h, HidD_GetProductString);
    info.serial       = hidString (h, HidD_GetSerialNumberString);

    PHIDP_PREPARSED_DATA pp = nullptr;
    if (HidD_GetPreparsedData (h, &pp))
    {
        HIDP_CAPS caps {};
        if (HidP_GetCaps (pp, &caps) == HIDP_STATUS_SUCCESS)
        {
            info.usagePage          = caps.UsagePage;
            info.usage              = caps.Usage;
            info.inputReportLength  = caps.InputReportByteLength;
            info.outputReportLength = caps.OutputReportByteLength;
        }
        HidD_FreePreparsedData (pp);
    }
}

/** Waits for an overlapped operation; cancels it on timeout. Returns transferred bytes or -1. */
int finishOverlapped (HANDLE h, OVERLAPPED& ov, int timeoutMs, bool& timedOut)
{
    timedOut = false;
    const auto wait = WaitForSingleObject (ov.hEvent, (DWORD) juce::jmax (0, timeoutMs));
    DWORD done = 0;
    if (wait == WAIT_OBJECT_0)
        return GetOverlappedResult (h, &ov, &done, FALSE) ? (int) done : -1;

    CancelIoEx (h, &ov);
    GetOverlappedResult (h, &ov, &done, TRUE); // wait for the cancel to complete
    timedOut = (wait == WAIT_TIMEOUT);
    return -1;
}
} // namespace

std::vector<DeviceInfo> enumerate (uint16_t vendorId, uint16_t productId)
{
    std::vector<DeviceInfo> result;
    GUID hidGuid;
    HidD_GetHidGuid (&hidGuid);

    HDEVINFO set = SetupDiGetClassDevsW (&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE)
        return result;

    for (DWORD index = 0;; ++index)
    {
        SP_DEVICE_INTERFACE_DATA ifData {};
        ifData.cbSize = sizeof (ifData);
        if (! SetupDiEnumDeviceInterfaces (set, nullptr, &hidGuid, index, &ifData))
            break;

        DWORD required = 0;
        SetupDiGetDeviceInterfaceDetailW (set, &ifData, nullptr, 0, &required, nullptr);
        if (required == 0)
            continue;

        juce::HeapBlock<char> detailMem (required, true);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*> (detailMem.get());
        detail->cbSize = sizeof (SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (! SetupDiGetDeviceInterfaceDetailW (set, &ifData, detail, required, nullptr, nullptr))
            continue;

        // Zero access rights: lets us query attributes even for devices opened exclusively (keyboards).
        HANDLE h = CreateFileW (detail->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            continue;

        DeviceInfo info;
        info.path = juce::String (detail->DevicePath);
        HIDD_ATTRIBUTES attr {};
        attr.Size = sizeof (attr);
        const bool ok = HidD_GetAttributes (h, &attr) != FALSE;
        if (ok && (vendorId == 0 || attr.VendorID == vendorId) && (productId == 0 || attr.ProductID == productId))
        {
            fillInfo (h, info);
            result.push_back (info);
        }
        CloseHandle (h);
    }

    SetupDiDestroyDeviceInfoList (set);
    return result;
}

HidDevice::~HidDevice() { close(); }

bool HidDevice::isOpen() const noexcept { return handle != nullptr; }

bool HidDevice::open (const juce::String& path, juce::String& error)
{
    close();
    HANDLE h = CreateFileW (path.toWideCharPointer(), GENERIC_READ | GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                            FILE_FLAG_OVERLAPPED, nullptr);
    if (h == INVALID_HANDLE_VALUE)
    {
        error = lastErrorText ("Opening the USB device");
        return false;
    }

    DeviceInfo info;
    fillInfo (h, info);
    inputLen  = info.inputReportLength;
    outputLen = info.outputReportLength;

    handle     = h;
    readEvent  = CreateEventW (nullptr, TRUE, FALSE, nullptr);
    writeEvent = CreateEventW (nullptr, TRUE, FALSE, nullptr);
    if (readEvent == nullptr || writeEvent == nullptr)
    {
        error = lastErrorText ("CreateEvent");
        close();
        return false;
    }
    return true;
}

void HidDevice::close()
{
    if (handle != nullptr)
    {
        CancelIoEx ((HANDLE) handle, nullptr);
        CloseHandle ((HANDLE) handle);
    }
    if (readEvent != nullptr)  CloseHandle ((HANDLE) readEvent);
    if (writeEvent != nullptr) CloseHandle ((HANDLE) writeEvent);
    handle = readEvent = writeEvent = nullptr;
    inputLen = outputLen = 0;
}

bool HidDevice::write (uint8_t reportId, const uint8_t* data, size_t size, uint8_t padByte,
                       int timeoutMs, juce::String& error)
{
    if (handle == nullptr)
    {
        error = "USB device is not open";
        return false;
    }

    const size_t len = (size_t) juce::jmax ((int) size + 1, outputLen);
    std::vector<uint8_t> buf (len, padByte);
    buf[0] = reportId;
    std::memcpy (buf.data() + 1, data, size);

    OVERLAPPED ov {};
    ov.hEvent = (HANDLE) writeEvent;
    ResetEvent (ov.hEvent);
    if (! WriteFile ((HANDLE) handle, buf.data(), (DWORD) buf.size(), nullptr, &ov)
        && GetLastError() != ERROR_IO_PENDING)
    {
        error = lastErrorText ("USB write");
        return false;
    }

    bool timedOut = false;
    if (finishOverlapped ((HANDLE) handle, ov, timeoutMs, timedOut) < 0)
    {
        error = timedOut ? juce::String ("USB write timed out") : lastErrorText ("USB write");
        return false;
    }
    return true;
}

bool HidDevice::read (std::vector<uint8_t>& out, int timeoutMs, juce::String& error)
{
    out.clear();
    error.clear();
    if (handle == nullptr)
    {
        error = "USB device is not open";
        return false;
    }

    std::vector<uint8_t> buf ((size_t) juce::jmax (65, inputLen), 0);
    OVERLAPPED ov {};
    ov.hEvent = (HANDLE) readEvent;
    ResetEvent (ov.hEvent);
    if (! ReadFile ((HANDLE) handle, buf.data(), (DWORD) buf.size(), nullptr, &ov)
        && GetLastError() != ERROR_IO_PENDING)
    {
        error = lastErrorText ("USB read");
        return false;
    }

    bool timedOut = false;
    const int n = finishOverlapped ((HANDLE) handle, ov, timeoutMs, timedOut);
    if (n < 0)
    {
        if (! timedOut)
            error = lastErrorText ("USB read");
        return false;
    }
    if (n <= 1)
        return true; // empty report

    // Windows always prefixes the report id byte.
    out.assign (buf.begin() + 1, buf.begin() + n);
    return true;
}

#else // ---------------------------------------------------------------- non-Windows stub

std::vector<DeviceInfo> enumerate (uint16_t, uint16_t) { return {}; }

HidDevice::~HidDevice() = default;
bool HidDevice::isOpen() const noexcept { return false; }

bool HidDevice::open (const juce::String&, juce::String& error)
{
    error = "USB HID access is only implemented on Windows";
    return false;
}

void HidDevice::close() {}

bool HidDevice::write (uint8_t, const uint8_t*, size_t, uint8_t, int, juce::String& error)
{
    error = "USB HID access is only implemented on Windows";
    return false;
}

bool HidDevice::read (std::vector<uint8_t>& out, int, juce::String& error)
{
    out.clear();
    error = "USB HID access is only implemented on Windows";
    return false;
}
#endif
} // namespace fa::hid
