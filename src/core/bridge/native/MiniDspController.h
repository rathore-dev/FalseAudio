// Native miniDSP controller over USB HID.
//
// The wire protocol (framing, checksum, command ids, address and value encodings, per-device
// memory maps) is ported from minidsp-rs by Mathieu Rene and contributors:
//     https://github.com/mrene/minidsp-rs   (protocol/ crate, branch "dev")
// minidsp-rs is licensed under the Apache License, Version 2.0. You may obtain a copy of the
// License at http://www.apache.org/licenses/LICENSE-2.0. This file is a C++ re-implementation
// (modified work) of parts of it, distributed under the same terms for those parts.
#pragma once

#include "../DspController.h"
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>

namespace fa::minidsp
{
using Bytes = std::vector<uint8_t>;

//==============================================================================
// Device memory maps (minidsp-rs protocol/src/device/*.rs)

enum class AddrEncoding  { len2 = 2, len3 = 3 };
enum class FloatEncoding { float32LE, fixedPoint };

struct Dialect
{
    AddrEncoding  addr  = AddrEncoding::len3;
    FloatEncoding value = FloatEncoding::float32LE;
};

struct OutputSpec
{
    int enable;          // mute gate (1 = muted, 2 = on for 3-byte dialects)
    int gain;            // -1 = none
    int delay;           // -1 = none
    int invert;
    int peq[10];         // order as in minidsp-rs (index 0 = first PEQ slot)
    int numPeq;
    int xover[4];        // first address of each 4-biquad crossover group
    int numXover;
};

struct DeviceSpec
{
    const char* kind;            // minidsp-rs DeviceKind (lowercase)
    const char* productName;
    int hwId;
    int dspVersion;              // -1 = any
    double sampleRate;           // internal rate; 0 = not known in minidsp-rs
    Dialect dialect;
    const OutputSpec* outputs;
    int numOutputs;

    int crossoverSlots() const noexcept { return numOutputs > 0 ? outputs[0].numXover * 4 : 0; }
};

const std::vector<DeviceSpec>& allDevices();

/** Same matching order as minidsp-rs `probe_kind`. nullptr = unsupported/unknown. */
const DeviceSpec* probe (int hwId, int dspVersion);
const DeviceSpec* byKind (const juce::String& kind);

/** True if `a` is a documented parameter address of `dev` (any output's gate/gain/delay/invert/PEQ
    slot/crossover slot). Every write is checked against this. */
bool isDocumentedAddress (const DeviceSpec& dev, int addr, bool biquad);

//==============================================================================
// Pure protocol layer (minidsp-rs protocol/src/packet.rs, commands.rs, dialect.rs, fixed_point.rs)

constexpr uint16_t vendorId = 0x2752;                 // all current miniDSP units
constexpr uint16_t oldVendorId = 0x04d8, oldProductId = 0x003f; // early units (Microchip VID)

namespace eeprom
{
    constexpr uint16_t firmwareVersion = 0xFFA1;   // u8 dsp version
    constexpr uint16_t preset          = 0xFFD8;   // master status block (9 bytes)
    constexpr uint16_t serialShort     = 0xFFFE;   // u16, +900000
}

constexpr int readFloatsMax = 14;
constexpr int maxDelaySamples = 8000;               // minidsp-rs limit

/** Sum of all bytes (mod 256). */
uint8_t checksum (const uint8_t* data, size_t size);

/** [len = payload+1] payload [checksum over len+payload]. */
Bytes frame (const Bytes& payload);

/** Extracts the payload of a response report: bytes [1, len). False if malformed. */
bool unframe (const Bytes& report, Bytes& payload);

void writeAddr (Bytes& out, AddrEncoding enc, uint32_t addr, bool extraBit = true);

/** 4-byte encodings of the dialect value types. */
std::array<uint8_t, 4> encodeFloat32LE (float v);
std::array<uint8_t, 4> encodeInt16 (uint16_t v);   // u16 LE + 00 00
std::array<uint8_t, 4> encodeInt32BE (uint32_t v);
uint32_t fixedPointFromFloat (float v);            // 5.23 offset encoding, see fixed_point.rs
float    fixedPointToFloat (uint32_t v);
std::array<uint8_t, 4> encodeFixed (float v);      // big-endian

std::array<uint8_t, 4> encodeCoefficient (const Dialect&, double v);
std::array<uint8_t, 4> encodeGainDb (const Dialect&, double db);
std::array<uint8_t, 4> encodeMute (const Dialect&, bool muted);
std::array<uint8_t, 4> encodeInvert (const Dialect&, bool inverted);
std::array<uint8_t, 4> encodeDelay (const Dialect&, uint32_t samples);

/** Delay in device units (samples at the internal rate), rounded like minidsp-rs (microsecond
    resolution, then round). */
uint32_t delaySamples (double delayMs, double sampleRate);

/** FalseAudio biquad (a0 = 1, y = b·x - a1 y1 - a2 y2) -> miniDSP order [b0, b1, b2, -a1, -a2]. */
std::array<double, 5> toDeviceCoefficients (const BiquadCoeffs& b);

namespace cmd
{
    Bytes readHardwareId();                                   // 0x31
    Bytes readMemory (uint16_t addr, uint8_t size);           // 0x05
    Bytes readFloats (uint16_t addr, uint8_t count);          // 0x14
    Bytes write (const Dialect&, int addr, const std::array<uint8_t, 4>& value);         // 0x13
    Bytes writeBiquad (const Dialect&, int addr, const std::array<double, 5>& coeffs);   // 0x30
    Bytes writeBiquadRaw (const Dialect&, int addr, const Bytes& twentyBytes);          // 0x30
    Bytes writeBiquadBypass (const Dialect&, int addr, bool bypass);                    // 0x19
}

struct Response
{
    enum class Kind { ack, hardwareId, memory, floats, unknown } kind = Kind::unknown;
    uint8_t fwMajor = 0, fwMinor = 0, hwId = 0;
    uint16_t base = 0;
    Bytes data;      // memory bytes, or raw float words (4 bytes each, as received)
};

Response parseResponse (const Bytes& payload);

/** Whether `r` answers the command `payload` (minidsp-rs Commands::matches_response). */
bool responseMatches (const Bytes& commandPayload, const Response& r);

//==============================================================================
/** Byte pipe to a device. HidTransport is the real one; tests use a mock. */
class Transport
{
public:
    virtual ~Transport() = default;
    virtual bool sendFrame (const Bytes& framed, juce::String& error) = 0;
    /** Receives one report (without HID report id). False + empty error = timeout. */
    virtual bool receive (Bytes& report, int timeoutMs, juce::String& error) = 0;
};

std::unique_ptr<Transport> openHidTransport (const juce::String& path, juce::String& error);

struct DeviceIdentity
{
    int hwId = -1, dspVersion = -1, fwMajor = 0, fwMinor = 0;
    uint32_t serial = 0;
};
} // namespace fa::minidsp

namespace fa
{
//==============================================================================
/** Drives a miniDSP unit directly over USB HID (no vendor plugin, no UI clicking).

    Safety:
     - Before the first write a snapshot of every parameter it may touch is read back and saved
       to <appdata>/FalseAudio/backups/ (see restoreSnapshot()).
     - Only documented addresses from the device table are written (checked per command).
     - Channel gain is limited to <= 0 dB unless Options::allowPositiveGain.
     - Writes are cached (unchanged values are skipped) and batched between beginBatch/endBatch. */
class MiniDspController : public DspController
{
public:
    struct DetectedDevice
    {
        juce::String path;          // HID path (Options::devicePath)
        uint16_t vendorId = 0, productId = 0;
        juce::String usbProduct, usbSerial;
        minidsp::DeviceIdentity identity;
        juce::String kind;          // minidsp-rs kind, empty if unsupported
        juce::String name;          // human readable
        bool supported = false;
        juce::String error;         // why it could not be identified/supported
    };

    struct Options
    {
        juce::String devicePath;          // empty = first supported device found
        juce::String forceKind;           // e.g. "c8x12v2" when the probe ids are not in the table
        double sampleRateOverride = 0.0;  // required where minidsp-rs has no rate (C-DSP 8x12 v2)
        bool   allowPositiveGain  = false;
        bool   requireSnapshot    = true; // refuse to write if the backup cannot be taken
        juce::File backupFolder;          // default <appdata>/FalseAudio/backups
        int    timeoutMs          = 1000;
    };

    /** Lists miniDSP HID interfaces and identifies each (opens it briefly). Safe to call from the
        message thread; takes ~10-50 ms per device. */
    static std::vector<DetectedDevice> detect();

    explicit MiniDspController (Options options);
    /** For tests / alternative transports: an already-open transport. */
    MiniDspController (std::unique_ptr<minidsp::Transport> transport, Options options);
    ~MiniDspController() override;

    juce::String getName() const override;
    bool isAutomatic() const override             { return true; }
    DspCapabilities getCapabilities() const override;

    bool connect (juce::String& error) override;
    bool isConnected() const override;
    void disconnect();

    bool applyChannel (const OutputChannel& channel, juce::String& error) override;
    bool setMuted (const OutputChannel& channel, bool muted, juce::String& error) override;
    bool beginBatch (juce::String& error) override;
    bool endBatch (juce::String& error) override;

    /** Rewrites every value stored in a snapshot file. */
    bool restoreSnapshot (const juce::File& snapshot, juce::String& error, bool allowUnverifiedDialect = false);
    juce::File getSnapshotFile() const;
    const minidsp::DeviceSpec* getDevice() const noexcept { return device; }
    minidsp::DeviceIdentity getIdentity() const noexcept { return identity; }
    juce::StringArray getWarnings() const;
    int getWritesSent() const noexcept { return writesSent; }

    /** Capabilities for a device table entry (pure). */
    static DspCapabilities capabilitiesFor (const minidsp::DeviceSpec& dev, double sampleRate, bool allowPositiveGain);

    /** Builds (does not send) every command needed to put `channel` on `dev`, in a safe order.
        Pure; used by applyChannel and the tests. */
    static bool buildChannelCommands (const minidsp::DeviceSpec& dev, double sampleRate,
                                      const OutputChannel& channel, bool allowPositiveGain,
                                      std::vector<minidsp::Bytes>& out, juce::StringArray& warnings,
                                      juce::String& error);

private:
    Options options;
    std::unique_ptr<minidsp::Transport> transport;
    bool externalTransport = false;
    const minidsp::DeviceSpec* device = nullptr;
    minidsp::DeviceIdentity identity;
    double sampleRate = 0.0;

    bool snapshotTaken = false;
    juce::File snapshotFile;
    bool batching = false;
    std::vector<minidsp::Bytes> pending;
    std::map<juce::String, minidsp::Bytes> lastWritten;   // key -> payload
    juce::StringArray warnings;
    int writesSent = 0;
    mutable juce::CriticalSection lock;

    bool roundtrip (const minidsp::Bytes& payload, minidsp::Response& response, juce::String& error);
    bool readIdentity (juce::String& error);
    bool readFloatWords (int addr, int count, minidsp::Bytes& words, juce::String& error);
    bool ensureSnapshot (juce::String& error);
    bool queue (std::vector<minidsp::Bytes> commands, juce::String& error);
    bool send (const std::vector<minidsp::Bytes>& commands, juce::String& error);
    bool checkWritable (const OutputChannel& ch, juce::String& error) const;
    static juce::String writeKey (const minidsp::Bytes& payload);
};
} // namespace fa
