// Spec field descriptions for the custom-product form, plus dotted-key get/set helpers.
#include "Gear.h"

namespace fa::gear
{
namespace
{
using Kind = SpecField::Kind;

SpecField number (const char* key, const char* label, const char* unit, const char* usedFor, const char* hint,
                  bool required, double minV, double maxV, const char* group = "")
{
    SpecField f;
    f.key = key;
    f.label = juce::String (juce::CharPointer_UTF8 (label));
    f.unit = juce::String (juce::CharPointer_UTF8 (unit));
    f.usedFor = usedFor;
    f.hint = juce::String (juce::CharPointer_UTF8 (hint));
    f.required = required;
    f.minV = minV;
    f.maxV = maxV;
    f.requiredGroup = group;
    return f;
}

SpecField text (const char* key, const char* label, const char* usedFor, const char* hint, bool required,
                juce::StringArray options = {})
{
    auto f = number (key, label, "", usedFor, hint, required, 0, 0);
    f.kind = Kind::text;
    f.options = std::move (options);
    return f;
}

SpecField flag (const char* key, const char* label, const char* usedFor, const char* hint = "")
{
    auto f = number (key, label, "", usedFor, hint, false, 0, 0);
    f.kind = Kind::boolean;
    return f;
}

#define FA_OHM "\xce\xa9"

void addHeadUnit (std::vector<SpecField>& f)
{
    f.push_back (number ("source.preoutVoltageV", "Pre-out voltage", "V", "Amp gain matching",
                         "Max RCA output from the manual, e.g. 2, 4 or 5 V", true, 0.1, 15));
    f.push_back (number ("source.maxVolumeStep", "Max volume step", "", "Gain setting volume position",
                         "Highest number on the volume display, e.g. 40, 50, 62", true, 1, 100));
    f.push_back (number ("source.cleanVolumeStep", "Clean volume step", "", "Gain setting volume position",
                         "Last step before pre-out clipping with a 0 dBFS signal, EQ flat, if known", false, 1, 100));
    f.push_back (number ("source.preoutChannels", "Pre-out channels", "ch", "System layout",
                         "Total RCA output channels (front/rear/sub pairs = 6)", false, 1, 16));
    f.push_back (number ("source.preoutRatedLoadOhms", "Pre-out rated load", FA_OHM, "Amp gain matching",
                         "Load the pre-out voltage was rated into, e.g. 10000", false, 100, 1000000));
    f.push_back (number ("source.outputImpedanceOhms", "Output impedance", FA_OHM, "Noise / cable advice", "", false, 1, 10000));
    f.push_back (flag ("source.outputImpedanceIsUpperBound", "Output impedance is a maximum", "Noise / cable advice",
                       "Manual says \"max\" or \"<\""));
    f.push_back (number ("source.snrDb", "SNR", "dB", "Noise floor estimate", "Manufacturer analog / USB figure", false, 50, 140));
    f.push_back (flag ("source.timeAlignment", "Time alignment", "Tuning: delay per channel"));
    f.push_back (flag ("source.crossovers", "Crossovers", "Tuning: crossover in head unit"));
    f.push_back (number ("source.eqBands", "EQ bands", "", "Tuning: EQ", "", false, 1, 64));
    f.push_back (text ("source.eqType", "EQ type", "Tuning: EQ", "", false, { "graphic", "parametric" }));
}

void addDsp (std::vector<SpecField>& f, bool requiredCore)
{
    f.push_back (number ("source.inputs", "Inputs", "ch", "Routing", "", requiredCore, 1, 32));
    f.push_back (number ("source.outputs", "Outputs", "ch", "Routing / channel assignment", "", requiredCore, 1, 32));
    f.push_back (number ("source.peqBandsPerChannel", "PEQ bands per channel", "", "Auto-EQ band budget",
                         "", requiredCore, 1, 64));
    f.push_back (number ("source.maxDelayMs", "Max delay", "ms", "Time alignment range", "", requiredCore, 0.1, 100));
    f.push_back (number ("source.sampleRateHz", "Sample rate", "Hz", "Filter design", "e.g. 48000 or 96000",
                         requiredCore, 8000, 384000));
    f.push_back (number ("source.peqBandsByStage.input", "PEQ bands per input", "", "Auto-EQ band budget",
                         "Input-stage bands, if the DSP has them", false, 1, 64));
    f.push_back (number ("source.peqBandsByStage.virtual", "PEQ bands per virtual channel", "", "Auto-EQ band budget",
                         "", false, 1, 64));
    f.push_back (number ("source.maxInputVoltageV", "Max input voltage", "V", "Input gain staging",
                         "RCA / high-level input clip point", false, 0.1, 40));
    f.push_back (number ("source.highLevelInputs", "High-level inputs", "ch", "Routing (speaker-level input)", "", false, 1, 32));
    f.push_back (number ("source.digitalInputs", "Digital inputs", "", "Routing (optical / coax / USB)", "", false, 1, 8));
    f.push_back (number ("source.preoutVoltageV", "Max output voltage", "V", "Amp gain matching", "", false, 0.1, 15));
    f.push_back (number ("source.snrDb", "SNR", "dB", "Noise floor estimate", "Analog input figure", false, 50, 140));
    f.push_back (number ("source.snrDigitalDb", "SNR (digital input)", "dB", "Noise floor estimate", "", false, 50, 140));
    f.push_back (text ("source.software", "PC software", "DSP profile", "Vendor tuning tool name", false));
    f.push_back (text ("source.softwareExe", "Software exe", "DSP profile", "e.g. TuN.exe", false));
}

void addDac (std::vector<SpecField>& f)
{
    f.push_back (number ("source.preoutVoltageV", "Max output voltage", "V", "Amp gain matching", "", true, 0.1, 15));
    f.push_back (number ("source.inputs", "Inputs", "ch", "Routing", "", false, 1, 32));
    f.push_back (number ("source.outputs", "Outputs", "ch", "Routing", "", false, 1, 32));
    f.push_back (number ("source.sampleRateHz", "Sample rate", "Hz", "Filter design", "", false, 8000, 384000));
    f.push_back (number ("source.snrDb", "SNR", "dB", "Noise floor estimate", "", false, 50, 140));
}

void addAmp (std::vector<SpecField>& f)
{
    const char* rmsUse = "Power budget, gain setting, speaker matching";
    f.push_back (number ("amp.channels", "Channels", "ch", "Channel assignment", "", true, 1, 16));
    f.push_back (number ("amp.rmsPerChannelW.4ohm", "RMS per channel @ 4 " FA_OHM, "W", rmsUse,
                         "At least one RMS rating is required", true, 1, 10000, "rms"));
    f.push_back (number ("amp.rmsPerChannelW.3ohm", "RMS per channel @ 3 " FA_OHM, "W", rmsUse, "", false, 1, 10000));
    f.push_back (number ("amp.rmsPerChannelW.2ohm", "RMS per channel @ 2 " FA_OHM, "W", rmsUse, "", true, 1, 10000, "rms"));
    f.push_back (number ("amp.rmsPerChannelW.1ohm", "RMS per channel @ 1 " FA_OHM, "W", rmsUse, "", true, 1, 20000, "rms"));
    f.push_back (number ("amp.rmsPerChannelW.0.5ohm", "RMS per channel @ 0.5 " FA_OHM, "W", rmsUse, "", false, 1, 40000));
    f.push_back (number ("amp.rmsPerChannelW.bridged4ohm", "RMS bridged @ 4 " FA_OHM, "W", rmsUse, "", true, 1, 20000, "rms"));
    f.push_back (number ("amp.rmsPerChannelW.bridged2ohm", "RMS bridged @ 2 " FA_OHM, "W", rmsUse, "", false, 1, 20000));
    f.push_back (number ("amp.inputSensitivityMinV", "Input sensitivity min", "V", "Gain setting (gain knob range)",
                         "Lowest voltage of the gain range, e.g. 0.2 V", true, 0.01, 20));
    f.push_back (number ("amp.inputSensitivityMaxV", "Input sensitivity max", "V", "Gain setting (gain knob range)",
                         "Highest voltage of the gain range, e.g. 6 V", true, 0.1, 40));
    f.push_back (number ("amp.minStableOhms", "Minimum stable load", FA_OHM, "Speaker wiring / load check",
                         "e.g. 2 or 1", true, 0.25, 16));
    f.push_back (number ("amp.highLevelInputMaxV", "High-level input max", "V", "Speaker-level input gain staging", "", false, 0.5, 50));
    f.push_back (number ("amp.fuseRatingA", "Fuse rating", "A", "Power wire and fuse sizing", "Total on-board fuse amps", false, 1, 1000));
    f.push_back (number ("amp.fuseCount", "On-board fuses", "", "Info", "Number of fuses (fuse rating = total)", false, 1, 16));
    f.push_back (number ("amp.recommendedFuseA", "Recommended external fuse", "A", "Power wire and fuse sizing",
                         "In-line fuse at the battery from the manual", false, 1, 2000));
    f.push_back (number ("amp.ratedVoltageV", "Rated at supply voltage", "V", "Power at your system voltage",
                         "Voltage the RMS ratings were measured at, e.g. 14.4", false, 9, 18));
    f.push_back (number ("amp.efficiencyPercent", "Efficiency", "%", "Current draw estimate",
                         "If unknown, class D is estimated at 80 %, AB at 55 %", false, 20, 99));
    f.push_back (text ("amp.amplifierClass", "Amplifier class", "Current draw estimate", "", false, { "D", "AB", "G/H" }));
    f.push_back (number ("amp.snrDb", "SNR", "dB", "Noise floor estimate", "", false, 50, 140));
    f.push_back (number ("amp.thdPercent", "THD", "%", "Info", "", false, 0, 10));
    f.push_back (number ("amp.dampingFactor", "Damping factor", "", "Info", "", false, 1, 10000));
    f.push_back (flag ("amp.hasHpf", "High-pass filter", "Crossover plan"));
    f.push_back (flag ("amp.hasLpf", "Low-pass filter", "Crossover plan"));
    f.push_back (flag ("amp.hasSubsonic", "Subsonic filter", "Subwoofer protection"));
    f.push_back (flag ("amp.hasBassBoost", "Bass boost", "Setup advice (turn it off)"));
    f.push_back (number ("amp.bassBoostMaxDb", "Bass boost max", "dB", "Setup advice", "", false, 1, 24));
}

void addDriverOptional (std::vector<SpecField>& f, bool isSub)
{
    if (isSub)
        f.push_back (number ("driver.fsHz", "Fs", "Hz", "Enclosure and subsonic filter", "", false, 5, 300));
    f.push_back (number ("driver.xmaxMm", "Xmax", "mm", "Excursion-safe high-pass", "One-way linear excursion", false, 0.05, 60));
    f.push_back (number ("driver.sdCm2", "Sd", "cm\xc2\xb2", "Excursion-safe high-pass, output estimate", "Cone area", false, 1, 5000));
    f.push_back (number ("driver.qts", "Qts", "", "Enclosure / low-frequency roll-off", "", false, 0.1, 3));
    f.push_back (number ("driver.qes", "Qes", "", "Enclosure modelling", "", false, 0.1, 5));
    f.push_back (number ("driver.qms", "Qms", "", "Enclosure modelling", "", false, 0.3, 50));
    f.push_back (number ("driver.vasLitres", "Vas", "L", "Enclosure modelling", "", false, 0.01, 1000));
    f.push_back (number ("driver.reOhms", "Re", FA_OHM, "Power / excursion modelling", "DC resistance", false, 0.1, 20));
    f.push_back (number ("driver.bl", "BL", "T\xc2\xb7m", "Excursion modelling", "", false, 0.5, 50));
    f.push_back (number ("driver.mmsGrams", "Mms", "g", "Excursion modelling", "", false, 0.1, 1000));
    f.push_back (number ("driver.leMh", "Le", "mH", "Impedance modelling", "", false, 0.001, 20));
    f.push_back (text ("driver.tsWiring", "T/S measured with coils", "Modelling (which coil wiring Re/Le/BL refer to)",
                       "Re is always ONE coil", false, { "single", "series", "parallel" }));
    f.push_back (number ("driver.sensitivityDb", "Sensitivity", "dB", "Level matching between ways", "", false, 70, 110));
    f.push_back (text ("driver.sensitivityRef", "Sensitivity reference", "Level matching", "", false, { "1W/1m", "2.83V/1m" }));
    f.push_back (number ("driver.secondarySensitivityDb", "Second sensitivity", "dB", "Level matching",
                         "Other published figure, e.g. at 2.83 V", false, 70, 110));
    f.push_back (text ("driver.secondarySensitivityRef", "Second sensitivity reference", "Level matching", "", false,
                       { "1W/1m", "2.83V/1m" }));
    f.push_back (number ("driver.powerPeakW", "Peak power", "W", "Info", "", false, 1, 20000));
    f.push_back (number ("driver.freqLowHz", "Frequency range low", "Hz", "Crossover plan", "", false, 5, 20000));
    f.push_back (number ("driver.freqHighHz", "Frequency range high", "Hz", "Crossover plan", "", false, 20, 60000));
    f.push_back (number ("driver.recommendedHpHz", "Recommended high-pass", "Hz", "Crossover plan",
                         "Manufacturer recommendation, if published", false, 5, 20000));
    f.push_back (number ("driver.recommendedLpHz", "Recommended low-pass", "Hz", "Crossover plan", "", false, 20, 40000));
    f.push_back (number ("driver.mountingDepthMm", "Mounting depth", "mm", "Install fit", "", false, 5, 500));
    f.push_back (number ("driver.cutoutMm", "Cut-out diameter", "mm", "Install fit", "Side length for square cut-outs", false, 10, 600));
    f.push_back (text ("driver.cutoutShape", "Cut-out shape", "Install fit", "", false, { "round", "square", "oval" }));
    f.push_back (number ("driver.cutoutWidthMm", "Oval cut-out width", "mm", "Install fit", "Only for oval cut-outs", false, 10, 600));
    f.push_back (number ("driver.cutoutHeightMm", "Oval cut-out height", "mm", "Install fit", "Only for oval cut-outs", false, 10, 600));
    f.push_back (text ("driver.switchableImpedance", "Switchable impedance", "Amp load / wiring",
                       "e.g. \"2 or 4 " FA_OHM " (SSI)\"", false));
}

void addDriver (std::vector<SpecField>& f)
{
    f.push_back (number ("driver.sizeInches", "Size", "in", "Install fit, crossover plan", "e.g. 6.5", true, 0.5, 24));
    f.push_back (number ("driver.nominalOhms", "Nominal impedance", FA_OHM, "Amp load / wiring", "e.g. 4", true, 0.5, 32));
    f.push_back (number ("driver.powerRmsW", "RMS power handling", "W", "Amp matching", "", true, 1, 5000));
    f.push_back (number ("driver.fsHz", "Fs", "Hz", "Safe high-pass frequency", "Free-air resonance", true, 10, 5000));
    addDriverOptional (f, false);
}

void addSubwoofer (std::vector<SpecField>& f)
{
    f.push_back (number ("driver.sizeInches", "Size", "in", "Enclosure, install fit", "e.g. 10, 12", true, 4, 24));
    f.push_back (number ("driver.voiceCoils", "Voice coils", "", "Wiring options (SVC = 1, DVC = 2)", "", true, 1, 4));
    f.push_back (number ("driver.coilOhms", "Impedance per coil", FA_OHM, "Wiring / final load", "e.g. 2 or 4", true, 0.5, 16));
    f.push_back (number ("driver.powerRmsW", "RMS power handling", "W", "Amp matching", "", true, 1, 10000));
    f.push_back (number ("driver.nominalOhms", "Nominal impedance", FA_OHM, "Wiring", "", false, 0.5, 32));
    addDriverOptional (f, true);
}

void addWire (std::vector<SpecField>& f, bool required)
{
    f.push_back (number ("wire.awg", "Gauge (AWG)", "AWG", "Voltage drop, fuse sizing",
                         "0 = 1/0, -1 = 2/0. Give AWG or mm\xc2\xb2", required, -3, 30, required ? "size" : ""));
    f.push_back (number ("wire.mm2", "Cross-section", "mm\xc2\xb2", "Voltage drop, fuse sizing",
                         "Use when AWG is unknown", required, 0.1, 150, required ? "size" : ""));
    f.push_back (text ("wire.material", "Material", "Resistance (CCA is ~60 % worse)", "", required,
                       { "OFC", "CCA", "Tinned OFC" }));
    f.push_back (number ("wire.ohmsPerMetre", "Resistance", FA_OHM "/m", "Voltage drop",
                         "If published; otherwise computed from size and material", false, 0.00001, 10));
    f.push_back (number ("wire.ampacityA", "Ampacity", "A", "Fuse sizing", "", false, 1, 1000));
}

void addInterface (std::vector<SpecField>& f)
{
    f.push_back (number ("audioInterface.inputs", "Inputs", "ch", "Mic / loopback routing", "", true, 1, 64));
    f.push_back (number ("audioInterface.outputs", "Outputs", "ch", "Signal output routing", "", true, 1, 64));
    f.push_back (number ("audioInterface.micPreamps", "Mic preamps", "", "Mic connection", "", false, 0, 64));
    f.push_back (flag ("audioInterface.phantomPower", "Phantom power", "XLR measurement mics"));
    f.push_back (flag ("audioInterface.hasLoopback", "Loopback", "Timing reference"));
    f.push_back (flag ("audioInterface.asio", "ASIO driver", "Low latency"));
    f.push_back (number ("audioInterface.maxGainDb", "Max preamp gain", "dB", "Mic level setup", "", false, 0, 100));
    f.push_back (number ("audioInterface.maxOutputDbu", "Max output", "dBu", "Output level setup", "", false, -20, 30));
    f.push_back (text ("audioInterface.connection", "Connection", "Info", "", false, { "USB-C", "USB", "Thunderbolt" }));
}

void addMic (std::vector<SpecField>& f)
{
    f.push_back (text ("mic.type", "Mic type", "Connection setup", "", true, { "USB", "XLR", "TRRS" }));
    f.push_back (flag ("mic.needsPhantom", "Needs phantom power", "Interface setup"));
    f.push_back (number ("mic.sensitivityMvPa", "Sensitivity", "mV/Pa", "Absolute SPL", "", false, 0.1, 200));
    f.push_back (number ("mic.sensitivityDbfs", "Sensitivity (digital)", "dBFS", "Absolute SPL",
                         "USB mics: level at 94 dB SPL, e.g. -18", false, -80, -0.01));
    f.push_back (flag ("mic.calFileAvailable", "Calibration file available", "Measurement accuracy"));
    f.push_back (text ("mic.calFileUrl", "Calibration file URL", "Measurement accuracy", "", false));
    f.push_back (number ("mic.maxSplDb", "Max SPL", "dB", "Measurement level limit", "", false, 80, 180));
}

void addFuse (std::vector<SpecField>& f, bool required)
{
    f.push_back (number ("fuse.ratingA", "Fuse rating", "A", "Fuse sizing", "", required, 1, 2000));
    f.push_back (text ("fuse.type", "Fuse type", "Fuse sizing / holder match", "", false,
                       { "ANL", "mini-ANL", "MIDI", "MAXI", "AGU", "ATC", "breaker" }));
}

void addBattery (std::vector<SpecField>& f)
{
    f.push_back (number ("battery.ah", "Capacity", "Ah", "Reserve / play time with engine off", "20 h rate", false, 1, 2000));
    f.push_back (number ("battery.cca", "Cold cranking amps", "A", "Info", "", false, 50, 10000));
    f.push_back (number ("battery.ca", "Cranking amps", "A", "Info", "", false, 50, 10000));
    f.push_back (number ("battery.reserveMin", "Reserve capacity", "min", "Reserve / play time", "", false, 1, 2000));
    f.push_back (number ("battery.maxAmps", "Max current", "A", "Current budget", "", false, 10, 20000));
    f.push_back (text ("battery.chemistry", "Chemistry", "Charging voltage advice", "", false,
                       { "AGM", "flooded", "LiFePO4", "LTO" }));
}

void addAlternator (std::vector<SpecField>& f)
{
    f.push_back (number ("alternator.ratedA", "Rated output", "A", "Current budget", "", false, 10, 1000));
    f.push_back (number ("alternator.idleA", "Output at idle", "A", "Current budget at idle", "", false, 5, 1000));
    f.push_back (number ("alternator.hotRatedA", "Hot-rated output", "A", "Current budget", "", false, 10, 1000));
}

#undef FA_OHM

/** Splits "<block>.<field>.<rest>" into at most 3 parts; the last part keeps any further dots (map keys like "0.5ohm"). */
juce::StringArray splitKey (const juce::String& key)
{
    juce::StringArray path;
    auto rest = key.trim();
    while (rest.isNotEmpty() && path.size() < 2 && rest.containsChar ('.'))
    {
        const auto part = rest.upToFirstOccurrenceOf (".", false, false);
        if (part.isNotEmpty())
            path.add (part);
        rest = rest.fromFirstOccurrenceOf (".", false, false);
    }
    if (rest.isNotEmpty())
        path.add (rest);
    return path;
}

bool isPresent (const Product& p, const juce::String& key)
{
    return ! getSpecValue (p, key).isVoid();
}
} // namespace

std::vector<SpecField> specFields (Category c)
{
    std::vector<SpecField> f;

    switch (c)
    {
        case Category::headUnit:        addHeadUnit (f); break;
        case Category::dsp:             addDsp (f, true); break;
        case Category::dspAmp:          addAmp (f); addDsp (f, true); break;
        case Category::dac:             addDac (f); break;
        case Category::ampMulti:
        case Category::ampMono:         addAmp (f); break;
        case Category::driverTweeter:
        case Category::driverMidrange:
        case Category::driverMidbass:
        case Category::driverCoax:
        case Category::componentSet:    addDriver (f); break;
        case Category::subwoofer:       addSubwoofer (f); break;
        case Category::powerWire:       addWire (f, true); addFuse (f, false); break;   // kits may include a fuse
        case Category::speakerWire:     addWire (f, true); break;
        case Category::rca:             addWire (f, false); break;
        case Category::fuseHolder:      addFuse (f, false); break;
        case Category::audioInterface:  addInterface (f); break;
        case Category::measurementMic:  addMic (f); break;
        case Category::battery:         addBattery (f); break;
        case Category::alternator:      addAlternator (f); break;
        default: break;
    }

    // safety net for combined categories (dspAmp): never list the same key twice
    std::vector<SpecField> unique;
    for (auto& field : f)
    {
        bool dup = false;
        for (auto& u : unique)
            dup = dup || u.key == field.key;
        if (! dup)
            unique.push_back (std::move (field));
    }
    return unique;
}

juce::StringArray missingRequiredKeys (const Product& p)
{
    juce::StringArray out;
    if (p.brand.trim().isEmpty()) out.add ("brand");
    if (p.model.trim().isEmpty()) out.add ("model");

    const auto fields = specFields (p.category);
    for (auto& f : fields)
    {
        if (! f.required)
            continue;

        if (f.requiredGroup.isEmpty())
        {
            if (! isPresent (p, f.key))
                out.add (f.key);
            continue;
        }

        bool any = false;
        for (auto& g : fields)
            if (g.requiredGroup == f.requiredGroup && isPresent (p, g.key))
                any = true;

        if (! any)
            out.addIfNotAlreadyThere (f.key);
    }
    return out;
}

juce::StringArray missingRequired (const Product& p)
{
    juce::StringArray out;
    if (p.brand.trim().isEmpty()) out.add ("Brand");
    if (p.model.trim().isEmpty()) out.add ("Model");

    const auto fields = specFields (p.category);
    const auto keys = missingRequiredKeys (p);
    juce::StringArray groupsDone;

    for (auto& f : fields)
    {
        if (! keys.contains (f.key))
            continue;

        if (f.requiredGroup.isEmpty())
        {
            out.add (f.label);
        }
        else if (! groupsDone.contains (f.requiredGroup))
        {
            groupsDone.add (f.requiredGroup);
            juce::StringArray names;
            for (auto& g : fields)
                if (g.requiredGroup == f.requiredGroup)
                    names.add (g.label);
            out.add ("One of: " + names.joinIntoString (" / "));
        }
    }
    return out;
}

juce::var getSpecValue (const Product& p, const juce::String& key)
{
    if (key == "wire.awg")
        return p.wire.has_value() ? juce::var (p.wire->awg) : juce::var();

    const auto path = splitKey (key);
    if (path.isEmpty())
        return {};

    juce::var v = p.toVar();
    for (auto& part : path)
    {
        if (! v.isObject() || ! v.hasProperty (juce::Identifier (part)))
            return {};
        v = v.getProperty (juce::Identifier (part), {});
    }

    if (v.isString() && v.toString().isEmpty())
        return {};
    if (v.isBool() && ! (bool) v)
        return {};
    return v;
}

void setSpecValue (Product& p, const juce::String& key, const juce::var& value)
{
    const auto path = splitKey (key);
    if (path.isEmpty())
        return;

    juce::var root = p.toVar();
    juce::var node = root;

    for (int i = 0; i < path.size() - 1; ++i)
    {
        const juce::Identifier id (path[i]);
        auto child = node.getProperty (id, {});
        if (! child.isObject())
        {
            if (value.isVoid() || (value.isString() && value.toString().trim().isEmpty()))
                return;   // nothing to remove

            child = juce::var (new juce::DynamicObject());
            node.getDynamicObject()->setProperty (id, child);
        }
        node = child;
    }

    auto* obj = node.getDynamicObject();
    const juce::Identifier leaf (path[path.size() - 1]);

    if (value.isVoid() || (value.isString() && value.toString().trim().isEmpty()))
        obj->removeProperty (leaf);
    else
        obj->setProperty (leaf, value);

    const bool user = p.userDefined;
    p = Product::fromVar (root);
    p.userDefined = user;
}
} // namespace fa::gear
