// Native miniDSP controller over USB HID.
//
// Protocol, value encodings and the device memory maps below are ported from minidsp-rs
// (https://github.com/mrene/minidsp-rs, Apache License 2.0, (c) Mathieu Rene and contributors):
//   protocol/src/packet.rs       framing + checksum
//   protocol/src/commands.rs     command ids, address encoding, value encoding, responses
//   protocol/src/dialect.rs      per-device value dialects (float vs fixed point, 2/3-byte addresses)
//   protocol/src/fixed_point.rs  5.23 fixed point codec
//   protocol/src/device/*.rs     memory maps (generated from the vendor's XML configs)
//   minidsp/src/lib.rs           how gain/mute/delay/invert/PEQ/crossover are written
// Licensed under the Apache License, Version 2.0: http://www.apache.org/licenses/LICENSE-2.0
// Changes: re-implemented in C++, synchronous transport, snapshot/backup and write-cache added.

#include "MiniDspController.h"
#include "HidDevice.h"

#include <cstring>
#include <set>

namespace fa::minidsp
{
//==============================================================================
// Device tables. Generated from minidsp-rs protocol/src/device/*.rs (branch "dev", 2026-10) by
// resolving each symbol to its numeric address. Field order: enable, gain, delay, invert,
// { PEQ slots }, numPeq, { crossover group starts }, numXover.
namespace
{
// 2x4HD (minidsp-rs protocol/src/device/m2x4hd.rs)
static const OutputSpec kM2x4HdOutputs[] = {
    { 2, 28, 64, 80, { 8425, 8420, 8415, 8410, 8405, 8400, 8395, 8390, 8385, 8380 }, 10, { 8580, 8600 }, 2 },
    { 3, 29, 65, 81, { 8475, 8470, 8465, 8460, 8455, 8450, 8445, 8440, 8435, 8430 }, 10, { 8620, 8640 }, 2 },
    { 4, 30, 66, 82, { 8525, 8520, 8515, 8510, 8505, 8500, 8495, 8490, 8485, 8480 }, 10, { 8660, 8680 }, 2 },
    { 5, 31, 67, 83, { 8575, 8570, 8565, 8560, 8555, 8550, 8545, 8540, 8535, 8530 }, 10, { 8700, 8720 }, 2 },
};

// Flex (minidsp-rs protocol/src/device/flex.rs)
static const OutputSpec kFlexOutputs[] = {
    { 2, 28, 64, 80, { 8425, 8420, 8415, 8410, 8405, 8400, 8395, 8390, 8385, 8380 }, 10, { 8580, 8600 }, 2 },
    { 3, 29, 65, 81, { 8475, 8470, 8465, 8460, 8455, 8450, 8445, 8440, 8435, 8430 }, 10, { 8620, 8640 }, 2 },
    { 4, 30, 66, 82, { 8525, 8520, 8515, 8510, 8505, 8500, 8495, 8490, 8485, 8480 }, 10, { 8660, 8680 }, 2 },
    { 5, 31, 67, 83, { 8575, 8570, 8565, 8560, 8555, 8550, 8545, 8540, 8535, 8530 }, 10, { 8700, 8720 }, 2 },
};

// MiniDSP C-DSP 8x12 v2 (minidsp-rs protocol/src/device/c8x12v2.rs)
static const OutputSpec kC8x12v2Outputs[] = {
    { 752, 754, 761, 764, { 810, 805, 800, 795, 790, 785, 780, 775, 770, 765 }, 10, { 815, 835 }, 2 },
    { 855, 857, 864, 867, { 913, 908, 903, 898, 893, 888, 883, 878, 873, 868 }, 10, { 918, 938 }, 2 },
    { 958, 960, 967, 970, { 1016, 1011, 1006, 1001, 996, 991, 986, 981, 976, 971 }, 10, { 1021, 1041 }, 2 },
    { 1061, 1063, 1070, 1073, { 1119, 1114, 1109, 1104, 1099, 1094, 1089, 1084, 1079, 1074 }, 10, { 1124, 1144 }, 2 },
    { 1164, 1166, 1173, 1176, { 1222, 1217, 1212, 1207, 1202, 1197, 1192, 1187, 1182, 1177 }, 10, { 1227, 1247 }, 2 },
    { 1267, 1269, 1276, 1279, { 1325, 1320, 1315, 1310, 1305, 1300, 1295, 1290, 1285, 1280 }, 10, { 1330, 1350 }, 2 },
    { 1370, 1372, 1379, 1382, { 1428, 1423, 1418, 1413, 1408, 1403, 1398, 1393, 1388, 1383 }, 10, { 1433, 1453 }, 2 },
    { 1473, 1475, 1482, 1485, { 1531, 1526, 1521, 1516, 1511, 1506, 1501, 1496, 1491, 1486 }, 10, { 1536, 1556 }, 2 },
    { 1576, 1578, 1585, 1588, { 1634, 1629, 1624, 1619, 1614, 1609, 1604, 1599, 1594, 1589 }, 10, { 1639, 1659 }, 2 },
    { 1679, 1681, 1688, 1691, { 1737, 1732, 1727, 1722, 1717, 1712, 1707, 1702, 1697, 1692 }, 10, { 1742, 1762 }, 2 },
    { 1782, 1784, 1791, 1794, { 1840, 1835, 1830, 1825, 1820, 1815, 1810, 1805, 1800, 1795 }, 10, { 1845, 1865 }, 2 },
    { 1885, 1887, 1894, 1897, { 1943, 1938, 1933, 1928, 1923, 1918, 1913, 1908, 1903, 1898 }, 10, { 1948, 1968 }, 2 },
};

// DDRC-24 (minidsp-rs protocol/src/device/ddrc24.rs)
static const OutputSpec kDDRC24Outputs[] = {
    { 32, 34, 41, 45, { 91, 86, 81, 76, 71, 66, 61, 56, 51, 46 }, 10, { 96, 116 }, 2 },
    { 136, 138, 145, 149, { 195, 190, 185, 180, 175, 170, 165, 160, 155, 150 }, 10, { 200, 220 }, 2 },
    { 240, 242, 249, 253, { 299, 294, 289, 284, 279, 274, 269, 264, 259, 254 }, 10, { 304, 324 }, 2 },
    { 344, 346, 353, 357, { 403, 398, 393, 388, 383, 378, 373, 368, 363, 358 }, 10, { 408, 428 }, 2 },
};

// DDRC-88BM (minidsp-rs protocol/src/device/ddrc88bm.rs)
static const OutputSpec kDDRC88BMOutputs[] = {
    { 472, 474, 481, 485, { 531, 526, 521, 516, 511, 506, 501, 496, 491, 486 }, 10, { 536, 556 }, 2 },
    { 576, 578, 585, 589, { 635, 630, 625, 620, 615, 610, 605, 600, 595, 590 }, 10, { 640, 660 }, 2 },
    { 680, 682, 689, 693, { 739, 734, 729, 724, 719, 714, 709, 704, 699, 694 }, 10, { 744, 764 }, 2 },
    { 784, 786, 793, 797, { 843, 838, 833, 828, 823, 818, 813, 808, 803, 798 }, 10, { 848, 868 }, 2 },
    { 888, 890, 897, 901, { 947, 942, 937, 932, 927, 922, 917, 912, 907, 902 }, 10, { 952, 972 }, 2 },
    { 992, 994, 1001, 1005, { 1051, 1046, 1041, 1036, 1031, 1026, 1021, 1016, 1011, 1006 }, 10, { 1056, 1076 }, 2 },
    { 1096, 1098, 1105, 1109, { 1155, 1150, 1145, 1140, 1135, 1130, 1125, 1120, 1115, 1110 }, 10, { 1160, 1180 }, 2 },
    { 1200, 1202, 1209, 1213, { 1259, 1254, 1249, 1244, 1239, 1234, 1229, 1224, 1219, 1214 }, 10, { 1264, 1284 }, 2 },
};

// MiniDSP SHD (minidsp-rs protocol/src/device/shd.rs)
static const OutputSpec kShdOutputs[] = {
    { 32, 34, 41, 45, { 91, 86, 81, 76, 71, 66, 61, 56, 51, 46 }, 10, { 96, 116 }, 2 },
    { 136, 138, 145, 149, { 195, 190, 185, 180, 175, 170, 165, 160, 155, 150 }, 10, { 200, 220 }, 2 },
    { 240, 242, 249, 253, { 299, 294, 289, 284, 279, 274, 269, 264, 259, 254 }, 10, { 304, 324 }, 2 },
    { 344, 346, 353, 357, { 403, 398, 393, 388, 383, 378, 373, 368, 363, 358 }, 10, { 408, 428 }, 2 },
};

// NanoDigi 2x8 (minidsp-rs protocol/src/device/nanodigi2x8.rs)
static const OutputSpec kNanodigi2x8Outputs[] = {
    { 879, 896, 859, 869, { 829, 824, 819, 814, 809 }, 5, { 569, 589 }, 2 },
    { 880, 895, 866, 877, { 629, 624, 619, 614, 609 }, 5, { 529, 549 }, 2 },
    { 881, 894, 860, 878, { 654, 649, 644, 639, 634 }, 5, { 209, 229 }, 2 },
    { 888, 889, 861, 876, { 854, 849, 844, 839, 834 }, 5, { 489, 509 }, 2 },
    { 882, 890, 862, 875, { 679, 674, 669, 664, 659 }, 5, { 249, 269 }, 2 },
    { 883, 891, 863, 874, { 704, 699, 694, 689, 684 }, 5, { 289, 309 }, 2 },
    { 884, 892, 864, 873, { 729, 724, 719, 714, 709 }, 5, { 329, 349 }, 2 },
    { 885, 893, 865, 872, { 754, 749, 744, 739, 734 }, 5, { 369, 389 }, 2 },
};

// MiniDSP 4x10HD (minidsp-rs protocol/src/device/m4x10hd.rs)
static const OutputSpec kM4x10HdOutputs[] = {
    { 879, 896, 859, 869, { 829, 824, 819, 814, 809 }, 5, { 569, 589 }, 2 },
    { 880, 895, 866, 877, { 629, 624, 619, 614, 609 }, 5, { 529, 549 }, 2 },
    { 881, 894, 860, 878, { 654, 649, 644, 639, 634 }, 5, { 209, 229 }, 2 },
    { 888, 889, 861, 876, { 854, 849, 844, 839, 834 }, 5, { 489, 509 }, 2 },
    { 882, 890, 862, 875, { 679, 674, 669, 664, 659 }, 5, { 249, 269 }, 2 },
    { 883, 891, 863, 874, { 704, 699, 694, 689, 684 }, 5, { 289, 309 }, 2 },
    { 884, 892, 864, 873, { 729, 724, 719, 714, 709 }, 5, { 329, 349 }, 2 },
    { 885, 893, 865, 872, { 754, 749, 744, 739, 734 }, 5, { 369, 389 }, 2 },
    { 870, 886, -1, 867, { 779, 774, 769, 764, 759 }, 5, { 409, 429 }, 2 },
    { 871, 887, -1, 868, { 804, 799, 794, 789, 784 }, 5, { 449, 469 }, 2 },
};

// MiniDSP 10x10HD (minidsp-rs protocol/src/device/m10x10hd.rs)
static const OutputSpec kM10x10HdOutputs[] = {
    { 1214, 1224, 1194, 1204, { 1154, 1149, 1144, 1139, 1134 }, 5, { 494, 514 }, 2 },
    { 1215, 1225, 1203, 1212, { 914, 909, 904, 899, 894 }, 5, { 534, 554 }, 2 },
    { 1216, 1233, 1195, 1213, { 944, 939, 934, 929, 924 }, 5, { 574, 594 }, 2 },
    { 1223, 1226, 1196, 1211, { 1184, 1179, 1174, 1169, 1164 }, 5, { 614, 634 }, 2 },
    { 1217, 1227, 1197, 1210, { 974, 969, 964, 959, 954 }, 5, { 654, 674 }, 2 },
    { 1218, 1228, 1198, 1209, { 1004, 999, 994, 989, 984 }, 5, { 694, 714 }, 2 },
    { 1219, 1229, 1199, 1208, { 1034, 1029, 1024, 1019, 1014 }, 5, { 734, 754 }, 2 },
    { 1220, 1230, 1200, 1207, { 1064, 1059, 1054, 1049, 1044 }, 5, { 774, 794 }, 2 },
    { 1221, 1231, 1201, 1206, { 1094, 1089, 1084, 1079, 1074 }, 5, { 814, 834 }, 2 },
    { 1222, 1232, 1202, 1205, { 1124, 1119, 1114, 1109, 1104 }, 5, { 854, 874 }, 2 },
};

// MiniSHARC 4x8 (minidsp-rs protocol/src/device/msharc4x8.rs)
static const OutputSpec kMSharc4x8Outputs[] = {
    { 4, 72, 160, 192, { 16837, 16832, 16827, 16822, 16817, 16812, 16807, 16802, 16797, 16792 }, 10, { 17192, 17212 }, 2 },
    { 5, 73, 161, 193, { 16887, 16882, 16877, 16872, 16867, 16862, 16857, 16852, 16847, 16842 }, 10, { 17232, 17252 }, 2 },
    { 6, 74, 162, 194, { 16937, 16932, 16927, 16922, 16917, 16912, 16907, 16902, 16897, 16892 }, 10, { 17272, 17292 }, 2 },
    { 7, 75, 163, 195, { 16987, 16982, 16977, 16972, 16967, 16962, 16957, 16952, 16947, 16942 }, 10, { 17312, 17332 }, 2 },
    { 8, 76, 164, 196, { 17037, 17032, 17027, 17022, 17017, 17012, 17007, 17002, 16997, 16992 }, 10, { 17352, 17372 }, 2 },
    { 9, 77, 165, 197, { 17087, 17082, 17077, 17072, 17067, 17062, 17057, 17052, 17047, 17042 }, 10, { 17392, 17412 }, 2 },
    { 10, 78, 166, 198, { 17137, 17132, 17127, 17122, 17117, 17112, 17107, 17102, 17097, 17092 }, 10, { 17432, 17452 }, 2 },
    { 11, 79, 167, 199, { 17187, 17182, 17177, 17172, 17167, 17162, 17157, 17152, 17147, 17142 }, 10, { 17472, 17492 }, 2 },
};

// FlexHtx (minidsp-rs protocol/src/device/flexhtx.rs)
static const OutputSpec kFlexHtxOutputs[] = {
    { 824, 826, 833, 837, {  }, 0, { 888, 908, 272, 292 }, 4 },
    { 928, 930, 937, 941, {  }, 0, { 992, 1012, 312, 332 }, 4 },
    { 1032, 1034, 1041, 1045, {  }, 0, { 1096, 1116, 352, 372 }, 4 },
    { 1136, 1138, 1145, 1149, {  }, 0, { 1200, 1220, 392, 412 }, 4 },
    { 1240, 1242, 1249, 1253, {  }, 0, { 1304, 1324, 432, 452 }, 4 },
    { 1344, 1346, 1353, 1357, {  }, 0, { 1408, 1428, 472, 492 }, 4 },
    { 1448, 1450, 1457, 1461, {  }, 0, { 1512, 1532, 512, 532 }, 4 },
    { 1552, 1554, 1561, 1565, {  }, 0, { 1616, 1636, 552, 572 }, 4 },
};
} // namespace

#define FA_DEV(kind, name, hw, dsp, rate, addr, val, table) \
    DeviceSpec { kind, name, hw, dsp, rate, Dialect { AddrEncoding::addr, FloatEncoding::val }, table, (int) std::size (table) }

const std::vector<DeviceSpec>& allDevices()
{
    // Same order as minidsp-rs probe_kind(): the first matching (hwId, dspVersion) wins.
    static const std::vector<DeviceSpec> devices {
        FA_DEV ("m10x10hd",    "miniDSP 10x10 HD",      1,  51,  96000.0, len2, fixedPoint, kM10x10HdOutputs),
        FA_DEV ("m4x10hd",     "miniDSP 4x10 HD",       1,  -1,  96000.0, len2, fixedPoint, kM4x10HdOutputs),
        FA_DEV ("msharc4x8",   "miniDSP miniSHARC 4x8", 4,  -1,  96000.0, len3, float32LE,  kMSharc4x8Outputs),
        FA_DEV ("m2x4hd",      "miniDSP 2x4 HD",        10, 100, 96000.0, len3, float32LE,  kM2x4HdOutputs),
        FA_DEV ("ddrc24",      "miniDSP DDRC-24",       10, 101, 96000.0, len3, float32LE,  kDDRC24Outputs),
        FA_DEV ("ddrc88bm",    "miniDSP DDRC-88BM",     6,  95,  48000.0, len3, float32LE,  kDDRC88BMOutputs),
        FA_DEV ("shd",         "miniDSP SHD",           14, -1,  96000.0, len3, float32LE,  kShdOutputs),
        FA_DEV ("nanodigi2x8", "miniDSP nanoDIGI 2x8",  2,  54,  96000.0, len2, fixedPoint, kNanodigi2x8Outputs),
        // minidsp-rs has no internal rate for the C-DSP 8x12 v2 (0): delay and filters need
        // Options::sampleRateOverride until it is confirmed on hardware.
        FA_DEV ("c8x12v2",     "miniDSP C-DSP 8x12 v2", 11, 97,  0.0,     len3, float32LE,  kC8x12v2Outputs),
        FA_DEV ("flex",        "miniDSP Flex",          27, 100, 96000.0, len3, float32LE,  kFlexOutputs),
        FA_DEV ("flexhtx",     "miniDSP Flex HTx",      32, 115, 48000.0, len3, float32LE,  kFlexHtxOutputs),
    };
    return devices;
}
#undef FA_DEV

const DeviceSpec* probe (int hwId, int dspVersion)
{
    for (auto& d : allDevices())
        if (d.hwId == hwId && (d.dspVersion < 0 || d.dspVersion == dspVersion))
            return &d;
    return nullptr;
}

const DeviceSpec* byKind (const juce::String& kind)
{
    for (auto& d : allDevices())
        if (kind.equalsIgnoreCase (d.kind))
            return &d;
    return nullptr;
}

bool isDocumentedAddress (const DeviceSpec& dev, int addr, bool biquad)
{
    for (int i = 0; i < dev.numOutputs; ++i)
    {
        const auto& o = dev.outputs[i];
        if (biquad)
        {
            for (int p = 0; p < o.numPeq; ++p)
                if (o.peq[p] == addr)
                    return true;
            for (int g = 0; g < o.numXover; ++g)
                for (int k = 0; k < 4; ++k)
                    if (o.xover[g] + 5 * k == addr)
                        return true;
        }
        else if (addr == o.enable || addr == o.invert || (o.gain >= 0 && addr == o.gain) || (o.delay >= 0 && addr == o.delay))
        {
            return true;
        }
    }
    return false;
}

//==============================================================================
uint8_t checksum (const uint8_t* data, size_t size)
{
    uint32_t sum = 0;
    for (size_t i = 0; i < size; ++i)
        sum += data[i];
    return (uint8_t) (sum & 0xFF);
}

Bytes frame (const Bytes& payload)
{
    Bytes out;
    out.reserve (payload.size() + 2);
    out.push_back ((uint8_t) (payload.size() + 1));
    out.insert (out.end(), payload.begin(), payload.end());
    out.push_back (checksum (out.data(), out.size()));
    return out;
}

bool unframe (const Bytes& report, Bytes& payload)
{
    payload.clear();
    if (report.empty())
        return false;
    const size_t len = report[0];
    if (len == 0 || report.size() < len)
        return false;
    payload.assign (report.begin() + 1, report.begin() + (long) len);
    return true;
}

void writeAddr (Bytes& out, AddrEncoding enc, uint32_t addr, bool extraBit)
{
    if (enc == AddrEncoding::len2)
    {
        uint32_t v = 0x8000u | (addr & 0x7FFFu);
        if (! extraBit)
            v ^= 0x8000u;
        out.push_back ((uint8_t) (v >> 8));
        out.push_back ((uint8_t) v);
    }
    else
    {
        uint32_t v = 0x800000u | (addr & 0x7FFFFFu);
        if (! extraBit)
            v ^= 0x800000u;
        out.push_back ((uint8_t) (v >> 16));
        out.push_back ((uint8_t) (v >> 8));
        out.push_back ((uint8_t) v);
    }
}

static int decodeAddr (const Bytes& payload, AddrEncoding enc)
{
    if (enc == AddrEncoding::len2)
        return payload.size() >= 3 ? ((payload[1] & 0x7F) << 8) | payload[2] : -1;
    return payload.size() >= 4 ? ((payload[1] & 0x7F) << 16) | (payload[2] << 8) | payload[3] : -1;
}

std::array<uint8_t, 4> encodeFloat32LE (float v)
{
    uint32_t u;
    std::memcpy (&u, &v, 4);
    return { (uint8_t) u, (uint8_t) (u >> 8), (uint8_t) (u >> 16), (uint8_t) (u >> 24) };
}

std::array<uint8_t, 4> encodeInt16 (uint16_t v)
{
    return { (uint8_t) v, (uint8_t) (v >> 8), 0, 0 };
}

std::array<uint8_t, 4> encodeInt32BE (uint32_t v)
{
    return { (uint8_t) (v >> 24), (uint8_t) (v >> 16), (uint8_t) (v >> 8), (uint8_t) v };
}

uint32_t fixedPointFromFloat (float v)
{
    const double encoded = (double) v * (double) (1 << 23) + (double) (1 << 27);
    auto u = (uint32_t) (int64_t) encoded;
    u ^= 0x08000000u;
    if ((u & 0xF0000000u) == 0xF0000000u)
        u &= 0x0FFFFFFFu;
    return u;
}

float fixedPointToFloat (uint32_t v)
{
    const uint32_t x = v ^ 0x08000000u;
    const auto sub = (int32_t) (x - (1u << 27));
    return (float) sub / (float) (1 << 23);
}

std::array<uint8_t, 4> encodeFixed (float v)
{
    return encodeInt32BE (fixedPointFromFloat (v));
}

std::array<uint8_t, 4> encodeCoefficient (const Dialect& d, double v)
{
    return d.value == FloatEncoding::fixedPoint ? encodeFixed ((float) v) : encodeFloat32LE ((float) v);
}

std::array<uint8_t, 4> encodeGainDb (const Dialect& d, double db)
{
    if (d.value == FloatEncoding::fixedPoint)
        return encodeFixed ((float) std::pow (10.0, (double) (float) db / 20.0));
    return encodeFloat32LE ((float) db);
}

std::array<uint8_t, 4> encodeMute (const Dialect& d, bool muted)
{
    if (d.addr == AddrEncoding::len2)
        return encodeInt32BE (muted ? 0x0u : 0x00800000u);
    return encodeInt16 (muted ? 1 : 2);
}

std::array<uint8_t, 4> encodeInvert (const Dialect& d, bool inverted)
{
    if (d.addr == AddrEncoding::len2)
        return encodeInt32BE (inverted ? 0xFF800000u : 0x00800000u);
    return encodeInt16 (inverted ? 1 : 0);
}

std::array<uint8_t, 4> encodeDelay (const Dialect& d, uint32_t samples)
{
    if (d.addr == AddrEncoding::len2)
        return encodeInt32BE (samples);
    return encodeInt16 ((uint16_t) samples);
}

uint32_t delaySamples (double delayMs, double sampleRate)
{
    if (delayMs <= 0.0 || sampleRate <= 0.0)
        return 0;
    const auto micros = (double) std::llround (delayMs * 1000.0);
    return (uint32_t) std::llround (micros * sampleRate / 1.0e6);
}

std::array<double, 5> toDeviceCoefficients (const BiquadCoeffs& b)
{
    // miniDSP (and REW's miniDSP export) use the opposite sign for the feedback terms.
    return { b.b0, b.b1, b.b2, -b.a1, -b.a2 };
}

namespace cmd
{
Bytes readHardwareId() { return { 0x31 }; }

Bytes readMemory (uint16_t addr, uint8_t size)
{
    return { 0x05, (uint8_t) (addr >> 8), (uint8_t) addr, size };
}

Bytes readFloats (uint16_t addr, uint8_t count)
{
    return { 0x14, (uint8_t) (addr >> 8), (uint8_t) addr, count };
}

Bytes write (const Dialect& d, int addr, const std::array<uint8_t, 4>& value)
{
    Bytes b { 0x13 };
    writeAddr (b, d.addr, (uint32_t) addr);
    b.insert (b.end(), value.begin(), value.end());
    return b;
}

Bytes writeBiquad (const Dialect& d, int addr, const std::array<double, 5>& coeffs)
{
    Bytes raw;
    for (auto c : coeffs)
    {
        const auto e = encodeCoefficient (d, c);
        raw.insert (raw.end(), e.begin(), e.end());
    }
    return writeBiquadRaw (d, addr, raw);
}

Bytes writeBiquadRaw (const Dialect& d, int addr, const Bytes& twentyBytes)
{
    Bytes b { 0x30 };
    writeAddr (b, d.addr, (uint32_t) addr);
    b.push_back (0x00);
    b.push_back (0x00);
    b.insert (b.end(), twentyBytes.begin(), twentyBytes.end());
    return b;
}

Bytes writeBiquadBypass (const Dialect& d, int addr, bool bypass)
{
    Bytes b { 0x19 };
    writeAddr (b, d.addr, (uint32_t) addr, bypass);
    return b;
}
} // namespace cmd

Response parseResponse (const Bytes& p)
{
    Response r;
    if (p.empty())
    {
        r.kind = Response::Kind::ack;
        return r;
    }

    switch (p[0])
    {
        case 0x05:
            if (p.size() >= 3)
            {
                r.kind = Response::Kind::memory;
                r.base = (uint16_t) ((p[1] << 8) | p[2]);
                r.data.assign (p.begin() + 3, p.end());
            }
            break;
        case 0x14:
            if (p.size() >= 3)
            {
                r.kind = Response::Kind::floats;
                r.base = (uint16_t) ((p[1] << 8) | p[2]);
                const size_t n = (p.size() - 3) / 4 * 4;
                r.data.assign (p.begin() + 3, p.begin() + 3 + (long) n);
            }
            break;
        case 0x31:
            if (p.size() >= 4)
            {
                r.kind = Response::Kind::hardwareId;
                r.fwMajor = p[1];
                r.fwMinor = p[2];
                r.hwId = p[3];
            }
            break;
        default:
            break;
    }
    if (r.kind == Response::Kind::unknown)
        r.data = p;
    return r;
}

bool responseMatches (const Bytes& c, const Response& r)
{
    if (c.empty())
        return false;
    switch (c[0])
    {
        case 0x05: return r.kind == Response::Kind::memory && c.size() >= 4
                          && r.base == (uint16_t) ((c[1] << 8) | c[2]) && r.data.size() == c[3];
        case 0x14: return r.kind == Response::Kind::floats && c.size() >= 4
                          && r.base == (uint16_t) ((c[1] << 8) | c[2]) && r.data.size() == (size_t) c[3] * 4;
        case 0x31: return r.kind == Response::Kind::hardwareId;
        case 0x13:
        case 0x30:
        case 0x19: return r.kind == Response::Kind::ack;
        default:   return true;
    }
}

//==============================================================================
namespace
{
class HidTransport : public Transport
{
public:
    bool open (const juce::String& path, juce::String& error) { return dev.open (path, error); }

    bool sendFrame (const Bytes& framed, juce::String& error) override
    {
        // minidsp-rs: report id 0, frame, padded with 0xFF to 65 bytes. Retries like the original.
        for (int attempt = 0;; ++attempt)
        {
            if (dev.write (0x00, framed.data(), framed.size(), 0xFF, 1000, error))
                return true;
            if (attempt >= 2)
                return false;
            juce::Thread::sleep (100);
        }
    }

    bool receive (Bytes& report, int timeoutMs, juce::String& error) override
    {
        return dev.read (report, timeoutMs, error);
    }

private:
    hid::HidDevice dev;
};

bool roundtripOn (Transport& t, const Bytes& payload, Response& response, int timeoutMs, juce::String& error)
{
    if (! t.sendFrame (frame (payload), error))
        return false;

    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    for (;;)
    {
        const auto now = juce::Time::getMillisecondCounter();
        if (now >= deadline)
            break;

        Bytes report, data;
        if (! t.receive (report, (int) (deadline - now), error))
        {
            if (error.isNotEmpty())
                return false;
            break; // timeout
        }
        if (! unframe (report, data))
            continue;
        auto r = parseResponse (data);
        if (responseMatches (payload, r))
        {
            response = std::move (r);
            return true;
        }
        // Unsolicited update (e.g. the volume knob): ignore and keep waiting.
    }
    error = "The miniDSP did not answer (timeout). Close the miniDSP plugin/app if it is open, "
            "then unplug and reconnect the USB cable.";
    return false;
}

bool identify (Transport& t, int timeoutMs, DeviceIdentity& id, juce::String& error)
{
    Response r;
    if (! roundtripOn (t, cmd::readHardwareId(), r, timeoutMs, error))
        return false;
    id.hwId = r.hwId;
    id.fwMajor = r.fwMajor;
    id.fwMinor = r.fwMinor;

    if (! roundtripOn (t, cmd::readMemory (eeprom::firmwareVersion, 1), r, timeoutMs, error) || r.data.size() < 1)
        return false;
    id.dspVersion = r.data[0];

    if (roundtripOn (t, cmd::readMemory (eeprom::serialShort, 2), r, timeoutMs, error) && r.data.size() >= 2)
        id.serial = 900000u + (uint32_t) ((r.data[0] << 8) | r.data[1]);
    error.clear();
    return true;
}

juce::String toHex (const uint8_t* d, size_t n)
{
    return juce::String::toHexString (d, (int) n, 0);
}

Bytes fromHex (const juce::String& s)
{
    Bytes out;
    juce::MemoryBlock mb;
    mb.loadFromHexString (s);
    out.assign ((const uint8_t*) mb.getData(), (const uint8_t*) mb.getData() + mb.getSize());
    return out;
}

std::array<double, 5> unityBiquad() { return { 1.0, 0.0, 0.0, 0.0, 0.0 }; }

bool isUnityRaw (const Dialect& d, const uint8_t* w)
{
    for (int i = 0; i < 5; ++i)
    {
        float v;
        if (d.value == FloatEncoding::fixedPoint)
        {
            const uint32_t u = ((uint32_t) w[i * 4] << 24) | ((uint32_t) w[i * 4 + 1] << 16) | ((uint32_t) w[i * 4 + 2] << 8) | w[i * 4 + 3];
            v = fixedPointToFloat (u);
        }
        else
        {
            std::memcpy (&v, w + i * 4, 4);
        }
        if (std::abs (v - (i == 0 ? 1.0f : 0.0f)) > 1.0e-5f)
            return false;
    }
    return true;
}
} // namespace

std::unique_ptr<Transport> openHidTransport (const juce::String& path, juce::String& error)
{
    auto t = std::make_unique<HidTransport>();
    if (! t->open (path, error))
        return {};
    return t;
}
} // namespace fa::minidsp

//==============================================================================
namespace fa
{
using namespace minidsp;

std::vector<MiniDspController::DetectedDevice> MiniDspController::detect()
{
    std::vector<DetectedDevice> result;
    auto list = hid::enumerate (minidsp::vendorId, 0);
    for (auto& d : hid::enumerate (oldVendorId, oldProductId))
        list.push_back (d);

    for (auto& info : list)
    {
        if (info.outputReportLength != 0 && info.outputReportLength < 64)
            continue; // not the command interface

        DetectedDevice dd;
        dd.path = info.path;
        dd.vendorId = info.vendorId;
        dd.productId = info.productId;
        dd.usbProduct = info.product;
        dd.usbSerial = info.serial;

        juce::String error;
        auto t = openHidTransport (info.path, error);
        if (t == nullptr || ! identify (*t, 500, dd.identity, error))
        {
            dd.error = error.isNotEmpty() ? error : juce::String ("Could not open the device");
            dd.name = info.product.isNotEmpty() ? info.product : juce::String ("miniDSP (unidentified)");
            result.push_back (dd);
            continue;
        }

        if (auto* spec = minidsp::probe (dd.identity.hwId, dd.identity.dspVersion))
        {
            dd.kind = spec->kind;
            dd.name = spec->productName;
            dd.supported = true;
        }
        else
        {
            dd.name = info.product.isNotEmpty() ? info.product : juce::String ("miniDSP");
            dd.error = "Unsupported miniDSP (hardware id " + juce::String (dd.identity.hwId) + ", DSP version "
                     + juce::String (dd.identity.dspVersion) + "). Use the manual or UI-automation route.";
        }
        result.push_back (dd);
    }
    return result;
}

MiniDspController::MiniDspController (Options o) : options (std::move (o)) {}

MiniDspController::MiniDspController (std::unique_ptr<Transport> t, Options o)
    : options (std::move (o)), transport (std::move (t)), externalTransport (true)
{
}

MiniDspController::~MiniDspController() = default;

juce::String MiniDspController::getName() const
{
    const juce::ScopedLock sl (lock);
    return device != nullptr ? juce::String (device->productName) + " (USB)" : juce::String ("miniDSP (USB)");
}

DspCapabilities MiniDspController::capabilitiesFor (const DeviceSpec& dev, double fs, bool allowPositiveGain)
{
    DspCapabilities c;
    const double rate = fs > 0.0 ? fs : 48000.0;
    c.sampleRate         = rate;
    c.peqBandsPerChannel = dev.numOutputs > 0 ? dev.outputs[0].numPeq : 0;
    c.peqMinQ            = 0.1;      // raw biquads: no plugin Q limits
    c.peqMaxQ            = 20.0;
    c.peqMaxGainDb       = 12.0;
    c.peqMinGainDb       = -20.0;
    c.delayStepMs        = 1000.0 / rate;
    c.maxDelayMs         = (fs > 0.0 && dev.numOutputs > 0 && dev.outputs[0].delay >= 0) ? maxDelaySamples * 1000.0 / rate : 0.0;
    c.gainStepDb         = 0.01;
    c.minChannelGainDb   = -72.0;
    c.maxChannelGainDb   = allowPositiveGain ? 12.0 : 0.0;
    c.freqStepHz         = 0.0;
    c.availableSlopes    = { 6, 12, 18, 24, 30, 36, 42, 48 };
    c.hasLinkwitzRiley   = true;
    c.hasBessel          = true;
    c.hasAllPass         = c.peqBandsPerChannel > 0;
    return c;
}

DspCapabilities MiniDspController::getCapabilities() const
{
    const juce::ScopedLock sl (lock);
    if (device == nullptr)
    {
        if (auto* forced = byKind (options.forceKind))
            return capabilitiesFor (*forced, options.sampleRateOverride > 0 ? options.sampleRateOverride : forced->sampleRate, options.allowPositiveGain);
        return {};
    }
    return capabilitiesFor (*device, sampleRate, options.allowPositiveGain);
}

bool MiniDspController::connect (juce::String& error)
{
    const juce::ScopedLock sl (lock);
    if (isConnected())
        return true;

    if (transport == nullptr)
    {
        juce::String path = options.devicePath;
        if (path.isEmpty())
        {
            const auto found = detect();
            for (auto& d : found)
                if (d.supported || options.forceKind.isNotEmpty())
                {
                    path = d.path;
                    break;
                }
            if (path.isEmpty())
            {
                error = found.empty() ? "No miniDSP found on USB. Plug it in, power it on and close the miniDSP plugin."
                                      : "A miniDSP was found but it is not supported: " + found.front().error;
                return false;
            }
        }
        transport = openHidTransport (path, error);
        if (transport == nullptr)
        {
            error = "Could not open the miniDSP: " + error;
            return false;
        }
    }

    if (! identify (*transport, options.timeoutMs, identity, error))
    {
        disconnect();
        return false;
    }

    device = options.forceKind.isNotEmpty() ? byKind (options.forceKind) : probe (identity.hwId, identity.dspVersion);
    if (device == nullptr)
    {
        error = "Unsupported miniDSP (hardware id " + juce::String (identity.hwId) + ", DSP version "
              + juce::String (identity.dspVersion) + ").";
        disconnect();
        return false;
    }

    sampleRate = options.sampleRateOverride > 0.0 ? options.sampleRateOverride : device->sampleRate;
    if (sampleRate <= 0.0)
        warnings.add (juce::String (device->productName) + ": internal sample rate unknown - delay and filters "
                      "are disabled until it is set (Options::sampleRateOverride).");
    lastWritten.clear();
    return true;
}

bool MiniDspController::isConnected() const
{
    const juce::ScopedLock sl (lock);
    return transport != nullptr && device != nullptr;
}

void MiniDspController::disconnect()
{
    const juce::ScopedLock sl (lock);
    if (! externalTransport)
        transport.reset();
    device = nullptr;
    lastWritten.clear();
    pending.clear();
    batching = false;
}

juce::StringArray MiniDspController::getWarnings() const
{
    const juce::ScopedLock sl (lock);
    return warnings;
}

juce::File MiniDspController::getSnapshotFile() const
{
    const juce::ScopedLock sl (lock);
    return snapshotFile;
}

//==============================================================================
bool MiniDspController::buildChannelCommands (const DeviceSpec& dev, double fs, const OutputChannel& ch,
                                              bool allowPositiveGain, std::vector<Bytes>& out,
                                              juce::StringArray& warn, juce::String& error)
{
    if (ch.dspOutput < 0 || ch.dspOutput >= dev.numOutputs)
    {
        error = ch.label + ": output " + juce::String (ch.dspOutput + 1) + " does not exist on the "
              + dev.productName + " (" + juce::String (dev.numOutputs) + " outputs).";
        return false;
    }

    const auto& o = dev.outputs[ch.dspOutput];
    const auto& d = dev.dialect;
    const auto& s = ch.settings;
    std::vector<Bytes> cmds;

    // Mute first when muting, so nothing below is heard half-applied.
    if (s.muted)
        cmds.push_back (cmd::write (d, o.enable, encodeMute (d, true)));

    if (o.gain >= 0)
    {
        const double maxGain = allowPositiveGain ? 12.0 : 0.0;
        double g = s.gainDb;
        if (g > maxGain)
        {
            warn.add (ch.label + ": gain " + juce::String (g, 2) + " dB limited to " + juce::String (maxGain, 1) + " dB.");
            g = maxGain;
        }
        g = juce::jmax (-72.0, g);
        cmds.push_back (cmd::write (d, o.gain, encodeGainDb (d, g)));
    }

    if (s.delayMs > 0.0 && (o.delay < 0 || fs <= 0.0))
    {
        error = ch.label + ": cannot set delay (" + (fs <= 0.0 ? juce::String ("internal sample rate unknown")
                                                              : juce::String ("no delay on this output")) + ").";
        return false;
    }
    if (o.delay >= 0 && fs > 0.0)
    {
        const auto samples = delaySamples (s.delayMs, fs);
        if (samples > (uint32_t) maxDelaySamples)
        {
            error = ch.label + ": delay " + juce::String (s.delayMs, 2) + " ms exceeds the maximum of "
                  + juce::String (maxDelaySamples * 1000.0 / fs, 2) + " ms.";
            return false;
        }
        cmds.push_back (cmd::write (d, o.delay, encodeDelay (d, samples)));
    }

    cmds.push_back (cmd::write (d, o.invert, encodeInvert (d, s.inverted)));

    // ---- Crossover: group 0 = high-pass, group 1 = low-pass (4 biquads each) ----
    std::vector<BiquadCoeffs> groups[2];
    if (ch.hasHighPass() && s.highPass.enabled)
        groups[0] = s.highPass.toBiquads (fs > 0.0 ? fs : 48000.0);
    if (ch.hasLowPass() && s.lowPass.enabled)
        groups[1] = s.lowPass.toBiquads (fs > 0.0 ? fs : 48000.0);

    const bool needXover = ! groups[0].empty() || ! groups[1].empty();
    if (needXover && fs <= 0.0)
    {
        error = ch.label + ": cannot write crossover filters, the internal sample rate of the "
              + dev.productName + " is unknown.";
        return false;
    }
    if (needXover && o.numXover < 2)
    {
        error = ch.label + ": this output has no crossover biquads.";
        return false;
    }

    for (int g = 0; g < juce::jmin (2, o.numXover); ++g)
    {
        const int start = o.xover[g];
        if (groups[g].empty())
        {
            cmds.push_back (cmd::writeBiquadBypass (d, start, true));
            continue;
        }
        if (groups[g].size() > 4)
        {
            error = ch.label + ": crossover needs " + juce::String ((int) groups[g].size()) + " biquads, the device has 4 per filter.";
            return false;
        }
        for (int k = 0; k < 4; ++k)
            cmds.push_back (cmd::writeBiquad (d, start + 5 * k,
                                              k < (int) groups[g].size() ? toDeviceCoefficients (groups[g][(size_t) k]) : unityBiquad()));
        cmds.push_back (cmd::writeBiquadBypass (d, start, false));
    }

    // ---- PEQ ----
    std::vector<BiquadCoeffs> peq;
    for (auto& f : s.peq)
        if (f.enabled)
            for (auto& bq : f.toBiquads (fs > 0.0 ? fs : 48000.0))
                peq.push_back (bq);

    if (! peq.empty() && fs <= 0.0)
    {
        error = ch.label + ": cannot write EQ, the internal sample rate of the " + juce::String (dev.productName) + " is unknown.";
        return false;
    }
    if ((int) peq.size() > o.numPeq)
    {
        error = ch.label + ": " + juce::String ((int) peq.size()) + " EQ biquads requested, the device has "
              + juce::String (o.numPeq) + " per output.";
        return false;
    }
    for (int i = 0; i < o.numPeq; ++i)
    {
        if (i < (int) peq.size())
        {
            cmds.push_back (cmd::writeBiquad (d, o.peq[i], toDeviceCoefficients (peq[(size_t) i])));
            cmds.push_back (cmd::writeBiquadBypass (d, o.peq[i], false));
        }
        else
        {
            cmds.push_back (cmd::writeBiquadBypass (d, o.peq[i], true));
            cmds.push_back (cmd::writeBiquad (d, o.peq[i], unityBiquad()));
        }
    }

    if (! s.muted)
        cmds.push_back (cmd::write (d, o.enable, encodeMute (d, false)));

    out.insert (out.end(), cmds.begin(), cmds.end());
    return true;
}

bool MiniDspController::checkWritable (const OutputChannel& ch, juce::String& error) const
{
    if (! isConnected())
    {
        error = "The miniDSP is not connected.";
        return false;
    }
    if (ch.dspOutput < 0 || ch.dspOutput >= device->numOutputs)
    {
        error = ch.label + ": output " + juce::String (ch.dspOutput + 1) + " does not exist on the " + device->productName + ".";
        return false;
    }
    return true;
}

bool MiniDspController::applyChannel (const OutputChannel& ch, juce::String& error)
{
    const juce::ScopedLock sl (lock);
    if (! checkWritable (ch, error))
        return false;

    std::vector<Bytes> cmds;
    if (! buildChannelCommands (*device, sampleRate, ch, options.allowPositiveGain, cmds, warnings, error))
        return false;
    return queue (std::move (cmds), error);
}

bool MiniDspController::setMuted (const OutputChannel& ch, bool muted, juce::String& error)
{
    const juce::ScopedLock sl (lock);
    if (! checkWritable (ch, error))
        return false;
    const auto& o = device->outputs[ch.dspOutput];
    return queue ({ cmd::write (device->dialect, o.enable, encodeMute (device->dialect, muted)) }, error);
}

bool MiniDspController::beginBatch (juce::String&)
{
    const juce::ScopedLock sl (lock);
    batching = true;
    return true;
}

bool MiniDspController::endBatch (juce::String& error)
{
    const juce::ScopedLock sl (lock);
    batching = false;
    if (pending.empty())
        return true;

    // Keep only the last write per parameter, in the order of those last writes.
    std::vector<Bytes> deduped;
    std::set<juce::String> seen;
    for (auto it = pending.rbegin(); it != pending.rend(); ++it)
        if (seen.insert (writeKey (*it)).second)
            deduped.push_back (*it);
    std::reverse (deduped.begin(), deduped.end());
    pending.clear();
    return send (deduped, error);
}

bool MiniDspController::queue (std::vector<Bytes> commands, juce::String& error)
{
    if (batching)
    {
        pending.insert (pending.end(), commands.begin(), commands.end());
        return true;
    }
    return send (commands, error);
}

juce::String MiniDspController::writeKey (const Bytes& p)
{
    // Key = command id + address bytes. For 0x19 the bypass flag rides in the address' top bit,
    // so it is masked out (bypass on/off of one slot share a key).
    juce::String key = juce::String::toHexString ((int) (p.empty() ? 0 : p[0])) + ":";
    for (size_t i = 1; i < p.size() && i < 4; ++i)
        key << juce::String::toHexString ((int) (i == 1 ? (p[i] & 0x7F) : p[i])) << ".";
    return key;
}

bool MiniDspController::send (const std::vector<Bytes>& commands, juce::String& error)
{
    if (! isConnected())
    {
        error = "The miniDSP is not connected.";
        return false;
    }

    for (auto& p : commands)
    {
        if (p.empty())
            continue;

        // Safety: only documented parameter addresses, only known write commands.
        const bool isBiquad = (p[0] == 0x30 || p[0] == 0x19);
        if ((p[0] != 0x13 && ! isBiquad) || ! isDocumentedAddress (*device, decodeAddr (p, device->dialect.addr), isBiquad))
        {
            error = "Refused to write to an undocumented miniDSP address.";
            jassertfalse;
            return false;
        }

        const auto key = writeKey (p);
        if (auto it = lastWritten.find (key); it != lastWritten.end() && it->second == p)
            continue; // unchanged

        if (! ensureSnapshot (error))
            return false;

        Response r;
        if (! roundtrip (p, r, error))
            return false;
        lastWritten[key] = p;
        ++writesSent;
    }
    return true;
}

bool MiniDspController::roundtrip (const Bytes& payload, Response& response, juce::String& error)
{
    if (transport == nullptr)
    {
        error = "The miniDSP is not connected.";
        return false;
    }
    if (roundtripOn (*transport, payload, response, options.timeoutMs, error))
        return true;

    // Treat any I/O failure as a lost device: drop the cache so the next connect starts clean.
    lastWritten.clear();
    return false;
}

bool MiniDspController::readFloatWords (int addr, int count, Bytes& words, juce::String& error)
{
    words.clear();
    for (int done = 0; done < count;)
    {
        const int n = juce::jmin (readFloatsMax, count - done);
        Response r;
        if (! roundtrip (cmd::readFloats ((uint16_t) (addr + done), (uint8_t) n), r, error))
            return false;
        words.insert (words.end(), r.data.begin(), r.data.end());
        done += n;
    }
    return true;
}

bool MiniDspController::ensureSnapshot (juce::String& error)
{
    if (snapshotTaken)
        return true;

    auto fail = [&] (const juce::String& why)
    {
        if (options.requireSnapshot)
        {
            error = "Could not back up the current miniDSP settings, nothing was written: " + why;
            return false;
        }
        warnings.add ("No backup was taken before writing: " + why);
        snapshotTaken = true;
        return true;
    };

    // Collect every word the controller may write.
    std::set<int> words;
    for (int i = 0; i < device->numOutputs; ++i)
    {
        const auto& o = device->outputs[i];
        for (int a : { o.enable, o.gain, o.delay, o.invert })
            if (a >= 0)
                words.insert (a);
        for (int p = 0; p < o.numPeq; ++p)
            for (int k = 0; k < 5; ++k)
                words.insert (o.peq[p] + k);
        for (int g = 0; g < juce::jmin (2, o.numXover); ++g)
            for (int k = 0; k < 20; ++k)
                words.insert (o.xover[g] + k);
    }

    // Read in spans of up to 14 words.
    std::map<int, Bytes> memory;
    juce::String readError;
    for (auto it = words.begin(); it != words.end();)
    {
        const int start = *it;
        int last = start;
        auto jt = it;
        while (jt != words.end() && *jt < start + readFloatsMax)
            last = *jt++;
        Bytes data;
        if (! readFloatWords (start, last - start + 1, data, readError))
            return fail (readError);
        for (int a = start; a <= last; ++a)
        {
            const size_t off = (size_t) (a - start) * 4;
            if (off + 4 <= data.size())
                memory[a] = Bytes (data.begin() + (long) off, data.begin() + (long) off + 4);
        }
        it = jt;
    }

    auto wordsHex = [&] (int addr, int count) -> juce::String
    {
        juce::String hex;
        for (int k = 0; k < count; ++k)
        {
            auto m = memory.find (addr + k);
            if (m == memory.end())
                return {};
            hex << toHex (m->second.data(), 4);
        }
        return hex;
    };

    juce::Array<juce::var> scalars, peqs, xovers;
    for (int i = 0; i < device->numOutputs; ++i)
    {
        const auto& o = device->outputs[i];
        const std::pair<const char*, int> roles[] = { { "enable", o.enable }, { "gain", o.gain }, { "delay", o.delay }, { "invert", o.invert } };
        for (auto& [role, a] : roles)
        {
            if (a < 0)
                continue;
            auto* e = new juce::DynamicObject();
            e->setProperty ("output", i);
            e->setProperty ("role", role);
            e->setProperty ("addr", a);
            e->setProperty ("hex", wordsHex (a, 1));
            scalars.add (juce::var (e));
        }
        for (int p = 0; p < o.numPeq; ++p)
        {
            auto* e = new juce::DynamicObject();
            e->setProperty ("output", i);
            e->setProperty ("addr", o.peq[p]);
            e->setProperty ("hex", wordsHex (o.peq[p], 5));
            peqs.add (juce::var (e));
        }
        for (int g = 0; g < juce::jmin (2, o.numXover); ++g)
        {
            auto* e = new juce::DynamicObject();
            e->setProperty ("output", i);
            e->setProperty ("addr", o.xover[g]);
            e->setProperty ("hex", wordsHex (o.xover[g], 20));
            xovers.add (juce::var (e));
        }
    }

    // EEPROM master status (preset, source, volume, mute) for reference.
    juce::String master;
    {
        Response r;
        juce::String e;
        if (roundtrip (cmd::readMemory (eeprom::preset, 9), r, e))
            master = toHex (r.data.data(), r.data.size());
    }

    auto* root = new juce::DynamicObject();
    root->setProperty ("format", "FalseAudio miniDSP snapshot");
    root->setProperty ("version", 1);
    root->setProperty ("created", juce::Time::getCurrentTime().toISO8601 (true));
    root->setProperty ("kind", device->kind);
    root->setProperty ("product", device->productName);
    root->setProperty ("hwId", identity.hwId);
    root->setProperty ("dspVersion", identity.dspVersion);
    root->setProperty ("firmware", juce::String (identity.fwMajor) + "." + juce::String (identity.fwMinor));
    root->setProperty ("serial", (juce::int64) identity.serial);
    root->setProperty ("dialect", device->dialect.value == FloatEncoding::fixedPoint ? "fixed" : "float32");
    root->setProperty ("masterStatusHex", master);
    root->setProperty ("note", "Raw words as read with command 0x14. Biquad bypass flags cannot be read back; "
                               "restore bypasses all-unity biquads and enables the rest.");
    root->setProperty ("scalars", scalars);
    root->setProperty ("peq", peqs);
    root->setProperty ("xover", xovers);

    auto folder = options.backupFolder != juce::File()
                      ? options.backupFolder
                      : juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                            .getChildFile ("FalseAudio").getChildFile ("backups");
    if (! folder.createDirectory())
        return fail ("cannot create " + folder.getFullPathName());

    auto file = folder.getNonexistentChildFile ("minidsp-" + juce::String (device->kind) + "-" + juce::String ((juce::int64) identity.serial)
                                                    + "-" + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S"),
                                                ".json", false);
    if (! file.replaceWithText (juce::JSON::toString (juce::var (root))))
        return fail ("cannot write " + file.getFullPathName());

    snapshotFile = file;
    snapshotTaken = true;
    return true;
}

bool MiniDspController::restoreSnapshot (const juce::File& f, juce::String& error, bool allowUnverifiedDialect)
{
    const juce::ScopedLock sl (lock);
    if (! isConnected())
    {
        error = "The miniDSP is not connected.";
        return false;
    }

    const auto v = juce::JSON::parse (f);
    if (v.getProperty ("format", {}).toString() != "FalseAudio miniDSP snapshot")
    {
        error = "Not a FalseAudio miniDSP snapshot: " + f.getFullPathName();
        return false;
    }
    if (v.getProperty ("kind", {}).toString() != device->kind)
    {
        error = "This snapshot is for a " + v.getProperty ("product", {}).toString() + ", connected is a " + device->productName + ".";
        return false;
    }
    const auto serial = (juce::int64) v.getProperty ("serial", 0);
    if (serial != 0 && identity.serial != 0 && serial != (juce::int64) identity.serial)
    {
        error = "This snapshot was taken from another unit (serial " + juce::String (serial) + ").";
        return false;
    }
    if (device->dialect.value == FloatEncoding::fixedPoint && ! allowUnverifiedDialect)
    {
        error = "Restoring snapshots on fixed-point miniDSPs has not been verified on hardware yet.";
        return false;
    }

    const auto& d = device->dialect;
    std::vector<Bytes> cmds;
    auto addBiquads = [&] (const juce::var& list, int numBiquads, bool perSlotBypass)
    {
        if (auto* arr = list.getArray())
            for (auto& e : *arr)
            {
                const int addr = e.getProperty ("addr", -1);
                const auto raw = fromHex (e.getProperty ("hex", {}).toString());
                if (addr < 0 || raw.size() != (size_t) numBiquads * 20)
                    continue;
                bool allUnity = true;
                for (int k = 0; k < numBiquads; ++k)
                {
                    cmds.push_back (cmd::writeBiquadRaw (d, addr + 5 * k, Bytes (raw.begin() + k * 20, raw.begin() + k * 20 + 20)));
                    allUnity = allUnity && isUnityRaw (d, raw.data() + k * 20);
                }
                juce::ignoreUnused (perSlotBypass);
                cmds.push_back (cmd::writeBiquadBypass (d, addr, allUnity));
            }
    };

    if (auto* arr = v.getProperty ("scalars", {}).getArray())
        for (auto& e : *arr)
        {
            const int addr = e.getProperty ("addr", -1);
            const auto raw = fromHex (e.getProperty ("hex", {}).toString());
            if (addr >= 0 && raw.size() == 4)
                cmds.push_back (cmd::write (d, addr, { raw[0], raw[1], raw[2], raw[3] }));
        }
    addBiquads (v.getProperty ("xover", {}), 4, false);
    addBiquads (v.getProperty ("peq", {}), 1, true);

    if (cmds.empty())
    {
        error = "The snapshot contains no values.";
        return false;
    }

    // A restore is itself the backup: don't take another snapshot first.
    const bool had = snapshotTaken;
    snapshotTaken = true;
    lastWritten.clear();
    const bool ok = send (cmds, error);
    snapshotTaken = had;
    return ok;
}
} // namespace fa
