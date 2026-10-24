#pragma once

#include <juce_core/juce_core.h>
#include <cstdint>
#include <vector>

namespace fa::hid
{
/** One HID interface found on the system. */
struct DeviceInfo
{
    juce::String path;              // OS device path, pass to HidDevice::open()
    uint16_t     vendorId  = 0;
    uint16_t     productId = 0;
    uint16_t     version   = 0;
    juce::String manufacturer;
    juce::String product;
    juce::String serial;
    int          usagePage = 0;
    int          usage     = 0;
    int          inputReportLength  = 0;   // including the report-id byte
    int          outputReportLength = 0;   // including the report-id byte
};

/** Lists HID interfaces. vendorId/productId 0 = match any. Never throws; returns an empty list
    on platforms without HID support. */
std::vector<DeviceInfo> enumerate (uint16_t vendorId = 0, uint16_t productId = 0);

/** Thin, blocking wrapper over a Windows HID interface (SetupAPI + hid.dll). On other platforms
    every call fails with a clear error. Not thread-safe; callers serialise access. */
class HidDevice
{
public:
    HidDevice() = default;
    ~HidDevice();

    HidDevice (const HidDevice&) = delete;
    HidDevice& operator= (const HidDevice&) = delete;

    bool open (const juce::String& path, juce::String& error);
    void close();
    bool isOpen() const noexcept;

    int getInputReportLength() const noexcept  { return inputLen; }
    int getOutputReportLength() const noexcept { return outputLen; }

    /** Sends one output report: [reportId, data..., pad...] padded to the interface's output report
        length (or to 1 + minPayload when the length is unknown). */
    bool write (uint8_t reportId, const uint8_t* data, size_t size, uint8_t padByte,
                int timeoutMs, juce::String& error);

    /** Reads one input report. On success `out` holds the report WITHOUT its report-id byte.
        Returns false with an empty error on timeout, false with an error message on failure. */
    bool read (std::vector<uint8_t>& out, int timeoutMs, juce::String& error);

private:
    void* handle = nullptr;     // HANDLE
    void* readEvent = nullptr;  // HANDLE
    void* writeEvent = nullptr; // HANDLE
    int inputLen = 0, outputLen = 0;
};
} // namespace fa::hid
