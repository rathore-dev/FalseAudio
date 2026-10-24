#include "TestRegistry.h"
#include "core/gear/Gear.h"

#include <chrono>

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
    check (true, "Gear: real data/gear loads without crashing",
           juce::String (n) + " products, " + juce::String (db.getLoadWarnings().size()) + " warnings, from "
               + repo.getFullPathName());

    for (int i = 0; i < juce::jmin (10, db.getLoadWarnings().size()); ++i)
        std::cout << "    warning: " << db.getLoadWarnings()[i] << "\n";
}
