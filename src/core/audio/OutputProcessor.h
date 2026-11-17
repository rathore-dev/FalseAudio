#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace fa
{
/** A DSP that lives inside the app (see SoftwareDspController): the AudioEngine renders every stimulus it plays on a
    DSP output through it, so measurements and playback hear the settings exactly as a hardware DSP would apply them. */
class OutputProcessor
{
public:
    virtual ~OutputProcessor() = default;

    /** Renders `in` (mono, at `sampleRate`) through the chain of DSP output `dspOutput` (0 = the left interface output,
        1 = the right one) into `out`. `out` is `in.size()` plus the delay and a short filter tail. Returns false when
        that output has no settings (the engine then plays the signal unprocessed). Thread-safe; called off the audio thread. */
    virtual bool processOutput (int dspOutput, const std::vector<float>& in, double sampleRate, std::vector<float>& out) const = 0;
};
} // namespace fa
