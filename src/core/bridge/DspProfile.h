#pragma once

#include "UIAutomation.h"
#include "../tune/SystemConfig.h"
#include <map>

namespace fa
{
/** How to change one parameter in a vendor DSP application. */
struct ControlBinding
{
    enum class Method
    {
        uiaText,       // ValuePattern / type into an edit box
        uiaRange,      // RangeValuePattern (sliders, spinners)
        uiaToggle,     // TogglePattern (checkbox / toggle button)
        uiaInvoke,     // press a button / tab / list item
        uiaSelect,     // pick an item in a combo/list by its text
        clickPoint,    // click a fixed point in the window (custom-drawn UI)
        clickAndType   // double-click a fixed point, type the value, Enter
    };

    Method method = Method::uiaText;
    uia::ElementPath path;
    double relX = 0.5, relY = 0.5;           // position inside the window for click methods
    juce::String unit;                       // delay: ms | cm | m | in | samples ; empty = native
    int    decimals = 2;
    double scale = 1.0, offset = 0.0;        // value' = value * scale + offset (after unit conversion)
    bool   invertToggle = false;             // e.g. a "Phase 0" checkbox that is ON for normal
    std::map<juce::String, juce::String> valueMap; // e.g. "LR24" -> "Link-Riley 24dB"
    int    settleMs = 60;                    // wait after acting (UI refresh)
    juce::String capturedDescription;        // what the user clicked, for display

    juce::var toVar() const;
    static ControlBinding fromVar (const juce::var& v);
};

/** A reusable "driver" for one vendor's DSP software, built with the Teach wizard. */
struct DspProfile
{
    juce::String name = "New DSP profile";
    juce::String processName;                // e.g. "DSP PC-Tool.exe"
    juce::String windowTitleContains;
    DspCapabilities caps;
    std::map<juce::String, ControlBinding> bindings;

    // Additive (AI operator): profiles written by the AI keep what it learned about value formats.
    bool aiLearned = false;
    juce::String learnedAt;                  // ISO time
    juce::String notes;                      // free text: value formats, units, quirks
    /** True only when every output this map drives passed verify_channel (a physical test sound came from the expected
        driver). Legacy / unverified maps load as false and must be re-verified before a tune is pushed through them. */
    bool verified = false;
    std::vector<int> verifiedOutputs;        // 0-based DSP outputs confirmed by verify_channel
    /** Explicit "select a channel, then edit one shared set of controls" model. Also implied by any channel.select@cN binding. */
    bool selectThenEdit = false;

    /** True when this (AI-learned) profile edits one channel at a time through a channel.select control. */
    bool selectsChannels() const;

    /** Most specific binding for a slot: key@cN@bM, key@cN, key@bM, key.
        AI-learned profiles do NOT fall back to a binding without an output number when a channel is asked for, unless the
        profile declares the "select channel then edit" model (selectsChannels()): a generic control could be editing another output. */
    const ControlBinding* find (const juce::String& key, int channel = -1, int band = -1) const;

    static juce::String keyFor (const juce::String& key, int channel = -1, int band = -1);

    juce::var toVar() const;
    static DspProfile fromVar (const juce::var& v);

    bool save (const juce::File& f) const;
    static bool load (const juce::File& f, DspProfile& out);

    static juce::File profilesFolder();
    static juce::Array<juce::File> listProfiles();
};

/** The parameters a profile can teach, in the order the Teach wizard walks through them. */
struct TeachSlot
{
    enum class Kind { numeric, toggle, select, action };
    enum class Scope { global, perChannel, perBand };

    juce::String key;
    juce::String label;
    juce::String hint;
    Kind kind = Kind::numeric;
    Scope scope = Scope::global;
    bool important = true;
};

const std::vector<TeachSlot>& teachSlots();

/** Canonical text for a crossover type used as valueMap key, e.g. "LR24", "BW12", "BS18". */
juce::String crossoverKey (const FilterSpec& f);
} // namespace fa
