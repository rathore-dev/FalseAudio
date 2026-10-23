#pragma once

#include "SystemConfig.h"
#include <map>

namespace fa
{
/** Built-in and user target curves.
    Built-ins come from the HouseCurves collection (Audiofrog, Harman, JBL, JL Audio, RAW-Cat,
    Whitledge, Half Whitledge, Flat) embedded in the app, plus the parametric presets. Each
    house curve may carry per-band sub-targets (sub / midbass / midrange / tweeter sums). */
struct TargetLibrary
{
    struct Entry
    {
        TargetCurve curve;                       // full-range target (normalised 0 dB at 1 kHz)
        std::map<DriverRole, TargetCurve> bands; // optional per-driver-band targets
        juce::String family;                     // "Audiofrog", "Harman", ...
        juce::String description;
        bool builtIn = true;
    };

    static const std::vector<Entry>& builtIn();

    /** Curves imported by the user (stored in the app data folder). */
    static std::vector<Entry> userCurves();

    /** Parses REW / CSV / txt (freq dB) into a target and copies it into the user folder. */
    static bool import (const juce::File& file, Entry& out, juce::String& error);

    static const Entry* findByName (const juce::String& name);
};
} // namespace fa
