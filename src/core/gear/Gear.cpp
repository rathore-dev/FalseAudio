#include "Gear.h"

#include <algorithm>
#include <cmath>

namespace fa::gear
{
//==============================================================================
// Categories
//==============================================================================
namespace
{
struct CategoryName { Category c; const char* name; };

const CategoryName categoryNames[] =
{
    { Category::headUnit, "headUnit" },             { Category::dsp, "dsp" },
    { Category::dspAmp, "dspAmp" },                 { Category::dac, "dac" },
    { Category::ampMulti, "ampMulti" },             { Category::ampMono, "ampMono" },
    { Category::driverTweeter, "driverTweeter" },   { Category::driverMidrange, "driverMidrange" },
    { Category::driverMidbass, "driverMidbass" },   { Category::driverCoax, "driverCoax" },
    { Category::componentSet, "componentSet" },     { Category::subwoofer, "subwoofer" },
    { Category::powerWire, "powerWire" },           { Category::speakerWire, "speakerWire" },
    { Category::rca, "rca" },                       { Category::fuseHolder, "fuseHolder" },
    { Category::battery, "battery" },               { Category::alternator, "alternator" },
    { Category::audioInterface, "audioInterface" }, { Category::measurementMic, "measurementMic" }
};
} // namespace

juce::String toString (Category c)
{
    for (auto& cn : categoryNames)
        if (cn.c == c)
            return cn.name;

    return "headUnit";
}

std::optional<Category> tryCategoryFromString (const juce::String& s)
{
    const auto t = s.trim();

    for (auto& cn : categoryNames)
        if (t.equalsIgnoreCase (cn.name))
            return cn.c;

    return std::nullopt;
}

Category categoryFromString (const juce::String& s)
{
    return tryCategoryFromString (s).value_or (Category::headUnit);
}

const std::vector<Category>& allCategories()
{
    static const std::vector<Category> all = []
    {
        std::vector<Category> v;
        for (auto& cn : categoryNames)
            v.push_back (cn.c);
        return v;
    }();
    return all;
}

juce::String makeProductId (const juce::String& brand, const juce::String& model)
{
    const auto src = (brand.trim() + " " + model.trim()).toLowerCase();
    juce::String out;
    out.preallocateBytes ((size_t) src.getNumBytesAsUTF8());
    bool lastDash = true;   // suppresses leading dashes

    for (auto p = src.getCharPointer(); ! p.isEmpty(); ++p)
    {
        const auto ch = *p;

        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9'))
        {
            out << juce::String::charToString (ch);
            lastDash = false;
        }
        else if (! lastDash)
        {
            out << "-";
            lastDash = true;
        }
    }

    while (out.endsWithChar ('-'))
        out = out.dropLastCharacters (1);

    return out;
}

//==============================================================================
// JSON <-> struct field visitors
//==============================================================================
namespace
{
bool toNumber (const juce::var& v, double& out)
{
    if (v.isDouble() || v.isInt() || v.isInt64() || v.isBool())
    {
        const double d = (double) v;
        if (! std::isfinite (d))
            return false;
        out = d;
        return true;
    }

    if (v.isString())
    {
        const auto s = v.toString().trim();
        if (s.isEmpty() || ! s.containsOnly ("0123456789.-+eE"))
            return false;
        const double d = s.getDoubleValue();
        if (! std::isfinite (d))
            return false;
        out = d;
        return true;
    }

    return false;
}

/** Reads fields from a JSON object; unknown / mistyped fields keep their defaults. */
struct Reader
{
    const juce::var& obj;

    juce::var get (const char* key) const   { return obj.getProperty (juce::Identifier (key), {}); }

    void operator() (const char* key, double& x) const
    {
        double d;
        if (toNumber (get (key), d))
            x = d;
    }

    void operator() (const char* key, int& x) const
    {
        double d;
        if (toNumber (get (key), d))
            x = (int) std::lround (d);
    }

    void operator() (const char* key, bool& x) const
    {
        const auto v = get (key);
        if (v.isBool() || v.isInt() || v.isInt64() || v.isDouble())
            x = (bool) v;
        else if (v.isString())
            x = v.toString().trim().equalsIgnoreCase ("true") || v.toString().trim().equalsIgnoreCase ("yes")
                 || v.toString().trim() == "1";
    }

    void operator() (const char* key, juce::String& x) const
    {
        const auto v = get (key);
        if (v.isString() || v.isDouble() || v.isInt() || v.isInt64())
            x = v.toString();
    }

    void operator() (const char* key, std::map<juce::String, double>& x) const
    {
        const auto v = get (key);
        if (auto* o = v.getDynamicObject())
        {
            for (auto& nv : o->getProperties())
            {
                double d;
                if (toNumber (nv.value, d))
                    x[nv.name.toString()] = d;
            }
        }
    }

    void operator() (const char* key, juce::StringArray& x) const
    {
        const auto v = get (key);
        if (auto* arr = v.getArray())
        {
            for (auto& e : *arr)
                if (e.isString() || e.isDouble() || e.isInt() || e.isInt64())
                    x.add (e.toString());
        }
        else if (v.isString() && v.toString().isNotEmpty())
        {
            x.addTokens (v.toString(), ",", "\"");
            x.trim();
            x.removeEmptyStrings();
        }
    }
};

/** Writes fields to a JSON object, omitting defaults (0 / false / empty). */
struct Writer
{
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();

    void operator() (const char* key, const double& x) const        { if (x != 0.0) obj->setProperty (key, x); }
    void operator() (const char* key, const int& x) const           { if (x != 0) obj->setProperty (key, x); }
    void operator() (const char* key, const bool& x) const          { if (x) obj->setProperty (key, true); }
    void operator() (const char* key, const juce::String& x) const  { if (x.isNotEmpty()) obj->setProperty (key, x); }

    void operator() (const char* key, const std::map<juce::String, double>& x) const
    {
        if (x.empty())
            return;

        juce::DynamicObject::Ptr m = new juce::DynamicObject();
        for (auto& [k, val] : x)
            m->setProperty (k, val);
        obj->setProperty (key, juce::var (m.get()));
    }

    void operator() (const char* key, const juce::StringArray& x) const
    {
        if (x.isEmpty())
            return;

        juce::Array<juce::var> arr;
        for (auto& s : x)
            arr.add (s);
        obj->setProperty (key, arr);
    }
};

template <typename S, typename V>
void visitDriver (S& d, V& v)
{
    v ("sizeInches", d.sizeInches);     v ("nominalOhms", d.nominalOhms);   v ("reOhms", d.reOhms);
    v ("fsHz", d.fsHz);                 v ("qts", d.qts);                   v ("qes", d.qes);
    v ("qms", d.qms);                   v ("vasLitres", d.vasLitres);       v ("xmaxMm", d.xmaxMm);
    v ("sdCm2", d.sdCm2);               v ("bl", d.bl);                     v ("mmsGrams", d.mmsGrams);
    v ("leMh", d.leMh);                 v ("sensitivityDb", d.sensitivityDb);
    v ("sensitivityRef", d.sensitivityRef);
    v ("powerRmsW", d.powerRmsW);       v ("powerPeakW", d.powerPeakW);
    v ("freqLowHz", d.freqLowHz);       v ("freqHighHz", d.freqHighHz);
    v ("voiceCoils", d.voiceCoils);     v ("coilOhms", d.coilOhms);
    v ("mountingDepthMm", d.mountingDepthMm);   v ("cutoutMm", d.cutoutMm);
    v ("recommendedHpHz", d.recommendedHpHz);   v ("recommendedLpHz", d.recommendedLpHz);
}

template <typename S, typename V>
void visitAmp (S& a, V& v)
{
    v ("channels", a.channels);                 v ("rmsPerChannelW", a.rmsPerChannelW);
    v ("inputSensitivityMinV", a.inputSensitivityMinV);
    v ("inputSensitivityMaxV", a.inputSensitivityMaxV);
    v ("highLevelInputMaxV", a.highLevelInputMaxV);
    v ("snrDb", a.snrDb);                       v ("thdPercent", a.thdPercent);
    v ("dampingFactor", a.dampingFactor);       v ("fuseRatingA", a.fuseRatingA);
    v ("efficiencyPercent", a.efficiencyPercent);
    v ("amplifierClass", a.amplifierClass);     v ("minStableOhms", a.minStableOhms);
    v ("hasBassBoost", a.hasBassBoost);         v ("hasLpf", a.hasLpf);
    v ("hasHpf", a.hasHpf);                     v ("hasSubsonic", a.hasSubsonic);
    v ("bassBoostMaxDb", a.bassBoostMaxDb);
}

template <typename S, typename V>
void visitSource (S& s, V& v)
{
    v ("preoutVoltageV", s.preoutVoltageV);     v ("preoutChannels", s.preoutChannels);
    v ("outputImpedanceOhms", s.outputImpedanceOhms);
    v ("maxVolumeStep", s.maxVolumeStep);       v ("cleanVolumeStep", s.cleanVolumeStep);
    v ("snrDb", s.snrDb);                       v ("timeAlignment", s.timeAlignment);
    v ("crossovers", s.crossovers);             v ("eqBands", s.eqBands);
    v ("eqType", s.eqType);                     v ("inputs", s.inputs);
    v ("outputs", s.outputs);                   v ("peqBandsPerChannel", s.peqBandsPerChannel);
    v ("maxDelayMs", s.maxDelayMs);             v ("sampleRateHz", s.sampleRateHz);
    v ("maxInputVoltageV", s.maxInputVoltageV); v ("software", s.software);
    v ("softwareExe", s.softwareExe);
}

template <typename S, typename V>
void visitWire (S& w, V& v)
{
    v ("awg", w.awg);               v ("mm2", w.mm2);       v ("material", w.material);
    v ("ohmsPerMetre", w.ohmsPerMetre);                     v ("ampacityA", w.ampacityA);
}

template <typename S, typename V>
void visitInterface (S& i, V& v)
{
    v ("inputs", i.inputs);                 v ("outputs", i.outputs);       v ("micPreamps", i.micPreamps);
    v ("phantomPower", i.phantomPower);     v ("hasLoopback", i.hasLoopback);
    v ("asio", i.asio);                     v ("maxGainDb", i.maxGainDb);   v ("maxOutputDbu", i.maxOutputDbu);
    v ("sampleRates", i.sampleRates);       v ("connection", i.connection);
}

template <typename S, typename V>
void visitMic (S& m, V& v)
{
    v ("type", m.type);                         v ("needsPhantom", m.needsPhantom);
    v ("sensitivityMvPa", m.sensitivityMvPa);   v ("calFileAvailable", m.calFileAvailable);
    v ("calFileUrl", m.calFileUrl);             v ("maxSplDb", m.maxSplDb);
}

template <typename Spec, typename VisitFn>
void readBlock (const juce::var& parent, const char* key, std::optional<Spec>& out, VisitFn visit)
{
    const auto v = parent.getProperty (juce::Identifier (key), {});
    if (! v.isObject())
        return;

    Spec s;
    Reader r { v };
    visit (s, r);
    out = s;
}

template <typename Spec, typename VisitFn>
void writeBlock (juce::DynamicObject& parent, const char* key, const std::optional<Spec>& in, VisitFn visit)
{
    if (! in.has_value())
        return;

    Writer w;
    visit (*in, w);
    parent.setProperty (key, juce::var (w.obj.get()));
}

/** Parses one product entry; returns false (with a reason) when it isn't usable. */
bool parseProduct (const juce::var& v, std::optional<Category> fallbackCategory, Product& p, juce::String& reason)
{
    if (! v.isObject())
    {
        reason = "entry is not an object";
        return false;
    }

    Reader r { v };
    r ("id", p.id);
    r ("brand", p.brand);
    r ("model", p.model);
    r ("series", p.series);
    r ("year", p.year);
    r ("sourceUrl", p.sourceUrl);
    r ("notes", p.notes);
    r ("verified", p.verified);

    p.id = p.id.trim();
    p.brand = p.brand.trim();
    p.model = p.model.trim();
    p.series = p.series.trim();

    bool ok = true;
    const auto catVar = v.getProperty ("category", {});
    if (auto c = tryCategoryFromString (catVar.toString()))
        p.category = *c;
    else if (fallbackCategory.has_value() && catVar.toString().trim().isEmpty())   // only when omitted
        p.category = *fallbackCategory;
    else
    {
        reason = "unknown category '" + catVar.toString() + "'";
        ok = false;
    }

    readBlock (v, "driver", p.driver, [] (auto& s, auto& vis) { visitDriver (s, vis); });
    readBlock (v, "amp", p.amp, [] (auto& s, auto& vis) { visitAmp (s, vis); });
    readBlock (v, "source", p.source, [] (auto& s, auto& vis) { visitSource (s, vis); });
    readBlock (v, "wire", p.wire, [] (auto& s, auto& vis) { visitWire (s, vis); });
    readBlock (v, "audioInterface", p.audioInterface, [] (auto& s, auto& vis) { visitInterface (s, vis); });
    readBlock (v, "mic", p.mic, [] (auto& s, auto& vis) { visitMic (s, vis); });

    if (ok && (p.brand.isEmpty() || p.model.isEmpty()))
    {
        reason = "missing brand or model";
        ok = false;
    }

    if (p.id.isEmpty())
        p.id = makeProductId (p.brand, p.model);

    if (ok && p.id.isEmpty())
    {
        reason = "cannot derive an id";
        ok = false;
    }

    return ok;
}

juce::String alnumOnly (const juce::String& s)
{
    juce::String out;
    for (auto p = s.getCharPointer(); ! p.isEmpty(); ++p)
        if (juce::CharacterFunctions::isLetterOrDigit (*p))
            out << juce::String::charToString (*p);
    return out;
}

juce::String makeSearchKey (const Product& p)
{
    return (p.brand + " " + p.model + " " + p.series + " " + p.id + " " + alnumOnly (p.brand + p.model) + " "
            + alnumOnly (p.model)).toLowerCase();
}

bool lessByBrandModel (const Product* a, const Product* b)
{
    if (const int c = a->brand.compareIgnoreCase (b->brand); c != 0)
        return c < 0;
    return a->model.compareNatural (b->model) < 0;
}
} // namespace

//==============================================================================
// Product
//==============================================================================
juce::var Product::toVar() const
{
    juce::DynamicObject::Ptr o = new juce::DynamicObject();
    o->setProperty ("id", id);
    o->setProperty ("category", toString (category));
    o->setProperty ("brand", brand);
    o->setProperty ("model", model);
    if (series.isNotEmpty())     o->setProperty ("series", series);
    if (year != 0)               o->setProperty ("year", year);
    if (sourceUrl.isNotEmpty())  o->setProperty ("sourceUrl", sourceUrl);
    o->setProperty ("verified", verified);
    if (notes.isNotEmpty())      o->setProperty ("notes", notes);

    writeBlock (*o, "driver", driver, [] (auto& s, auto& vis) { visitDriver (s, vis); });
    writeBlock (*o, "amp", amp, [] (auto& s, auto& vis) { visitAmp (s, vis); });
    writeBlock (*o, "source", source, [] (auto& s, auto& vis) { visitSource (s, vis); });
    writeBlock (*o, "wire", wire, [] (auto& s, auto& vis) { visitWire (s, vis); });
    writeBlock (*o, "audioInterface", audioInterface, [] (auto& s, auto& vis) { visitInterface (s, vis); });
    writeBlock (*o, "mic", mic, [] (auto& s, auto& vis) { visitMic (s, vis); });

    // awg 0 means 1/0, so keep it explicitly when no mm2 size is given
    if (wire.has_value() && wire->awg == 0.0 && wire->mm2 == 0.0)
        if (auto* w = o->getProperty ("wire").getDynamicObject())
            w->setProperty ("awg", 0);

    return juce::var (o.get());
}

Product Product::fromVar (const juce::var& v)
{
    Product p;
    juce::String reason;
    parseProduct (v, std::nullopt, p, reason);
    return p;
}

//==============================================================================
// specsSummary
//==============================================================================
namespace
{
juce::String num (double v, int maxDecimals = 2)
{
    juce::String s (v, maxDecimals);
    if (s.containsChar ('.'))
    {
        s = s.trimCharactersAtEnd ("0");
        if (s.endsWithChar ('.'))
            s = s.dropLastCharacters (1);
    }
    return s;
}

const juce::String& ohm()
{
    static const juce::String s (juce::CharPointer_UTF8 ("\xce\xa9"));
    return s;
}

juce::String awgName (double awg)
{
    if (awg <= -3.0) return "4/0 AWG";
    if (awg <= -2.0) return "3/0 AWG";
    if (awg <= -1.0) return "2/0 AWG";
    if (awg == 0.0)  return "1/0 AWG";
    return num (awg, 1) + " AWG";
}

void addDriverSummary (const DriverSpecs& d, juce::StringArray& out)
{
    if (d.sizeInches > 0)   out.add (num (d.sizeInches, 2) + "\"");
    if (d.voiceCoils >= 2 && d.coilOhms > 0)
        out.add ("DVC " + num (d.coilOhms) + " " + ohm());
    else if (d.nominalOhms > 0)
        out.add (num (d.nominalOhms) + " " + ohm());
    if (d.fsHz > 0)          out.add ("Fs " + num (d.fsHz, 1) + " Hz");
    if (d.qts > 0)           out.add ("Qts " + num (d.qts, 3));
    if (d.vasLitres > 0)     out.add ("Vas " + num (d.vasLitres, 1) + " L");
    if (d.xmaxMm > 0)        out.add ("Xmax " + num (d.xmaxMm, 1) + " mm");
    if (d.sensitivityDb > 0) out.add (num (d.sensitivityDb, 1) + " dB");
    if (d.powerRmsW > 0)     out.add (num (d.powerRmsW, 0) + " W RMS");
    if (d.mountingDepthMm > 0) out.add ("Depth " + num (d.mountingDepthMm, 1) + " mm");
}

void addAmpSummary (const AmpSpecs& a, juce::StringArray& out)
{
    if (a.channels > 0)
        out.add (juce::String (a.channels) + " ch");

    static const char* order[] = { "4ohm", "2ohm", "1ohm", "bridged4ohm", "bridged2ohm" };
    int shown = 0;
    for (auto* key : order)
    {
        auto it = a.rmsPerChannelW.find (key);
        if (it == a.rmsPerChannelW.end() || it->second <= 0 || shown >= 2)
            continue;

        const juce::String k (key);
        const bool bridged = k.startsWith ("bridged");
        const auto load = k.fromLastOccurrenceOf ("bridged", false, false).upToFirstOccurrenceOf ("ohm", false, false);
        juce::String s = num (it->second, 0) + " W";
        if (! bridged && a.channels > 1)
            s << " x " << a.channels;
        s << " @ " << load << " " << ohm();
        if (bridged)
            s << " bridged";
        out.add (s);
        ++shown;
    }

    if (a.amplifierClass.isNotEmpty())  out.add ("Class " + a.amplifierClass);
    if (a.minStableOhms > 0)            out.add (num (a.minStableOhms, 1) + " " + ohm() + " stable");
    if (a.inputSensitivityMaxV > 0)
        out.add ("Gain " + num (a.inputSensitivityMinV, 2) + "-" + num (a.inputSensitivityMaxV, 1) + " V");
}

void addSourceSummary (const SourceSpecs& s, Category c, juce::StringArray& out)
{
    const bool isProcessor = c == Category::dsp || c == Category::dspAmp || c == Category::dac;

    if (isProcessor && (s.inputs > 0 || s.outputs > 0))
        out.add (juce::String (s.inputs) + " in / " + juce::String (s.outputs) + " out");
    if (s.preoutVoltageV > 0)
        out.add (num (s.preoutVoltageV, 1) + " V preout"
                 + (s.preoutChannels > 0 ? " (" + juce::String (s.preoutChannels) + " ch)" : juce::String()));
    if (s.peqBandsPerChannel > 0)  out.add (juce::String (s.peqBandsPerChannel) + "-band PEQ/ch");
    else if (s.eqBands > 0)        out.add (juce::String (s.eqBands) + "-band " + (s.eqType.isNotEmpty() ? s.eqType + " " : juce::String()) + "EQ");
    if (s.timeAlignment && ! isProcessor) out.add ("Time alignment");
    if (s.maxDelayMs > 0)          out.add ("Delay " + num (s.maxDelayMs, 1) + " ms");
    if (s.sampleRateHz > 0)        out.add (num (s.sampleRateHz / 1000.0, 1) + " kHz");
    if (s.maxVolumeStep > 0)       out.add ("Vol 0-" + juce::String (s.maxVolumeStep));
    if (s.software.isNotEmpty())   out.add (s.software);
}

void addWireSummary (const WireSpecs& w, juce::StringArray& out)
{
    if (w.awg != 0.0 || w.mm2 == 0.0) out.add (awgName (w.awg));
    if (w.mm2 > 0)                    out.add (num (w.mm2, 1) + " mm2");
    if (w.material.isNotEmpty())      out.add (w.material);
    if (w.ampacityA > 0)              out.add (num (w.ampacityA, 0) + " A");
}

void addInterfaceSummary (const InterfaceSpecs& i, juce::StringArray& out)
{
    if (i.inputs > 0 || i.outputs > 0)
        out.add (juce::String (i.inputs) + " in / " + juce::String (i.outputs) + " out");
    if (i.micPreamps > 0)       out.add (juce::String (i.micPreamps) + " mic pre");
    if (i.phantomPower)         out.add ("48 V phantom");
    if (i.hasLoopback)          out.add ("Loopback");
    if (i.asio)                 out.add ("ASIO");
    if (i.connection.isNotEmpty()) out.add (i.connection);
}

void addMicSummary (const MicSpecs& m, juce::StringArray& out)
{
    if (m.type.isNotEmpty())     out.add (m.type);
    if (m.needsPhantom)          out.add ("Needs phantom");
    if (m.calFileAvailable)      out.add ("Cal file");
    if (m.sensitivityMvPa > 0)   out.add (num (m.sensitivityMvPa, 1) + " mV/Pa");
    if (m.maxSplDb > 0)          out.add ("Max " + num (m.maxSplDb, 0) + " dB SPL");
}
} // namespace

juce::StringArray specsSummary (const Product& p)
{
    juce::StringArray out;

    if (p.driver)         addDriverSummary (*p.driver, out);
    if (p.amp)            addAmpSummary (*p.amp, out);
    if (p.source)         addSourceSummary (*p.source, p.category, out);
    if (p.wire)           addWireSummary (*p.wire, out);
    if (p.audioInterface) addInterfaceSummary (*p.audioInterface, out);
    if (p.mic)            addMicSummary (*p.mic, out);

    out.removeDuplicates (false);
    return out;
}

//==============================================================================
// GearDatabase
//==============================================================================
GearDatabase& GearDatabase::instance()
{
    static GearDatabase db;
    return db;
}

void GearDatabase::clear()
{
    products.clear();
    searchKeys.clear();
    fromUser.clear();
    alive.clear();
    shadowed.clear();
    index.clear();
    warnings.clear();
    liveCount = 0;
}

juce::File GearDatabase::getUserFolder() const
{
    if (userFolder != juce::File())
        return userFolder;

    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("FalseAudio").getChildFile ("Gear");
}

void GearDatabase::warn (const juce::String& message)
{
    juce::Logger::writeToLog ("[Gear] " + message);
    if (warnings.size() < 1000)
        warnings.add (message);
}

bool GearDatabase::insert (Product&& p, bool userSource)
{
    const auto key = p.id.toLowerCase();
    p.userDefined = userSource;
    auto it = index.find (key);

    if (it == index.end())
    {
        index[key] = products.size();
        searchKeys.push_back (makeSearchKey (p));
        fromUser.push_back (userSource);
        alive.push_back (true);
        products.push_back (std::move (p));
        ++liveCount;
        return true;
    }

    const size_t i = it->second;
    const auto& existing = products[i];
    const auto betterBuiltIn = [] (const Product& newer, const Product& older)
    {
        return newer.verified || ! older.verified;
    };

    if (! userSource && fromUser[i])
    {
        // a built-in arriving after the user override: only update the stashed built-in
        auto s = shadowed.find (key);
        if (s == shadowed.end())
            shadowed.emplace (key, std::move (p));
        else if (betterBuiltIn (p, s->second))
            s->second = std::move (p);
        return false;
    }

    if (! userSource && ! betterBuiltIn (p, existing))
        return false;

    if (userSource && ! fromUser[i])
        shadowed[key] = existing;

    searchKeys[i] = makeSearchKey (p);
    fromUser[i] = userSource;
    products[i] = std::move (p);   // in place: pointers handed out earlier stay valid
    return true;
}

int GearDatabase::loadFile (const juce::File& file, bool userSource)
{
    const auto text = file.loadFileAsString();
    if (text.trim().isEmpty())
    {
        warn ("Empty file skipped: " + file.getFullPathName());
        return 0;
    }

    juce::var root;
    const auto result = juce::JSON::parse (text, root);
    if (result.failed())
    {
        warn ("Invalid JSON in " + file.getFullPathName() + ": " + result.getErrorMessage());
        return 0;
    }

    const juce::var list = root.isArray() ? root : root.getProperty ("products", {});
    auto* arr = list.getArray();
    if (arr == nullptr)
    {
        warn ("No \"products\" array in " + file.getFullPathName());
        return 0;
    }

    // folder name gives the category when an entry omits it
    const auto fallback = tryCategoryFromString (file.getParentDirectory().getFileName());

    int loaded = 0;
    for (int i = 0; i < arr->size(); ++i)
    {
        Product p;
        juce::String reason;
        if (! parseProduct (arr->getReference (i), fallback, p, reason))
        {
            warn (file.getFileName() + " entry " + juce::String (i) + " skipped: " + reason);
            continue;
        }

        insert (std::move (p), userSource);
        ++loaded;
    }

    return loaded;
}

int GearDatabase::loadFolderImpl (const juce::File& folder, bool userSource)
{
    if (! folder.isDirectory())
        return 0;

    auto files = folder.findChildFiles (juce::File::findFiles, true, "*.json");
    // deterministic order: sort by path so "later" has a stable meaning
    std::sort (files.begin(), files.end(),
               [] (const juce::File& a, const juce::File& b) { return a.getFullPathName().compareIgnoreCase (b.getFullPathName()) < 0; });

    int total = 0;
    for (auto& f : files)
        total += loadFile (f, userSource);

    return total;
}

int GearDatabase::loadFolder (const juce::File& folder)
{
    return loadFolderImpl (folder, false);
}

int GearDatabase::loadUserFolder (const juce::File& folder)
{
    return loadFolderImpl (folder, true);
}

int GearDatabase::loadDefaults()
{
    clear();

    const auto exeDir = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
    juce::Array<juce::File> folders;

    const auto bundled = exeDir.getChildFile ("data").getChildFile ("gear");
    if (bundled.isDirectory())
        folders.add (bundled);

    // dev builds: walk up from the exe to find the repo's data/gear
    for (auto dir = exeDir.getParentDirectory(); ; dir = dir.getParentDirectory())
    {
        const auto candidate = dir.getChildFile ("data").getChildFile ("gear");
        if (candidate.isDirectory())
        {
            if (! folders.contains (candidate))
                folders.add (candidate);
            break;
        }

        if (dir.isRoot() || dir.getParentDirectory() == dir)
            break;
    }

    for (auto& f : folders)
        loadFolder (f);

    loadUserFolder (getUserFolder());
    return size();
}

std::vector<const Product*> GearDatabase::search (const juce::String& text, std::optional<Category> category,
                                                  int maxResults) const
{
    const auto query = text.trim().toLowerCase();
    juce::StringArray tokens;
    tokens.addTokens (query, " \t,", "\"");
    tokens.removeEmptyStrings();

    struct Hit { int rank; const Product* p; };
    std::vector<Hit> hits;

    for (size_t i = 0; i < products.size(); ++i)
    {
        const auto& p = products[i];
        if (! alive[i] || (category.has_value() && p.category != *category))
            continue;

        const auto& key = searchKeys[i];
        bool all = true;
        for (auto& t : tokens)
        {
            if (! key.contains (t))
            {
                all = false;
                break;
            }
        }

        if (! all)
            continue;

        int rank = 3;
        if (query.isNotEmpty())
        {
            const auto model = p.model.toLowerCase();
            const auto name = p.displayName().toLowerCase();

            if (model == query || name == query || p.id.equalsIgnoreCase (query))
                rank = 0;
            else if (model.startsWith (query) || name.startsWith (query))
                rank = 1;
            else if (model.contains (query))
                rank = 2;
        }

        hits.push_back ({ rank, &p });
    }

    std::sort (hits.begin(), hits.end(), [] (const Hit& a, const Hit& b)
    {
        if (a.rank != b.rank)
            return a.rank < b.rank;
        return lessByBrandModel (a.p, b.p);
    });

    std::vector<const Product*> out;
    const size_t n = maxResults > 0 ? std::min (hits.size(), (size_t) maxResults) : hits.size();
    out.reserve (n);
    for (size_t i = 0; i < n; ++i)
        out.push_back (hits[i].p);

    return out;
}

juce::StringArray GearDatabase::brands (Category category) const
{
    juce::StringArray out;
    for (size_t i = 0; i < products.size(); ++i)
        if (alive[i] && products[i].category == category)
            out.addIfNotAlreadyThere (products[i].brand, true);

    out.sort (true);
    return out;
}

std::vector<const Product*> GearDatabase::byBrand (Category category, const juce::String& brand) const
{
    std::vector<const Product*> out;
    const auto b = brand.trim();
    for (size_t i = 0; i < products.size(); ++i)
        if (alive[i] && products[i].category == category && products[i].brand.equalsIgnoreCase (b))
            out.push_back (&products[i]);

    std::sort (out.begin(), out.end(), [] (const Product* a, const Product* b)
    {
        return a->model.compareNatural (b->model) < 0;
    });
    return out;
}

const Product* GearDatabase::find (const juce::String& id) const
{
    auto it = index.find (id.trim().toLowerCase());
    return it != index.end() ? &products[it->second] : nullptr;
}

bool GearDatabase::writeUserFile (const std::function<void (juce::Array<juce::var>&)>& edit, juce::String& error)
{
    const auto folder = getUserFolder();
    if (! folder.isDirectory() && ! folder.createDirectory())
    {
        error = "Cannot create folder " + folder.getFullPathName();
        return false;
    }

    const auto file = folder.getChildFile ("user.json");
    juce::Array<juce::var> list;

    if (file.existsAsFile() && file.loadFileAsString().trim().isNotEmpty())
    {
        juce::var root;
        const auto result = juce::JSON::parse (file.loadFileAsString(), root);
        auto* arr = root.isArray() ? root.getArray() : root.getProperty ("products", {}).getArray();

        if (result.failed() || arr == nullptr)
        {
            // keep the damaged file for the user, start a fresh one
            const auto backup = folder.getChildFile ("user.json.bak");
            file.copyFileTo (backup);
            warn ("user.json was unreadable; backed up to " + backup.getFullPathName());
        }
        else
        {
            list = *arr;
        }
    }

    edit (list);

    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty ("products", list);

    if (! file.replaceWithText (juce::JSON::toString (juce::var (root.get()), false)))
    {
        error = "Cannot write " + file.getFullPathName();
        return false;
    }

    return true;
}

bool GearDatabase::addUserProduct (const Product& product, juce::String& error)
{
    Product p = product;
    p.brand = p.brand.trim();
    p.model = p.model.trim();
    p.id = p.id.trim();

    if (p.brand.isEmpty() || p.model.isEmpty())
    {
        error = "Brand and model are required.";
        return false;
    }

    if (p.id.isEmpty())
        p.id = makeProductId (p.brand, p.model);

    const auto entry = p.toVar();
    const bool ok = writeUserFile ([&] (juce::Array<juce::var>& list)
    {
        for (auto& e : list)
        {
            if (e.getProperty ("id", {}).toString().trim().equalsIgnoreCase (p.id))
            {
                e = entry;
                return;
            }
        }
        list.add (entry);
    }, error);

    if (! ok)
        return false;

    insert (std::move (p), true);
    error.clear();
    return true;
}

bool GearDatabase::removeUserProduct (const juce::String& id)
{
    const auto key = id.trim().toLowerCase();
    auto it = index.find (key);
    if (key.isEmpty() || it == index.end() || ! fromUser[it->second])
        return false;

    juce::String error;
    const bool ok = writeUserFile ([&] (juce::Array<juce::var>& list)
    {
        for (int i = list.size(); --i >= 0;)
            if (list.getReference (i).getProperty ("id", {}).toString().trim().equalsIgnoreCase (key))
                list.remove (i);
    }, error);

    if (! ok)
    {
        warn ("removeUserProduct: " + error);
        return false;
    }

    const size_t i = it->second;
    auto s = shadowed.find (key);
    if (s != shadowed.end())
    {
        // restore the built-in in place
        products[i] = std::move (s->second);
        products[i].userDefined = false;
        searchKeys[i] = makeSearchKey (products[i]);
        fromUser[i] = false;
        shadowed.erase (s);
    }
    else
    {
        alive[i] = false;
        index.erase (it);
        --liveCount;
    }

    return true;
}
} // namespace fa::gear
