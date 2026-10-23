#pragma once

#include "../measure/Measurement.h"
#include "../dsp/Generators.h"

namespace fa
{
/** Which DSP input(s) receive the stimulus. The DSP decides which speakers play (via mutes). */
enum class Feed { left, right, both };

struct SweepRequest
{
    LogSweep sweep;
    Feed feed = Feed::both;
    juce::String label;          // stored as Measurement::channelId
};

/** Anything that can run a sweep and return impulse responses: real hardware or the virtual car.
    measureSweep() blocks and must be called from a background thread. It returns one
    Measurement per measurement microphone (several mics = several positions in one sweep). */
class MeasurementSource
{
public:
    virtual ~MeasurementSource() = default;

    virtual bool isReady (juce::String& whyNot) const = 0;
    virtual int  numMicrophones() const = 0;
    virtual double getSampleRate() const = 0;

    virtual std::vector<Measurement> measureSweep (const SweepRequest& request,
                                                   std::atomic<bool>& cancelFlag,
                                                   juce::String& error) = 0;
};
} // namespace fa
