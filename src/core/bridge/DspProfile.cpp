#include "DspProfile.h"

namespace fa
{
juce::var ControlBinding::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("method", (int) method);
    o->setProperty ("path", path.toVar());
    o->setProperty ("relX", relX);
    o->setProperty ("relY", relY);
    o->setProperty ("unit", unit);
    o->setProperty ("decimals", decimals);
    o->setProperty ("scale", scale);
    o->setProperty ("offset", offset);
    o->setProperty ("invertToggle", invertToggle);
    o->setProperty ("settleMs", settleMs);
    o->setProperty ("captured", capturedDescription);

    auto* map = new juce::DynamicObject();
    for (auto& [k, v] : valueMap)
        map->setProperty (k, v);
    o->setProperty ("valueMap", juce::var (map));
    return juce::var (o);
}

ControlBinding ControlBinding::fromVar (const juce::var& v)
{
    ControlBinding b;
    b.method       = (Method) (int) v.getProperty ("method", 0);
    b.path         = uia::ElementPath::fromVar (v.getProperty ("path", {}));
    b.relX         = v.getProperty ("relX", 0.5);
    b.relY         = v.getProperty ("relY", 0.5);
    b.unit         = v.getProperty ("unit", {}).toString();
    b.decimals     = v.getProperty ("decimals", 2);
    b.scale        = v.getProperty ("scale", 1.0);
    b.offset       = v.getProperty ("offset", 0.0);
    b.invertToggle = v.getProperty ("invertToggle", false);
    b.settleMs     = v.getProperty ("settleMs", 60);
    b.capturedDescription = v.getProperty ("captured", {}).toString();

    if (auto* map = v.getProperty ("valueMap", {}).getDynamicObject())
        for (auto& p : map->getProperties())
            b.valueMap[p.name.toString()] = p.value.toString();
    return b;
}

//==============================================================================
juce::String DspProfile::keyFor (const juce::String& key, int channel, int band)
{
    juce::String k = key;
    if (channel >= 0) k << "@c" << channel;
    if (band >= 0)    k << "@b" << band;
    return k;
}

const ControlBinding* DspProfile::find (const juce::String& key, int channel, int band) const
{
    for (auto& k : { keyFor (key, channel, band), keyFor (key, channel, -1), keyFor (key, -1, band), keyFor (key, -1, -1) })
    {
        auto it = bindings.find (k);
        if (it != bindings.end())
            return &it->second;
    }
    return nullptr;
}

juce::var DspProfile::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("format", "FalseAudio DSP profile 1");
    o->setProperty ("name", name);
    o->setProperty ("process", processName);
    o->setProperty ("title", windowTitleContains);
    o->setProperty ("caps", caps.toVar());

    auto* b = new juce::DynamicObject();
    for (auto& [k, v] : bindings)
        b->setProperty (k, v.toVar());
    o->setProperty ("bindings", juce::var (b));
    return juce::var (o);
}

DspProfile DspProfile::fromVar (const juce::var& v)
{
    DspProfile p;
    p.name = v.getProperty ("name", p.name).toString();
    p.processName = v.getProperty ("process", {}).toString();
    p.windowTitleContains = v.getProperty ("title", {}).toString();
    p.caps = DspCapabilities::fromVar (v.getProperty ("caps", {}));
    if (auto* b = v.getProperty ("bindings", {}).getDynamicObject())
        for (auto& prop : b->getProperties())
            p.bindings[prop.name.toString()] = ControlBinding::fromVar (prop.value);
    return p;
}

bool DspProfile::save (const juce::File& f) const
{
    f.getParentDirectory().createDirectory();
    return f.replaceWithText (juce::JSON::toString (toVar(), false));
}

bool DspProfile::load (const juce::File& f, DspProfile& out)
{
    const auto v = juce::JSON::parse (f.loadFileAsString());
    if (! v.isObject() || ! v.getProperty ("format", {}).toString().startsWith ("FalseAudio DSP profile"))
        return false;
    out = fromVar (v);
    return true;
}

juce::File DspProfile::profilesFolder()
{
    auto f = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                 .getChildFile ("FalseAudio").getChildFile ("DSP Profiles");
    f.createDirectory();
    return f;
}

juce::Array<juce::File> DspProfile::listProfiles()
{
    return profilesFolder().findChildFiles (juce::File::findFiles, false, "*.json");
}

//==============================================================================
const std::vector<TeachSlot>& teachSlots()
{
    using K = TeachSlot::Kind;
    using S = TeachSlot::Scope;
    static const std::vector<TeachSlot> slots
    {
        { "channel.select", "Select output channel", "Click the control that selects this output for editing (tab, button or list entry). Skip if every channel has its own controls.", K::action, S::perChannel, false },
        { "gain",      "Channel gain / level",   "The gain or level field of the selected output.", K::numeric, S::global, true },
        { "delay",     "Channel delay",          "The delay / distance field. Set the unit it uses (ms, cm, m, in, samples).", K::numeric, S::global, true },
        { "polarity",  "Polarity / phase invert","The phase 0/180 switch or checkbox.", K::toggle, S::global, true },
        { "mute",      "Mute",                   "The mute button of the selected output.", K::toggle, S::global, true },
        { "hp.freq",   "High-pass frequency",    "Frequency field of the high-pass filter.", K::numeric, S::global, true },
        { "hp.type",   "High-pass type / slope", "The combo that selects filter type and slope (e.g. Linkwitz 24 dB).", K::select, S::global, true },
        { "lp.freq",   "Low-pass frequency",     "Frequency field of the low-pass filter.", K::numeric, S::global, true },
        { "lp.type",   "Low-pass type / slope",  "The combo that selects filter type and slope.", K::select, S::global, true },
        { "peq.select","Select EQ band",         "Control that selects an EQ band for editing. Skip if every band has its own fields.", K::action, S::perBand, false },
        { "peq.freq",  "EQ band frequency",      "Frequency field of the selected EQ band.", K::numeric, S::global, true },
        { "peq.gain",  "EQ band gain",           "Gain field of the selected EQ band.", K::numeric, S::global, true },
        { "peq.q",     "EQ band Q / bandwidth",  "Q (or bandwidth) field of the selected EQ band.", K::numeric, S::global, true },
        { "save",      "Store / save to DSP",    "Optional: the button that stores the settings in the DSP's memory.", K::action, S::global, false },
    };
    return slots;
}

juce::String crossoverKey (const FilterSpec& f)
{
    juce::String prefix = f.alignment == Alignment::linkwitzRiley ? "LR" : f.alignment == Alignment::bessel ? "BS" : "BW";
    return prefix + juce::String (f.slopeDbPerOct);
}
} // namespace fa
