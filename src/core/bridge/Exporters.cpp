#include "Exporters.h"
#include "DspController.h"

namespace fa::exporters
{
juce::String toText (const SystemConfig& cfg)
{
    juce::String s;
    s << "FalseAudio tune - " << cfg.name << "\n";
    s << "Created " << juce::Time::getCurrentTime().toString (true, true) << "\n";
    s << "Target: " << cfg.target.name << "\n\n";

    for (auto& ch : cfg.channels)
    {
        for (auto& line : ManualDspController::describeChannel (ch))
            s << line << "\n";
        s << "\n";
    }
    return s;
}

static juce::String rewType (const FilterSpec& f)
{
    switch (f.type)
    {
        case FilterType::peaking:   return "PK";
        case FilterType::lowShelf:  return "LSQ";
        case FilterType::highShelf: return "HSQ";
        case FilterType::highPass:  return "HPQ";
        case FilterType::lowPass:   return "LPQ";
        case FilterType::allPass:   return "AP";
    }
    return "PK";
}

juce::String toREW (const OutputChannel& ch)
{
    juce::String s;
    s << "Filter Settings file\n\n";
    s << "Room EQ V5.40\n";
    s << "Dated: " << juce::Time::getCurrentTime().toString (true, true) << "\n\n";
    s << "Notes: FalseAudio " << ch.label << "\n\n";
    s << "Equaliser: Generic\n";

    int n = 1;
    for (auto& f : ch.settings.peq)
    {
        s << "Filter " << juce::String (n++).paddedLeft (' ', 2) << ": " << (f.enabled ? "ON " : "OFF") << " "
          << rewType (f).paddedRight (' ', 4) << "  Fc " << juce::String (f.freq, 1).paddedLeft (' ', 8) << " Hz  Gain "
          << juce::String (f.gainDb, 1).paddedLeft (' ', 5) << " dB  Q " << juce::String (f.q, 3).paddedLeft (' ', 7) << "\n";
    }
    while (n <= 20)
        s << "Filter " << juce::String (n++).paddedLeft (' ', 2) << ": OFF None\n";
    return s;
}

juce::String toEqualizerAPO (const SystemConfig& cfg)
{
    juce::String s;
    s << "# FalseAudio - " << cfg.name << "\n";
    for (auto& ch : cfg.channels)
    {
        const auto& st = ch.settings;
        s << "\n# " << ch.label << "\nChannel: " << (ch.dspOutput + 1) << "\n";
        s << "Preamp: " << juce::String (st.gainDb, 1) << " dB\n";
        if (st.delayMs > 0.0)
            s << "Delay: " << juce::String (st.delayMs, 2) << " ms\n";
        if (st.inverted)
            s << "Copy: " << (ch.dspOutput + 1) << "=-1*" << (ch.dspOutput + 1) << "\n";

        // APO has 2nd-order HP/LP; cascade biquads to build higher orders.
        auto writeXo = [&] (const FilterSpec& f)
        {
            if (! f.enabled)
                return;
            for (auto& bq : f.toBiquads (cfg.caps.sampleRate))
                s << "Filter: ON IIR Order 2 Coefficients " << bq.b0 << " " << bq.b1 << " " << bq.b2 << " 1 "
                  << bq.a1 << " " << bq.a2 << "\n";
        };
        writeXo (st.highPass);
        writeXo (st.lowPass);

        for (auto& f : st.peq)
            s << "Filter: " << (f.enabled ? "ON" : "OFF") << " PK Fc " << juce::String (f.freq, 1) << " Hz Gain "
              << juce::String (f.gainDb, 1) << " dB Q " << juce::String (f.q, 3) << "\n";
    }
    return s;
}

juce::String toMiniDSPBiquads (const OutputChannel& ch, double fs)
{
    std::vector<BiquadCoeffs> all;
    auto add = [&] (const FilterSpec& f)
    {
        if (f.enabled)
            for (auto& bq : f.toBiquads (fs))
                all.push_back (bq);
    };
    add (ch.settings.highPass);
    add (ch.settings.lowPass);
    for (auto& f : ch.settings.peq)
        add (f);

    // miniDSP uses the opposite sign convention for a1/a2.
    juce::String s;
    for (size_t i = 0; i < all.size(); ++i)
    {
        const auto& b = all[i];
        s << "biquad" << (int) (i + 1) << ",\n"
          << "b0=" << juce::String (b.b0, 15) << ",\n"
          << "b1=" << juce::String (b.b1, 15) << ",\n"
          << "b2=" << juce::String (b.b2, 15) << ",\n"
          << "a1=" << juce::String (-b.a1, 15) << ",\n"
          << "a2=" << juce::String (-b.a2, 15) << (i + 1 < all.size() ? ",\n" : "\n");
    }
    return s;
}

juce::String toJSON (const SystemConfig& cfg)
{
    return juce::JSON::toString (cfg.toVar());
}

bool fromJSON (const juce::String& json, SystemConfig& out)
{
    const auto v = juce::JSON::parse (json);
    if (! v.isObject())
        return false;
    out = SystemConfig::fromVar (v);
    return ! out.channels.empty();
}
} // namespace fa::exporters
