#pragma once

#include "SystemConfig.h"
#include <map>
#include <optional>

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

        // Additions (W7) ------------------------------------------------------
        std::map<juce::String, TargetCurve> extraBands; // bands without a DriverRole, e.g. "Rear sub"
        juce::File file;                                // user curves: the stored copy

        juce::String name() const { return curve.name; }
        bool hasBands() const     { return ! bands.empty(); }

        /** Band target for a role, or nullptr. Band levels share the full curve's 0 dB @ 1 kHz
            reference (they are NOT individually normalised), so they show each band's intended
            contribution to the sum. */
        const TargetCurve* band (DriverRole role) const;
    };

    static const std::vector<Entry>& builtIn();

    /** Curves imported by the user (stored in the app data folder). */
    static std::vector<Entry> userCurves();

    /** Parses REW / CSV / txt (freq dB) into a target and copies it into the user folder. */
    static bool import (const juce::File& file, Entry& out, juce::String& error);

    static const Entry* findByName (const juce::String& name);

    // Additions (W7) ----------------------------------------------------------
    /** Folder holding imported curves: <appdata>/FalseAudio/Targets (created on demand). */
    static juce::File userFolder();

    /** Built-ins first, then user curves; returns a copy (user entries are not cached). */
    static std::optional<Entry> find (const juce::String& name);

    /** builtIn() followed by userCurves(). */
    static std::vector<Entry> all();

    /** Deletes an imported curve from the user folder. */
    static bool removeUserCurve (const juce::String& name);
};
} // namespace fa
