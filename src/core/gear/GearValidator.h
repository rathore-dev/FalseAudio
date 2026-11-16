#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace fa::gear
{
/** Plausibility checks on the raw gear JSON (data/gear/<category>/<brand>.json).
    error   = impossible value (0 / negative ohms, Fs, Qts, RMS ...): the data is wrong, fix or remove the field.
    warning = implausible or unusual but possibly real (Qts > 1.5, RMS > 5000 W, sensitivity outside 70-110 dB,
              peak below RMS, missing impedance ...): listed in the report, never fails the build. */
struct GearIssue
{
    enum class Level { warning, error };
    Level level = Level::warning;
    juce::String file, productId, field, message;
};

/** Checks one product object (the entry of "products"). `file` is only used to label issues. */
void validateGearProduct (const juce::var& product, const juce::String& file, std::vector<GearIssue>& out);

/** Checks every data/gear/<category>/*.json under `gearRoot`. Returns the number of products checked. */
int validateGearFolder (const juce::File& gearRoot, std::vector<GearIssue>& out);

juce::String formatGearReport (const std::vector<GearIssue>& issues, int productsChecked);
} // namespace fa::gear
