#include "TestRegistry.h"
#include "core/gear/Gear.h"

#include <chrono>
#include <cmath>

using fa::test::check;
using namespace fa::gear;

namespace
{
juce::File makeTempDir (const juce::String& name)
{
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getChildFile ("FalseAudioGearTests").getChildFile (name);
    dir.deleteRecursively();
    dir.createDirectory();
    return dir;
}

void write (const juce::File& f, const juce::String& text)
{
    f.getParentDirectory().createDirectory();
    f.replaceWithText (text);
}

juce::File findRepoGear()
{
    for (auto dir = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
         ! dir.isRoot(); dir = dir.getParentDirectory())
    {
        const auto c = dir.getChildFile ("data").getChildFile ("gear");
        if (c.isDirectory())
            return c;
        if (dir.getParentDirectory() == dir)
            break;
    }
    return {};
}
} // namespace

FA_TEST (gearCategoriesAndIds)
{
    bool allRoundTrip = true;
    for (auto c : allCategories())
        allRoundTrip = allRoundTrip && categoryFromString (toString (c)) == c;

    check (allRoundTrip && allCategories().size() == 20, "Gear: category <-> string round trip");
    check (tryCategoryFromString ("SUBWOOFER") == Category::subwoofer && ! tryCategoryFromString ("toaster").has_value(),
           "Gear: category parse is case-insensitive, rejects unknown");
    check (makeProductId ("JL Audio", "VX800/8i") == "jl-audio-vx800-8i", "Gear: makeProductId",
           makeProductId ("JL Audio", "VX800/8i"));
}

FA_TEST (gearLoadFolder)
{
    const auto dir = makeTempDir ("load");

    write (dir.getChildFile ("subwoofer/acme.json"), R"({ "products": [
        { "id": "acme-w12", "category": "subwoofer", "brand": "Acme", "model": "W12", "verified": false,
          "driver": { "sizeInches": 12, "fsHz": 30, "xmaxMm": 14, "powerRmsW": 500 } },
        { "category": "subwoofer", "brand": "Acme", "model": "W10" },
        { "brand": "Acme", "model": "W8" },
        { "id": "broken", "category": "subwoofer", "brand": "", "model": "x" },
        { "id": "badcat", "category": "toaster", "brand": "Acme", "model": "T1" },
        42
    ] })");

    // duplicate id: verified version must win, and a later unverified one must not override it
    write (dir.getChildFile ("subwoofer/acme_verified.json"), R"({ "products": [
        { "id": "acme-w12", "category": "subwoofer", "brand": "Acme", "model": "W12", "verified": true,
          "driver": { "sizeInches": 12, "fsHz": 28 } } ] })");
    write (dir.getChildFile ("subwoofer/zz_unverified.json"), R"({ "products": [
        { "id": "ACME-W12", "category": "subwoofer", "brand": "Acme", "model": "W12", "verified": false,
          "driver": { "fsHz": 99 } } ] })");

    write (dir.getChildFile ("ampMono/_bolt.json"), R"({ "products": [
        { "id": "bolt-m1", "category": "ampMono", "brand": "Bolt", "model": "M1",
          "amp": { "channels": 1, "rmsPerChannelW": { "4ohm": 300, "1ohm": 1000 } } },
        { "id": "bolt-m1000", "category": "ampMono", "brand": "bolt", "model": "M1000" } ] })");

    write (dir.getChildFile ("dsp/broken.json"), "{ \"products\": [ { \"id\": ");     // invalid JSON
    write (dir.getChildFile ("dsp/empty.json"), "");
    write (dir.getChildFile ("dsp/noarray.json"), "{ \"foo\": 1 }");
    write (dir.getChildFile ("SCHEMA.md"), "# not json");

    GearDatabase db;
    db.loadFolder (dir);

    // acme-w12, W10, W8 (category from folder), bolt-m1, bolt-m1000 = 5 unique
    check (db.size() == 5, "Gear: loads valid entries, skips invalid ones", juce::String (db.size()));
    check (db.getLoadWarnings().size() >= 6, "Gear: warnings logged for invalid files/entries",
           juce::String (db.getLoadWarnings().size()));

    auto* w12 = db.find ("ACME-W12");
    check (w12 != nullptr && w12->verified && w12->driver && w12->driver->fsHz == 28.0,
           "Gear: duplicate id -> verified source wins");
    check (db.find ("acme-w8") != nullptr && db.find ("acme-w8")->category == Category::subwoofer,
           "Gear: missing category taken from folder, missing id derived");

    auto br = db.brands (Category::ampMono);
    check (br.size() == 1, "Gear: brands() unique case-insensitively", br.joinIntoString (","));
    check (db.brands (Category::subwoofer) == juce::StringArray ("Acme"), "Gear: brands() per category");

    auto acme = db.byBrand (Category::subwoofer, "acme");
    check (acme.size() == 3 && acme[0]->model == "W8" && acme[1]->model == "W10" && acme[2]->model == "W12",
           "Gear: byBrand sorted naturally by model");

    // search ranking: exact model > prefix > contains
    auto r = db.search ("m1");
    check (r.size() == 2 && r[0]->id == "bolt-m1" && r[1]->id == "bolt-m1000", "Gear: search exact before prefix");

    auto r2 = db.search ("acme w1");
    check (r2.size() == 2 && r2[0]->model == "W10", "Gear: multi-word search", juce::String ((int) r2.size()));

    auto r3 = db.search ("W", Category::ampMono);
    check (r3.empty(), "Gear: category filter");

    auto r4 = db.search ("", Category::subwoofer);
    check (r4.size() == 3, "Gear: empty query lists category");

    auto r5 = db.search ("12", std::nullopt, 1);
    check (r5.size() == 1, "Gear: maxResults honoured");

    dir.getParentDirectory().deleteRecursively();
}

FA_TEST (gearRoundTrip)
{
    Product p;
    p.id = "test-dspamp";
    p.category = Category::dspAmp;
    p.brand = "Test";
    p.model = "DSP 8/6";
    p.series = "Pro";
    p.year = 2021;
    p.verified = true;
    p.sourceUrl = "https://example.com";

    AmpSpecs a;
    a.channels = 6;
    a.rmsPerChannelW = { { "4ohm", 75.0 }, { "2ohm", 110.0 } };
    a.amplifierClass = "D";
    a.hasHpf = true;
    a.inputSensitivityMinV = 0.2;
    a.inputSensitivityMaxV = 8;
    p.amp = a;

    SourceSpecs s;
    s.inputs = 8;
    s.outputs = 8;
    s.peqBandsPerChannel = 10;
    s.maxDelayMs = 10;
    s.sampleRateHz = 48000;
    s.software = juce::String (juce::CharPointer_UTF8 ("T\xc3\xbcN"));
    p.source = s;

    WireSpecs w;   // awg 0 == 1/0 must survive
    w.material = "OFC";
    Product wireP;
    wireP.brand = "W";
    wireP.model = "1/0";
    wireP.category = Category::powerWire;
    wireP.wire = w;

    const auto json = juce::JSON::toString (p.toVar());
    juce::var parsed;
    juce::JSON::parse (json, parsed);
    const auto q = Product::fromVar (parsed);

    check (q.id == p.id && q.category == p.category && q.model == p.model && q.series == p.series
               && q.year == 2021 && q.verified && q.amp && q.source && ! q.driver && ! q.wire && ! q.mic,
           "Gear: toVar/fromVar round trip (header + blocks)");
    check (q.amp->rmsPerChannelW == a.rmsPerChannelW && q.amp->hasHpf && ! q.amp->hasLpf
               && q.amp->amplifierClass == "D" && q.amp->inputSensitivityMaxV == 8.0,
           "Gear: amp block round trip");
    check (q.source->software == s.software && q.source->peqBandsPerChannel == 10 && q.source->sampleRateHz == 48000,
           "Gear: source block round trip");
    check (! parsed.hasProperty ("driver") && ! parsed.getProperty ("amp", {}).hasProperty ("snrDb"),
           "Gear: absent optionals / unknown fields omitted");

    const auto wv = wireP.toVar();
    check (wv.getProperty ("wire", {}).hasProperty ("awg"), "Gear: awg 0 (1/0) kept");

    const auto summary = specsSummary (q);
    check (summary.size() >= 4, "Gear: specsSummary", summary.joinIntoString (" | "));
}

FA_TEST (gearUserProducts)
{
    const auto dir = makeTempDir ("user");

    GearDatabase db;
    db.setUserFolder (dir);

    // partially specified user products: only what the user knows
    Product amp;
    amp.category = Category::ampMulti;
    amp.brand = "Garage";
    amp.model = "Amp 4";
    AmpSpecs a;
    a.channels = 4;
    a.rmsPerChannelW["4ohm"] = 50;
    amp.amp = a;

    Product drv;
    drv.category = Category::driverMidbass;
    drv.brand = "Garage";
    drv.model = "Woofer 6.5";
    DriverSpecs d;
    d.sizeInches = 6.5;
    d.fsHz = 55;   // no Xmax, no Sd
    drv.driver = d;

    juce::String err;
    const bool ok1 = db.addUserProduct (amp, err);
    const bool ok2 = db.addUserProduct (drv, err);
    check (ok1 && ok2 && dir.getChildFile ("user.json").existsAsFile(), "Gear: user products saved", err);

    // update existing
    drv.driver->qts = 0.4;
    db.addUserProduct (drv, err);

    Product bad;
    check (! db.addUserProduct (bad, err) && err.isNotEmpty(), "Gear: user product without brand/model rejected");

    GearDatabase db2;
    db2.setUserFolder (dir);
    db2.loadUserFolder (dir);
    auto* w = db2.find ("garage-woofer-6-5");
    check (db2.size() == 2 && w != nullptr && w->driver && w->driver->qts == 0.4,
           "Gear: user products reload (updated in place)", juce::String (db2.size()));
    check (w != nullptr && w->driver->xmaxMm == 0.0 && w->driver->sdCm2 == 0.0 && w->driver->fsHz == 55.0,
           "Gear: partial specs stay absent (0) after save/load, nothing invented");

    auto* a2 = db2.find ("garage-amp-4");
    check (a2 != nullptr && a2->amp && a2->amp->channels == 4 && a2->amp->efficiencyPercent == 0.0
               && a2->amp->inputSensitivityMaxV == 0.0,
           "Gear: partial user amp loads without defaults");

    // user folder overrides a verified built-in with the same id
    const auto builtIn = makeTempDir ("builtin");
    write (builtIn.getChildFile ("driverMidbass/garage.json"), R"({ "products": [
        { "id": "garage-woofer-6-5", "category": "driverMidbass", "brand": "Garage", "model": "Woofer 6.5",
          "verified": true, "driver": { "fsHz": 70 } } ] })");
    GearDatabase db3;
    db3.loadFolder (builtIn);
    db3.loadUserFolder (dir);
    check (db3.find ("garage-woofer-6-5")->driver->fsHz == 55.0 && db3.find ("garage-woofer-6-5")->userDefined,
           "Gear: user product overrides built-in");

    // editing a built-in via addUserProduct, then removing the user copy restores the built-in
    GearDatabase db4;
    db4.setUserFolder (makeTempDir ("user4"));
    db4.loadFolder (builtIn);
    const Product* ptr = db4.find ("garage-woofer-6-5");
    Product edited = *ptr;
    edited.driver->xmaxMm = 5;
    check (db4.addUserProduct (edited, err) && ptr->userDefined && ptr->driver->xmaxMm == 5.0 && db4.size() == 1,
           "Gear: edit built-in -> user copy wins (same pointer)");
    check (db4.removeUserProduct ("garage-woofer-6-5") && ! ptr->userDefined && ptr->driver->fsHz == 70.0
               && ptr->driver->xmaxMm == 0.0 && db4.size() == 1,
           "Gear: removeUserProduct restores built-in");
    check (! db4.removeUserProduct ("garage-woofer-6-5"), "Gear: removing a built-in is refused");

    check (db2.removeUserProduct ("garage-amp-4") && db2.find ("garage-amp-4") == nullptr && db2.size() == 1
               && db2.search ("amp 4").empty() && db2.brands (Category::ampMulti).isEmpty(),
           "Gear: removeUserProduct deletes user-only product");
    GearDatabase db5;
    db5.loadUserFolder (dir);
    check (db5.size() == 1 && db5.find ("garage-amp-4") == nullptr, "Gear: removal persisted to user.json");

    dir.getParentDirectory().deleteRecursively();
}

FA_TEST (gearSpecFields)
{
    bool allValid = true;
    for (auto c : allCategories())
    {
        for (auto& f : specFields (c))
        {
            allValid = allValid && f.key.containsChar ('.') && f.label.isNotEmpty() && f.usedFor.isNotEmpty();
            if (f.kind == SpecField::Kind::number && f.maxV > 0)
                allValid = allValid && f.minV < f.maxV;
        }
    }
    check (allValid, "Gear: spec fields well formed for all categories");

    const auto keysOf = [] (Category c)
    {
        juce::StringArray req;
        for (auto& f : specFields (c))
            if (f.required)
                req.add (f.key);
        return req;
    };
    check (keysOf (Category::headUnit).contains ("source.preoutVoltageV") && keysOf (Category::headUnit).contains ("source.maxVolumeStep"),
           "Gear: head unit required fields");
    check (keysOf (Category::driverMidbass).contains ("driver.fsHz") && ! keysOf (Category::driverMidbass).contains ("driver.xmaxMm"),
           "Gear: driver required vs optional");
    check (keysOf (Category::dsp).size() == 5, "Gear: DSP required fields", keysOf (Category::dsp).joinIntoString (","));

    // amp: empty -> every required listed; RMS group satisfied by any one load
    Product amp;
    amp.category = Category::ampMono;
    auto missing = missingRequired (amp);
    check (missing.contains ("Brand") && missing.size() == 7, "Gear: missingRequired on empty amp", missing.joinIntoString (" | "));

    amp.brand = "X";
    amp.model = "M";
    setSpecValue (amp, "amp.channels", 1);
    setSpecValue (amp, "amp.rmsPerChannelW.1ohm", "1000");
    setSpecValue (amp, "amp.inputSensitivityMinV", 0.2);
    setSpecValue (amp, "amp.inputSensitivityMaxV", 6.0);
    check (amp.amp && amp.amp->rmsPerChannelW["1ohm"] == 1000.0 && amp.amp->channels == 1,
           "Gear: setSpecValue writes nested / map keys");
    missing = missingRequiredKeys (amp);
    check (missing == juce::StringArray ("amp.minStableOhms"), "Gear: RMS group satisfied by one load",
           missing.joinIntoString (","));
    setSpecValue (amp, "amp.minStableOhms", 1);
    check (missingRequired (amp).isEmpty(), "Gear: complete amp has nothing missing");
    check ((double) getSpecValue (amp, "amp.inputSensitivityMaxV") == 6.0 && getSpecValue (amp, "amp.snrDb").isVoid(),
           "Gear: getSpecValue present / absent");
    setSpecValue (amp, "amp.minStableOhms", juce::var());
    check (amp.amp->minStableOhms == 0.0 && missingRequiredKeys (amp).contains ("amp.minStableOhms"),
           "Gear: setSpecValue with void removes");

    // wire: awg or mm2 + material
    Product wire;
    wire.category = Category::powerWire;
    wire.brand = "W";
    wire.model = "4ga";
    check (missingRequiredKeys (wire).size() == 3, "Gear: wire missing size group + material",
           missingRequiredKeys (wire).joinIntoString (","));
    setSpecValue (wire, "wire.mm2", 21.0);
    setSpecValue (wire, "wire.material", "OFC");
    check (missingRequired (wire).isEmpty(), "Gear: wire complete with mm2 + material");

    Product sub;
    sub.category = Category::subwoofer;
    sub.brand = "S";
    sub.model = "12";
    setSpecValue (sub, "driver.sizeInches", 12);
    setSpecValue (sub, "driver.voiceCoils", 2);
    setSpecValue (sub, "driver.coilOhms", 2);
    check (missingRequiredKeys (sub) == juce::StringArray ("driver.powerRmsW"), "Gear: subwoofer required fields",
           missingRequiredKeys (sub).joinIntoString (","));
}

FA_TEST (gearScale)
{
    GearDatabase db;
    const auto dir = makeTempDir ("scale");
    juce::MemoryOutputStream json;   // juce::String << would be quadratic here
    json << "{ \"products\": [";
    for (int i = 0; i < 20000; ++i)
        json << (i ? "," : "") << "{\"category\":\"driverMidbass\",\"brand\":\"Brand" << (i % 50)
             << "\",\"model\":\"M" << i << "\",\"driver\":{\"fsHz\":" << (30 + i % 40) << "}}";
    json << "]}";
    write (dir.getChildFile ("driverMidbass/big.json"), json.toString());

    const auto t0 = std::chrono::steady_clock::now();
    db.loadFolder (dir);
    const auto t1 = std::chrono::steady_clock::now();
    auto r = db.search ("brand7 m17");
    const auto t2 = std::chrono::steady_clock::now();

    const auto ms = [] (auto a, auto b) { return (int) std::chrono::duration_cast<std::chrono::milliseconds> (b - a).count(); };
    check (db.size() == 20000 && ! r.empty() && ms (t1, t2) < 200, "Gear: 20k products load + search",
           "load " + juce::String (ms (t0, t1)) + " ms, search " + juce::String (ms (t1, t2)) + " ms, "
               + juce::String ((int) r.size()) + " hits");

    dir.getParentDirectory().deleteRecursively();
}

FA_TEST (gearRealDataLoads)
{
    const auto repo = findRepoGear();
    GearDatabase db;
    const int n = repo.isDirectory() ? db.loadFolder (repo) : 0;
    check (! repo.isDirectory() || db.getLoadWarnings().isEmpty(), "Gear: real data/gear loads with 0 warnings",
           juce::String (n) + " products, " + juce::String (db.getLoadWarnings().size()) + " warnings, from "
               + repo.getFullPathName());

    for (int i = 0; i < juce::jmin (10, db.getLoadWarnings().size()); ++i)
        std::cout << "    warning: " << db.getLoadWarnings()[i] << "\n";

    // every real product survives toVar -> JSON -> fromVar unchanged (no field lost or invented)
    int mismatches = 0;
    juce::String first;
    for (auto* p : db.search ({}, std::nullopt, 0))
    {
        const auto a = juce::JSON::toString (p->toVar(), true);
        const auto b = juce::JSON::toString (Product::fromVar (juce::JSON::parse (a)).toVar(), true);
        if (a != b)
        {
            if (mismatches++ == 0)
                first = p->id;
        }
    }
    check (mismatches == 0, "Gear: real products round-trip stably", juce::String (mismatches) + " " + first);
}

//==============================================================================
namespace
{
Product parseJson (const juce::String& json)
{
    return Product::fromVar (juce::JSON::parse (json));
}

Product roundTrip (const Product& p)
{
    return Product::fromVar (juce::JSON::parse (juce::JSON::toString (p.toVar())));
}
} // namespace

FA_TEST (gearSchemaAdditionsRoundTrip)
{
    // amp: channel groups, extra loads, ranges, fuses
    Product amp;
    amp.category = Category::ampMulti;
    amp.brand = "T";
    amp.model = "5ch";
    AmpSpecs a;
    a.channels = 5;
    a.rmsPerChannelW = { { "4ohm", 75.0 }, { "3ohm", 85.0 }, { "2ohm", 100.0 } };
    a.minStableOhms = 2;
    AmpChannelGroup sub;
    sub.channels = 1;
    sub.label = "sub";
    sub.rmsPerChannelW = { { "2ohm", 500.0 }, { "1ohm", 650.0 }, { "0.5ohm", 700.0 } };
    sub.minStableOhms = 0.5;
    a.channelGroups.push_back (sub);
    a.ratedVoltageV = 14.4;
    a.recommendedFuseA = 100;
    a.fuseCount = 2;
    a.inputSensitivityRanges = { { 0.2, 4.0, "low" }, { 2.0, 16.0, "high" } };
    amp.amp = a;

    const auto qa = roundTrip (amp);
    check (qa.amp && qa.amp->channelGroups == a.channelGroups && qa.amp->rmsPerChannelW == a.rmsPerChannelW
               && qa.amp->ratedVoltageV == 14.4 && qa.amp->recommendedFuseA == 100.0 && qa.amp->fuseCount == 2
               && qa.amp->inputSensitivityRanges == a.inputSensitivityRanges,
           "Gear: amp additions round trip", juce::JSON::toString (amp.toVar(), true));

    // driver additions
    Product drv;
    drv.category = Category::subwoofer;
    drv.brand = "T";
    drv.model = "Sub";
    DriverSpecs d;
    d.voiceCoils = 2;
    d.reOhms = 1.8;
    d.tsWiring = "parallel";
    d.switchableImpedance = "2 or 4 ohm (SSI)";
    d.cutoutShape = "square";
    d.cutoutMm = 280;
    d.sensitivityDb = 88;
    d.sensitivityRef = "1W/1m";
    d.secondarySensitivityDb = 91;
    d.secondarySensitivityRef = "2.83V/1m";
    d.boxRecommendations = { { "sealed", 28.0, 0.0 }, { "ported", 42.0, 32.0 } };
    drv.driver = d;

    const auto qd = roundTrip (drv);
    check (qd.driver && qd.driver->tsWiring == "parallel" && qd.driver->switchableImpedance == d.switchableImpedance
               && qd.driver->cutoutShape == "square" && qd.driver->secondarySensitivityDb == 91.0
               && qd.driver->secondarySensitivityRef == "2.83V/1m" && qd.driver->boxRecommendations == d.boxRecommendations,
           "Gear: driver additions round trip");

    // source additions
    Product dsp;
    dsp.category = Category::dsp;
    dsp.brand = "T";
    dsp.model = "DSP";
    SourceSpecs s;
    s.peqBandsPerChannel = 31;
    s.peqBandsByStage = { 10, 31, 15 };
    s.digitalInputs = 2;
    s.digitalInputTypes = juce::StringArray ("optical", "coax");
    s.highLevelInputs = 6;
    s.snrDb = 105;
    s.snrDigitalDb = 112;
    s.preoutRatedLoadOhms = 10000;
    s.outputImpedanceOhms = 200;
    s.outputImpedanceIsUpperBound = true;
    s.maxDelayMs = 20;
    s.delayRangeMs = { 0.02, 20.0 };
    dsp.source = s;

    const auto dspVar = dsp.toVar();
    const auto qs = roundTrip (dsp);
    check (qs.source && qs.source->peqBandsByStage == s.peqBandsByStage && qs.source->digitalInputs == 2
               && qs.source->digitalInputTypes == s.digitalInputTypes && qs.source->highLevelInputs == 6
               && qs.source->snrDigitalDb == 112.0 && qs.source->preoutRatedLoadOhms == 10000.0
               && qs.source->outputImpedanceIsUpperBound && qs.source->delayRangeMs == s.delayRangeMs
               && dspVar.getProperty ("source", {}).getProperty ("peqBandsByStage", {}).hasProperty ("virtual"),
           "Gear: source additions round trip", juce::JSON::toString (dspVar, true));

    // new blocks + mic
    Product bat;
    bat.category = Category::battery;
    bat.brand = "T";
    bat.model = "B";
    bat.battery = BatterySpecs { 100.0, 1150.0, 1400.0, 200.0, 3000.0, "AGM" };
    const auto qb = roundTrip (bat);
    check (qb.battery && qb.battery->ah == 100.0 && qb.battery->cca == 1150.0 && qb.battery->ca == 1400.0
               && qb.battery->reserveMin == 200.0 && qb.battery->maxAmps == 3000.0 && qb.battery->chemistry == "AGM"
               && ! qb.driver && specsSummary (qb).contains ("100 Ah"),
           "Gear: battery block round trip", specsSummary (qb).joinIntoString (" | "));

    Product alt;
    alt.category = Category::alternator;
    alt.brand = "T";
    alt.model = "A";
    alt.alternator = AlternatorSpecs { 250.0, 180.0, 220.0 };
    const auto qal = roundTrip (alt);
    check (qal.alternator && qal.alternator->ratedA == 250.0 && qal.alternator->idleA == 180.0
               && qal.alternator->hotRatedA == 220.0,
           "Gear: alternator block round trip");

    Product kit;
    kit.category = Category::powerWire;
    kit.brand = "T";
    kit.model = "4ga kit";
    kit.wire = WireSpecs { 4.0, 0.0, "OFC", 0.0, 0.0 };
    kit.fuse = FuseSpecs { 80.0, "ANL" };
    const auto qk = roundTrip (kit);
    check (qk.fuse && qk.fuse->ratingA == 80.0 && qk.fuse->type == "ANL" && qk.wire && qk.wire->awg == 4.0
               && specsSummary (qk).contains ("80 A ANL fuse"),
           "Gear: fuse block round trip (kit)", specsSummary (qk).joinIntoString (" | "));

    Product mic;
    mic.category = Category::measurementMic;
    mic.brand = "T";
    mic.model = "UMIK";
    MicSpecs m;
    m.type = "TRRS";
    m.sensitivityDbfs = -18;
    mic.mic = m;
    const auto qm = roundTrip (mic);
    check (qm.mic && qm.mic->type == "TRRS" && qm.mic->sensitivityDbfs == -18.0, "Gear: mic dBFS / TRRS round trip");

    // dotted keys with a dot in the map key ("0.5ohm") and nested stage objects
    setSpecValue (amp, "amp.rmsPerChannelW.0.5ohm", 40.0);
    check (amp.amp->rmsPerChannelW["0.5ohm"] == 40.0 && (double) getSpecValue (amp, "amp.rmsPerChannelW.0.5ohm") == 40.0
               && amp.amp->channelGroups == a.channelGroups,
           "Gear: setSpecValue / getSpecValue with '0.5ohm' key; arrays survive the edit");
    setSpecValue (dsp, "source.peqBandsByStage.input", 12);
    check (dsp.source->peqBandsByStage.input == 12 && (int) getSpecValue (dsp, "source.peqBandsByStage.virtual") == 15,
           "Gear: setSpecValue on peqBandsByStage");
}

FA_TEST (gearSchemaBackwardCompatible)
{
    // a file written in the original schema loads exactly as before and writes back without new keys
    const juce::String legacy = R"({ "id": "old-amp", "category": "ampMulti", "brand": "Old", "model": "Amp",
        "verified": true, "amp": { "channels": 4, "rmsPerChannelW": { "4ohm": 50, "2ohm": 75, "bridged4ohm": 150 },
        "inputSensitivityMinV": 0.2, "inputSensitivityMaxV": 6, "minStableOhms": 2, "fuseRatingA": 40 },
        "source": { "preoutVoltageV": 4, "preoutChannels": 6, "maxVolumeStep": 40, "peqBandsPerChannel": 10, "maxDelayMs": 10 },
        "driver": { "sizeInches": 6.5, "reOhms": 3.2, "fsHz": 55 },
        "mic": { "type": "USB", "sensitivityMvPa": 12 } })";
    const auto p = parseJson (legacy);
    const auto v = p.toVar();

    check (p.amp && p.amp->channels == 4 && p.amp->rmsPerChannelW.size() == 3 && p.amp->channelGroups.empty()
               && p.amp->inputSensitivityRanges.empty() && p.amp->recommendedFuseA == 0.0 && p.amp->fuseCount == 0,
           "Gear: legacy amp loads unchanged");
    check (p.source && p.source->peqBandsPerChannel == 10 && ! p.source->peqBandsByStage.isSet()
               && p.source->maxDelayMs == 10.0 && ! p.source->delayRangeMs.isSet() && p.source->digitalInputs == 0,
           "Gear: legacy source loads unchanged");
    check (p.driver && p.driver->tsWiring.isEmpty() && p.driver->boxRecommendations.empty() && ! p.battery && ! p.fuse,
           "Gear: legacy driver loads unchanged, no new blocks");

    const auto json = juce::JSON::toString (v);
    bool noNewKeys = true;
    for (auto* k : { "channelGroups", "inputSensitivityRanges", "recommendedFuseA", "fuseCount", "ratedVoltageV",
                     "peqBandsByStage", "digitalInputs", "highLevelInputs", "delayRangeMs", "outputImpedanceIsUpperBound",
                     "tsWiring", "cutoutShape", "boxRecommendations", "sensitivityDbfs", "\"battery\"", "\"fuse\"" })
        noNewKeys = noNewKeys && ! json.contains (k);
    check (noNewKeys, "Gear: legacy product writes no new keys", json);

    // tolerant forms researchers may write
    const auto t = parseJson (R"({ "category": "dsp", "brand": "X", "model": "Y",
        "source": { "peqBandsByStage": { "input": 10, "output": 30, "virtual": 10 }, "digitalInputs": ["optical", "coax"],
                    "delayRangeMs": 15, "outputImpedanceIsUpperBound": "yes" },
        "amp": { "channelGroups": [ 5, { "channels": "1", "rmsPerChannelW": { "2ohm": "400" } } ] } })");
    check (t.source && t.source->peqBandsPerChannel == 30 && t.source->maxDelayMs == 15.0
               && t.source->delayRangeMs.max == 15.0 && t.source->digitalInputs == 2
               && t.source->digitalInputTypes == juce::StringArray ("optical", "coax") && t.source->outputImpedanceIsUpperBound,
           "Gear: peqBandsPerChannel / maxDelayMs filled from new fields; digitalInputs array accepted");
    check (t.amp && t.amp->channelGroups.size() == 1 && t.amp->channelGroups[0].channels == 1
               && t.amp->channelGroups[0].rmsPerChannelW.at ("2ohm") == 400.0,
           "Gear: channelGroups skips non-object entries, accepts numeric strings");

    const auto r = parseJson (R"({ "category": "dsp", "brand": "X", "model": "Z", "source": { "delayRangeMs": { "min": 0, "max": 8.5 } } })");
    check (r.source && r.source->delayRangeMs.max == 8.5 && r.source->maxDelayMs == 8.5, "Gear: delayRangeMs object form");
}

FA_TEST (gearAmpChannelGroups)
{
    AmpSpecs a;
    a.channels = 5;
    a.rmsPerChannelW = { { "4ohm", 75.0 }, { "2ohm", 100.0 } };
    a.minStableOhms = 2;
    a.inputSensitivityMaxV = 4;
    AmpChannelGroup sub;
    sub.channels = 1;
    sub.label = "sub";
    sub.rmsPerChannelW = { { "4ohm", 300.0 }, { "2ohm", 500.0 }, { "1ohm", 600.0 } };
    sub.minStableOhms = 1;
    a.channelGroups.push_back (sub);

    const auto layout = ampChannelLayout (a);
    check (layout.size() == 2 && layout[0].groupIndex == -1 && layout[0].group.channels == 4 && layout[0].firstChannel == 0
               && layout[1].groupIndex == 0 && layout[1].firstChannel == 4 && layout[1].group.label == "sub",
           "Gear: ampChannelLayout main + sub");
    check (ampGroupIndexForChannel (a, 0) == -1 && ampGroupIndexForChannel (a, 3) == -1 && ampGroupIndexForChannel (a, 4) == 0
               && ampGroupIndexForChannel (a, 9) == -1,
           "Gear: ampGroupIndexForChannel");
    check (ampPowerForGroup (a, 0, 4.0) == 75.0 && ampPowerForGroup (a, 4, 2.0) == 500.0 && ampPowerForGroup (a, 4, 1.0) == 600.0,
           "Gear: ampPowerForGroup picks the group's ratings");

    juce::String note;
    const double p3 = ampPowerForGroup (a, 1, 3.0, false, &note);
    check (p3 > 75.0 && p3 < 100.0 && note.isNotEmpty(), "Gear: ampPowerForGroup interpolates", juce::String (p3) + " " + note);
    check (ampPowerForGroup (a, 0, 8.0) == 37.5, "Gear: above highest rated load is voltage limited");
    check (std::abs (ampPowerForGroup (a, 0, 4.0, true) - 200.0) < 1e-9, "Gear: bridged estimate = 2 x P(R/2)");
    check (ampMinStableOhmsForChannel (a, 0) == 2.0 && ampMinStableOhmsForChannel (a, 4) == 1.0, "Gear: min stable per group");

    const auto subView = ampSpecsForChannel (a, 4);
    const auto mainView = ampSpecsForChannel (a, 2);
    check (subView.channels == 1 && subView.rmsPerChannelW == sub.rmsPerChannelW && subView.minStableOhms == 1.0
               && subView.inputSensitivityMaxV == 4.0 && subView.channelGroups.empty()
               && mainView.channels == 4 && mainView.rmsPerChannelW == a.rmsPerChannelW,
           "Gear: ampSpecsForChannel views");
    check (ampTotalMaxRmsW (a) == 4 * 100.0 + 600.0, "Gear: ampTotalMaxRmsW", juce::String (ampTotalMaxRmsW (a)));

    // amp without groups behaves as before
    AmpSpecs plain;
    plain.channels = 4;
    plain.rmsPerChannelW = { { "4ohm", 50.0 } };
    check (ampChannelLayout (plain).size() == 1 && ampPowerForGroup (plain, 3, 4.0) == 50.0 && ampTotalMaxRmsW (plain) == 200.0,
           "Gear: single-group amp");

    Product prod;
    prod.category = Category::ampMulti;
    prod.amp = a;
    const auto summary = specsSummary (prod).joinIntoString (" | ");
    check (summary.contains ("x 4") && summary.contains ("(sub)"), "Gear: specsSummary shows channel groups", summary);
}

FA_TEST (gearOvalAndComponentSet)
{
    const auto p = parseJson (R"({ "category": "componentSet", "brand": "X", "model": "Set",
        "driver": { "sizeInches": 6.5, "cutoutShape": "oval", "cutoutWidthMm": 160, "cutoutHeightMm": 230,
                    "recommendedHpHz": 80, "recommendedHpBySlope": [ { "slopeDbPerOct": 12, "hpHz": 90 }, { "slopeDbPerOct": 24, "hpHz": 60 } ] },
        "componentSet": { "woofer": { "productId": "x-woofer" }, "tweeter": { "driver": { "fsHz": 900, "recommendedHpHz": 3000 } },
                         "crossoverHz": 3200, "crossoverSlopeDbPerOct": 12 } })");
    check (p.driver && p.driver->cutoutWidthMm == 160 && p.driver->cutoutHeightMm == 230, "Gear: oval cutout parsed");
    bool shown = false;
    for (auto& s : specsSummary (p))
        shown = shown || s.contains ("160x230");
    check (shown, "Gear: oval cutout in specsSummary");
    check (p.driver && recommendedHpForSlope (*p.driver, 24) == 60 && recommendedHpForSlope (*p.driver, 12) == 90
               && recommendedHpForSlope (*p.driver, 36) == 80, "Gear: HP by slope");
    check (p.componentSet && p.componentSet->woofer.productId == "x-woofer" && p.componentSet->crossoverHz == 3200
               && p.componentSet->tweeter.driver && p.componentSet->tweeter.driver->fsHz == 900, "Gear: componentSet parsed");
    const auto back = Product::fromVar (p.toVar());
    check (back.componentSet && back.componentSet->tweeter.driver && back.componentSet->tweeter.driver->recommendedHpHz == 3000
               && back.driver && back.driver->recommendedHpBySlope.size() == 2, "Gear: componentSet + slope table round trip");
    const auto old = parseJson (R"({ "category": "componentSet", "brand": "X", "model": "Old", "driver": { "sizeInches": 6.5 } })");
    check (! old.componentSet && ! componentSubDriver (old, ComponentRole::woofer, {}), "Gear: legacy componentSet loads, no sub-drivers");
}
