#pragma once

#include <juce_core/juce_core.h>

namespace fa
{
/** Windows capture-endpoint (mic level slider) volume, via IAudioEndpointVolume. */
struct EndpointVolume
{
    bool   found = false;
    bool   valid = false;        // dB / scalar were read
    juce::String deviceName;     // friendly name of the matched endpoint
    double levelDb = 0.0;        // GetMasterVolumeLevel
    double scalar = 0.0;         // GetMasterVolumeLevelScalar, 0..1
    double minDb = 0.0, maxDb = 0.0, stepDb = 0.0;
    bool   muted = false;
    juce::String error;
};

/** Reads the volume of the first active capture endpoint whose friendly name contains `nameContains`
    (case-insensitive, e.g. "umik"). Read-only. */
EndpointVolume readCaptureEndpointVolume (const juce::String& nameContains);

/** Sets the volume as a 0..1 scalar. Only call this after the user has explicitly agreed. */
bool setCaptureEndpointVolumeScalar (const juce::String& nameContains, double scalar, juce::String& error);

/** Sets the endpoint level in dB (clamped to the device range). Only after explicit user consent. */
bool setCaptureEndpointVolumeDb (const juce::String& nameContains, double targetDb, juce::String& error);
} // namespace fa
