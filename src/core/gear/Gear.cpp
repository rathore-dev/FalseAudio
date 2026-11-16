#include "Gear.h"
#include "FalseAudioData.h"

#include <algorithm>
#include <cmath>
#include <type_traits>

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

// Fields of objects that appear inside arrays (amp.channelGroups etc.)
template <typename S, typename V>
void visitItem (S& x, V& v)
{
    using T = std::remove_const_t<S>;

    if constexpr (std::is_same_v<T, AmpChannelGroup>)
    {
        v ("channels", x.channels);             v ("rmsPerChannelW", x.rmsPerChannelW);
        v ("minStableOhms", x.minStableOhms);   v ("label", x.label);
    }
    else if constexpr (std::is_same_v<T, InputSensitivityRange>)
    {
        v ("minV", x.minV);     v ("maxV", x.maxV);     v ("label", x.label);
    }
    else if constexpr (std::is_same_v<T, SlopeHp>)
    {
        v ("slopeDbPerOct", x.slopeDbPerOct);   v ("hpHz", x.hpHz);
    }
    else if constexpr (std::is_same_v<T, BoxRecommendation>)
    {
        v ("type", x.type);     v ("litres", x.litres); v ("tuningHz", x.tuningHz);
    }
    else if constexpr (std::is_same_v<T, SourceSpecs::PeqBandsByStage>)
    {
        v ("input", x.input);   v ("output", x.output); v ("virtual", x.virtualChannel);
    }
    else
    {
        static_assert (! std::is_same_v<T, T>, "no field list for this type");
    }
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

    /** Array of objects; non-object entries are ignored. */
    template <typename T>
    void operator() (const char* key, std::vector<T>& x) const
    {
        if (auto* arr = get (key).getArray())
        {
            for (auto& e : *arr)
            {
                if (! e.isObject())
                    continue;
                T item;
                Reader r { e };
                visitItem (item, r);
                x.push_back (std::move (item));
            }
        }
    }

    void operator() (const char* key, SourceSpecs::PeqBandsByStage& x) const
    {
        const auto v = get (key);
        if (v.isObject())
        {
            Reader r { v };
            visitItem (x, r);
        }
    }

    /** [min, max], {"min":..,"max":..} or a single number (= max). */
    void operator() (const char* key, ValueRange& x) const
    {
        const auto v = get (key);
        double d;
        if (auto* arr = v.getArray())
        {
            if (arr->size() >= 2 && toNumber (arr->getReference (0), d))   x.min = d;
            if (arr->size() >= 2 && toNumber (arr->getReference (1), d))   x.max = d;
            if (arr->size() == 1 && toNumber (arr->getReference (0), d))   x.max = d;
        }
        else if (v.isObject())
        {
            Reader r { v };
            r ("min", x.min);
            r ("max", x.max);
        }
        else if (toNumber (v, d))
        {
            x.max = d;
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

    template <typename T>
    void operator() (const char* key, const std::vector<T>& x) const
    {
        if (x.empty())
            return;

        juce::Array<juce::var> arr;
        for (auto& item : x)
        {
            Writer w;
            visitItem (item, w);
            arr.add (juce::var (w.obj.get()));
        }
        obj->setProperty (key, arr);
    }

    void operator() (const char* key, const SourceSpecs::PeqBandsByStage& x) const
    {
        if (! x.isSet())
            return;

        Writer w;
        visitItem (x, w);
        obj->setProperty (key, juce::var (w.obj.get()));
    }

    void operator() (const char* key, const ValueRange& x) const
    {
        if (! x.isSet())
            return;

        juce::Array<juce::var> arr;
        arr.add (x.min);
        arr.add (x.max);
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
    v ("tsWiring", d.tsWiring);                 v ("switchableImpedance", d.switchableImpedance);
    v ("cutoutShape", d.cutoutShape);
    v ("cutoutWidthMm", d.cutoutWidthMm);       v ("cutoutHeightMm", d.cutoutHeightMm);
    v ("recommendedHpBySlope", d.recommendedHpBySlope);
    v ("secondarySensitivityDb", d.secondarySensitivityDb);
    v ("secondarySensitivityRef", d.secondarySensitivityRef);
    v ("boxRecommendations", d.boxRecommendations);
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
    v ("channelGroups", a.channelGroups);       v ("ratedVoltageV", a.ratedVoltageV);
    v ("recommendedFuseA", a.recommendedFuseA); v ("fuseCount", a.fuseCount);
    v ("inputSensitivityRanges", a.inputSensitivityRanges);
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
    v ("peqBandsByStage", s.peqBandsByStage);   v ("digitalInputs", s.digitalInputs);
    v ("digitalInputTypes", s.digitalInputTypes);
    v ("highLevelInputs", s.highLevelInputs);   v ("snrDigitalDb", s.snrDigitalDb);
    v ("preoutRatedLoadOhms", s.preoutRatedLoadOhms);
    v ("outputImpedanceIsUpperBound", s.outputImpedanceIsUpperBound);
    v ("delayRangeMs", s.delayRangeMs);
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
    v ("sensitivityDbfs", m.sensitivityDbfs);
}

template <typename S, typename V>
void visitBattery (S& b, V& v)
{
    v ("ah", b.ah);                 v ("cca", b.cca);               v ("ca", b.ca);
    v ("reserveMin", b.reserveMin); v ("maxAmps", b.maxAmps);       v ("chemistry", b.chemistry);
}

template <typename S, typename V>
void visitAlternator (S& a, V& v)
{
    v ("ratedA", a.ratedA);         v ("idleA", a.idleA);           v ("hotRatedA", a.hotRatedA);
}

template <typename S, typename V>
void visitFuse (S& f, V& v)
{
    v ("ratingA", f.ratingA);       v ("type", f.type);
}

/** Derived / tolerant fields after reading the source block (keeps older consumers working). */
void finishSource (SourceSpecs& s, const juce::var& raw)
{
    // peqBandsPerChannel is the output stage; fill it from peqBandsByStage when only that was given
    if (s.peqBandsPerChannel == 0 && s.peqBandsByStage.output > 0)
        s.peqBandsPerChannel = s.peqBandsByStage.output;

    if (s.maxDelayMs == 0.0 && s.delayRangeMs.max > 0.0)
        s.maxDelayMs = s.delayRangeMs.max;

    // "digitalInputs": ["optical", "coax"] or "optical, coax" -> count + types
    const auto di = raw.getProperty ("digitalInputs", {});
    juce::StringArray types;
    if (auto* arr = di.getArray())
    {
        for (auto& e : *arr)
            if (e.isString() && e.toString().trim().isNotEmpty())
                types.add (e.toString().trim());
    }
    else if (di.isString() && ! di.toString().trim().containsOnly ("0123456789.-+eE"))
    {
        types.addTokens (di.toString(), ",/", "\"");
        types.trim();
        types.removeEmptyStrings();
    }

    if (! types.isEmpty())
    {
        if (s.digitalInputs == 0)
            s.digitalInputs = types.size();
        if (s.digitalInputTypes.isEmpty())
            s.digitalInputTypes = types;
    }
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

void readComponentPart (const juce::var& v, ComponentPart& part)
{
    if (! v.isObject())
        return;
    Reader r { v };
    r ("productId", part.productId);
    readBlock (v, "driver", part.driver, [] (auto& s, auto& vis) { visitDriver (s, vis); });
}

void readComponentSet (const juce::var& v, std::optional<ComponentSetSpecs>& out)
{
    const auto cs = v.getProperty ("componentSet", {});
    if (! cs.isObject())
        return;
    ComponentSetSpecs c;
    readComponentPart (cs.getProperty ("woofer", {}), c.woofer);
    readComponentPart (cs.getProperty ("tweeter", {}), c.tweeter);
    Reader r { cs };
    r ("crossoverHz", c.crossoverHz);
    r ("crossoverSlopeDbPerOct", c.crossoverSlopeDbPerOct);
    out = c;
}

juce::var writeComponentPart (const ComponentPart& part)
{
    juce::DynamicObject::Ptr o = new juce::DynamicObject();
    if (part.productId.isNotEmpty())
        o->setProperty ("productId", part.productId);
    writeBlock (*o, "driver", part.driver, [] (auto& s, auto& vis) { visitDriver (s, vis); });
    return juce::var (o.get());
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
    readBlock (v, "battery", p.battery, [] (auto& s, auto& vis) { visitBattery (s, vis); });
    readBlock (v, "alternator", p.alternator, [] (auto& s, auto& vis) { visitAlternator (s, vis); });
    readBlock (v, "fuse", p.fuse, [] (auto& s, auto& vis) { visitFuse (s, vis); });

    readComponentSet (v, p.componentSet);

    if (p.source.has_value())
        finishSource (*p.source, v.getProperty ("source", {}));

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
    writeBlock (*o, "battery", battery, [] (auto& s, auto& vis) { visitBattery (s, vis); });
    writeBlock (*o, "alternator", alternator, [] (auto& s, auto& vis) { visitAlternator (s, vis); });
    writeBlock (*o, "fuse", fuse, [] (auto& s, auto& vis) { visitFuse (s, vis); });

    if (componentSet.has_value())
    {
        juce::DynamicObject::Ptr c = new juce::DynamicObject();
        c->setProperty ("woofer", writeComponentPart (componentSet->woofer));
        c->setProperty ("tweeter", writeComponentPart (componentSet->tweeter));
        if (componentSet->crossoverHz != 0.0)           c->setProperty ("crossoverHz", componentSet->crossoverHz);
        if (componentSet->crossoverSlopeDbPerOct != 0.0) c->setProperty ("crossoverSlopeDbPerOct", componentSet->crossoverSlopeDbPerOct);
        o->setProperty ("componentSet", juce::var (c.get()));
    }

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
    if (d.cutoutShape.equalsIgnoreCase ("oval") && d.cutoutWidthMm > 0 && d.cutoutHeightMm > 0)
        out.add ("Cutout " + num (d.cutoutWidthMm, 0) + "x" + num (d.cutoutHeightMm, 0) + " mm oval");
    if (d.mountingDepthMm > 0) out.add ("Depth " + num (d.mountingDepthMm, 1) + " mm");
    if (! d.boxRecommendations.empty())
    {
        const auto& b = d.boxRecommendations.front();
        juce::String s = b.type.isNotEmpty() ? b.type.substring (0, 1).toUpperCase() + b.type.substring (1) : juce::String ("Box");
        if (b.litres > 0)   s << " " << num (b.litres, 1) << " L";
        if (b.tuningHz > 0) s << " @ " << num (b.tuningHz, 1) << " Hz";
        out.add (s);
    }
}

void addRatings (const std::map<juce::String, double>& rms, int channels, int maxShown, const juce::String& suffix,
                 juce::StringArray& out)
{
    static const char* order[] = { "4ohm", "3ohm", "2ohm", "1ohm", "0.5ohm", "bridged4ohm", "bridged2ohm" };
    int shown = 0;
    for (auto* key : order)
    {
        auto it = rms.find (key);
        if (it == rms.end() || it->second <= 0 || shown >= maxShown)
            continue;

        const juce::String k (key);
        const bool bridged = k.startsWith ("bridged");
        const auto load = k.fromLastOccurrenceOf ("bridged", false, false).upToFirstOccurrenceOf ("ohm", false, false);
        juce::String s = num (it->second, 0) + " W";
        if (! bridged && channels > 1)
            s << " x " << channels;
        s << " @ " << load << " " << ohm();
        if (bridged)
            s << " bridged";
        out.add (s + suffix);
        ++shown;
    }
}

void addAmpSummary (const AmpSpecs& a, juce::StringArray& out)
{
    if (a.channels > 0)
        out.add (juce::String (a.channels) + " ch");

    const auto layout = ampChannelLayout (a);
    if (a.channelGroups.empty())
    {
        addRatings (a.rmsPerChannelW, a.channels, 2, {}, out);
    }
    else
    {
        const bool mainListed = ! layout.empty() && layout.front().groupIndex == -1;
        if (! mainListed)   // channel count missing / groups cover everything: still show the main ratings
            addRatings (a.rmsPerChannelW, 0, 1, {}, out);

        for (auto& g : layout)
            addRatings (g.group.rmsPerChannelW, g.group.channels, 1,
                        g.group.label.isNotEmpty() ? " (" + g.group.label + ")" : juce::String(), out);
    }

    if (a.amplifierClass.isNotEmpty())  out.add ("Class " + a.amplifierClass);
    if (a.minStableOhms > 0)            out.add (num (a.minStableOhms, 1) + " " + ohm() + " stable");
    if (a.inputSensitivityMaxV > 0)
        out.add ("Gain " + num (a.inputSensitivityMinV, 2) + "-" + num (a.inputSensitivityMaxV, 1) + " V");
    if (a.recommendedFuseA > 0)         out.add (num (a.recommendedFuseA, 0) + " A fuse");
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
    if (s.digitalInputs > 0)
        out.add (s.digitalInputTypes.isEmpty() ? juce::String (s.digitalInputs) + " digital in"
                                                : s.digitalInputTypes.joinIntoString ("/") + " in");
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
    if (m.sensitivityDbfs != 0)  out.add (num (m.sensitivityDbfs, 1) + " dBFS");
    if (m.maxSplDb > 0)          out.add ("Max " + num (m.maxSplDb, 0) + " dB SPL");
}

void addBatterySummary (const BatterySpecs& b, juce::StringArray& out)
{
    if (b.chemistry.isNotEmpty())   out.add (b.chemistry);
    if (b.ah > 0)                   out.add (num (b.ah, 1) + " Ah");
    if (b.cca > 0)                  out.add (num (b.cca, 0) + " CCA");
    if (b.reserveMin > 0)           out.add ("RC " + num (b.reserveMin, 0) + " min");
    if (b.maxAmps > 0)              out.add ("Max " + num (b.maxAmps, 0) + " A");
}

void addAlternatorSummary (const AlternatorSpecs& a, juce::StringArray& out)
{
    if (a.ratedA > 0)               out.add (num (a.ratedA, 0) + " A");
    if (a.hotRatedA > 0)            out.add (num (a.hotRatedA, 0) + " A hot");
    if (a.idleA > 0)                out.add (num (a.idleA, 0) + " A idle");
}

void addFuseSummary (const FuseSpecs& f, juce::StringArray& out)
{
    if (f.ratingA > 0)
        out.add (num (f.ratingA, 0) + " A" + (f.type.isNotEmpty() ? " " + f.type : juce::String()) + " fuse");
    else if (f.type.isNotEmpty())
        out.add (f.type + " fuse");
}
} // namespace

double recommendedHpForSlope (const DriverSpecs& d, double slopeDbPerOct)
{
    for (auto& e : d.recommendedHpBySlope)
        if (e.hpHz > 0 && std::abs (e.slopeDbPerOct - slopeDbPerOct) < 0.5)
            return e.hpHz;
    return d.recommendedHpHz;
}

const DriverSpecs* componentSubDriver (const Product& set, ComponentRole role,
                                       const std::function<const Product*(const juce::String&)>& lookup)
{
    if (! set.componentSet.has_value())
        return nullptr;
    const auto& part = role == ComponentRole::woofer ? set.componentSet->woofer : set.componentSet->tweeter;
    if (part.driver.has_value())
        return &*part.driver;
    if (part.productId.isNotEmpty() && lookup)
        if (const auto* p = lookup (part.productId); p != nullptr && p->driver.has_value())
            return &*p->driver;
    return nullptr;
}

juce::StringArray specsSummary (const Product& p)
{
    juce::StringArray out;

    if (p.driver)         addDriverSummary (*p.driver, out);
    if (p.amp)            addAmpSummary (*p.amp, out);
    if (p.source)         addSourceSummary (*p.source, p.category, out);
    if (p.wire)           addWireSummary (*p.wire, out);
    if (p.audioInterface) addInterfaceSummary (*p.audioInterface, out);
    if (p.mic)            addMicSummary (*p.mic, out);
    if (p.battery)        addBatterySummary (*p.battery, out);
    if (p.alternator)     addAlternatorSummary (*p.alternator, out);
    if (p.fuse)           addFuseSummary (*p.fuse, out);

    out.removeDuplicates (false);
    return out;
}

//==============================================================================
// Amplifier channel groups
//==============================================================================
std::vector<AmpGroupInfo> ampChannelLayout (const AmpSpecs& a)
{
    int extra = 0;
    for (auto& g : a.channelGroups)
        extra += std::max (0, g.channels);

    std::vector<AmpGroupInfo> out;

    AmpGroupInfo main;
    main.groupIndex = -1;
    main.group.channels = a.channelGroups.empty() ? a.channels : std::max (0, a.channels - extra);
    main.group.rmsPerChannelW = a.rmsPerChannelW;
    main.group.minStableOhms = a.minStableOhms;
    if (main.group.channels > 0 || a.channelGroups.empty())
        out.push_back (main);

    int next = main.group.channels;
    for (size_t i = 0; i < a.channelGroups.size(); ++i)
    {
        const auto& g = a.channelGroups[i];
        if (g.channels <= 0)
            continue;

        AmpGroupInfo info;
        info.groupIndex = (int) i;
        info.firstChannel = next;
        info.group = g;
        out.push_back (info);
        next += g.channels;
    }

    return out;
}

int ampGroupIndexForChannel (const AmpSpecs& a, int channelIndex)
{
    if (a.channelGroups.empty() || channelIndex < 0)
        return -1;

    for (auto& g : ampChannelLayout (a))
        if (channelIndex >= g.firstChannel && channelIndex < g.firstChannel + g.group.channels)
            return g.groupIndex;

    return -1;
}

AmpSpecs ampSpecsForChannel (const AmpSpecs& a, int channelIndex)
{
    AmpSpecs out = a;
    out.channelGroups.clear();

    const int gi = ampGroupIndexForChannel (a, channelIndex);
    if (gi < 0)
    {
        if (! a.channelGroups.empty())
            for (auto& g : ampChannelLayout (a))
                if (g.groupIndex == -1)
                    out.channels = g.group.channels;
        return out;
    }

    const auto& g = a.channelGroups[(size_t) gi];
    out.channels = g.channels;
    out.rmsPerChannelW = g.rmsPerChannelW;
    out.minStableOhms = g.minStableOhms > 0 ? g.minStableOhms : a.minStableOhms;
    return out;
}

double rmsAtLoad (const std::map<juce::String, double>& ratings, double loadOhms, bool bridged, juce::String* note)
{
    if (loadOhms <= 0 || ! std::isfinite (loadOhms))
        return 0;

    std::vector<std::pair<double, double>> pts;   // (ohms, watts)
    for (auto& [key, watts] : ratings)
    {
        auto k = key.toLowerCase().removeCharacters (" _-");
        const bool isBridged = k.startsWith ("bridge");
        if (isBridged != bridged)
            continue;
        if (isBridged)
            k = k.replace ("bridged", "").replace ("bridge", "");
        if (! k.contains ("ohm"))
            continue;
        const double ohms = k.upToFirstOccurrenceOf ("ohm", false, true).getDoubleValue();
        if (ohms > 0 && watts > 0)
            pts.push_back ({ ohms, watts });
    }

    if (pts.empty() && bridged)
    {
        const double p = 2.0 * rmsAtLoad (ratings, loadOhms / 2.0, false);
        if (note != nullptr && p > 0)
            *note = "bridged power estimated as 2 x the per-channel rating at half the load";
        return p;
    }
    if (pts.empty())
        return 0;

    std::sort (pts.begin(), pts.end());
    for (auto& pt : pts)
        if (std::abs (pt.first - loadOhms) / loadOhms < 0.02)
            return pt.second;

    if (loadOhms > pts.back().first)
    {
        if (note != nullptr)
            *note = "extrapolated (constant output voltage) above the highest rated load";
        return pts.back().second * pts.back().first / loadOhms;
    }
    if (loadOhms < pts.front().first)
    {
        if (note != nullptr)
            *note = "below the lowest rated load - using that rating (no more power is assumed)";
        return pts.front().second;
    }

    for (size_t i = 1; i < pts.size(); ++i)
    {
        if (loadOhms < pts[i].first)
        {
            const auto& lo = pts[i - 1];
            const auto& hi = pts[i];
            const double t = std::log (loadOhms / lo.first) / std::log (hi.first / lo.first);
            if (note != nullptr)
                *note = "interpolated between the " + num (lo.first, 1) + " and " + num (hi.first, 1) + " ohm ratings";
            return std::exp (std::log (lo.second) + t * (std::log (hi.second) - std::log (lo.second)));
        }
    }

    return pts.back().second;
}

double ampPowerForGroup (const AmpSpecs& a, int channelIndex, double loadOhms, bool bridged, juce::String* note)
{
    const int gi = ampGroupIndexForChannel (a, channelIndex);
    const auto& ratings = gi < 0 ? a.rmsPerChannelW : a.channelGroups[(size_t) gi].rmsPerChannelW;
    return rmsAtLoad (ratings, loadOhms, bridged, note);
}

double ampMinStableOhmsForChannel (const AmpSpecs& a, int channelIndex)
{
    const int gi = ampGroupIndexForChannel (a, channelIndex);
    if (gi >= 0 && a.channelGroups[(size_t) gi].minStableOhms > 0)
        return a.channelGroups[(size_t) gi].minStableOhms;
    return a.minStableOhms;
}

double ampTotalMaxRmsW (const AmpSpecs& a)
{
    double total = 0;
    for (auto& g : ampChannelLayout (a))
    {
        double best = 0;
        for (auto& [key, watts] : g.group.rmsPerChannelW)
            if (! key.toLowerCase().startsWith ("bridge"))
                best = std::max (best, watts);
        total += best * std::max (0, g.group.channels);
    }
    return total;
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
    return loadText (file.loadFileAsString(), file.getFullPathName(), file.getFileName(),
                     file.getParentDirectory().getFileName(), userSource);
}

int GearDatabase::loadText (const juce::String& text, const juce::String& fullName, const juce::String& shortName,
                            const juce::String& parentFolderName, bool userSource)
{
    if (text.trim().isEmpty())
    {
        warn ("Empty file skipped: " + fullName);
        return 0;
    }

    juce::var root;
    const auto result = juce::JSON::parse (text, root);
    if (result.failed())
    {
        warn ("Invalid JSON in " + fullName + ": " + result.getErrorMessage());
        return 0;
    }

    const juce::var list = root.isArray() ? root : root.getProperty ("products", {});
    auto* arr = list.getArray();
    if (arr == nullptr)
    {
        warn ("No \"products\" array in " + fullName);
        return 0;
    }

    // folder name gives the category when an entry omits it
    const auto fallback = tryCategoryFromString (parentFolderName);

    int loaded = 0;
    for (int i = 0; i < arr->size(); ++i)
    {
        Product p;
        juce::String reason;
        if (! parseProduct (arr->getReference (i), fallback, p, reason))
        {
            warn (shortName + " entry " + juce::String (i) + " skipped: " + reason);
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

int GearDatabase::loadEmbedded()
{
    int size = 0;
    const auto* data = FalseAudioData::getNamedResource ("gear_zip_zip", size);
    if (data == nullptr || size <= 0)
        return 0;

    juce::MemoryInputStream stream (data, (size_t) size, false);
    juce::ZipFile zip (stream);

    // deterministic order, same as loadFolderImpl
    std::vector<std::pair<juce::String, int>> entries;
    for (int i = 0; i < zip.getNumEntries(); ++i)
    {
        const auto* e = zip.getEntry (i);
        if (e != nullptr && e->filename.endsWithIgnoreCase (".json"))
            entries.emplace_back (e->filename, i);
    }
    std::sort (entries.begin(), entries.end(), [] (const auto& a, const auto& b)
               { return a.first.compareIgnoreCase (b.first) < 0; });

    int total = 0;
    for (auto& [name, idx] : entries)
    {
        std::unique_ptr<juce::InputStream> in (zip.createStreamForEntry (idx));
        if (in == nullptr)
            continue;
        const auto text = in->readEntireStreamAsString();
        const auto parts = juce::StringArray::fromTokens (name, "/", "");
        total += loadText (text, "embedded:" + name, parts[parts.size() - 1],
                           parts.size() >= 2 ? parts[parts.size() - 2] : juce::String(), false);
    }
    return total;
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

    if (folders.isEmpty())
        loadEmbedded();   // single-exe distribution: database is linked into the binary

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
