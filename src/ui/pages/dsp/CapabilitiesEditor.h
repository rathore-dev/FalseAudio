#pragma once

#include "DspCommon.h"

namespace fa::ui::dsp
{
/** Editor for DspCapabilities: PEQ bands, Q and gain ranges, delay, sample rate, slopes and
    alignment availability. Natural height from idealHeight(). */
class CapabilitiesEditor : public juce::Component
{
public:
    CapabilitiesEditor();

    void setCapabilities (const DspCapabilities& c);
    DspCapabilities getCapabilities() const;
    std::function<void()> onChange;

    int idealHeight() const;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void notify() { if (onChange) onChange(); }

    ValueField bands     { "PEQ bands / ch", "",   1, 31, 1, 0 };
    ValueField minQ      { "Min Q",          "",   0.1, 5.0, 0.1, 2 };
    ValueField maxQ      { "Max Q",          "",   1.0, 50.0, 0.5, 1 };
    ValueField minGain   { "PEQ min gain",   "dB", -30, 0, 0.5, 1 };
    ValueField maxGain   { "PEQ max gain",   "dB", 0, 20, 0.5, 1 };
    ValueField maxDelay  { "Max delay",      "ms", 1, 100, 0.5, 1 };
    ValueField delayStep { "Delay step",     "ms", 0.001, 1.0, 0.001, 3 };
    ValueField gainStep  { "Gain step",      "dB", 0.01, 1.0, 0.01, 2 };
    ValueField minChGain { "Channel gain min", "dB", -90, 0, 1, 0 };
    ValueField maxChGain { "Channel gain max", "dB", 0, 24, 1, 0 };
    juce::ComboBox sampleRate;
    juce::OwnedArray<ToggleSwitch> slopeToggles;
    ToggleSwitch hasLR     { "Linkwitz-Riley" };
    ToggleSwitch hasBessel { "Bessel" };
    ToggleSwitch hasAllPass { "All-pass" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CapabilitiesEditor)
};
} // namespace fa::ui::dsp
