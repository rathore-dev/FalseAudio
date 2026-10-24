#pragma once

#include "TuneCommon.h"

namespace fa::ui
{
/** Auto-tune options bound directly to AutoTuneEngine::Options (mic positions, sweep length and
    level, right-to-left matching, crossover refinement, verify passes). */
class TuneOptionsPanel : public juce::Component
{
public:
    explicit TuneOptionsPanel (AutoTuneEngine::Options& options);

    /** Re-reads the bound options into the controls. */
    void refresh();
    /** Enables the band-target toggle only when the named target is a house curve with band levels. */
    void setTargetName (const juce::String& targetName);
    void setEnabledState (bool enabled);

    static int idealHeight (int width);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    AutoTuneEngine::Options& options;

    ValueField micPositions   { "Mic positions", "", 1, 9, 1, 0 };
    ValueField sweepSeconds   { "Sweep length", "s", 1.0, 10.0, 0.5, 1 };
    ValueField sweepLevel     { "Sweep level", "dBFS", -40.0, -6.0, 1.0, 0 };
    ValueField verifyPasses   { "Verify passes", "", 0, 4, 1, 0 };
    ValueField refineIters    { "Refine iterations", "", 50, 800, 50, 0 };
    ToggleSwitch matchRight   { "Match right channel to left" };
    ToggleSwitch competition  { "Competition refine: joint perceptual optimisation after solving" };
    ToggleSwitch refineXo     { "Refine crossovers from measurements" };
    ToggleSwitch bandTargets  { "Level-match each driver band to the house curve's band targets" };
    bool bandTargetsAvailable = false;

    juce::String hintFor (int column) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TuneOptionsPanel)
};
} // namespace fa::ui
