#pragma once

#include "../gear/Gear.h"
#include "../tune/SystemConfig.h"
#include "../audio/AudioEngine.h"

namespace fa::guide
{
/** A single guided step with an optional automatic check. */
struct Step
{
    juce::String id, title, detail;
    juce::StringArray bullets;
    enum class Status { pending, ok, warning, failed, skipped } status = Status::pending;
    juce::String result;           // outcome of the automatic check
};

/** Microphone placement for the selected seat and number of positions (head-centred pattern,
    ear height, orientation for 0/90 degree cal files), as both text and 3D-ish coordinates for drawing. */
struct MicPlacement
{
    struct Position { juce::String label; double xCm = 0, yCm = 0, zCm = 0; };  // relative to head centre: x right, y forward, z up
    std::vector<Position> positions;
    juce::String orientation;      // "Pointing at the roof (use 90° cal file)" ...
    juce::StringArray instructions;
};

MicPlacement micPlacement (const SystemConfig& config, int positions, const gear::Product* mic);

/** Interface setup: sample rate, buffer, phantom power, input gain, output level, loopback cable,
    driver type (WASAPI/ASIO) — specific to the selected interface + mic when known. */
std::vector<Step> interfaceSetup (const gear::Product* audioInterface, const gear::Product* mic, const AudioRouting& routing);

/** Live gain-staging check: plays pink noise at the sweep level and measures mic peak/RMS and
    noise floor; returns pass/fail with the exact knob advice ("raise input gain ~6 dB"). Blocks;
    call from a background thread. */
Step runGainStagingCheck (AudioEngine& audio, int micIndex, double testLevelDbFS, std::atomic<bool>& cancel);

/** Background-noise check (engine off/on, HVAC) with the mic: returns SPL per octave and advice. */
Step runNoiseCheck (AudioEngine& audio, int micIndex, std::atomic<bool>& cancel);
} // namespace fa::guide
