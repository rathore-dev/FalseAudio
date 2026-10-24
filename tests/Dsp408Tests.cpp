// Tests for the native Dayton Audio DSP-408 controller.
// Golden frames are copied verbatim from the reference projects:
//   dsp408-py     tests/test_frame.py            (USB captures of DSP-408.exe V1.24)
//   dsp408-tuner  tests/golden/dsp408_frames.json (Bluetooth capture of the Android app)

#include "TestRegistry.h"
#include "core/bridge/native/Dsp408Controller.h"

#include <deque>

using fa::test::check;
using namespace fa;
using namespace fa::dsp408;

namespace
{
Bytes hex (const juce::String& s)
{
    juce::MemoryBlock mb;
    mb.loadFromHexString (s);
    const auto* p = static_cast<const juce::uint8*> (mb.getData());
    return Bytes (p, p + mb.getSize());
}

Bytes padded (Bytes b)
{
    b.resize (64, 0);
    return b;
}

Bytes reportBytes (const Frame& f)
{
    const auto reports = toReports (f.encode());
    return reports.size() == 1 ? Bytes (reports[0].begin(), reports[0].end()) : Bytes();
}

juce::String dump (const Bytes& b)
{
    return juce::String::toHexString (b.data(), (int) b.size(), 1);
}

/** A factory-fresh output record, as dsp408-py and dsp408-tuner describe it. */
Bytes factoryRecord (int ch)
{
    static const int freqs[10] = { 31, 65, 125, 250, 500, 1000, 2000, 4000, 8000, 16000 };
    static const juce::uint8 speaker[8] = { 0x01, 0x02, 0x03, 0x07, 0x08, 0x09, 0x0f, 0x12 };

    Bytes b (296, 0xAA);   // uninitialised slots read as 0xAA
    for (int i = 0; i < 10; ++i)
    {
        EqBand e;
        e.freqHz = (juce::uint16) freqs[i];
        e.levelRaw = 600;
        e.bwRaw = 52;
        auto enc = e.encode();
        std::copy (enc.begin(), enc.end(), b.begin() + i * 8);
    }
    Misc m;
    m.speakerType = speaker[ch];
    auto me = m.encode();
    std::copy (me.begin(), me.end(), b.begin() + 248);
    Xover x;
    x.hpSlope = 1;
    x.lpSlope = 1;
    auto xe = x.encode();
    std::copy (xe.begin(), xe.end(), b.begin() + 256);
    Mix mix {};
    mix[(size_t) (ch % 2)] = 100;
    std::copy (mix.begin(), mix.end(), b.begin() + 264);
    const juce::uint8 dyn[8] = { 0xa4, 0x01, 0x38, 0x00, 0xf4, 0x01, 0x00, 0x00 };
    std::copy (dyn, dyn + 8, b.begin() + 280);
    std::fill (b.begin() + 288, b.end(), (juce::uint8) 0x20);
    return b;
}

/** Simulated DSP-408 on the far side of the HID transport. */
class FakeDsp408 : public Transport
{
public:
    FakeDsp408()
    {
        for (int ch = 0; ch < 8; ++ch)
            records[(size_t) ch] = factoryRecord (ch);
    }

    bool open (juce::String&) override { opened = true; return true; }
    void close() override              { opened = false; }
    bool isOpen() const override       { return opened; }

    bool writeReport (const Report& r, int) override
    {
        if (ra.feed (r.data(), r.size()) == Reassembler::Result::complete)
            handle (ra.frame);
        return true;
    }

    int readReport (Report& r, int) override
    {
        if (outbox.empty())
            return 0;
        r = outbox.front();
        outbox.pop_front();
        return 64;
    }

    std::array<Bytes, 8> records;
    std::vector<Frame> received;
    int writesReceived = 0;
    int dropNextWrites = 0;            // ACK but do not apply (the firmware's rapid-write quirk)
    juce::String identity = "MYDW-AV1.06";

private:
    bool opened = false;
    Reassembler ra;
    std::deque<Report> outbox;

    void reply (const Frame& req, juce::uint8 type, Bytes payload)
    {
        Frame f = req;
        f.type = type;
        f.payload = std::move (payload);
        for (auto& r : toReports (f.encode()))
            outbox.push_back (r);
    }

    void handle (const Frame& f)
    {
        received.push_back (f);
        if (f.type == frameType::read && f.dataType == dataType::system && f.channelId == systemCmd::connect)
            reply (f, frameType::data, { 0x00 });
        else if (f.type == frameType::read && f.dataType == dataType::system && f.channelId == systemCmd::identity)
            reply (f, frameType::data, Bytes (identity.toRawUTF8(), identity.toRawUTF8() + identity.length()));
        else if (f.type == frameType::read && f.dataType == dataType::output && f.dataId == outputBlock::bulkRead)
            reply (f, frameType::data, records[f.channelId]);
        else if (f.type == frameType::write && f.dataType == dataType::output && f.payload.size() == 8)
        {
            ++writesReceived;
            if (dropNextWrites > 0)
                --dropNextWrites;
            else
                std::copy (f.payload.begin(), f.payload.end(), records[f.channelId].begin() + f.dataId * 8);
            reply (f, frameType::ack, {});
        }
        else
            reply (f, frameType::nak, {});
    }
};

juce::File makeTempDir (const juce::String& name)
{
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getChildFile ("FalseAudioDsp408Tests").getChildFile (name);
    dir.deleteRecursively();
    dir.createDirectory();
    return dir;
}

OutputChannel makeChannel (int output)
{
    OutputChannel ch;
    ch.id = "FL.mid";
    ch.label = "Front Left Mid";
    ch.dspOutput = output;
    ch.settings.gainDb = -3.0;
    ch.settings.delayMs = 1.0;
    ch.settings.inverted = true;
    ch.settings.highPass = FilterSpec::crossover (FilterType::highPass, 80.4, 24, Alignment::linkwitzRiley);
    ch.settings.lowPass  = FilterSpec::crossover (FilterType::lowPass, 3000.0, 24, Alignment::linkwitzRiley);
    ch.settings.peq = { FilterSpec::peq (125.0, 4.3, -4.0), FilterSpec::peq (2500.0, 1.0, 2.5) };
    return ch;
}

bool sameBlocks (const Bytes& a, const Bytes& b)
{
    // PEQ 1-10, MISC, XOVER, MIX: everything the controller may write.
    return std::equal (a.begin(), a.begin() + 80, b.begin()) && std::equal (a.begin() + 248, a.begin() + 272, b.begin() + 248);
}
} // namespace

//==============================================================================
FA_TEST (dsp408GoldenUsbFrames)
{
    // dsp408-py tests/test_frame.py: frames captured from DSP-408.exe V1.24 over USB.
    check (reportBytes (connectRequest())
               == padded (hex ("808080eea2010009cc000000080000000000000000006eaa")),
           "DSP-408 CONNECT request matches the USB capture", dump (reportBytes (connectRequest())));
    check (reportBytes (identityRequest())
               == padded (hex ("808080eea20100090400000008000000000000000000a6aa")),
           "DSP-408 GET_INFO request matches the USB capture");
    check (reportBytes (readChannelRequest (0))
               == padded (hex ("808080eea20100040077000008000000000000000000d8aa")),
           "DSP-408 read-channel request (0x7700) matches the USB capture");

    Misc m;
    m.enabled = 1; m.polarity = 0; m.gainRaw = 0x196; m.delaySamples = 0; m.eqMode = 0; m.speakerType = 0x12;
    check (reportBytes (writeMisc (7, m)) == padded (hex ("808080eea1010004071f00000800010096010000001230aa")),
           "DSP-408 channel write 0x1F07 matches the USB capture", dump (reportBytes (writeMisc (7, m))));

    // EQ band and crossover writes: dsp408-py opcodes 0x10000 + (band << 8) + ch and 0x12000 + ch.
    EqBand e;
    e.freqHz = 1000; e.levelRaw = 720; e.bwRaw = 52;
    const auto eq = writeEqBand (2, 5, e).encode();
    check (eq[7] == 4 && eq[8] == 2 && eq[9] == 5 && eq[10] == 1 && eq[12] == 8,
           "DSP-408 EQ write header = cmd 0x10502, category 4", dump (eq));
    const auto xo = writeXover (3, Xover()).encode();
    check (xo[7] == 4 && xo[8] == 3 && xo[9] == 0x20 && xo[10] == 1, "DSP-408 crossover write header = cmd 0x12003", dump (xo));
}

FA_TEST (dsp408GoldenBluetoothFrames)
{
    // dsp408-tuner golden frames (Android app over RFCOMM). Same frame format; header byte 10
    // carries 4 over Bluetooth, so the USB builders are checked with that byte switched.
    auto bt = [] (Frame f) { f.aux = 4; return f.encode(); };

    Misc m;
    m.enabled = 1; m.polarity = 1; m.gainRaw = 480; m.delaySamples = 144; m.speakerType = 1;
    check (bt (writeMisc (0, m)) == hex ("808080eea101000400 1f 0400 0800 0101e00190000001 c7aa"),
           "Channel gain 480 (-12.0 dB) MISC block matches dsp408-tuner", dump (bt (writeMisc (0, m))));

    Xover x;
    x.hpFreqHz = 450; x.hpType = xoverType::linkwitzRiley; x.hpSlope = 1;
    x.lpFreqHz = 1234; x.lpType = xoverType::linkwitzRiley; x.lpSlope = 1;
    check (bt (writeXover (0, x)) == hex ("808080eea1010004002004000800c2010001d2040001 9daa"),
           "Low-pass 1234 Hz XOVER block matches dsp408-tuner", dump (bt (writeXover (0, x))));

    EqBand b3;
    b3.freqHz = 2514; b3.levelRaw = 600; b3.bwRaw = 110;
    check (bt (writeEqBand (0, 3, b3)) == hex ("808080eea1010004000304000800d20958026e000000 44aa"),
           "EQ band 3 level 600 (0.0 dB) matches dsp408-tuner");

    EqBand b2;
    b2.freqHz = 2245; b2.levelRaw = 689; b2.bwRaw = 106;
    check (bt (writeEqBand (0, 2, b2)) == hex ("808080eea1010004000204000800c508b1026a000000 beaa"),
           "EQ band 2 at 12699->2245 Hz, +8.9 dB, bw 106 matches dsp408-tuner");

    Frame decoded;
    juce::String err;
    const auto raw = hex ("808080eea1010004001f04000800 0101fe0190000001 d9aa");
    check (Frame::decode (raw.data(), raw.size(), decoded, err) && decoded.dataId == outputBlock::misc
               && Misc::decode (decoded.payload.data()).gainRaw == 510,
           "Captured MISC frame decodes (gain raw 510)", err);

    auto bad = raw;
    bad[16] ^= 1;
    check (! Frame::decode (bad.data(), bad.size(), decoded, err), "Corrupted frame is rejected by checksum", err);
}

FA_TEST (dsp408MultiFrame)
{
    // dsp408-py: first IN report of a 296-byte channel read (header + 50 payload bytes, no checksum).
    const auto first = hex ("808080ee530100040077000028011f0058023400000041005802340000007d00580234000000fa00580234000000f401580234000000e803580234000000d007");
    check (first.size() == 64, "Captured multi-frame report is 64 bytes");

    Reassembler ra;
    check (ra.feed (first.data(), first.size()) == Reassembler::Result::needMore, "Multi-frame reply waits for continuations");

    const auto band0 = EqBand::decode (first.data() + 14);
    const auto band1 = EqBand::decode (first.data() + 22);
    check (band0.freqHz == 31 && band0.levelRaw == 600 && band0.bwRaw == 52 && band1.freqHz == 65,
           "Captured record decodes: band 1 = 31 Hz / 0 dB / bw 52, band 2 = 65 Hz");

    Frame reply = readChannelRequest (4);
    reply.type = frameType::data;
    reply.payload = factoryRecord (4);
    const auto reports = toReports (reply.encode());
    check (reports.size() == 5, "296-byte payload travels in 5 HID reports", juce::String ((int) reports.size()));
    check (std::equal (reply.payload.begin(), reply.payload.begin() + 50, reports[0].begin() + 14),
           "First report carries 50 payload bytes (dsp408-py multi-frame layout)");

    Reassembler ra2;
    Reassembler::Result res = Reassembler::Result::ignored;
    for (auto& r : reports)
        res = ra2.feed (r.data(), r.size());
    check (res == Reassembler::Result::complete && ra2.checksumOk && ra2.frame.payload == reply.payload,
           "Multi-frame reply reassembles with a valid checksum");

    ChannelState st;
    juce::String err;
    check (ChannelState::parse (ra2.frame.payload, st, err) && st.misc.speakerType == 0x08 && st.xover.lpFreqHz == 20000
               && st.eq[9].freqHz == 16000 && st.mix[0] == 100,
           "Channel record parses (MISC @248, XOVER @256, MIX @264)", err);
}

FA_TEST (dsp408Scaling)
{
    // dsp408-tuner: bw_raw -> Q the vendor app displays.
    const int raws[] = { 24, 43, 52, 90, 134 };
    const double qs[] = { 4.966, 2.992, 2.515, 1.492, 0.999 };
    bool ok = true;
    juce::String detail;
    for (int i = 0; i < 5; ++i)
    {
        ok = ok && std::abs (qFromBwRaw (raws[i]) - qs[i]) < 0.0015;
        detail << raws[i] << "->" << juce::String (qFromBwRaw (raws[i]), 3) << " ";
    }
    check (ok, "Bandwidth raw -> Q matches the vendor app display", detail);
    check (std::abs (qFromBwRaw (0) - 28.852) < 0.001 && std::abs (qFromBwRaw (295) - 0.404) < 0.001,
           "Q range is 0.404 .. 28.852 (raw 295 .. 0)");
    check (bwRawFromQ (1.0, true) == 134 && bwRawFromQ (3.0, true) == 43 && bwRawFromQ (1.5, true) == 90,
           "Vendor rounding (bandwidth up): Q 1 -> 134, Q 3 -> 43, Q 1.5 -> 90");
    check (bwRawFromQ (1.5) == 89 && bwRawFromQ (100.0) == 0 && bwRawFromQ (0.1) == 295,
           "Nearest rounding and clamping of Q");

    check (gainRawFromDb (-12.0) == 480 && gainRawFromDb (8.9) == 689 && std::abs (dbFromGainRaw (433) + 16.7) < 1e-9,
           "Gain / level encoding dB = raw/10 - 60");
    check (delaySamplesFromMs (2.2083) == 106 && delaySamplesFromMs (0.1875) == 9 && delaySamplesFromMs (20.0) == 359
               && delaySamplesFromMs (-1.0) == 0,
           "Delay is integer samples at 48 kHz, clamped at 359");

    // The device Q is RBJ half-gain bandwidth, which is what FalseAudio's peaking biquad uses:
    // a +12 dB band with bw_raw 65 must be 0.70 octave wide at its +6 dB points.
    const auto bq = BiquadCoeffs::peaking (1000.0, qFromBwRaw (65), 12.0, 48000.0);
    auto magDb = [&] (double f) { return 20.0 * std::log10 (std::abs (bq.response (f, 48000.0))); };
    auto edge = [&] (double lo, double hi)
    {
        for (int i = 0; i < 60; ++i)
        {
            const double mid = std::sqrt (lo * hi);
            ((magDb (mid) > 6.0) == (magDb (lo) > 6.0) ? lo : hi) = mid;
        }
        return std::sqrt (lo * hi);
    };
    const double octaves = std::log2 (edge (1000.0, 4000.0) / edge (250.0, 1000.0));
    check (std::abs (octaves - octavesFromBwRaw (65)) < 0.01, "FalseAudio RBJ Q matches the device bandwidth convention",
           juce::String (octaves, 4) + " oct");
}

FA_TEST (dsp408AllowList)
{
    juce::String why;
    check (isAllowed (connectRequest(), why) && isAllowed (identityRequest(), why) && isAllowed (readChannelRequest (7), why),
           "Handshake and channel reads are allowed", why);
    check (isAllowed (writeEqBand (0, 9, EqBand()), why) && isAllowed (writeMisc (0, Misc()), why)
               && isAllowed (writeXover (0, Xover()), why) && isAllowed (writeMix (0, Mix {}), why),
           "Block writes are allowed", why);

    auto recall = readChannelRequest (0);
    recall.userId = 1;
    check (! isAllowed (recall, why), "user_id != 0 (preset slot) is refused", why);

    auto presetRead = readChannelRequest (0);
    presetRead.dataId = 0;
    check (! isAllowed (presetRead, why), "Output read at DataID 0 (preset recall form) is refused", why);

    auto reset = connectRequest();
    reset.channelId = 96;
    check (! isAllowed (reset, why), "RESET_MCU is refused", why);

    auto bulk = writeMisc (0, Misc());
    bulk.payload.assign (296, 0);
    check (! isAllowed (bulk, why), "296-byte bulk write is refused", why);

    check (! isAllowed (writeEqBand (0, 10, EqBand()), why), "PEQ band 11+ is refused", why);
    auto dyn = writeMisc (0, Misc());
    dyn.dataId = 35;
    check (! isAllowed (dyn, why), "Dynamics block (unverified) is refused", why);
    check (! isAllowed (writeMisc (8, Misc()), why), "Output 9 is refused", why);
}

FA_TEST (dsp408Mapping)
{
    ChannelState cur;
    juce::String err;
    ChannelState::parse (factoryRecord (2), cur, err);

    auto ch = makeChannel (2);
    dsp408::ChannelPlan plan;
    check (planChannel (ch.settings, cur, false, plan, err), "Plan maps a typical channel", err);
    const auto& t = plan.target;
    check (t.misc.gainRaw == 570 && t.misc.polarity == 1 && t.misc.enabled == 1 && t.misc.delaySamples == 48
               && t.misc.speakerType == cur.misc.speakerType,
           "Gain/polarity/delay mapped, speaker type preserved");
    check (t.xover.hpFreqHz == 80 && t.xover.hpType == xoverType::linkwitzRiley && t.xover.hpSlope == 3
               && t.xover.lpFreqHz == 3000 && t.xover.lpSlope == 3,
           "LR24 crossovers -> type 0, slope index 3");
    check (t.eq[0].freqHz == 125 && t.eq[0].levelRaw == 560 && t.eq[0].bwRaw == (juce::uint16) bwRawFromQ (4.3)
               && t.eq[1].freqHz == 2500 && t.eq[1].levelRaw == 625 && t.eq[1].bwRaw == 134,
           "PEQ bands quantised to Hz / 0.1 dB / 0.01 oct");
    check (t.eq[5].levelRaw == 600 && t.eq[5].freqHz == 1000 && t.mix == cur.mix, "Unused bands flat, mixer untouched");

    bool hasQResidual = false, hasHpResidual = false;
    for (auto& r : plan.residuals)
    {
        hasQResidual |= r.what == "PEQ 1 Q";
        hasHpResidual |= r.what == "HP frequency" && std::abs (r.error() + 0.4) < 1e-9;
    }
    check (hasQResidual && hasHpResidual, "Quantisation residuals are reported",
           plan.residuals.empty() ? juce::String() : plan.residuals.front().describe());

    auto s = ch.settings;
    s.gainDb = 3.0;
    s.delayMs = 9.0;
    s.highPass = FilterSpec::crossover (FilterType::highPass, 80.0, 18, Alignment::linkwitzRiley);
    s.lowPass = FilterSpec::crossover (FilterType::lowPass, 3000.0, 36, Alignment::bessel);
    check (planChannel (s, cur, false, plan, err) && plan.target.misc.gainRaw == 600 && plan.target.misc.delaySamples == 359
               && plan.target.xover.hpType == xoverType::butterworth && plan.target.xover.hpSlope == 2
               && plan.target.xover.lpType == xoverType::bessel && plan.target.xover.lpSlope == 5
               && plan.notes.size() >= 4,
           "Limits and alignment mismatches are clamped and reported", plan.notes.joinIntoString (" | "));

    s.lowPass.enabled = false;
    planChannel (s, cur, false, plan, err);
    check (plan.target.xover.lpSlope == slopeBypass && plan.target.xover.lpFreqHz == 20000, "Disabled LP -> bypass (slope 8)");

    s = ch.settings;
    s.peq.assign (11, FilterSpec::peq (1000.0, 1.0, -1.0));
    check (! planChannel (s, cur, false, plan, err), "11 PEQ bands are refused", err);

    s.peq = { FilterSpec::peq (1000.0, 1.0, -1.0) };
    s.peq[0].type = FilterType::allPass;
    check (! planChannel (s, cur, false, plan, err), "All-pass is refused", err);

    s.peq = { FilterSpec::peq (1000.0, 1.0, -1.0), FilterSpec::peq (60.0, 0.7, 3.0) };
    s.peq[1].type = FilterType::lowShelf;
    check (! planChannel (s, cur, false, plan, err), "Shelves are refused unless allowed", err);
    check (planChannel (s, cur, true, plan, err) && plan.target.eq[0].type == eqType::lowShelf
               && plan.target.eq[0].freqHz == 60 && plan.target.eq[1].freqHz == 1000,
           "Allowed low shelf goes to band 1, peaks fill the rest", err);

    planChannel (ch.settings, cur, false, plan, err);
    const auto back = toChannelSettings (plan.target);
    check (back.peq.size() == 2 && std::abs (back.highPass.freq - 80.0) < 1e-9 && back.highPass.slopeDbPerOct == 24
               && back.highPass.alignment == Alignment::linkwitzRiley && std::abs (back.delayMs - 1.0) < 1e-9
               && back.inverted && std::abs (back.gainDb + 3.0) < 1e-9,
           "Device record converts back to FalseAudio settings");

    const auto caps = capabilities();
    check (caps.peqBandsPerChannel == 10 && std::abs (caps.peqMinQ - 0.404) < 0.001 && std::abs (caps.peqMaxQ - 28.85) < 0.01
               && caps.maxChannelGainDb == 0.0 && std::abs (caps.maxDelayMs - 7.479) < 0.001 && ! caps.hasAllPass,
           "Capabilities reflect the device grid");
}

FA_TEST (dsp408ControllerAgainstFakeDevice)
{
    const auto dir = makeTempDir ("backups");
    Dsp408Controller::Options opts;
    opts.backupDir = dir;
    opts.ioTimeoutMs = 200;

    auto fakeOwned = std::make_unique<FakeDsp408>();
    auto* fake = fakeOwned.get();
    const auto factory = fake->records;
    Dsp408Controller dsp (std::move (fakeOwned), opts);

    juce::String err;
    check (dsp.connect (err) && dsp.getIdentity() == "MYDW-AV1.06", "Connects and identifies the DSP-408", err);
    check (fake->writesReceived == 0 && dir.getNumberOfChildFiles (juce::File::findFiles) == 0,
           "Connect is read-only and leaves no backup yet");

    auto ch = makeChannel (2);
    check (dsp.applyChannel (ch, err), "applyChannel writes a channel", err);
    check (dsp.getSnapshotFile().existsAsFile() && dir.getNumberOfChildFiles (juce::File::findFiles) == 1,
           "Backup JSON written before the first write", dsp.getSnapshotFile().getFullPathName());
    check (fake->writesReceived == 4, "Only changed blocks are written (2 PEQ + MISC + XOVER)", juce::String (fake->writesReceived));
    const auto* st = dsp.getChannelState (2);
    check (st != nullptr && st->misc.gainRaw == 570 && st->eq[0].freqHz == 125 && fake->records[2] == st->raw,
           "Device holds the written values (read back)");

    const int before = fake->writesReceived;
    check (dsp.applyChannel (ch, err) && fake->writesReceived == before, "Re-applying identical settings writes nothing");

    // Firmware silently drops a write: the readback catches it and the block is re-sent.
    ch.settings.gainDb = -6.0;
    fake->dropNextWrites = 1;
    check (dsp.applyChannel (ch, err) && dsp.getChannelState (2)->misc.gainRaw == 540, "Dropped write is detected and re-sent", err);

    // Batching: nothing reaches the device until endBatch().
    SystemConfig cfg;
    for (int i = 0; i < 3; ++i)
    {
        auto c = makeChannel (i);
        c.id = "c" + juce::String (i);
        cfg.channels.push_back (c);
    }
    const int beforeBatch = fake->writesReceived;
    dsp.beginBatch (err);
    for (auto& c : cfg.channels)
        dsp.setMuted (c, true, err);
    check (fake->writesReceived == beforeBatch, "Batched mutes are deferred");
    check (dsp.endBatch (err) && fake->records[0][248] == 0 && fake->records[1][248] == 0 && fake->records[2][248] == 0,
           "endBatch applies the mutes", err);
    check (dsp.soloChannels (cfg, { "c1" }, err) && fake->records[1][248] == 1 && fake->records[0][248] == 0,
           "soloChannels unmutes only the soloed output", err);

    // Everything sent was on the allow-list and addressed the working area.
    bool allSafe = true;
    for (auto& f : fake->received)
    {
        juce::String why;
        allSafe = allSafe && f.userId == 0 && isAllowed (f, why);
    }
    check (allSafe, "Every frame sent passes the allow-list with user_id 0", juce::String ((int) fake->received.size()) + " frames");

    // Restore puts every output back to the state before the first write.
    check (dsp.restoreSnapshot (err), "restoreSnapshot succeeds", err);
    bool restored = true;
    for (int i = 0; i < 8; ++i)
        restored = restored && sameBlocks (fake->records[(size_t) i], factory[(size_t) i]);
    check (restored, "Device is back to its original state");

    // The backup file alone is enough to restore (e.g. after a crash).
    std::array<ChannelState, numOutputs> fromFile;
    check (Dsp408Controller::readSnapshot (dsp.getSnapshotFile(), fromFile, err) && fromFile[2].raw == factory[2],
           "Backup file round-trips the raw records", err);
    dsp.applyChannel (makeChannel (5), err);
    check (dsp.restoreSnapshot (dsp.getSnapshotFile(), err) && sameBlocks (fake->records[5], factory[5]),
           "restoreSnapshot(file) restores from the backup", err);

    check (dsp.getMappingReport().size() > 0, "Mapping report lists quantisation residuals",
           dsp.getMappingReport().joinIntoString (" | ").substring (0, 160));
    dir.deleteRecursively();
}

FA_TEST (dsp408RefusesUnknownDevice)
{
    auto fakeOwned = std::make_unique<FakeDsp408>();
    auto* fake = fakeOwned.get();
    fake->identity = "STM32 HID demo";
    Dsp408Controller::Options opts;
    opts.backupDir = makeTempDir ("unknown");
    opts.ioTimeoutMs = 200;
    Dsp408Controller dsp (std::move (fakeOwned), opts);

    juce::String err;
    check (! dsp.connect (err) && fake->writesReceived == 0, "A non-DSP-408 on 0483:5750 is refused", err);
    check (! dsp.applyChannel (makeChannel (0), err), "No writes without a connection", err);
    opts.backupDir.deleteRecursively();
}
