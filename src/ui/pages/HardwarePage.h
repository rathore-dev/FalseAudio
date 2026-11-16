#pragma once

#include "../Page.h"
#include "../AppShell.h"
#include "../components/Widgets.h"
#include "../components/Plots.h"
#include "system/PageKit.h"

namespace fa::ui
{
class ModeBanner;
class MicCalPanel;
class MicCalAssistant;
class RigGearPanel;
class MicCheckCard;

/** Hardware page: audio device, routing (DSP L/R outputs, up to 4 mics, loopback), per-mic
    calibration, live meters and a pink-noise level check. Works with any interface or mic. */
class HardwarePage : public Page,
                     private juce::Timer
{
public:
    explicit HardwarePage (Session& s);
    ~HardwarePage() override;

    juce::String getTitle() const override    { return "Rig"; }
    juce::String getSubtitle() const override { return "Audio interface, microphones and calibration"; }
    icons::Id getIcon() const override        { return icons::Id::mic; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void pageShown() override;
    void pageHidden() override;
    void sessionChanged() override;

private:
    struct MicRow
    {
        kit::Caption caption;
        juce::ComboBox input;
        FaButton remove { "", FaButton::Style::ghost, icons::Id::trash };
    };
    struct MeterRow
    {
        juce::Label name, value;
        LevelMeter meter;
        Chip clip { "CLIP", theme::bad };
        juce::uint32 clipAt = 0;
    };

    int layoutContent (int width);
    void timerCallback() override;

    void refreshDevices();            // repopulate device combos from the device manager
    void refreshRouting();            // repopulate routing combos from the current routing
    void refreshCalibration();        // rebuild per-mic calibration panels
    void refreshMeters();             // rebuild meter rows
    void refreshBanner();
    void applyDeviceSetup();
    void applyRouting();
    void loadCalibrationFor (int micIndex);
    void startLevelCheck();
    void finishLevelCheck();
    void toast (const juce::String& msg, AppShell::ToastKind kind = AppShell::ToastKind::info);

    kit::ScrollBody body;
    std::unique_ptr<ModeBanner> banner;
    std::unique_ptr<RigGearPanel> rigGear;      // interface + microphone products (chosen only here)
    std::unique_ptr<MicCheckCard> micCheck;     // input-only mic self-test

    // ---- device
    Card deviceCard { "Audio interface", "Any WASAPI / DirectSound device" };
    kit::Caption typeCaption { "Driver" }, outCaption { "Output device" }, inCaption { "Input device" },
                 rateCaption { "Sample rate" }, bufferCaption { "Buffer" };
    juce::ComboBox typeBox, outBox, inBox, rateBox, bufferBox;
    kit::Note deviceStatus;
    FaButton rescanBtn { "Rescan", FaButton::Style::ghost, icons::Id::refresh };

    // ---- routing
    Card routingCard { "Routing", "Interface channels" };
    kit::Caption leftCaption { "DSP left input  <-  interface output" }, rightCaption { "DSP right input  <-  interface output" },
                 micsCaption { "Measurement microphones" }, loopCaption { "Timing reference" };
    juce::ComboBox leftOutBox, rightOutBox;
    std::vector<std::unique_ptr<MicRow>> micRows;
    FaButton addMicBtn { "Add microphone", FaButton::Style::secondary, icons::Id::plus };
    ToggleSwitch loopToggle { "Loopback: one output wired straight back into an input" };
    kit::Caption loopOutCaption { "Loopback output" }, loopInCaption { "Loopback input" };
    juce::ComboBox loopOutBox, loopInBox;
    kit::Note routingHint { "Up to 4 mics measure several seats in one sweep. Loopback removes interface latency "
                            "jitter so absolute delays are exact; without it, delays are still correct relative to each other." };

    // ---- calibration
    Card calCard { "Microphone calibration", "Manufacturer files" };
    std::unique_ptr<MicCalAssistant> calAssistant;
    std::vector<std::unique_ptr<MicCalPanel>> calPanels;
    kit::Note calHint { "UMIK-1: download both files from miniDSP using the serial number. Use the 0-degree file when the mic "
                        "points at the speakers and the 90-degree file when it points at the roof (recommended in cars)." };

    // ---- levels
    Card levelsCard { "Levels", "Live peaks" };
    std::vector<std::unique_ptr<MeterRow>> micMeters;
    MeterRow outMeter;
    FaButton levelCheckBtn { "Level check", FaButton::Style::primary, icons::Id::play };
    kit::Note levelAdvice { "Plays 2.5 s of pink noise at -20 dBFS and reports each microphone's level and signal-to-noise ratio." };

    // ---- level-check state
    enum class CheckPhase { idle, noise, signal };
    CheckPhase phase = CheckPhase::idle;
    int phaseTicks = 0;
    std::vector<float> noisePeak, signalPeak, lastPeaks;
    float lastOutPeak = -120.0f;

    struct DeviceListener : juce::ChangeListener
    {
        std::function<void()> onChange;
        void changeListenerCallback (juce::ChangeBroadcaster*) override { if (onChange) onChange(); }
    } deviceListener;

    std::unique_ptr<juce::FileChooser> chooser;
    bool updating = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HardwarePage)
};
} // namespace fa::ui
