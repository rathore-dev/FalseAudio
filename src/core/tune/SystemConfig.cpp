#include "SystemConfig.h"

namespace fa
{
juce::String toString (DriverRole r)
{
    switch (r)
    {
        case DriverRole::subwoofer: return "Subwoofer";
        case DriverRole::midbass:   return "Midbass";
        case DriverRole::midrange:  return "Midrange";
        case DriverRole::tweeter:   return "Tweeter";
        case DriverRole::fullrange: return "Full Range";
    }
    return {};
}

juce::String toString (Side s)
{
    switch (s)
    {
        case Side::left:  return "Left";
        case Side::right: return "Right";
        case Side::mono:  return "Mono";
    }
    return {};
}

juce::String toString (ChannelKind k)
{
    switch (k)
    {
        case ChannelKind::activeDriver: return "Active driver";
        case ChannelKind::passiveSet:   return "Passive set";
        case ChannelKind::fullRange:    return "Full range";
        case ChannelKind::sub:          return "Subwoofer";
    }
    return {};
}

ChannelKind OutputChannel::defaultKindFor (DriverRole r)
{
    if (r == DriverRole::subwoofer) return ChannelKind::sub;
    if (r == DriverRole::fullrange) return ChannelKind::fullRange;
    return ChannelKind::activeDriver;
}

void OutputChannel::normaliseKind()
{
    switch (kind)
    {
        case ChannelKind::passiveSet:
            if (role == DriverRole::subwoofer || role == DriverRole::tweeter || role == DriverRole::fullrange)
                role = DriverRole::midbass;   // the woofer carries the set
            break;
        case ChannelKind::fullRange:    role = DriverRole::fullrange; break;
        case ChannelKind::sub:          role = DriverRole::subwoofer; break;
        case ChannelKind::activeDriver:
            if (role == DriverRole::subwoofer || role == DriverRole::fullrange)
                kind = defaultKindFor (role);
            break;
    }
}

double DriverInfo::effectiveDiameterM (DriverRole role) const
{
    double inches = sizeInches;
    if (inches <= 0.0)
    {
        switch (role)
        {
            case DriverRole::subwoofer: inches = 10.0; break;
            case DriverRole::midbass:   inches = 6.5;  break;
            case DriverRole::midrange:  inches = 3.0;  break;
            case DriverRole::tweeter:   inches = 1.0;  break;
            case DriverRole::fullrange: inches = 3.0;  break;
        }
    }
    // Radiating cone is roughly 80% of the nominal frame size (tweeter domes ~ nominal).
    return inches * 0.0254 * (role == DriverRole::tweeter ? 1.0 : 0.8);
}

//==============================================================================
FrequencyResponse ChannelSettings::response (const std::vector<double>& freqs, double fs, bool includeDelay) const
{
    auto r = FrequencyResponse::unity (freqs);
    if (muted)
        return r.scale (0.0);

    r.scale (magFromDb (gainDb) * (inverted ? -1.0 : 1.0));
    r *= highPass.response (freqs, fs);
    r *= lowPass.response (freqs, fs);
    r *= chainResponse (peq, freqs, fs);
    if (includeDelay)
        r.applyDelay (delayMs * 0.001);
    return r;
}

juce::var ChannelSettings::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("gain", gainDb);
    o->setProperty ("delay", delayMs);
    o->setProperty ("inverted", inverted);
    o->setProperty ("muted", muted);
    o->setProperty ("hp", highPass.toVar());
    o->setProperty ("lp", lowPass.toVar());
    juce::Array<juce::var> p;
    for (auto& f : peq)
        p.add (f.toVar());
    o->setProperty ("peq", p);
    return juce::var (o);
}

ChannelSettings ChannelSettings::fromVar (const juce::var& v)
{
    ChannelSettings s;
    s.gainDb   = v.getProperty ("gain", 0.0);
    s.delayMs  = v.getProperty ("delay", 0.0);
    s.inverted = v.getProperty ("inverted", false);
    s.muted    = v.getProperty ("muted", false);
    if (v.hasProperty ("hp")) s.highPass = FilterSpec::fromVar (v["hp"]);
    if (v.hasProperty ("lp")) s.lowPass  = FilterSpec::fromVar (v["lp"]);
    if (auto* p = v.getProperty ("peq", {}).getArray())
        for (auto& f : *p)
            s.peq.push_back (FilterSpec::fromVar (f));
    return s;
}

//==============================================================================
juce::var DspCapabilities::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("sampleRate", sampleRate);
    o->setProperty ("peqBands", peqBandsPerChannel);
    o->setProperty ("peqMinQ", peqMinQ);
    o->setProperty ("peqMaxQ", peqMaxQ);
    o->setProperty ("peqMaxGain", peqMaxGainDb);
    o->setProperty ("peqMinGain", peqMinGainDb);
    o->setProperty ("delayStep", delayStepMs);
    o->setProperty ("maxDelay", maxDelayMs);
    o->setProperty ("gainStep", gainStepDb);
    o->setProperty ("minGain", minChannelGainDb);
    o->setProperty ("maxGain", maxChannelGainDb);
    o->setProperty ("freqStep", freqStepHz);
    juce::Array<juce::var> slopes;
    for (auto s : availableSlopes)
        slopes.add (s);
    o->setProperty ("slopes", slopes);
    o->setProperty ("lr", hasLinkwitzRiley);
    o->setProperty ("bessel", hasBessel);
    o->setProperty ("allpass", hasAllPass);
    o->setProperty ("eqType", isGraphic() ? "graphic" : "parametric");
    if (isGraphic())
    {
        juce::Array<juce::var> bands;
        for (auto f : graphicBandsHz)
            bands.add (f);
        o->setProperty ("geqBands", bands);
        o->setProperty ("geqQ", graphicQ);
        o->setProperty ("geqStep", graphicGainStepDb);
        o->setProperty ("geqMaxBoost", graphicMaxBoostDb);
        o->setProperty ("geqMaxCut", graphicMaxCutDb);
        o->setProperty ("geqPerChannel", graphicPerChannel);
    }
    return juce::var (o);
}

DspCapabilities DspCapabilities::fromVar (const juce::var& v)
{
    DspCapabilities c;
    c.sampleRate         = v.getProperty ("sampleRate", c.sampleRate);
    c.peqBandsPerChannel = v.getProperty ("peqBands", c.peqBandsPerChannel);
    c.peqMinQ            = v.getProperty ("peqMinQ", c.peqMinQ);
    c.peqMaxQ            = v.getProperty ("peqMaxQ", c.peqMaxQ);
    c.peqMaxGainDb       = v.getProperty ("peqMaxGain", c.peqMaxGainDb);
    c.peqMinGainDb       = v.getProperty ("peqMinGain", c.peqMinGainDb);
    c.delayStepMs        = v.getProperty ("delayStep", c.delayStepMs);
    c.maxDelayMs         = v.getProperty ("maxDelay", c.maxDelayMs);
    c.gainStepDb         = v.getProperty ("gainStep", c.gainStepDb);
    c.minChannelGainDb   = v.getProperty ("minGain", c.minChannelGainDb);
    c.maxChannelGainDb   = v.getProperty ("maxGain", c.maxChannelGainDb);
    c.freqStepHz         = v.getProperty ("freqStep", c.freqStepHz);
    if (auto* s = v.getProperty ("slopes", {}).getArray())
    {
        c.availableSlopes.clear();
        for (auto& x : *s)
            c.availableSlopes.push_back ((int) x);
    }
    c.hasLinkwitzRiley = v.getProperty ("lr", c.hasLinkwitzRiley);
    c.hasBessel        = v.getProperty ("bessel", c.hasBessel);
    c.hasAllPass       = v.getProperty ("allpass", c.hasAllPass);
    c.eqType = v.getProperty ("eqType", "parametric").toString() == "graphic" ? EqType::graphic : EqType::parametric;
    if (auto* b = v.getProperty ("geqBands", {}).getArray())
        for (auto& x : *b)
            c.graphicBandsHz.push_back ((double) x);
    c.graphicQ          = v.getProperty ("geqQ", c.graphicQ);
    c.graphicGainStepDb = v.getProperty ("geqStep", c.graphicGainStepDb);
    c.graphicMaxBoostDb = v.getProperty ("geqMaxBoost", c.graphicMaxBoostDb);
    c.graphicMaxCutDb   = v.getProperty ("geqMaxCut", c.graphicMaxCutDb);
    c.graphicPerChannel = v.getProperty ("geqPerChannel", c.graphicPerChannel);
    return c;
}

std::vector<double> DspCapabilities::graphicCentres() const
{
    return graphicBandsHz.empty() ? standardGraphicBands (31) : graphicBandsHz;
}

std::vector<double> DspCapabilities::standardGraphicBands (int count)
{
    // ISO 266 nominal 1/3-octave centres 20 Hz..20 kHz.
    static const std::vector<double> iso31 { 20, 25, 31.5, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500,
                                             630, 800, 1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000,
                                             10000, 12500, 16000, 20000 };
    auto every = [] (size_t first, size_t step, size_t last)
    {
        std::vector<double> out;
        for (size_t i = first; i <= last && i < iso31.size(); i += step)
            out.push_back (iso31[i]);
        return out;
    };

    switch (count)
    {
        case 31: return iso31;
        case 30: return every (1, 1, 30);                    // 25 Hz..20 kHz (AudioControl, JL FiX)
        case 15: return every (1, 2, 29);                    // 25 Hz..16 kHz, 2/3 oct
        case 13: return every (4, 2, 28);                    // 50 Hz..12.5 kHz (Pioneer/Kenwood/JVC/Alpine)
        case 10: return every (2, 3, 29);                    // 31.5 Hz..16 kHz octaves
        case 20: return { 20, 28, 40, 56, 80, 112, 160, 225, 315, 450, 630, 900, 1250, 1800, 2500, 3550, 5000,
                          7100, 10000, 14000 };
        case 21: return { 20, 28, 40, 56, 80, 112, 160, 225, 315, 450, 630, 900, 1250, 1800, 2500, 3550, 5000,
                          7100, 10000, 14000, 20000 };
        default: break;
    }

    std::vector<double> out;
    count = juce::jlimit (1, 64, count);
    for (int i = 0; i < count; ++i)
    {
        const double f = count == 1 ? 1000.0 : 40.0 * std::pow (16000.0 / 40.0, (double) i / (count - 1));
        const double mag = std::pow (10.0, std::floor (std::log10 (f)) - 1.0);   // two significant digits
        out.push_back (std::round (f / mag) * mag);
    }
    return out;
}

//==============================================================================
OutputChannel* SystemConfig::find (const juce::String& id)
{
    for (auto& c : channels)
        if (c.id == id)
            return &c;
    return nullptr;
}

const OutputChannel* SystemConfig::find (const juce::String& id) const
{
    for (auto& c : channels)
        if (c.id == id)
            return &c;
    return nullptr;
}

std::vector<OutputChannel*> SystemConfig::channelsFor (Side side)
{
    std::vector<OutputChannel*> out;
    for (auto& c : channels)
        if (c.side == side)
            out.push_back (&c);
    return out;
}

std::vector<const OutputChannel*> SystemConfig::channelsFor (Side side) const
{
    std::vector<const OutputChannel*> out;
    for (auto& c : channels)
        if (c.side == side)
            out.push_back (&c);
    return out;
}

bool SystemConfig::hasSubwoofer() const
{
    for (auto& c : channels)
        if (c.role == DriverRole::subwoofer)
            return true;
    return false;
}

bool SystemConfig::isStereo() const
{
    for (auto& c : channels)
        if (c.side == Side::right && c.role != DriverRole::subwoofer)
            return true;
    return false;
}

bool SystemConfig::hasPassiveSets() const
{
    for (auto& c : channels)
        if (c.isPassiveSet())
            return true;
    return false;
}

juce::String SystemConfig::presetName (Preset p)
{
    switch (p)
    {
        case Preset::active3WaySub:  return "Active 3-way + sub";
        case Preset::active2WaySub:  return "Active 2-way + sub";
        case Preset::passive2WaySub: return "Passive 2-way front + sub";
        case Preset::passiveFront:   return "Passive front only";
        case Preset::fullRangePair:  return "Full-range pair";
        case Preset::singleDriver:   return "Single driver (test box)";
    }
    return {};
}

SystemConfig SystemConfig::makePreset (Preset p)
{
    SystemConfig cfg;
    switch (p)
    {
        case Preset::active3WaySub:  cfg = makeLayout (3, true); break;
        case Preset::active2WaySub:  cfg = makeLayout (2, true); break;
        case Preset::passive2WaySub:
        case Preset::passiveFront:
        {
            int out = 0;
            for (auto side : { Side::left, Side::right })
            {
                OutputChannel ch;
                ch.side = side;
                ch.role = DriverRole::midbass;
                ch.kind = ChannelKind::passiveSet;
                ch.dspOutput = out++;
                ch.id = side == Side::left ? "FL.set" : "FR.set";
                ch.label = side == Side::left ? "Left Component Set" : "Right Component Set";
                ch.driver.sizeInches = 6.5;
                cfg.channels.push_back (ch);
            }
            if (p == Preset::passive2WaySub)
            {
                OutputChannel sub;
                sub.side = Side::mono;
                sub.role = DriverRole::subwoofer;
                sub.kind = ChannelKind::sub;
                sub.dspOutput = out++;
                sub.id = "SUB";
                sub.label = "Subwoofer";
                cfg.channels.push_back (sub);
            }
            break;
        }
        case Preset::fullRangePair:  cfg = makeLayout (1, false); break;
        case Preset::singleDriver:
        {
            OutputChannel ch;
            ch.side = Side::mono;
            ch.role = DriverRole::fullrange;
            ch.kind = ChannelKind::fullRange;
            ch.dspOutput = 0;
            ch.id = "DRV";
            ch.label = "Driver";
            ch.driver.sizeInches = 5.25;
            cfg.channels.push_back (ch);
            break;
        }
    }
    cfg.name = presetName (p);
    return cfg;
}

SystemConfig SystemConfig::makeLayout (int ways, bool withSub, bool stereoSub)
{
    SystemConfig cfg;
    ways = juce::jlimit (1, 4, ways);

    std::vector<DriverRole> roles;
    switch (ways)
    {
        case 1:  roles = { DriverRole::fullrange }; break;
        case 2:  roles = { DriverRole::midbass, DriverRole::tweeter }; break;
        case 3:  roles = { DriverRole::midbass, DriverRole::midrange, DriverRole::tweeter }; break;
        default: roles = { DriverRole::subwoofer, DriverRole::midbass, DriverRole::midrange, DriverRole::tweeter }; break;
    }

    // In a "4-way" front stage the lowest way is usually a kick/midbass pair rather than a sub;
    // we model 4-way as kick (midbass, 8") + midbass (6.5") + mid + tweeter per side.
    int out = 0;
    for (auto side : { Side::left, Side::right })
    {
        const juce::String prefix = side == Side::left ? "FL" : "FR";
        for (size_t i = 0; i < roles.size(); ++i)
        {
            OutputChannel ch;
            ch.side = side;
            ch.role = roles[i];
            ch.dspOutput = out++;

            juce::String roleName = toString (roles[i]).toLowerCase().removeCharacters (" ");
            if (ways == 4 && i == 0)
            {
                ch.role = DriverRole::midbass;
                ch.driver.sizeInches = 8.0;
                roleName = "kick";
            }
            ch.kind = OutputChannel::defaultKindFor (ch.role);
            ch.id = prefix + "." + roleName;
            ch.label = (side == Side::left ? "Left " : "Right ") + (ways == 4 && i == 0 ? juce::String ("Kick") : toString (ch.role));
            cfg.channels.push_back (ch);
        }
    }

    if (withSub)
    {
        if (stereoSub)
        {
            for (auto side : { Side::left, Side::right })
            {
                OutputChannel ch;
                ch.side = side;
                ch.role = DriverRole::subwoofer;
                ch.dspOutput = out++;
                ch.kind = ChannelKind::sub;
                ch.id = side == Side::left ? "SUB.L" : "SUB.R";
                ch.label = side == Side::left ? "Subwoofer L" : "Subwoofer R";
                cfg.channels.push_back (ch);
            }
        }
        else
        {
            OutputChannel ch;
            ch.side = Side::mono;
            ch.role = DriverRole::subwoofer;
            ch.dspOutput = out++;
            ch.kind = ChannelKind::sub;
            ch.id = "SUB";
            ch.label = "Subwoofer";
            cfg.channels.push_back (ch);
        }
    }
    return cfg;
}

juce::var SystemConfig::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("name", name);
    o->setProperty ("caps", caps.toVar());
    o->setProperty ("target", target.toVar());
    o->setProperty ("seat", listeningSeat);

    juce::Array<juce::var> chs;
    for (auto& c : channels)
    {
        auto* co = new juce::DynamicObject();
        co->setProperty ("id", c.id);
        co->setProperty ("label", c.label);
        co->setProperty ("side", (int) c.side);
        co->setProperty ("role", (int) c.role);
        co->setProperty ("dspOutput", c.dspOutput);
        co->setProperty ("kind", (int) c.kind);
        if (c.isPassiveSet())
        {
            auto* po = new juce::DynamicObject();
            po->setProperty ("xover", c.passive.crossoverHz);
            po->setProperty ("slope", c.passive.slopeDbPerOct);
            po->setProperty ("coax", c.passive.coaxial);
            po->setProperty ("tweeterModel", c.passive.tweeterModel);
            po->setProperty ("tweeterSize", c.passive.tweeterSizeInches);
            co->setProperty ("passive", juce::var (po));
        }
        co->setProperty ("model", c.driver.model);
        co->setProperty ("size", c.driver.sizeInches);
        co->setProperty ("fs", c.driver.fsHz);
        co->setProperty ("power", c.driver.powerRmsW);
        co->setProperty ("xmax", c.driver.xmaxMm);
        co->setProperty ("settings", c.settings.toVar());
        chs.add (juce::var (co));
    }
    o->setProperty ("channels", chs);
    return juce::var (o);
}

SystemConfig SystemConfig::fromVar (const juce::var& v)
{
    SystemConfig cfg;
    cfg.name = v.getProperty ("name", cfg.name).toString();
    cfg.caps = DspCapabilities::fromVar (v.getProperty ("caps", {}));
    cfg.target = TargetCurve::fromVar (v.getProperty ("target", {}));
    cfg.listeningSeat = v.getProperty ("seat", 0);

    if (auto* chs = v.getProperty ("channels", {}).getArray())
    {
        for (auto& c : *chs)
        {
            OutputChannel ch;
            ch.id = c.getProperty ("id", {}).toString();
            ch.label = c.getProperty ("label", {}).toString();
            ch.side = (Side) (int) c.getProperty ("side", 0);
            ch.role = (DriverRole) (int) c.getProperty ("role", 1);
            ch.dspOutput = c.getProperty ("dspOutput", 0);
            // Legacy configs have no "kind": derive it from the role (fully active layouts).
            ch.kind = c.hasProperty ("kind") ? (ChannelKind) juce::jlimit (0, 3, (int) c.getProperty ("kind", 0))
                                             : OutputChannel::defaultKindFor (ch.role);
            if (c.hasProperty ("passive"))
            {
                const auto po = c["passive"];
                ch.passive.crossoverHz = po.getProperty ("xover", 0.0);
                ch.passive.slopeDbPerOct = po.getProperty ("slope", 12);
                ch.passive.coaxial = po.getProperty ("coax", false);
                ch.passive.tweeterModel = po.getProperty ("tweeterModel", {}).toString();
                ch.passive.tweeterSizeInches = po.getProperty ("tweeterSize", 1.0);
            }
            ch.driver.model = c.getProperty ("model", {}).toString();
            ch.driver.sizeInches = c.getProperty ("size", 0.0);
            ch.driver.fsHz = c.getProperty ("fs", 0.0);
            ch.driver.powerRmsW = c.getProperty ("power", 0.0);
            ch.driver.xmaxMm = c.getProperty ("xmax", 0.0);
            ch.settings = ChannelSettings::fromVar (c.getProperty ("settings", {}));
            cfg.channels.push_back (ch);
        }
    }
    return cfg;
}
} // namespace fa
