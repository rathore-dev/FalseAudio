#pragma once

// Native controller for the Dayton Audio DSP-408 (4 in / 8 out, USB HID 0483:5750).
//
// The wire protocol is ported from two MIT-licensed reverse-engineering projects. Their
// copyright notices are reproduced as the MIT licence requires:
//
//   dsp408-py      https://github.com/malaiwah/dsp408-py
//                  Copyright (c) 2026 dsp408-py contributors. MIT License.
//                  Source of the USB HID framing (64-byte reports, multi-frame reassembly),
//                  the USB opcodes, the 296-byte channel record layout and the firmware quirks.
//
//   dsp408-tuner   https://github.com/nick0451/dsp408-tuner
//                  Copyright (c) 2026 Nick Bell. MIT License.
//                  Source of the header field meanings (user_id = preset slot), the measured
//                  parameter scaling (bandwidth in octaves, RBJ half-gain Q), the crossover
//                  alignment enum, the destructive-opcode list and the read-modify-write rule.
//
//   Permission is hereby granted, free of charge, to any person obtaining a copy of this
//   software and associated documentation files (the "Software"), to deal in the Software
//   without restriction, including without limitation the rights to use, copy, modify, merge,
//   publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
//   to whom the Software is furnished to do so, subject to the following conditions: The above
//   copyright notice and this permission notice shall be included in all copies or substantial
//   portions of the Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
//
// Where the two sources disagree, this file follows the one with acoustic evidence and says so
// next to the constant.

#include "../DspController.h"
#include <array>
#include <map>
#include <memory>

namespace fa
{
namespace dsp408
{
using Bytes  = std::vector<juce::uint8>;
using Report = std::array<juce::uint8, 64>;

constexpr juce::uint16 usbVendorId  = 0x0483;   // STMicroelectronics: shared with other ST HID gadgets,
constexpr juce::uint16 usbProductId = 0x5750;   // so identity is confirmed by the GET_INFO reply.

constexpr int reportSize      = 64;
constexpr int headerSize      = 14;    // bytes before the payload
constexpr int frameOverhead   = 16;    // header + checksum + end marker
constexpr int numOutputs      = 8;
constexpr int numInputs       = 4;
constexpr int numPeqBands     = 10;    // the record has 31 slots; only 0..9 do anything
constexpr int channelBlobSize = 296;   // 37 blocks of 8 bytes, block n at offset 8n
constexpr int blockSize       = 8;

constexpr double deviceSampleRate = 48000.0;

// ---- scaling (dsp408-tuner, measured against the vendor app and acoustically) -------------
constexpr int    unityRaw          = 600;   // gain and PEQ level: dB = raw / 10 - 60
constexpr int    channelGainRawMax = 600;   // 0 dB: the output stage cannot boost
constexpr int    eqLevelRawMin     = 480;   // -12 dB (vendor app limit)
constexpr int    eqLevelRawMax     = 720;   // +12 dB
constexpr int    bwRawMin          = 0;     // 0.05 octave  -> Q 28.852
constexpr int    bwRawMax          = 295;   // 3.00 octaves -> Q 0.404
constexpr int    maxDelaySamples   = 359;   // firmware clamp measured by dsp408-py (7.479 ms @ 48 kHz);
                                            // the manual's 8.147 ms / 391 samples is not reached.
constexpr int    minFreqHz         = 20;
constexpr int    maxFreqHz         = 20000;

// ---- header values --------------------------------------------------------------------------
namespace frameType
{
    constexpr juce::uint8 write = 0xA1, read = 0xA2;           // host -> device
    constexpr juce::uint8 ack = 0x51, nak = 0x52, data = 0x53; // device -> host
}

namespace dataType
{
    constexpr juce::uint8 input = 3, output = 4, system = 9;
}

namespace outputBlock   // DataID on DataType 4 (0..30 are PEQ bands)
{
    constexpr juce::uint8 misc = 31, xover = 32, mix = 33, bulkRead = 119;
}

namespace systemCmd     // ChannelID on DataType 9
{
    constexpr juce::uint8 connect = 0xCC, identity = 0x04;
}

/** Crossover alignment byte. dsp408-py labels 0/1/2 as BW/Bessel/LR, but its own measurement
    (docs/measurements/crossover_filter_types.png) shows type 0 at -6 dB at the corner (LR),
    type 1 maximally flat at -3 dB (Butterworth) and type 2 the slow Bessel roll-off, which is
    the mapping dsp408-tuner settled by A/B in the vendor app. We follow the measurement. */
namespace xoverType
{
    constexpr juce::uint8 linkwitzRiley = 0, butterworth = 1, bessel = 2, defeat = 3; // 3: never written
}

constexpr juce::uint8 slopeBypass = 8;   // slope index 8 = filter off (acoustically flat, dsp408-py)

namespace eqType
{
    constexpr juce::uint8 peaking = 0, lowShelf = 1, highShelf = 2;
}

//==============================================================================
/** One logical protocol frame (all multi-byte fields little-endian):
      0..3 80 80 80 EE | 4 type | 5 device_id | 6 user_id | 7 data_type | 8 channel_id | 9 data_id
      10 aux | 11 pc_custom | 12..13 len | payload | XOR(4 .. 13+len) | AA */
struct Frame
{
    juce::uint8 type       = frameType::read;
    juce::uint8 deviceId   = 1;
    juce::uint8 userId     = 0;   // preset slot! 0 = working area. Anything else can recall a preset.
    juce::uint8 dataType   = dataType::system;
    juce::uint8 channelId  = 0;
    juce::uint8 dataId     = 0;
    juce::uint8 aux        = 0;   // dsp408-tuner: "bluetooth_device_id" (4 over BT); USB uses 0 or 1
    juce::uint8 pcCustom   = 0;
    Bytes payload;

    Bytes encode() const;
    static bool decode (const juce::uint8* data, size_t size, Frame& out, juce::String& error);
};

juce::uint8 checksum (const juce::uint8* frame, size_t payloadLen);

/** Splits a logical frame into 64-byte HID reports (zero padded). For payloads over 48 bytes this
    yields dsp408-py's multi-frame layout: header + 50 payload bytes, then raw continuations, with
    checksum and end marker right after the last payload byte. */
std::vector<Report> toReports (const Bytes& logical);

/** Collects HID reports into one logical frame (multi-frame replies are 296 bytes over 5 reports). */
class Reassembler
{
public:
    enum class Result { needMore, complete, ignored };

    Result feed (const juce::uint8* report, size_t size);
    void reset();

    Frame frame;
    bool checksumOk = false;

private:
    Bytes buffer;
    size_t expected = 0;
};

/** The allow-list every outgoing frame must pass. Only the frames this controller needs are
    allowed: connect/identity reads, whole-channel reads at DataID 119 and 8-byte writes to PEQ
    bands 0..9, MISC, XOVER and MIX on DataType 4 — always with user_id 0. Everything else
    (preset recall/store, RESET_MCU, TRANSMITTAL, RESET_GROUP_DATA, bulk 296-byte writes,
    input-side writes, firmware) is refused. */
bool isAllowed (const Frame& f, juce::String& reason);

// ---- pure command builders (USB form, byte-for-byte as dsp408-py sends them) -----------------
Frame connectRequest();
Frame identityRequest();
Frame readChannelRequest (int channel);

//==============================================================================
struct EqBand
{
    juce::uint16 freqHz   = 1000;
    juce::uint16 levelRaw = unityRaw;
    juce::uint16 bwRaw    = 52;
    juce::uint8  shelfDb  = 0;
    juce::uint8  type     = eqType::peaking;

    std::array<juce::uint8, 8> encode() const;
    static EqBand decode (const juce::uint8* p);
    bool isPlausible() const;   // rejects uninitialised (0xAA-filled) or nonsense records
    bool operator== (const EqBand&) const = default;
};

struct Misc
{
    juce::uint8  enabled      = 1;          // 1 = audible, 0 = muted (inverted vs. the vendor's name)
    juce::uint8  polarity     = 0;          // 1 = inverted
    juce::uint16 gainRaw      = unityRaw;
    juce::uint16 delaySamples = 0;
    juce::uint8  eqMode       = 0;          // unknown, carried through
    juce::uint8  speakerType  = 0;          // speaker-role byte, carried through

    std::array<juce::uint8, 8> encode() const;
    static Misc decode (const juce::uint8* p);
    bool operator== (const Misc&) const = default;
};

struct Xover
{
    juce::uint16 hpFreqHz = 20;
    juce::uint8  hpType   = xoverType::linkwitzRiley;
    juce::uint8  hpSlope  = slopeBypass;
    juce::uint16 lpFreqHz = 20000;
    juce::uint8  lpType   = xoverType::linkwitzRiley;
    juce::uint8  lpSlope  = slopeBypass;

    std::array<juce::uint8, 8> encode() const;
    static Xover decode (const juce::uint8* p);
    bool operator== (const Xover&) const = default;
};

using Mix = std::array<juce::uint8, 8>;   // per-input level 0..100 (only 4 inputs are real)

struct ChannelState
{
    std::array<EqBand, numPeqBands> eq;
    Misc  misc;
    Xover xover;
    Mix   mix {};
    Bytes raw;   // the full 296-byte record as read

    static bool parse (const Bytes& blob, ChannelState& out, juce::String& error);
};

Frame writeEqBand (int channel, int band, const EqBand& b);
Frame writeMisc   (int channel, const Misc& m);
Frame writeXover  (int channel, const Xover& x);
Frame writeMix    (int channel, const Mix& m);

// ---- scaling ---------------------------------------------------------------------------------
int    gainRawFromDb (double db);            // unclamped: round(db*10) + 600
double dbFromGainRaw (int raw);
double octavesFromBwRaw (int bwRaw);
double qFromBwRaw (int bwRaw);
/** Nearest bandwidth step to the requested Q (clamped to the device range). roundUp = the vendor
    app's rule (bandwidth rounded up, so Q never exceeds the request). */
int    bwRawFromQ (double q, bool roundUp = false);
int    delaySamplesFromMs (double ms);       // clamped 0..maxDelaySamples
double msFromDelaySamples (int samples);

//==============================================================================
/** How one FalseAudio value landed on the device grid. */
struct Residual
{
    juce::String what;
    double requested = 0.0;
    double achieved  = 0.0;
    juce::String unit;

    double error() const noexcept { return achieved - requested; }
    juce::String describe() const;
};

struct ChannelPlan
{
    ChannelState target;
    std::vector<Residual> residuals;   // every value the grid or a limit moved
    juce::StringArray notes;           // type mismatches (alignment, slope, shelves, ...)
};

/** Maps FalseAudio settings onto the device record. `current` supplies the fields FalseAudio does
    not own (mixer, speaker type, unused band positions). Fails without side effects if the
    settings cannot be expressed (all-pass, too many bands, shelves when not allowed). */
bool planChannel (const ChannelSettings& s, const ChannelState& current, bool allowShelves,
                  ChannelPlan& out, juce::String& error);

/** The device record expressed as FalseAudio settings (for read-back display / import). */
ChannelSettings toChannelSettings (const ChannelState& st);

DspCapabilities capabilities();

//==============================================================================
/** Raw HID access, abstracted so the controller can run against a simulated device in tests. */
class Transport
{
public:
    virtual ~Transport() = default;
    virtual bool open (juce::String& error) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
    virtual bool writeReport (const Report& r, int timeoutMs) = 0;
    /** Returns bytes read (64), 0 on timeout, < 0 on error. */
    virtual int readReport (Report& r, int timeoutMs) = 0;
};

/** Transport over the shared HidDevice (src/core/bridge/native/HidDevice.h). */
std::unique_ptr<Transport> makeHidTransport (const juce::String& devicePath);
} // namespace dsp408

//==============================================================================
/** DspController for the Dayton Audio DSP-408 over USB.

    Safety model (the device persists every write immediately and has no undo):
      - connect() reads all 8 output records (each read twice until identical) and keeps them as
        the original state;
      - before the first write a JSON snapshot goes to <appdata>/FalseAudio/backups/; if that
        fails, nothing is written;
      - writes are read-modify-write of whole 8-byte blocks, only for blocks that change, and
        each channel is read back and re-sent (twice at most) until the device holds the target;
      - every frame passes dsp408::isAllowed() first (no preset, reset or bulk opcodes);
      - restoreSnapshot() writes the original state back and verifies it. */
class Dsp408Controller : public DspController
{
public:
    struct Options
    {
        juce::String devicePath;           // empty = first detected DSP-408
        juce::File   backupDir;            // empty = <appdata>/FalseAudio/backups
        bool allowShelves = false;         // shelf bandwidth semantics are unverified on hardware
        int  ioTimeoutMs  = 1500;
        int  verifyRetries = 2;
    };

    struct DeviceInfo
    {
        juce::String path, serial, product, manufacturer;
        juce::String identity;             // firmware string, filled by detect (true)
    };

    /** Lists candidate DSP-408s (VID/PID match). With probe = true each candidate is opened and
        asked for its identity (two read-only frames), and non-DSP-408 devices are dropped. */
    static std::vector<DeviceInfo> detect (bool probe = false);

    explicit Dsp408Controller (Options options = {});
    Dsp408Controller (std::unique_ptr<dsp408::Transport> transport, Options options = {});
    ~Dsp408Controller() override;

    juce::String getName() const override        { return "Dayton Audio DSP-408 (USB)"; }
    bool isAutomatic() const override            { return true; }
    DspCapabilities getCapabilities() const override { return dsp408::capabilities(); }

    bool connect (juce::String& error) override;
    bool isConnected() const override;
    void disconnect();

    bool applyChannel (const OutputChannel& channel, juce::String& error) override;
    bool setMuted (const OutputChannel& channel, bool muted, juce::String& error) override;
    bool beginBatch (juce::String&) override;
    bool endBatch (juce::String& error) override;

    /** Writes the state captured at connect() back to the device and verifies it. */
    bool restoreSnapshot (juce::String& error);
    /** Writes a backup file's state back (e.g. after a crash). */
    bool restoreSnapshot (const juce::File& snapshotFile, juce::String& error);

    /** Reads every output record again from the device. */
    bool refresh (juce::String& error);

    juce::String getIdentity() const              { return identity; }
    juce::File   getSnapshotFile() const          { return snapshotFile; }
    const dsp408::ChannelState* getChannelState (int output) const;
    const dsp408::ChannelState* getOriginalState (int output) const;

    /** Quantisation residuals and type mismatches of every applyChannel since connect(). */
    juce::StringArray getMappingReport() const;
    /** Number of 8-byte blocks written since connect() (writes are flash wear). */
    int getBlocksWritten() const noexcept         { return blocksWritten; }

    /** Writes the snapshot JSON for `states` to `file`. */
    static bool writeSnapshot (const juce::File& file, const juce::String& identity, const juce::String& serial,
                               const std::array<dsp408::ChannelState, dsp408::numOutputs>& states, juce::String& error);
    static bool readSnapshot (const juce::File& file, std::array<dsp408::ChannelState, dsp408::numOutputs>& states,
                              juce::String& error);

private:
    Options options;
    std::unique_ptr<dsp408::Transport> transport;
    juce::CriticalSection lock;

    bool connected = false;
    juce::String identity, serial;
    std::array<dsp408::ChannelState, dsp408::numOutputs> current, original;
    juce::File snapshotFile;
    bool snapshotTaken = false;

    bool batching = false;
    std::map<int, dsp408::ChannelState> pending;
    juce::StringArray mappingReport;
    int blocksWritten = 0;

    bool handshake (juce::String& error);   // open + CONNECT + identity check (read-only)
    bool exchange (const dsp408::Frame& request, juce::uint8 expectedReply, dsp408::Frame& reply, juce::String& error);
    bool readChannelStable (int output, dsp408::ChannelState& out, juce::String& error);
    bool ensureSnapshot (juce::String& error);
    bool writeChannel (int output, const dsp408::ChannelState& target, bool includeMix, juce::String& error);
    bool queueOrWrite (int output, const dsp408::ChannelState& target, juce::String& error);
    dsp408::ChannelState stateFor (int output) const;
};
} // namespace fa
