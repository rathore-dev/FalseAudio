#pragma once

#include "../tune/SystemConfig.h"

namespace fa
{
/** Writes a tune in the formats other tools understand. */
namespace exporters
{
    /** Human-readable sheet: every value per output, ready to type into any DSP. */
    juce::String toText (const SystemConfig& cfg);

    /** REW "filter settings" text for one channel (importable in REW's EQ window). */
    juce::String toREW (const OutputChannel& ch);

    /** Equalizer APO config (for PC-based systems), one block per channel. */
    juce::String toEqualizerAPO (const SystemConfig& cfg);

    /** miniDSP-style biquad coefficient list for one channel (crossovers + PEQ). */
    juce::String toMiniDSPBiquads (const OutputChannel& ch, double sampleRate);

    /** Complete preset (FalseAudio JSON). */
    juce::String toJSON (const SystemConfig& cfg);
    bool fromJSON (const juce::String& json, SystemConfig& out);
}
} // namespace fa
