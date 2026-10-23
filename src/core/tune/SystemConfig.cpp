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
    return c;
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
