// Tests for the native miniDSP controller. Expected byte frames are taken from minidsp-rs
// (https://github.com/mrene/minidsp-rs, Apache-2.0): protocol/src/packet.rs, protocol/src/fixed_point.rs,
// minidsp/tests/m2x4hd.rs and minidsp/tests/m4x10hd.rs.
#include "TestRegistry.h"
#include "core/bridge/native/MiniDspController.h"

#include <deque>
#include <map>

using fa::test::check;
using namespace fa::minidsp;

namespace
{
Bytes hex (const char* text)
{
    juce::MemoryBlock mb;
    mb.loadFromHexString (juce::String (text).removeCharacters (" "));
    return Bytes ((const uint8_t*) mb.getData(), (const uint8_t*) mb.getData() + mb.getSize());
}

juce::String toHex (const Bytes& b)
{
    return juce::String::toHexString (b.data(), (int) b.size(), 1);
}

void checkFrame (const Bytes& payload, const char* expected, const juce::String& what)
{
    const auto got = frame (payload);
    check (got == hex (expected), "miniDSP frame: " + what, toHex (got));
}

/** A simulated miniDSP on the other end of the USB cable: answers like the firmware does and
    keeps a raw word memory so read-back / restore can be verified. */
class FakeMiniDsp : public Transport
{
public:
    FakeMiniDsp (uint8_t hw, uint8_t dsp) : hwId (hw), dspVersion (dsp) {}

    uint8_t hwId, dspVersion;
    std::map<int, Bytes> memory;              // word address -> 4 raw bytes
    std::map<int, bool> bypass;
    std::vector<Bytes> received;              // payloads of every command
    int writes = 0;
    bool badFrames = false;

    bool sendFrame (const Bytes& f, juce::String& error) override
    {
        if (f.size() < 2 || f[0] != f.size() - 1 || f.back() != checksum (f.data(), f.size() - 1))
        {
            badFrames = true;
            error = "bad frame";
            return false;
        }
        Bytes p (f.begin() + 1, f.end() - 1);
        received.push_back (p);

        // Something unsolicited first, like a volume knob update; the controller must skip it.
        reply ({ 0x05, 0xFF, 0xA9, 0x01 });

        switch (p[0])
        {
            case 0x31: reply ({ 0x31, 1, 10, hwId }); break;
            case 0x05:
            {
                const int addr = (p[1] << 8) | p[2];
                Bytes r { 0x05, p[1], p[2] };
                for (int i = 0; i < p[3]; ++i)
                {
                    const int a = addr + i;
                    r.push_back (a == eeprom::firmwareVersion ? dspVersion
                                 : a == eeprom::serialShort ? 0x00 : a == eeprom::serialShort + 1 ? 0x2A : 0x00);
                }
                reply (r);
                break;
            }
            case 0x14:
            {
                const int addr = (p[1] << 8) | p[2];
                Bytes r { 0x14, p[1], p[2] };
                for (int i = 0; i < p[3]; ++i)
                {
                    auto it = memory.find (addr + i);
                    const Bytes w = it != memory.end() ? it->second : Bytes { 0, 0, 0, 0 };
                    r.insert (r.end(), w.begin(), w.end());
                }
                reply (r);
                break;
            }
            case 0x13:
                memory[addr (p)] = Bytes (p.end() - 4, p.end());
                ++writes;
                reply ({});
                break;
            case 0x30:
            {
                const int a = addr (p);
                const size_t off = p.size() - 20;
                for (int k = 0; k < 5; ++k)
                    memory[a + k] = Bytes (p.begin() + (long) off + k * 4, p.begin() + (long) off + k * 4 + 4);
                ++writes;
                reply ({});
                break;
            }
            case 0x19:
                bypass[addr (p)] = (p[1] & 0x80) != 0;
                ++writes;
                reply ({});
                break;
            default:
                break;
        }
        return true;
    }

    bool receive (Bytes& report, int, juce::String&) override
    {
        if (queue.empty())
            return false;
        report = queue.front();
        queue.pop_front();
        report.resize (64, 0xFF); // padded like a real HID report
        return true;
    }

private:
    std::deque<Bytes> queue;
    void reply (const Bytes& payload) { queue.push_back (frame (payload)); }
    static int addr (const Bytes& p) { return ((p[1] & 0x7F) << 16) | (p[2] << 8) | p[3]; } // 3-byte dialect
};

juce::File tempBackupDir()
{
    auto d = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("FalseAudioMiniDspTests");
    d.deleteRecursively();
    d.createDirectory();
    return d;
}

fa::OutputChannel makeChannel (int output, fa::DriverRole role)
{
    fa::OutputChannel ch;
    ch.id = "FL.test";
    ch.label = "Front Left Test";
    ch.role = role;
    ch.dspOutput = output;
    ch.settings.gainDb = -3.0;
    ch.settings.delayMs = 1.0;
    ch.settings.highPass = fa::FilterSpec::crossover (fa::FilterType::highPass, 80.0, 24);
    ch.settings.lowPass  = fa::FilterSpec::crossover (fa::FilterType::lowPass, 3000.0, 24);
    ch.settings.peq = { fa::FilterSpec::peq (120.0, 2.0, -4.0), fa::FilterSpec::peq (900.0, 1.4, 2.0) };
    return ch;
}
} // namespace

//==============================================================================
FA_TEST (miniDspFramingAndChecksum)
{
    // packet.rs frame_test / unframe_test
    const Bytes framed = frame ({ 0x05, 0xFF, 0xDA, 0x02 });
    check (framed == Bytes ({ 5, 0x05, 0xFF, 0xDA, 0x02, 229 }), "miniDSP: frame = len + payload + checksum", toHex (framed));

    Bytes payload;
    check (unframe ({ 0x3, 0x1, 0x2, 0xFF, 0xFF, 0xFF }, payload) && payload == Bytes ({ 1, 2 }), "miniDSP: unframe strips length and padding");
    check (! unframe ({ 0x09, 0x01 }, payload), "miniDSP: unframe rejects short reports");
    check (parseResponse ({}).kind == Response::Kind::ack, "miniDSP: empty payload is an ack");

    const auto hw = parseResponse ({ 0x31, 1, 10, 10 });
    check (hw.kind == Response::Kind::hardwareId && hw.hwId == 10 && hw.fwMinor == 10, "miniDSP: hardware id response");
    check (responseMatches (cmd::readMemory (0xffda, 4), parseResponse ({ 0x5, 0xff, 0xda, 0x1, 0x2, 0x3, 0x4 })),
           "miniDSP: memory response matches its request (commands.rs test_read_reg)");
    check (! responseMatches (cmd::readMemory (0xffda, 4), parseResponse ({ 0x5, 0xff, 0xa9, 0x1 })),
           "miniDSP: unsolicited memory update does not match");
    check (cmd::readMemory (0xffd8, 9) == Bytes ({ 0x05, 0xff, 0xd8, 9 }), "miniDSP: read memory command");
    checkFrame (cmd::readHardwareId(), "02 31 33", "read hardware id");
}

FA_TEST (miniDspFixedPointCodec)
{
    // fixed_point.rs test_codec
    const std::pair<float, uint32_t> values[] = {
        { -64.f, 0xe0000000 }, { 0.05f, 0x00066666 }, { 0.025f, 0x00033333 }, { 0.0125f, 0x00019999 },
        { 0.125f, 0x00100000 }, { 0.0625f, 0x00080000 }, { 1.0f, 0x00800000 }, { -1.0f, 0x0f800000 },
        { -0.05f, 0x0ff99999 }, { -0.025f, 0x0ffccccc }, { -0.0125f, 0x0ffe6666 }, { -0.125f, 0x0ff00000 },
        { -0.062f, 0x0ff81062 }, { 64.f, 0x20000000 }, { 128.f, 0x40000000 }, { -128.f, 0xc0000000 }, { -111.f, 0xd8800000 }
    };
    bool ok = true;
    juce::String detail;
    for (auto [v, h] : values)
    {
        const auto enc = fixedPointFromFloat (v);
        const auto dec = fixedPointToFloat (enc);
        if (std::abs ((int64_t) enc - (int64_t) h) >= 2 || std::abs (dec - v) > 1e-5f)
        {
            ok = false;
            detail << juce::String (v) << "->" << juce::String::toHexString ((juce::int64) enc) << " ";
        }
    }
    check (ok, "miniDSP: fixed point encode/decode matches minidsp-rs vectors", detail);

    // fixed_point.rs test_db
    const std::pair<float, uint32_t> db[] = {
        { 12.f, 0x01fd93c1 }, { 6.f, 0x00ff64c1 }, { 3.1f, 0x00b6e5ff }, { 1.f, 0x008f9e4c }, { 0.5f, 0x008795a0 },
        { 0.3f, 0x00847f89 }, { -0.3f, 0x007ba78e }, { -0.5f, 0x0078d6fc }, { -3.1f, 0x0059946c }, { -6.f, 0x004026e7 },
        { -12.f, 0x002026f3 }, { -72.f, 0x0000083b }
    };
    ok = true;
    const Dialect fixed { AddrEncoding::len2, FloatEncoding::fixedPoint };
    for (auto [v, h] : db)
    {
        const auto e = encodeGainDb (fixed, v);
        const uint32_t enc = ((uint32_t) e[0] << 24) | ((uint32_t) e[1] << 16) | ((uint32_t) e[2] << 8) | e[3];
        ok = ok && std::abs ((int64_t) enc - (int64_t) h) < 2;
    }
    check (ok, "miniDSP: dB -> fixed point gain matches minidsp-rs vectors");
}

FA_TEST (miniDsp2x4HdFrames)
{
    // minidsp/tests/m2x4hd.rs (hw 10, dsp 100)
    const auto* dev = probe (10, 100);
    check (dev != nullptr && juce::String (dev->kind) == "m2x4hd", "miniDSP: probe (10,100) = 2x4HD");
    if (dev == nullptr)
        return;
    const auto& d = dev->dialect;
    const auto& out0 = dev->outputs[0];

    checkFrame (cmd::write (d, out0.enable, encodeMute (d, true)),  "09 13 800002 01000000 9f", "2x4HD output mute");
    checkFrame (cmd::write (d, out0.enable, encodeMute (d, false)), "09 13 800002 02000000 a0", "2x4HD output unmute");
    checkFrame (cmd::write (d, 26, encodeGainDb (d, -10.0)), "09 13 80001a 000020c1 97", "2x4HD input gain -10 dB (float dB)");
    checkFrame (cmd::write (d, 0, encodeMute (d, true)), "09 13 800000 01000000 9d", "2x4HD input mute");
    checkFrame (cmd::write (d, out0.delay, encodeDelay (d, delaySamples (0.01, dev->sampleRate))), "09 13 800040 01000000 dd", "2x4HD delay 10 us");
    checkFrame (cmd::write (d, out0.delay, encodeDelay (d, delaySamples (1.0, dev->sampleRate))),  "09 13 800040 60000000 3c", "2x4HD delay 1 ms = 96 samples");
    checkFrame (cmd::writeBiquad (d, out0.peq[0], { 1.0, 0.0, 0.0, 0.0, 0.0 }),
                "1b 30 8020e9 0000 0000803f 00000000 00000000 00000000 00000000 93", "2x4HD PEQ clear");
    checkFrame (cmd::writeBiquad (d, out0.peq[0], { 1.0, 0.5, 0.25, -0.25, -0.5 }),
                "1b 30 8020e9 0000 0000803f 0000003f 0000803e 000080be 000000bf 8d", "2x4HD PEQ coefficients (float32 LE)");
    checkFrame (cmd::writeBiquadBypass (d, 0x2085, true), "05 19 802085 43", "2x4HD PEQ bypass");
}

FA_TEST (miniDsp4x10HdFrames)
{
    // minidsp/tests/m4x10hd.rs (hw 1, dsp 50): 2-byte addresses, fixed-point values
    const auto* dev = probe (1, 50);
    check (dev != nullptr && juce::String (dev->kind) == "m4x10hd", "miniDSP: probe (1,50) = 4x10HD");
    if (dev == nullptr)
        return;
    const auto& d = dev->dialect;
    const auto& out0 = dev->outputs[0];

    checkFrame (cmd::write (d, 0x35, encodeMute (d, true)),  "08 13 8035 00000000 d0", "4x10HD mute");
    checkFrame (cmd::write (d, 0x35, encodeMute (d, false)), "08 13 8035 00800000 50", "4x10HD unmute");
    checkFrame (cmd::write (d, out0.gain, encodeGainDb (d, 10.0)),  "08 13 8380 0194c584 fc", "4x10HD output gain +10 dB");
    checkFrame (cmd::write (d, out0.gain, encodeGainDb (d, -12.0)), "08 13 8380 002026f3 57", "4x10HD output gain -12 dB");
    checkFrame (cmd::write (d, out0.delay, encodeDelay (d, delaySamples (9.0, dev->sampleRate))), "08 13 835b 00000360 5c", "4x10HD delay 9 ms");
    checkFrame (cmd::write (d, out0.invert, encodeInvert (d, true)),  "08 13 8365 ff800000 82", "4x10HD invert");
    checkFrame (cmd::write (d, out0.invert, encodeInvert (d, false)), "08 13 8365 00800000 83", "4x10HD not inverted");
    checkFrame (cmd::writeBiquad (d, 0x9a, { 1.0, 0.5, 0.25, -0.25, -0.5 }),
                "1a 30 809a 0000 00800000 00400000 00200000 0fe00000 0fc00000 02", "4x10HD PEQ coefficients (fixed point)");
    checkFrame (cmd::writeBiquadBypass (d, 0x9a, true),  "04 19 809a 37", "4x10HD PEQ bypass on");
    checkFrame (cmd::writeBiquadBypass (d, 0x9a, false), "04 19 009a b7", "4x10HD PEQ bypass off");
}

FA_TEST (miniDspDeviceTable)
{
    check (probe (1, 51) != nullptr && juce::String (probe (1, 51)->kind) == "m10x10hd", "miniDSP: (1,51) = 10x10HD before 4x10HD");
    check (probe (11, 97) != nullptr && juce::String (probe (11, 97)->kind) == "c8x12v2", "miniDSP: (11,97) = C-DSP 8x12 v2");
    check (probe (27, 100) != nullptr && probe (6, 95) != nullptr && probe (14, 3) != nullptr && probe (2, 54) != nullptr
           && probe (10, 101) != nullptr, "miniDSP: Flex, DDRC-88BM, SHD, nanoDIGI, DDRC-24 probe");
    check (probe (99, 1) == nullptr && probe (10, 7) == nullptr, "miniDSP: unknown ids are unsupported");

    bool sane = true;
    for (auto& dev : allDevices())
        for (int i = 0; i < dev.numOutputs; ++i)
            sane = sane && dev.outputs[i].numPeq <= 10 && dev.outputs[i].numXover >= 2 && dev.outputs[i].invert >= 0;
    check (sane, "miniDSP: every output has <= 10 PEQ slots and 2+ crossover groups");

    const auto* c8 = byKind ("c8x12v2");
    check (c8 != nullptr && c8->numOutputs == 12 && c8->outputs[0].numPeq == 10, "miniDSP: C-DSP 8x12 has 12 outputs x 10 PEQ");

    const auto& hd = *probe (10, 100);
    check (isDocumentedAddress (hd, 8580 + 15, true) && ! isDocumentedAddress (hd, 8580 + 16, true),
           "miniDSP: crossover slot addresses are documented");
    check (! isDocumentedAddress (hd, 9000, true) && ! isDocumentedAddress (hd, 6, false), "miniDSP: routing/unknown addresses are not writable");

    const auto caps = fa::MiniDspController::capabilitiesFor (hd, hd.sampleRate, false);
    check (caps.peqBandsPerChannel == 10 && caps.maxChannelGainDb == 0.0 && std::abs (caps.maxDelayMs - 83.333) < 0.01
           && std::abs (caps.delayStepMs - 1000.0 / 96000.0) < 1e-9, "miniDSP: 2x4HD capabilities",
           juce::String (caps.maxDelayMs));
}

FA_TEST (miniDspCoefficientsAndDelay)
{
    fa::BiquadCoeffs b;
    b.b0 = 0.5; b.b1 = 0.25; b.b2 = 0.125; b.a1 = -1.5; b.a2 = 0.6;
    const auto c = toDeviceCoefficients (b);
    check (c[0] == 0.5 && c[1] == 0.25 && c[2] == 0.125 && c[3] == 1.5 && c[4] == -0.6, "miniDSP: a1/a2 sign flipped (miniDSP convention)");

    check (delaySamples (0.0, 96000) == 0 && delaySamples (-1.0, 96000) == 0, "miniDSP: non-positive delay = 0 samples");
    check (delaySamples (2.5, 48000) == 120 && delaySamples (0.0104, 96000) == 1, "miniDSP: delay ms -> samples");
}

FA_TEST (miniDspChannelCommands)
{
    const auto& dev = *probe (10, 100);
    auto ch = makeChannel (1, fa::DriverRole::midrange);
    std::vector<Bytes> cmds;
    juce::StringArray warn;
    juce::String error;
    const bool ok = fa::MiniDspController::buildChannelCommands (dev, dev.sampleRate, ch, false, cmds, warn, error);
    check (ok, "miniDSP: build channel commands", error);
    if (! ok)
        return;

    // gain, delay, invert, xover 2x(4+1), PEQ 10x2, unmute
    check (cmds.size() == 1 + 1 + 1 + 10 + 20 + 1, "miniDSP: command count for one channel", juce::String ((int) cmds.size()));

    const auto& o = dev.outputs[1];
    const auto& d = dev.dialect;
    check (cmds.front() == cmd::write (d, o.gain, encodeGainDb (d, -3.0)), "miniDSP: gain written first when not muting");
    check (cmds.back() == cmd::write (d, o.enable, encodeMute (d, false)), "miniDSP: unmute is written last");

    const auto hp = ch.settings.highPass.toBiquads (96000.0);
    check (cmds[3] == cmd::writeBiquad (d, o.xover[0], toDeviceCoefficients (hp[0])), "miniDSP: HP biquad 1 in crossover group 0");
    check (cmds[7] == cmd::writeBiquadBypass (d, o.xover[0], false), "miniDSP: crossover group enabled after its coefficients");

    const auto peq0 = ch.settings.peq[0].toBiquads (96000.0)[0];
    check (cmds[13] == cmd::writeBiquad (d, o.peq[0], toDeviceCoefficients (peq0)) && cmds[14] == cmd::writeBiquadBypass (d, o.peq[0], false),
           "miniDSP: PEQ 1 written then enabled");
    check (cmds[17] == cmd::writeBiquadBypass (d, o.peq[2], true), "miniDSP: unused PEQ slot bypassed");

    // Sub: no HP -> group 0 bypassed; gain limited to 0 dB
    auto sub = makeChannel (3, fa::DriverRole::subwoofer);
    sub.settings.gainDb = 4.0;
    sub.settings.muted = true;
    cmds.clear();
    warn.clear();
    check (fa::MiniDspController::buildChannelCommands (dev, dev.sampleRate, sub, false, cmds, warn, error)
           && cmds[0] == cmd::write (d, dev.outputs[3].enable, encodeMute (d, true))
           && cmds[1] == cmd::write (d, dev.outputs[3].gain, encodeGainDb (d, 0.0))
           && std::find (cmds.begin(), cmds.end(), cmd::writeBiquadBypass (d, dev.outputs[3].xover[0], true)) != cmds.end()
           && warn.size() == 1,
           "miniDSP: sub mutes first, HP group bypassed, +4 dB limited to 0 dB");

    // Too many PEQ biquads
    auto many = makeChannel (0, fa::DriverRole::midbass);
    many.settings.peq.assign (11, fa::FilterSpec::peq (100.0, 1.0, -1.0));
    cmds.clear();
    check (! fa::MiniDspController::buildChannelCommands (dev, dev.sampleRate, many, false, cmds, warn, error) && cmds.empty(),
           "miniDSP: 11 PEQ bands rejected on a 10-band output", error);

    // Unknown sample rate (C-DSP 8x12 v2 in minidsp-rs)
    const auto& c8 = *byKind ("c8x12v2");
    check (! fa::MiniDspController::buildChannelCommands (c8, c8.sampleRate, ch, false, cmds, warn, error),
           "miniDSP: C-DSP 8x12 refuses delay/filters without a confirmed sample rate", error);
    cmds.clear();
    check (fa::MiniDspController::buildChannelCommands (c8, 48000.0, ch, false, cmds, warn, error),
           "miniDSP: C-DSP 8x12 works with a sample-rate override");
    check (! fa::MiniDspController::buildChannelCommands (dev, dev.sampleRate, makeChannel (4, fa::DriverRole::tweeter), false, cmds, warn, error),
           "miniDSP: output 5 rejected on a 4-output device");
}

FA_TEST (miniDspControllerWithFakeDevice)
{
    auto fakeOwned = std::make_unique<FakeMiniDsp> (10, 100);
    auto* fake = fakeOwned.get();
    // Pre-existing user tune on output 1 so the backup has something to restore.
    fake->memory[28] = { 0x00, 0x00, 0xC0, 0xC0 }; // gain -6 dB (float LE)
    {
        const int slot = probe (10, 100)->outputs[0].peq[5];   // a flat (unity) PEQ slot
        fake->memory[slot] = { 0x00, 0x00, 0x80, 0x3F };
        for (int k = 1; k < 5; ++k)
            fake->memory[slot + k] = { 0, 0, 0, 0 };
    }

    fa::MiniDspController::Options opt;
    opt.backupFolder = tempBackupDir();
    fa::MiniDspController ctl (std::move (fakeOwned), opt);

    juce::String error;
    check (ctl.connect (error) && ctl.isConnected(), "miniDSP: connect identifies the device (skips unsolicited reports)", error);
    check (ctl.getName().contains ("2x4 HD") && ctl.getIdentity().serial == 900042, "miniDSP: name and serial", ctl.getName());
    check (ctl.getCapabilities().peqBandsPerChannel == 10, "miniDSP: capabilities from the connected device");

    fa::SystemConfig cfg;
    cfg.channels = { makeChannel (0, fa::DriverRole::midbass), makeChannel (1, fa::DriverRole::tweeter) };
    check (ctl.applyAll (cfg, error), "miniDSP: applyAll", error);
    check (! fake->badFrames, "miniDSP: every frame has a valid length and checksum");

    const auto snap = ctl.getSnapshotFile();
    check (snap.existsAsFile() && snap.getParentDirectory() == opt.backupFolder, "miniDSP: snapshot saved before writing", snap.getFullPathName());

    // The snapshot must be read before the first write.
    size_t firstWrite = fake->received.size(), lastRead = 0;
    for (size_t i = 0; i < fake->received.size(); ++i)
    {
        const auto c = fake->received[i][0];
        if (c == 0x14) lastRead = i;
        if ((c == 0x13 || c == 0x30 || c == 0x19) && i < firstWrite) firstWrite = i;
    }
    check (lastRead < firstWrite, "miniDSP: all read-backs happen before the first write");

    const int writesAfterFirst = fake->writes;
    check (ctl.applyAll (cfg, error) && fake->writes == writesAfterFirst, "miniDSP: unchanged settings cause no USB writes",
           juce::String (fake->writes - writesAfterFirst));

    check (ctl.soloChannels (cfg, { "nothing" }, error) && fake->writes == writesAfterFirst + 2, "miniDSP: solo = two mute writes");

    // Batch dedupe: three gain changes in one batch -> one write
    const int before = fake->writes;
    auto ch = cfg.channels[0];
    ctl.beginBatch (error);
    for (double g : { -1.0, -2.0, -5.0 })
    {
        ch.settings.gainDb = g;
        ctl.applyChannel (ch, error);
    }
    check (ctl.endBatch (error) && fake->writes == before + 2, "miniDSP: batch keeps only the last gain (+ unmute after solo)",
           juce::String (fake->writes - before));
    check (fake->memory[28] == Bytes ({ 0x00, 0x00, 0xA0, 0xC0 }), "miniDSP: device holds the last gain (-5 dB)");

    // Restore the backup: output 1 gain back to -6 dB
    check (ctl.restoreSnapshot (snap, error) && fake->memory[28] == Bytes ({ 0x00, 0x00, 0xC0, 0xC0 }),
           "miniDSP: restoreSnapshot puts the old values back", error);
    check (fake->bypass[probe (10, 100)->outputs[0].peq[5]] == true, "miniDSP: restore bypasses unity PEQ slots");
}
