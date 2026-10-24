#pragma once

#include <juce_core/juce_core.h>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <vector>

namespace fa::gear
{
/** Every kind of product the equipment database knows about. */
enum class Category
{
    headUnit, dsp, dspAmp, dac, ampMulti, ampMono,
    driverTweeter, driverMidrange, driverMidbass, driverCoax, componentSet, subwoofer,
    powerWire, speakerWire, rca, fuseHolder, battery, alternator,
    audioInterface, measurementMic
};

juce::String toString (Category c);
/** Case-insensitive; unknown strings map to Category::headUnit (use tryCategoryFromString to detect them). */
Category categoryFromString (const juce::String& s);
/** Case-insensitive; std::nullopt for unknown strings. */
std::optional<Category> tryCategoryFromString (const juce::String& s);
/** All categories in declaration order. */
const std::vector<Category>& allCategories();
/** Stable id: lowercase "<brand>-<model>", non-alphanumerics replaced by single dashes. */
juce::String makeProductId (const juce::String& brand, const juce::String& model);

/** Thiele/Small and mechanical parameters of a driver (SI-ish units noted). */
struct DriverSpecs
{
    double sizeInches = 0, nominalOhms = 0, reOhms = 0, fsHz = 0, qts = 0, qes = 0, qms = 0;
    double vasLitres = 0, xmaxMm = 0, sdCm2 = 0, bl = 0, mmsGrams = 0, leMh = 0;
    double sensitivityDb = 0;      // dB 1W/1m (or 2.83V/1m, see sensitivityRef)
    juce::String sensitivityRef;   // "1W/1m" | "2.83V/1m"
    double powerRmsW = 0, powerPeakW = 0;
    double freqLowHz = 0, freqHighHz = 0;
    int    voiceCoils = 1;         // 2 = dual voice coil
    double coilOhms = 0;           // per coil
    double mountingDepthMm = 0, cutoutMm = 0;
    double recommendedHpHz = 0, recommendedLpHz = 0;  // manufacturer recommendation, if published
};

/** Amplifier (or amplified DSP) specs. */
struct AmpSpecs
{
    int channels = 0;
    std::map<juce::String, double> rmsPerChannelW;   // keys "4ohm", "2ohm", "1ohm", "bridged4ohm" ...
    double inputSensitivityMinV = 0, inputSensitivityMaxV = 0;   // gain range (RCA)
    double highLevelInputMaxV = 0;
    double snrDb = 0, thdPercent = 0, dampingFactor = 0;
    double fuseRatingA = 0;
    double efficiencyPercent = 0;      // class D ~ 80-90, AB ~ 50-60
    juce::String amplifierClass;       // "D", "AB", "G/H"
    double minStableOhms = 0;
    bool   hasBassBoost = false, hasLpf = false, hasHpf = false, hasSubsonic = false;
    double bassBoostMaxDb = 0;
};

/** Source / processor specs (head unit, DSP, DAC, DSP amp). */
struct SourceSpecs
{
    double preoutVoltageV = 0;          // max RCA output
    int    preoutChannels = 0;
    double outputImpedanceOhms = 0;
    int    maxVolumeStep = 0;           // e.g. 40, 50, 62 steps
    int    cleanVolumeStep = 0;         // last step before clipping with 0 dBFS content, if known
    double snrDb = 0;
    bool   timeAlignment = false, crossovers = false;
    int    eqBands = 0;
    juce::String eqType;                // "graphic" | "parametric"
    // DSP-specific
    int    inputs = 0, outputs = 0;
    int    peqBandsPerChannel = 0;
    double maxDelayMs = 0;
    double sampleRateHz = 0;
    double maxInputVoltageV = 0;        // RCA/high-level input clip point
    juce::String software;              // vendor PC tool name (for DSP profiles)
    juce::String softwareExe;           // usual executable name
};

/** Wire (power or speaker), RCA cables and other conductors. */
struct WireSpecs
{
    double awg = 0;                     // gauge (0 = 1/0); use mm2 when awg unknown
    double mm2 = 0;
    juce::String material;              // "OFC" | "CCA" | "Tinned OFC"
    double ohmsPerMetre = 0;            // if published; else computed from material and size
    double ampacityA = 0;
};

/** Audio interface / measurement mic specs (for setup guidance). */
struct InterfaceSpecs
{
    int inputs = 0, outputs = 0, micPreamps = 0;
    bool phantomPower = false, hasLoopback = false, asio = false;
    double maxGainDb = 0, maxOutputDbu = 0;
    juce::StringArray sampleRates;
    juce::String connection;            // "USB-C", "USB" ...
};

struct MicSpecs
{
    juce::String type;                  // "USB" | "XLR"
    bool needsPhantom = false;
    double sensitivityMvPa = 0;
    bool calFileAvailable = false;
    juce::String calFileUrl;
    double maxSplDb = 0;
};

/** One product. Only the spec block matching its category is meaningful. */
struct Product
{
    juce::String id;                    // stable: "<brand>-<model>" lowercase, dashes
    Category category = Category::headUnit;
    juce::String brand, model, series;
    int year = 0;
    juce::String sourceUrl;             // where the specs came from
    juce::String notes;
    bool verified = false;              // specs checked against an official source
    bool userDefined = false;           // came from / saved to the user folder (set by GearDatabase, not serialized)

    std::optional<DriverSpecs> driver;
    std::optional<AmpSpecs> amp;
    std::optional<SourceSpecs> source;
    std::optional<WireSpecs> wire;
    std::optional<InterfaceSpecs> audioInterface;
    std::optional<MicSpecs> mic;

    juce::String displayName() const { return brand + " " + model; }

    juce::var toVar() const;
    static Product fromVar (const juce::var& v);
};

/** Human readable key specs for list rows, e.g. { "Fs 32 Hz", "Xmax 6.5 mm", "90 W RMS" }.
    Join with joinIntoString (" · ") for a one-line summary. */
juce::StringArray specsSummary (const Product& p);

//==============================================================================
/** Describes one editable spec for the custom-product form.
    key is a dotted path into the product JSON: "<block>.<field>", e.g. "driver.fsHz",
    or "amp.rmsPerChannelW.4ohm" for a map entry. */
struct SpecField
{
    juce::String key, label, unit, usedFor, hint;
    bool required = false;
    double minV = 0, maxV = 0;          // sanity range for numbers (0/0 = unchecked)

    enum class Kind { number, text, boolean };
    Kind kind = Kind::number;
    juce::StringArray options;          // suggested values for text fields (e.g. "OFC", "CCA")
    juce::String requiredGroup;         // non-empty: requirement satisfied if ANY field of the group is set
};

/** Required + optional spec fields for a category (empty for categories without a spec block). */
std::vector<SpecField> specFields (Category c);

/** Labels of required fields that are missing (brand/model included; a group is listed once). */
juce::StringArray missingRequired (const Product& p);
/** Same, as SpecField keys ("brand", "model" or e.g. "driver.fsHz"; a group lists all its keys). */
juce::StringArray missingRequiredKeys (const Product& p);

/** Reads a spec by dotted key; returns a void var when absent (numbers 0 / false / empty count as absent,
    except "wire.awg" which is present whenever the wire block exists, since 0 means 1/0). */
juce::var getSpecValue (const Product& p, const juce::String& key);
/** Sets a spec by dotted key (creating the block); a void var / empty string removes it. */
void setSpecValue (Product& p, const juce::String& key, const juce::var& value);

/** The equipment database: built-in JSON files + user additions.
    Data files: data/gear/<category>/<brand>.json, each {"products":[Product...]} (see SCHEMA.md).

    Threading: load (loadDefaults / loadFolder) once at startup before other threads use it; afterwards the
    database is read-only and the const queries are safe from any thread. addUserProduct / clear mutate it
    and must only be called from the message thread while no other thread is querying.
    Product pointers returned by queries stay valid until clear() (storage is a deque; updates are in place).

    Loading is robust: invalid files / entries are skipped with a logged warning (see getLoadWarnings()).
    Duplicate ids: a later source replaces an earlier one unless the earlier is verified and the later is not;
    products from the user folder always win. */
class GearDatabase
{
public:
    static GearDatabase& instance();

    /** Loads every *.json file under the folder (recursively). Returns number of products loaded from it. */
    int loadFolder (const juce::File& folder);
    /** Default locations: <exe>/data/gear, <repo>/data/gear (dev, found by walking up from the exe),
        <appdata>/FalseAudio/Gear (user). Clears first. Returns size(). */
    int loadDefaults();
    /** Loads a folder whose products override everything (used for the user folder). */
    int loadUserFolder (const juce::File& folder);

    void clear();

    /** Folder holding user.json (default <userAppData>/FalseAudio/Gear). Tests may redirect it. */
    void setUserFolder (const juce::File& folder)   { userFolder = folder; }
    juce::File getUserFolder() const;

    /** Warnings collected while loading (invalid files / entries). */
    const juce::StringArray& getLoadWarnings() const noexcept { return warnings; }

    std::vector<const Product*> search (const juce::String& text, std::optional<Category> category = std::nullopt,
                                        int maxResults = 200) const;
    juce::StringArray brands (Category category) const;
    std::vector<const Product*> byBrand (Category category, const juce::String& brand) const;
    const Product* find (const juce::String& id) const;
    int size() const noexcept { return liveCount; }

    /** Adds/updates a user product (saved to <userFolder>/user.json). Also used to edit/override a built-in
        product: pass it with the same id; the user copy wins (the built-in is kept and restored on remove).
        The stored copy gets userDefined = true. */
    bool addUserProduct (const Product& p, juce::String& error);
    /** Removes a user product from user.json and memory (restoring the built-in it overrode, if any).
        Returns false if the id is not a user product or the file can't be written. Pointers to a removed
        product must no longer be used. */
    bool removeUserProduct (const juce::String& id);

private:
    int loadFolderImpl (const juce::File& folder, bool userSource);
    int loadFile (const juce::File& file, bool userSource);
    bool insert (Product&& p, bool userSource);
    void warn (const juce::String& message);
    bool writeUserFile (const std::function<void (juce::Array<juce::var>&)>& edit, juce::String& error);

    std::deque<Product> products;
    std::vector<juce::String> searchKeys;           // lowercase, parallel to products
    std::vector<bool> fromUser;                     // parallel to products
    std::vector<bool> alive;                        // parallel to products (false = removed)
    std::map<juce::String, Product> shadowed;       // built-ins overridden by a user product
    int liveCount = 0;
    std::map<juce::String, size_t> index;           // lowercase id -> products index
    juce::File userFolder;
    juce::StringArray warnings;
};
} // namespace fa::gear
