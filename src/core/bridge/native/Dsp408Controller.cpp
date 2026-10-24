// Dayton Audio DSP-408 native controller.
// Protocol ported from dsp408-py (Copyright (c) 2026 dsp408-py contributors, MIT) and
// dsp408-tuner (Copyright (c) 2026 Nick Bell, MIT). Full notices in Dsp408Controller.h.

#include "Dsp408Controller.h"

#if __has_include("HidDevice.h")
 #include "HidDevice.h"
 #define FA_DSP408_HAS_HID 1
#else
 #define FA_DSP408_HAS_HID 0
#endif

namespace fa
{
namespace dsp408
{
//==============================================================================
// Framing

juce::uint8 checksum (const juce::uint8* frame, size_t payloadLen)
{
    juce::uint8 c = 0;
    for (size_t i = 4; i < (size_t) headerSize + payloadLen; ++i)
        c ^= frame[i];
    return c;
}

Bytes Frame::encode() const
{
    const auto n = payload.size();
    Bytes b (n + (size_t) frameOverhead, 0);
    b[0] = b[1] = b[2] = 0x80;
    b[3]  = 0xEE;
    b[4]  = type;
    b[5]  = deviceId;
    b[6]  = userId;
    b[7]  = dataType;
    b[8]  = channelId;
    b[9]  = dataId;
    b[10] = aux;
    b[11] = pcCustom;
    b[12] = (juce::uint8) (n & 0xFF);
    b[13] = (juce::uint8) ((n >> 8) & 0xFF);
    std::copy (payload.begin(), payload.end(), b.begin() + headerSize);
    b[(size_t) headerSize + n]     = checksum (b.data(), n);
    b[(size_t) headerSize + n + 1] = 0xAA;
    return b;
}

static bool hasMagic (const juce::uint8* d, size_t size)
{
    return size >= 4 && d[0] == 0x80 && d[1] == 0x80 && d[2] == 0x80 && d[3] == 0xEE;
}

bool Frame::decode (const juce::uint8* d, size_t size, Frame& out, juce::String& error)
{
    if (size < (size_t) frameOverhead)  { error = "frame too short"; return false; }
    if (! hasMagic (d, size))            { error = "bad frame head"; return false; }

    const size_t n = (size_t) d[12] | ((size_t) d[13] << 8);
    if (size < n + (size_t) frameOverhead) { error = "truncated frame"; return false; }
    if (d[headerSize + n + 1] != 0xAA)     { error = "bad frame end marker"; return false; }
    if (d[headerSize + n] != checksum (d, n)) { error = "checksum mismatch"; return false; }

    out.type      = d[4];
    out.deviceId  = d[5];
    out.userId    = d[6];
    out.dataType  = d[7];
    out.channelId = d[8];
    out.dataId    = d[9];
    out.aux       = d[10];
    out.pcCustom  = d[11];
    out.payload.assign (d + headerSize, d + headerSize + n);
    return true;
}

std::vector<Report> toReports (const Bytes& logical)
{
    std::vector<Report> out;
    for (size_t pos = 0; pos < logical.size(); pos += (size_t) reportSize)
    {
        Report r {};
        const auto count = std::min ((size_t) reportSize, logical.size() - pos);
        std::copy (logical.begin() + (std::ptrdiff_t) pos, logical.begin() + (std::ptrdiff_t) (pos + count), r.begin());
        out.push_back (r);
    }
    return out;
}

void Reassembler::reset()
{
    buffer.clear();
    expected = 0;
    checksumOk = false;
}

Reassembler::Result Reassembler::feed (const juce::uint8* report, size_t size)
{
    if (buffer.empty())
    {
        if (size < (size_t) headerSize || ! hasMagic (report, size))
            return Result::ignored;
        expected = ((size_t) report[12] | ((size_t) report[13] << 8)) + (size_t) frameOverhead;
    }

    const auto take = std::min (size, expected - buffer.size());
    buffer.insert (buffer.end(), report, report + take);

    if (buffer.size() < expected)
        return Result::needMore;

    const size_t n = expected - (size_t) frameOverhead;
    frame.type      = buffer[4];
    frame.deviceId  = buffer[5];
    frame.userId    = buffer[6];
    frame.dataType  = buffer[7];
    frame.channelId = buffer[8];
    frame.dataId    = buffer[9];
    frame.aux       = buffer[10];
    frame.pcCustom  = buffer[11];
    frame.payload.assign (buffer.begin() + headerSize, buffer.begin() + headerSize + (std::ptrdiff_t) n);
    checksumOk = buffer[headerSize + n] == checksum (buffer.data(), n) && buffer[headerSize + n + 1] == 0xAA;
    buffer.clear();
    expected = 0;
    return Result::complete;
}

//==============================================================================
// Safety allow-list

static bool isZeroReadPayload (const Bytes& p)
{
    return p.size() <= 8 && std::all_of (p.begin(), p.end(), [] (juce::uint8 b) { return b == 0; });
}

bool isAllowed (const Frame& f, juce::String& reason)
{
    if (f.userId != 0)
    {
        reason = "user_id " + juce::String (f.userId) + " addresses a stored preset slot (a read there recalls the "
                 "preset over the working tune)";
        return false;
    }
    if (f.deviceId != 1 || f.pcCustom != 0)
    {
        reason = "unexpected device_id / pc_custom header bytes";
        return false;
    }
    if (f.type != frameType::read && f.type != frameType::write)
    {
        reason = "not a host frame type";
        return false;
    }

    const auto enc = f.encode();
    if (enc[(size_t) headerSize + f.payload.size()] == 0)
    {
        reason = "checksum computes to zero (the vendor software never sends such frames)";
        return false;
    }

    if (f.dataType == dataType::system)
    {
        if (f.channelId == 96 || f.channelId == 97 || f.channelId == 98)
        {
            reason = "destructive system opcode (RESET_MCU / TRANSMITTAL / RESET_GROUP_DATA)";
            return false;
        }
        if (f.type == frameType::read && f.dataId == 0 && isZeroReadPayload (f.payload)
            && (f.channelId == systemCmd::connect || f.channelId == systemCmd::identity))
            return true;

        reason = "system frame not on the allow-list";
        return false;
    }

    if (f.dataType == dataType::output)
    {
        if (f.channelId >= numOutputs)
        {
            reason = "output channel out of range";
            return false;
        }
        if (f.type == frameType::read)
        {
            if (f.dataId == outputBlock::bulkRead && isZeroReadPayload (f.payload))
                return true;
            reason = "only whole-channel reads (DataID 119) are allowed; other output reads can recall presets";
            return false;
        }
        if (f.payload.size() != (size_t) blockSize)
        {
            reason = "output writes must be exactly one 8-byte block (bulk writes are refused)";
            return false;
        }
        if (f.dataId < numPeqBands || f.dataId == outputBlock::misc || f.dataId == outputBlock::xover
            || f.dataId == outputBlock::mix)
            return true;

        reason = "output block " + juce::String (f.dataId) + " is not on the allow-list";
        return false;
    }

    reason = "data type " + juce::String (f.dataType) + " is not on the allow-list";
    return false;
}

//==============================================================================
// Command builders (USB). The aux byte (header byte 10) follows dsp408-py's USB opcodes:
// EQ band writes are cmd 0x10000 + (band << 8) + ch and crossover writes 0x12000 + ch, so
// byte 10 = 1 for those; MISC (0x1F00 + ch), MIX (0x2100 + ch) and reads carry 0.

static Frame readFrame (juce::uint8 dt, juce::uint8 ch, juce::uint8 id)
{
    Frame f;
    f.type = frameType::read;
    f.dataType = dt;
    f.channelId = ch;
    f.dataId = id;
    f.payload.assign (8, 0);   // the Windows app sends 8 zero bytes with every read
    return f;
}

Frame connectRequest()               { return readFrame (dataType::system, systemCmd::connect, 0); }
Frame identityRequest()              { return readFrame (dataType::system, systemCmd::identity, 0); }
Frame readChannelRequest (int ch)    { return readFrame (dataType::output, (juce::uint8) ch, outputBlock::bulkRead); }

static Frame blockWrite (int ch, juce::uint8 id, juce::uint8 aux, const std::array<juce::uint8, 8>& block)
{
    Frame f;
    f.type = frameType::write;
    f.dataType = dataType::output;
    f.channelId = (juce::uint8) ch;
    f.dataId = id;
    f.aux = aux;
    f.payload.assign (block.begin(), block.end());
    return f;
}

Frame writeEqBand (int ch, int band, const EqBand& b) { return blockWrite (ch, (juce::uint8) band, 1, b.encode()); }
Frame writeMisc   (int ch, const Misc& m)            { return blockWrite (ch, outputBlock::misc, 0, m.encode()); }
Frame writeXover  (int ch, const Xover& x)           { return blockWrite (ch, outputBlock::xover, 1, x.encode()); }
Frame writeMix    (int ch, const Mix& m)             { return blockWrite (ch, outputBlock::mix, 0, m); }

//==============================================================================
// Blocks

static juce::uint16 le16 (const juce::uint8* p) { return (juce::uint16) (p[0] | (p[1] << 8)); }
static juce::uint8 lo (int v) { return (juce::uint8) (v & 0xFF); }
static juce::uint8 hi (int v) { return (juce::uint8) ((v >> 8) & 0xFF); }

std::array<juce::uint8, 8> EqBand::encode() const
{
    return { lo (freqHz), hi (freqHz), lo (levelRaw), hi (levelRaw), lo (bwRaw), hi (bwRaw), shelfDb, type };
}

EqBand EqBand::decode (const juce::uint8* p)
{
    EqBand b;
    b.freqHz = le16 (p);
    b.levelRaw = le16 (p + 2);
    b.bwRaw = le16 (p + 4);
    b.shelfDb = p[6];
    b.type = p[7];
    return b;
}

bool EqBand::isPlausible() const
{
    return freqHz >= 10 && freqHz <= 24000 && levelRaw <= 1200 && bwRaw <= 1000 && type <= eqType::highShelf;
}

std::array<juce::uint8, 8> Misc::encode() const
{
    return { enabled, polarity, lo (gainRaw), hi (gainRaw), lo (delaySamples), hi (delaySamples), eqMode, speakerType };
}

Misc Misc::decode (const juce::uint8* p)
{
    Misc m;
    m.enabled = p[0];
    m.polarity = p[1];
    m.gainRaw = le16 (p + 2);
    m.delaySamples = le16 (p + 4);
    m.eqMode = p[6];
    m.speakerType = p[7];
    return m;
}

std::array<juce::uint8, 8> Xover::encode() const
{
    return { lo (hpFreqHz), hi (hpFreqHz), hpType, hpSlope, lo (lpFreqHz), hi (lpFreqHz), lpType, lpSlope };
}

Xover Xover::decode (const juce::uint8* p)
{
    Xover x;
    x.hpFreqHz = le16 (p);
    x.hpType = p[2];
    x.hpSlope = p[3];
    x.lpFreqHz = le16 (p + 4);
    x.lpType = p[6];
    x.lpSlope = p[7];
    return x;
}

bool ChannelState::parse (const Bytes& blob, ChannelState& out, juce::String& error)
{
    if (blob.size() < (size_t) channelBlobSize)
    {
        error = "channel record is " + juce::String ((int) blob.size()) + " bytes, expected 296";
        return false;
    }

    ChannelState s;
    for (int b = 0; b < numPeqBands; ++b)
        s.eq[(size_t) b] = EqBand::decode (blob.data() + b * blockSize);
    s.misc  = Misc::decode  (blob.data() + outputBlock::misc * blockSize);
    s.xover = Xover::decode (blob.data() + outputBlock::xover * blockSize);
    std::copy_n (blob.begin() + outputBlock::mix * blockSize, blockSize, s.mix.begin());
    s.raw.assign (blob.begin(), blob.begin() + channelBlobSize);

    if (s.misc.enabled > 1 || s.misc.gainRaw > channelGainRawMax || s.misc.polarity > 1)
    {
        error = "channel record looks corrupt (enable/gain/polarity out of range)";
        return false;
    }

    out = std::move (s);
    return true;
}

//==============================================================================
// Scaling

int gainRawFromDb (double db)          { return (int) std::lround (db * 10.0) + unityRaw; }
double dbFromGainRaw (int raw)         { return (raw - unityRaw) / 10.0; }
double octavesFromBwRaw (int bwRaw)    { return (bwRaw + 5) / 100.0; }

double qFromBwRaw (int bwRaw)
{
    return 1.0 / (2.0 * std::sinh (std::log (2.0) / 2.0 * octavesFromBwRaw (bwRaw)));
}

int bwRawFromQ (double q, bool roundUp)
{
    q = juce::jlimit (qFromBwRaw (bwRawMax), qFromBwRaw (bwRawMin), q);
    const double octaves = std::asinh (1.0 / (2.0 * q)) / (std::log (2.0) / 2.0);
    const double x = octaves * 100.0 - 5.0;
    const int raw = roundUp ? (int) std::ceil (x - 1e-9) : (int) std::lround (x);
    return juce::jlimit (bwRawMin, bwRawMax, raw);
}

int delaySamplesFromMs (double ms)
{
    return juce::jlimit (0, maxDelaySamples, (int) std::lround (ms * deviceSampleRate / 1000.0));
}

double msFromDelaySamples (int samples)  { return samples * 1000.0 / deviceSampleRate; }

//==============================================================================
// Mapping FalseAudio <-> device

juce::String Residual::describe() const
{
    auto fmt = [this] (double v) { return juce::String (v, unit == "Hz" ? 1 : 3); };
    const auto err = error();
    return what + ": " + fmt (requested) + " -> " + fmt (achieved) + " " + unit
           + " (" + (err >= 0 ? "+" : "") + fmt (err) + ")";
}

static void addResidual (ChannelPlan& p, const juce::String& what, double requested, double achieved, const juce::String& unit)
{
    if (std::abs (achieved - requested) > 1e-6 * std::max (1.0, std::abs (requested)))
        p.residuals.push_back ({ what, requested, achieved, unit });
}

static const int defaultBandFreqs[numPeqBands] = { 31, 65, 125, 250, 500, 1000, 2000, 4000, 8000, 16000 };

static void mapCrossover (const FilterSpec& spec, bool isHighPass, const juce::uint8 currentType,
                          juce::uint16& freq, juce::uint8& type, juce::uint8& slope, ChannelPlan& p)
{
    const juce::String side = isHighPass ? "HP" : "LP";

    if (! spec.enabled)
    {
        // Real bypass is slope index 8 (flat, measured by dsp408-py). The corner also goes to the band
        // edge, so the filter would be inaudible even on firmware that reads slope 8 differently.
        freq = (juce::uint16) (isHighPass ? minFreqHz : maxFreqHz);
        type = currentType <= xoverType::bessel ? currentType : xoverType::linkwitzRiley;
        slope = slopeBypass;
        return;
    }

    const int f = juce::jlimit (minFreqHz, maxFreqHz, (int) std::lround (spec.freq));
    addResidual (p, side + " frequency", spec.freq, f, "Hz");
    freq = (juce::uint16) f;

    const int index = juce::jlimit (0, 7, (int) std::lround (spec.slopeDbPerOct / 6.0) - 1);
    const int achievedSlope = (index + 1) * 6;
    if (achievedSlope != spec.slopeDbPerOct)
    {
        addResidual (p, side + " slope", spec.slopeDbPerOct, achievedSlope, "dB/oct");
        p.notes.add (side + " slope " + juce::String (spec.slopeDbPerOct) + " dB/oct is not native; written as "
                     + juce::String (achievedSlope) + " dB/oct.");
    }
    slope = (juce::uint8) index;

    if (achievedSlope > 24)
        p.notes.addIfNotAlreadyThere (side + " " + juce::String (achievedSlope) + " dB/oct: the firmware accepts slopes "
                                      "above 24 dB/oct (dsp408-py) but the vendor app stops at 24 and they are not "
                                      "acoustically verified; check the measurement.");

    switch (spec.alignment)
    {
        case Alignment::linkwitzRiley:
            if (achievedSlope % 12 == 0)
            {
                type = xoverType::linkwitzRiley;
            }
            else
            {
                type = xoverType::butterworth;
                p.notes.add (side + " Linkwitz-Riley " + juce::String (achievedSlope) + " dB/oct (odd order) written as "
                             "Butterworth, which is how FalseAudio models odd-order LR; the device's own odd-order LR "
                             "was not used.");
            }
            break;

        case Alignment::bessel:
            type = xoverType::bessel;
            if (achievedSlope > 24)
                p.notes.add (side + " Bessel " + juce::String (achievedSlope) + " dB/oct: FalseAudio models Bessel above "
                             "24 dB/oct as 24 dB/oct, so the predicted response differs from the device.");
            break;

        case Alignment::butterworth:
        default:
            type = xoverType::butterworth;
            break;
    }
}

bool planChannel (const ChannelSettings& s, const ChannelState& current, bool allowShelves,
                  ChannelPlan& out, juce::String& error)
{
    ChannelPlan p;
    p.target = current;
    auto& t = p.target;

    // ---- gain / mute / polarity / delay (MISC block) ----
    {
        const int raw = juce::jlimit (0, channelGainRawMax, gainRawFromDb (s.gainDb));
        addResidual (p, "Gain", s.gainDb, dbFromGainRaw (raw), "dB");
        if (s.gainDb > 0.05)
            p.notes.add ("Channel gain is limited to 0 dB on the DSP-408 (no output boost); "
                         + juce::String (s.gainDb, 1) + " dB written as 0 dB.");
        else if (s.gainDb < -60.0)
            p.notes.add ("Channel gain below -60 dB written as -60 dB.");

        t.misc.gainRaw  = (juce::uint16) raw;
        t.misc.enabled  = s.muted ? 0 : 1;
        t.misc.polarity = s.inverted ? 1 : 0;

        const int samples = delaySamplesFromMs (s.delayMs);
        addResidual (p, "Delay", s.delayMs, msFromDelaySamples (samples), "ms");
        if (s.delayMs > msFromDelaySamples (maxDelaySamples) + 1e-9)
            p.notes.add ("Delay " + juce::String (s.delayMs, 3) + " ms exceeds the device maximum of "
                         + juce::String (msFromDelaySamples (maxDelaySamples), 3) + " ms (359 samples).");
        else if (s.delayMs < 0.0)
            p.notes.add ("Negative delay written as 0 ms.");
        t.misc.delaySamples = (juce::uint16) samples;
    }

    // ---- crossovers ----
    mapCrossover (s.highPass, true,  current.xover.hpType, t.xover.hpFreqHz, t.xover.hpType, t.xover.hpSlope, p);
    mapCrossover (s.lowPass,  false, current.xover.lpType, t.xover.lpFreqHz, t.xover.lpType, t.xover.lpSlope, p);

    // ---- PEQ ----
    std::array<const FilterSpec*, numPeqBands> slots {};
    std::vector<const FilterSpec*> peaks;

    for (auto& f : s.peq)
    {
        if (! f.enabled)
            continue;

        switch (f.type)
        {
            case FilterType::peaking:
                peaks.push_back (&f);
                break;

            case FilterType::lowShelf:
            case FilterType::highShelf:
            {
                if (! allowShelves)
                {
                    error = "the DSP-408 shelf bands are not verified on hardware (their bandwidth meaning is "
                            "unknown); enable shelves explicitly or use peaking bands";
                    return false;
                }
                // The vendor app offers the low shelf only on band 1 and the high shelf only on band 10.
                const size_t slot = f.type == FilterType::lowShelf ? 0 : (size_t) numPeqBands - 1;
                if (slots[slot] != nullptr)
                {
                    error = "only one low shelf and one high shelf fit on the DSP-408";
                    return false;
                }
                slots[slot] = &f;
                p.notes.addIfNotAlreadyThere ("Shelf written with the peaking bandwidth formula; shelf Q on the DSP-408 "
                                              "is unverified, so check the measurement.");
                break;
            }

            case FilterType::allPass:
                error = "the DSP-408 has no all-pass filters";
                return false;

            case FilterType::highPass:
            case FilterType::lowPass:
            default:
                error = "crossover filters in the PEQ list are not supported by the DSP-408 PEQ bands";
                return false;
        }
    }

    size_t next = 0;
    for (auto* f : peaks)
    {
        while (next < slots.size() && slots[next] != nullptr)
            ++next;
        if (next >= slots.size())
        {
            error = juce::String ((int) s.peq.size()) + " PEQ bands requested; the DSP-408 has 10 per output";
            return false;
        }
        slots[next++] = f;
    }

    for (int b = 0; b < numPeqBands; ++b)
    {
        auto& band = t.eq[(size_t) b];
        const auto* f = slots[(size_t) b];

        if (f == nullptr)
        {
            // Unused: flat, but keep where the band sits unless the record holds rubbish.
            if (! band.isPlausible())
            {
                band = EqBand();
                band.freqHz = (juce::uint16) defaultBandFreqs[b];
            }
            band.levelRaw = unityRaw;
            band.type = eqType::peaking;
            band.shelfDb = 0;
            continue;
        }

        const juce::String name = "PEQ " + juce::String (b + 1);
        const int freq = juce::jlimit (minFreqHz, maxFreqHz, (int) std::lround (f->freq));
        const int level = juce::jlimit (eqLevelRawMin, eqLevelRawMax, gainRawFromDb (f->gainDb));
        const int bw = bwRawFromQ (f->q);

        addResidual (p, name + " frequency", f->freq, freq, "Hz");
        addResidual (p, name + " gain", f->gainDb, dbFromGainRaw (level), "dB");
        addResidual (p, name + " Q", f->q, qFromBwRaw (bw), "");
        if (std::abs (f->gainDb) > 12.05)
            p.notes.add (name + " gain " + juce::String (f->gainDb, 1) + " dB clipped to the device's +/-12 dB.");

        band.freqHz = (juce::uint16) freq;
        band.levelRaw = (juce::uint16) level;
        band.bwRaw = (juce::uint16) bw;
        band.shelfDb = 0;
        band.type = f->type == FilterType::lowShelf  ? eqType::lowShelf
                  : f->type == FilterType::highShelf ? eqType::highShelf
                                                     : eqType::peaking;
    }

    out = std::move (p);
    return true;
}

static void toFilter (const juce::uint16 freq, const juce::uint8 type, const juce::uint8 slope, FilterSpec& f)
{
    f.freq = freq;
    f.enabled = slope < slopeBypass && type != xoverType::defeat;
    f.slopeDbPerOct = (juce::jlimit (0, 7, (int) slope) + 1) * 6;
    f.alignment = type == xoverType::butterworth ? Alignment::butterworth
                : type == xoverType::bessel      ? Alignment::bessel
                                                 : Alignment::linkwitzRiley;
}

ChannelSettings toChannelSettings (const ChannelState& st)
{
    ChannelSettings s;
    s.gainDb = dbFromGainRaw (st.misc.gainRaw);
    s.delayMs = msFromDelaySamples (st.misc.delaySamples);
    s.inverted = st.misc.polarity != 0;
    s.muted = st.misc.enabled == 0;
    toFilter (st.xover.hpFreqHz, st.xover.hpType, st.xover.hpSlope, s.highPass);
    toFilter (st.xover.lpFreqHz, st.xover.lpType, st.xover.lpSlope, s.lowPass);

    for (auto& b : st.eq)
    {
        if (b.levelRaw == unityRaw || ! b.isPlausible())
            continue;
        auto f = FilterSpec::peq (b.freqHz, qFromBwRaw (juce::jlimit (bwRawMin, bwRawMax, (int) b.bwRaw)),
                                  dbFromGainRaw (b.levelRaw));
        if (b.type == eqType::lowShelf)  f.type = FilterType::lowShelf;
        if (b.type == eqType::highShelf) f.type = FilterType::highShelf;
        s.peq.push_back (f);
    }
    return s;
}

DspCapabilities capabilities()
{
    DspCapabilities c;
    c.sampleRate = deviceSampleRate;
    c.peqBandsPerChannel = numPeqBands;
    c.peqMinQ = qFromBwRaw (bwRawMax);          // 0.404
    c.peqMaxQ = qFromBwRaw (bwRawMin);          // 28.85
    c.peqMaxGainDb = 12.0;
    c.peqMinGainDb = -12.0;
    c.delayStepMs = 1000.0 / deviceSampleRate;  // one sample
    c.maxDelayMs = msFromDelaySamples (maxDelaySamples);
    c.gainStepDb = 0.1;
    c.minChannelGainDb = -60.0;
    c.maxChannelGainDb = 0.0;
    c.freqStepHz = 0.0;                         // 1 Hz resolution: effectively continuous
    c.availableSlopes = { 6, 12, 18, 24, 30, 36, 42, 48 };
    c.hasLinkwitzRiley = true;
    c.hasBessel = true;
    c.hasAllPass = false;
    return c;
}

//==============================================================================
// HID transport over the shared HidDevice

#if FA_DSP408_HAS_HID
class HidTransport : public Transport
{
public:
    explicit HidTransport (juce::String p) : path (std::move (p)) {}
    ~HidTransport() override { close(); }

    bool open (juce::String& error) override
    {
        juce::String why;
        if (device.open (path, why))
            return true;
        error = "Could not open the DSP-408 USB device (" + why + "). Close the vendor app and retry.";
        return false;
    }

    void close() override        { device.close(); }
    bool isOpen() const override { return device.isOpen(); }

    bool writeReport (const Report& r, int timeoutMs) override
    {
        // Unnumbered reports: report ID 0 followed by the 64-byte report (dsp408-py transport).
        juce::String error;
        return device.write (0, r.data(), r.size(), 0, timeoutMs, error);
    }

    int readReport (Report& r, int timeoutMs) override
    {
        std::vector<uint8_t> in;
        juce::String error;
        if (! device.read (in, timeoutMs, error))
            return error.isEmpty() ? 0 : -1;   // empty error = timeout
        r.fill (0);
        const auto n = std::min (in.size(), r.size());
        std::copy_n (in.begin(), n, r.begin());
        return (int) n;
    }

private:
    juce::String path;
    hid::HidDevice device;
};
#endif

class UnavailableTransport : public Transport
{
public:
    bool open (juce::String& error) override { error = "USB HID support is not available in this build."; return false; }
    void close() override {}
    bool isOpen() const override { return false; }
    bool writeReport (const Report&, int) override { return false; }
    int readReport (Report&, int) override { return -1; }
};

std::unique_ptr<Transport> makeHidTransport (const juce::String& devicePath)
{
   #if FA_DSP408_HAS_HID
    return std::make_unique<HidTransport> (devicePath);
   #else
    juce::ignoreUnused (devicePath);
    return std::make_unique<UnavailableTransport>();
   #endif
}
} // namespace dsp408

//==============================================================================
// Controller

using namespace dsp408;

Dsp408Controller::Dsp408Controller (Options o)
    : options (std::move (o))
{
}

Dsp408Controller::Dsp408Controller (std::unique_ptr<Transport> t, Options o)
    : options (std::move (o)), transport (std::move (t))
{
}

Dsp408Controller::~Dsp408Controller()
{
    disconnect();
}

std::vector<Dsp408Controller::DeviceInfo> Dsp408Controller::detect (bool probe)
{
    std::vector<DeviceInfo> out;
   #if FA_DSP408_HAS_HID
    for (auto& info : hid::enumerate (usbVendorId, usbProductId))
    {
        DeviceInfo d;
        d.path = info.path;
        d.serial = info.serial;
        d.product = info.product;
        d.manufacturer = info.manufacturer;

        if (probe)
        {
            Options o;
            o.devicePath = d.path;
            Dsp408Controller c (makeHidTransport (d.path), o);
            juce::String error;
            if (! c.handshake (error))
                continue;
            d.identity = c.identity;
            c.disconnect();
        }
        out.push_back (d);
    }
   #else
    juce::ignoreUnused (probe);
   #endif
    return out;
}

bool Dsp408Controller::isConnected() const
{
    const juce::ScopedLock sl (lock);
    return connected && transport != nullptr && transport->isOpen();
}

void Dsp408Controller::disconnect()
{
    const juce::ScopedLock sl (lock);
    if (transport != nullptr)
        transport->close();
    connected = false;
    batching = false;
    pending.clear();
}

bool Dsp408Controller::exchange (const Frame& request, juce::uint8 expectedReply, Frame& reply, juce::String& error)
{
    juce::String why;
    if (! isAllowed (request, why))
    {
        error = "Refused to send a DSP-408 frame: " + why + ".";
        return false;
    }
    if (transport == nullptr || ! transport->isOpen())
    {
        error = "The DSP-408 is not connected.";
        return false;
    }

    for (auto& r : toReports (request.encode()))
        if (! transport->writeReport (r, options.ioTimeoutMs))
        {
            error = "USB write to the DSP-408 failed.";
            return false;
        }

    Reassembler ra;
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + options.ioTimeoutMs;

    while (juce::Time::getMillisecondCounterHiRes() < deadline)
    {
        const int remaining = (int) (deadline - juce::Time::getMillisecondCounterHiRes());
        Report in {};
        const int n = transport->readReport (in, juce::jlimit (1, 200, remaining));
        if (n < 0)
        {
            error = "USB read from the DSP-408 failed.";
            return false;
        }
        if (n == 0 || ra.feed (in.data(), (size_t) n) != Reassembler::Result::complete)
            continue;

        const auto& f = ra.frame;
        if (f.dataType != request.dataType || f.channelId != request.channelId || f.dataId != request.dataId)
            continue;   // a late reply to an earlier request

        if (f.type == frameType::nak)
        {
            error = "The DSP-408 rejected a frame (data type " + juce::String (request.dataType) + ", channel "
                    + juce::String (request.channelId) + ", block " + juce::String (request.dataId) + ").";
            return false;
        }
        if (f.type != expectedReply)
        {
            error = "Unexpected reply type from the DSP-408.";
            return false;
        }
        // Reply checksums are recorded but not enforced: dsp408-py never validated them on USB.
        // Channel records are instead read twice and must agree (readChannelStable).
        reply = f;
        return true;
    }

    error = "The DSP-408 did not answer (timeout).";
    return false;
}

bool Dsp408Controller::handshake (juce::String& error)
{
    if (transport == nullptr)
    {
        auto path = options.devicePath;
        if (path.isEmpty())
        {
            const auto found = detect (false);
            if (found.empty())
            {
                error = "No Dayton Audio DSP-408 found on USB (0483:5750). Check the cable and close the vendor app.";
                return false;
            }
            path = found.front().path;
            serial = found.front().serial;
        }
        transport = makeHidTransport (path);
    }

    if (! transport->isOpen() && ! transport->open (error))
        return false;

    // Drop anything left over from a previous session.
    Report junk {};
    for (int i = 0; i < 32 && transport->readReport (junk, 5) > 0; ++i) {}

    Frame reply;
    if (! exchange (connectRequest(), frameType::data, reply, error))
        return false;
    if (reply.payload.empty() || reply.payload[0] != 0)
    {
        error = "The DSP-408 refused the session (connect status "
                + juce::String (reply.payload.empty() ? -1 : (int) reply.payload[0]) + "). Power-cycle it and retry.";
        return false;
    }

    if (! exchange (identityRequest(), frameType::data, reply, error))
        return false;

    juce::String id;
    for (auto b : reply.payload)
        if (b >= 0x20 && b < 0x7F)
            id += juce::String::charToString ((juce::juce_wchar) b);
    identity = id.trim();

    if (! identity.startsWith ("MYDW-AV"))
    {
        error = "The USB device answered \"" + identity + "\", which is not a DSP-408 firmware; nothing will be written to it.";
        transport->close();
        return false;
    }
    return true;
}

bool Dsp408Controller::readChannelStable (int output, ChannelState& out, juce::String& error)
{
    Bytes previous;
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        Frame reply;
        if (! exchange (readChannelRequest (output), frameType::data, reply, error))
            return false;

        if (reply.payload.size() != (size_t) channelBlobSize)
        {
            error = "DSP-408 output " + juce::String (output + 1) + " returned "
                    + juce::String ((int) reply.payload.size()) + " bytes instead of 296.";
            previous.clear();
            continue;
        }
        if (! previous.empty() && previous == reply.payload)
            return ChannelState::parse (reply.payload, out, error);
        previous = reply.payload;
    }

    if (error.isEmpty())
        error = "DSP-408 output " + juce::String (output + 1) + " gave inconsistent reads.";
    return false;
}

bool Dsp408Controller::connect (juce::String& error)
{
    const juce::ScopedLock sl (lock);
    if (connected && transport != nullptr && transport->isOpen())
        return true;

    if (! handshake (error))
        return false;

    // Reading every output also drains the firmware's start-up write-drop quirk (dsp408-py).
    for (int ch = 0; ch < numOutputs; ++ch)
        if (! readChannelStable (ch, current[(size_t) ch], error))
            return false;

    original = current;
    connected = true;
    snapshotTaken = false;
    snapshotFile = juce::File();
    batching = false;
    pending.clear();
    mappingReport.clear();
    blocksWritten = 0;
    return true;
}

bool Dsp408Controller::refresh (juce::String& error)
{
    const juce::ScopedLock sl (lock);
    if (! connected)
    {
        error = "The DSP-408 is not connected.";
        return false;
    }
    for (int ch = 0; ch < numOutputs; ++ch)
        if (! readChannelStable (ch, current[(size_t) ch], error))
            return false;
    if (! snapshotTaken)
        original = current;
    return true;
}

bool Dsp408Controller::ensureSnapshot (juce::String& error)
{
    if (snapshotTaken)
        return true;

    auto dir = options.backupDir;
    if (dir == juce::File())
        dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                  .getChildFile ("FalseAudio").getChildFile ("backups");

    if (! dir.createDirectory())
    {
        error = "Could not create the backup folder " + dir.getFullPathName() + "; nothing was written to the DSP-408.";
        return false;
    }

    auto tag = serial.retainCharacters ("0123456789ABCDEFabcdefghijklmnopqrstuvwxyzGHIJKLMNOPQRSTUVWXYZ-_");
    if (tag.isEmpty())
        tag = "usb";
    auto file = dir.getChildFile ("DSP-408_" + tag + "_" + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S") + ".json")
                   .getNonexistentSibling (false);

    original = current;   // the state just before the first write
    if (! writeSnapshot (file, identity, serial, original, error))
    {
        error = "Could not save the DSP-408 backup (" + error + "); nothing was written.";
        return false;
    }

    snapshotFile = file;
    snapshotTaken = true;
    return true;
}

static void avoidZeroChecksums (int output, ChannelState& t, const ChannelState& cur, juce::StringArray& notes)
{
    // The vendor software never sends a frame whose checksum is zero, so neither do we: nudge the least
    // significant field of such a block by one step.
    auto zero = [] (const Frame& f) { const auto e = f.encode(); return e[(size_t) headerSize + f.payload.size()] == 0; };
    const auto label = "Output " + juce::String (output + 1) + ": ";

    for (int b = 0; b < numPeqBands; ++b)
    {
        auto& band = t.eq[(size_t) b];
        if (band == cur.eq[(size_t) b])
            continue;
        while (zero (writeEqBand (output, b, band)))
        {
            band.freqHz = (juce::uint16) (band.freqHz < maxFreqHz ? band.freqHz + 1 : band.freqHz - 1);
            notes.add (label + "PEQ " + juce::String (b + 1) + " moved 1 Hz to avoid a zero checksum.");
        }
    }
    if (! (t.misc == cur.misc))
        while (zero (writeMisc (output, t.misc)))
        {
            t.misc.gainRaw = (juce::uint16) (t.misc.gainRaw > 0 ? t.misc.gainRaw - 1 : t.misc.gainRaw + 1);
            notes.add (label + "gain moved 0.1 dB to avoid a zero checksum.");
        }
    if (! (t.xover == cur.xover))
        while (zero (writeXover (output, t.xover)))
        {
            t.xover.hpFreqHz = (juce::uint16) (t.xover.hpFreqHz < maxFreqHz ? t.xover.hpFreqHz + 1 : t.xover.hpFreqHz - 1);
            notes.add (label + "HP moved 1 Hz to avoid a zero checksum.");
        }
}

bool Dsp408Controller::writeChannel (int output, const ChannelState& target, bool includeMix, juce::String& error)
{
    if (! ensureSnapshot (error))
        return false;

    auto t = target;
    juce::StringArray notes;
    avoidZeroChecksums (output, t, current[(size_t) output], notes);
    for (auto& n : notes)
        mappingReport.addIfNotAlreadyThere (n);

    for (int attempt = 0; attempt <= options.verifyRetries; ++attempt)
    {
        const auto& cur = current[(size_t) output];
        std::vector<Frame> frames;
        for (int b = 0; b < numPeqBands; ++b)
            if (! (cur.eq[(size_t) b] == t.eq[(size_t) b]))
                frames.push_back (writeEqBand (output, b, t.eq[(size_t) b]));
        if (! (cur.misc == t.misc))
            frames.push_back (writeMisc (output, t.misc));
        if (! (cur.xover == t.xover))
            frames.push_back (writeXover (output, t.xover));
        if (includeMix && cur.mix != t.mix)
            frames.push_back (writeMix (output, t.mix));

        if (frames.empty())
            return true;   // the device holds the target

        for (auto& f : frames)
        {
            Frame ack;
            if (! exchange (f, frameType::ack, ack, error))
                return false;
            ++blocksWritten;
        }

        // Read back: the firmware can ACK a write and still drop it (dsp408-py, rapid-write quirk).
        ChannelState back;
        if (! readChannelStable (output, back, error))
            return false;
        current[(size_t) output] = back;
    }

    error = "DSP-408 output " + juce::String (output + 1) + " did not keep the written values after "
            + juce::String (options.verifyRetries + 1) + " attempts.";
    return false;
}

ChannelState Dsp408Controller::stateFor (int output) const
{
    if (batching)
        if (auto it = pending.find (output); it != pending.end())
            return it->second;
    return current[(size_t) output];
}

bool Dsp408Controller::queueOrWrite (int output, const ChannelState& target, juce::String& error)
{
    if (batching)
    {
        pending[output] = target;
        return true;
    }
    return writeChannel (output, target, false, error);
}

static bool checkOutput (const OutputChannel& ch, juce::String& error)
{
    if (ch.dspOutput < 0 || ch.dspOutput >= numOutputs)
    {
        error = ch.label + ": DSP output " + juce::String (ch.dspOutput + 1) + " does not exist on the DSP-408 (1-8).";
        return false;
    }
    return true;
}

bool Dsp408Controller::applyChannel (const OutputChannel& ch, juce::String& error)
{
    const juce::ScopedLock sl (lock);
    if (! connected)
    {
        error = "The DSP-408 is not connected.";
        return false;
    }
    if (! checkOutput (ch, error))
        return false;

    dsp408::ChannelPlan plan;   // (fa::ChannelPlan is the install planner's)
    if (! planChannel (ch.settings, stateFor (ch.dspOutput), options.allowShelves, plan, error))
    {
        error = ch.label + ": " + error + ".";
        return false;
    }

    const auto prefix = ch.label + " (out " + juce::String (ch.dspOutput + 1) + "): ";
    for (auto& r : plan.residuals)
        mappingReport.addIfNotAlreadyThere (prefix + r.describe());
    for (auto& n : plan.notes)
        mappingReport.addIfNotAlreadyThere (prefix + n);

    return queueOrWrite (ch.dspOutput, plan.target, error);
}

bool Dsp408Controller::setMuted (const OutputChannel& ch, bool muted, juce::String& error)
{
    const juce::ScopedLock sl (lock);
    if (! connected)
    {
        error = "The DSP-408 is not connected.";
        return false;
    }
    if (! checkOutput (ch, error))
        return false;

    auto t = stateFor (ch.dspOutput);
    t.misc.enabled = muted ? 0 : 1;
    return queueOrWrite (ch.dspOutput, t, error);
}

bool Dsp408Controller::beginBatch (juce::String&)
{
    const juce::ScopedLock sl (lock);
    batching = true;
    pending.clear();
    return true;
}

bool Dsp408Controller::endBatch (juce::String& error)
{
    const juce::ScopedLock sl (lock);
    batching = false;
    auto work = std::move (pending);
    pending.clear();

    for (auto& [output, target] : work)
        if (! writeChannel (output, target, false, error))
            return false;
    return true;
}

static ChannelState restorable (const ChannelState& wanted, const ChannelState& now)
{
    // Never write a band we could not make sense of (uninitialised 0xAA fill, layout doubt on bands 6-9).
    auto t = wanted;
    for (size_t b = 0; b < t.eq.size(); ++b)
        if (! t.eq[b].isPlausible())
            t.eq[b] = now.eq[b];
    return t;
}

bool Dsp408Controller::restoreSnapshot (juce::String& error)
{
    const juce::ScopedLock sl (lock);
    if (! connected)
    {
        error = "The DSP-408 is not connected.";
        return false;
    }
    if (! snapshotTaken)
        return true;   // nothing has been written since connect()

    batching = false;
    pending.clear();
    for (int ch = 0; ch < numOutputs; ++ch)
        if (! writeChannel (ch, restorable (original[(size_t) ch], current[(size_t) ch]), true, error))
            return false;
    return true;
}

bool Dsp408Controller::restoreSnapshot (const juce::File& file, juce::String& error)
{
    const juce::ScopedLock sl (lock);
    if (! connected)
    {
        error = "The DSP-408 is not connected.";
        return false;
    }

    std::array<ChannelState, numOutputs> states;
    if (! readSnapshot (file, states, error))
        return false;

    batching = false;
    pending.clear();
    for (int ch = 0; ch < numOutputs; ++ch)
        if (! writeChannel (ch, restorable (states[(size_t) ch], current[(size_t) ch]), true, error))
            return false;
    return true;
}

const ChannelState* Dsp408Controller::getChannelState (int output) const
{
    return output >= 0 && output < numOutputs ? &current[(size_t) output] : nullptr;
}

const ChannelState* Dsp408Controller::getOriginalState (int output) const
{
    return output >= 0 && output < numOutputs ? &original[(size_t) output] : nullptr;
}

juce::StringArray Dsp408Controller::getMappingReport() const
{
    const juce::ScopedLock sl (lock);
    return mappingReport;
}

//==============================================================================
// Snapshot files

static const char* snapshotFormat = "FalseAudio DSP-408 snapshot";

bool Dsp408Controller::writeSnapshot (const juce::File& file, const juce::String& identity, const juce::String& serial,
                                      const std::array<ChannelState, numOutputs>& states, juce::String& error)
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("format", snapshotFormat);
    root->setProperty ("version", 1);
    root->setProperty ("model", "Dayton Audio DSP-408");
    root->setProperty ("identity", identity);
    root->setProperty ("serial", serial);
    root->setProperty ("created", juce::Time::getCurrentTime().toISO8601 (true));
    root->setProperty ("note", "Every DSP-408 write is permanent. 'record' is the raw 296-byte output record; "
                               "FalseAudio restores PEQ bands 1-10, gain/mute/polarity/delay, crossovers and mixer from it.");

    juce::Array<juce::var> channels;
    for (int ch = 0; ch < numOutputs; ++ch)
    {
        const auto& st = states[(size_t) ch];
        auto* c = new juce::DynamicObject();
        c->setProperty ("output", ch + 1);
        c->setProperty ("record", juce::String::toHexString (st.raw.data(), (int) st.raw.size(), 0));
        c->setProperty ("settings", toChannelSettings (st).toVar());
        channels.add (juce::var (c));
    }
    root->setProperty ("channels", channels);

    if (! file.replaceWithText (juce::JSON::toString (juce::var (root))))
    {
        error = "could not write " + file.getFullPathName();
        return false;
    }
    return true;
}

bool Dsp408Controller::readSnapshot (const juce::File& file, std::array<ChannelState, numOutputs>& states, juce::String& error)
{
    const auto v = juce::JSON::parse (file);
    if (v.getProperty ("format", {}).toString() != snapshotFormat)
    {
        error = file.getFileName() + " is not a FalseAudio DSP-408 snapshot.";
        return false;
    }

    const auto* channels = v.getProperty ("channels", {}).getArray();
    if (channels == nullptr || channels->size() != numOutputs)
    {
        error = file.getFileName() + " does not hold 8 output records.";
        return false;
    }

    for (auto& c : *channels)
    {
        const int output = (int) c.getProperty ("output", 0) - 1;
        if (output < 0 || output >= numOutputs)
        {
            error = file.getFileName() + " has an invalid output number.";
            return false;
        }
        juce::MemoryBlock mb;
        mb.loadFromHexString (c.getProperty ("record", {}).toString());
        const auto* p = static_cast<const juce::uint8*> (mb.getData());
        const Bytes blob (p, p + mb.getSize());
        if (! ChannelState::parse (blob, states[(size_t) output], error))
        {
            error = file.getFileName() + ", output " + juce::String (output + 1) + ": " + error;
            return false;
        }
    }
    return true;
}
} // namespace fa
