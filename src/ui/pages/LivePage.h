#pragma once

#include "../Page.h"
#include "../AppShell.h"
#include "../components/Widgets.h"
#include "../components/Plots.h"
#include "live/SplMeter.h"
#include "live/GeneratorPanel.h"
#include "live/DemoBanner.h"

namespace fa::ui
{
/** Real-time analyser: RTA (bars/line, peak hold, snapshot) and dual-channel transfer
    function (magnitude with coherence, phase, coherence strip, delay finder), a pro-style
    SPL meter and the signal generator. Polls Session::getAnalyzer() at ~25 fps while shown. */
class LivePage : public Page,
                 private juce::Timer
{
public:
    explicit LivePage (Session& s);
    ~LivePage() override;

    juce::String getTitle() const override    { return "Live"; }
    juce::String getSubtitle() const override { return "Real-time analyzer and transfer function"; }
    icons::Id getIcon() const override        { return icons::Id::activity; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void pageShown() override;
    void pageHidden() override;
    void sessionChanged() override;

private:
    enum class Mode { rta, transfer };

    // ---- top row ----
    SegmentedControl modeSelector { { "RTA", "Transfer function" } };
    Chip micChip { "Mic 1", theme::textDim };      // read-only: the microphone is chosen on the Rig page
    DemoBanner banner { juce::String (juce::CharPointer_UTF8 ("Live analysis needs real hardware \xE2\x80\x94 switch to Live mode in the header.")), "Switch to Live" };

    // ---- RTA ----
    SegmentedControl rtaDisplay { { "Bars", "Line" } };
    SegmentedControl rtaResolution { { "1/3", "1/6", "1/12", "1/24" } };
    SegmentedControl rtaAveraging { { "Slow", "Medium", "Fast" } };
    ToggleSwitch peakHoldToggle { "Peak hold" };
    FaButton captureButton { "Capture", FaButton::Style::secondary, icons::Id::target };
    FaButton clearButton { "Clear", FaButton::Style::ghost, icons::Id::refresh };
    FrequencyPlot rtaPlot;

    // ---- Transfer function ----
    FrequencyPlot tfMagnitude, tfPhase, tfCoherence;
    FaButton findDelayButton { "Find delay", FaButton::Style::secondary, icons::Id::clock };
    FaButton clearDelayButton { "Reset", FaButton::Style::ghost, icons::Id::cross };
    Chip delayChip { "No delay compensation", theme::textDim };

    // ---- right column ----
    SplMeter spl;
    GeneratorPanel generator;

    Mode mode = Mode::rta;
    int micIndex = 0;
    bool liveAvailable = false;
    juce::String unavailableReason;

    // RTA state
    std::vector<double> bandCentres;
    std::vector<double> peakHold;
    std::vector<double> snapshotFreqs, snapshotDb;
    bool haveSnapshot = false;
    double rtaYMax = 0.0, rtaYMin = -100.0;
    bool rtaRangeSet = false;
    int lastBandsPerOctave = 0;

    // TF state
    double delayMs = 0.0;
    bool haveDelay = false;

    juce::Rectangle<int> mainCardArea, controlsRowArea;

    void timerCallback() override;
    void updateAvailability();
    void applyAnalyzerSettings();
    void setMode (Mode m);
    void rebuildBands();
    void tickRta();
    void tickTransfer();
    void pushGenerator();
    void layoutMode();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LivePage)
};
} // namespace fa::ui
