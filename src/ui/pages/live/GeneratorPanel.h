#pragma once

#include "../../Page.h"
#include "../../components/Widgets.h"
#include "core/audio/AudioEngine.h"

namespace fa::ui
{
/** Signal generator controls: signal type, level, sine frequency, feed, output meter and a
    big safety-first Stop button. Emits onChange with the complete GeneratorSettings. */
class GeneratorPanel : public juce::Component
{
public:
    GeneratorPanel();

    void setSettings (const GeneratorSettings& s);
    GeneratorSettings getSettings() const;
    void setOutputPeak (float dbfs);
    void setHardwareAvailable (bool available);

    std::function<void (const GeneratorSettings&)> onChange;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    SegmentedControl signal { { "Off", "Pink", "Pink R", "White", "Sine" } };
    ValueField level { "Level", "dBFS", -60.0, 0.0, 0.5, 1 };
    ValueField sineHz { "Frequency", "Hz", 10.0, 22000.0, 1.0, 0 };
    SegmentedControl feed { { "L", "R", "L+R" } };
    LevelMeter outputMeter;
    FaButton stopButton { "Stop output", FaButton::Style::danger, icons::Id::stop };

    bool hardware = true;
    float outPeak = -90.0f;

    void emit();
    void updateEnablement();
};
} // namespace fa::ui
