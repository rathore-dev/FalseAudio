#pragma once

#include "../Page.h"
#include "../AppShell.h"
#include "../components/Widgets.h"
#include "../components/Plots.h"
#include "system/PageKit.h"
#include "system/ChannelCard.h"
#include "system/TargetTile.h"

namespace fa::ui
{
/** System page: speaker layout wizard, car view, per-channel driver details, target curve
    library with parametric fine-tuning, safety limits and DSP capabilities. */
class SystemPage : public Page
{
public:
    explicit SystemPage (Session& s);
    ~SystemPage() override;

    juce::String getTitle() const override    { return "System"; }
    juce::String getSubtitle() const override { return "Speakers, layout and target curve"; }
    icons::Id getIcon() const override        { return icons::Id::car; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void sessionChanged() override;
    void pageShown() override;

private:
    int layoutContent (int width);
    void refreshFromSession();
    void rebuildChannelCards();
    void rebuildTargetTiles();
    void updateTargetPlot();
    void selectChannel (const juce::String& id);
    void applyLayout();
    void applyPreset (SystemConfig::Preset p);
    void editTarget (std::function<void (TargetCurve&)> edit);
    void editConfig (std::function<void (SystemConfig&)> edit);
    void importTarget();
    void toast (const juce::String& msg, AppShell::ToastKind kind = AppShell::ToastKind::info);
    static int waysOf (const SystemConfig& cfg);
    static int subModeOf (const SystemConfig& cfg);

    kit::ScrollBody body;

    // ---- layout wizard
    Card layoutCard { "Layout", "Front stage" };
    kit::Caption presetCaption { "Quick preset" };
    juce::ComboBox presetBox;
    kit::Caption waysCaption { "Ways per side" }, subCaption { "Subwoofer" }, seatCaption { "Listening seat" },
                 steerCaption { "Steering wheel" };
    SegmentedControl waysSeg { { "2-way", "3-way", "4-way" } };
    SegmentedControl subSeg { { "None", "Mono", "Stereo" } };
    SegmentedControl seatSeg { { "Driver", "Passenger", "Centre" } };
    SegmentedControl steerSeg { { "Left-hand drive", "Right-hand drive" } };
    kit::Note layoutSummary;
    kit::Note layoutHint { "Changing the layout rebuilds the channel list with fresh defaults. Driver details, "
                           "crossovers and EQ for the old layout are discarded." };

    // ---- car + channels
    Card carCard { "Your car", "Click a speaker" };
    CarView car;
    Card channelsCard { "Channels", "" };
    juce::Viewport channelViewport;
    juce::Component channelList;
    std::vector<std::unique_ptr<ChannelCard>> channelCards;
    juce::String selectedId;

    // ---- target curve
    Card targetCard { "Target curve", "House curve the tune aims for" };
    std::vector<std::unique_ptr<TargetTile>> tiles;
    FrequencyPlot targetPlot;
    kit::Caption tuneCaption { "Fine-tune (parametric curves)" };
    ValueField bassBoost  { "Bass boost", "dB", 0, 15, 0.5, 1 };
    ValueField transLow   { "Boost below", "Hz", 30, 150, 1, 0 };
    ValueField transHigh  { "Flat from", "Hz", 80, 400, 1, 0 };
    ValueField trebleTilt { "Treble at 20k", "dB", -12, 3, 0.5, 1 };
    ValueField tiltStart  { "Tilt from", "Hz", 300, 5000, 10, 0 };
    kit::Note tuneHint;
    Chip targetChip { "", theme::accent };

    // ---- safety
    Card safetyCard { "Safety limits", "Obeyed by every step" };
    ValueField maxBoost   { "Max EQ boost", "dB", 0, 12, 0.5, 1 };
    ValueField lfBoost    { "Max LF boost", "dB", 0, 12, 0.5, 1 };
    ValueField sweepLevel { "Sweep level", "dBFS", -40, -6, 1, 0 };
    ValueField tweeterHp  { "Tweeter HP min", "x Fs", 1, 4, 0.1, 1 };
    ValueField minTwHp    { "Min tweeter HP", "Hz", 800, 6000, 50, 0 };
    ValueField minMidHp   { "Min midrange HP", "Hz", 80, 1000, 10, 0 };
    ValueField minMbHp    { "Min midbass HP", "Hz", 20, 200, 5, 0 };
    kit::Note safetyHint { "Boost limits protect small drivers and cheap amplifiers. The tweeter high-pass is never "
                           "placed below Fs x factor, or below the minimum when Fs is unknown." };

    // ---- DSP capabilities
    Card capsCard { "DSP capabilities", "What your DSP can do" };
    ValueField peqBands  { "PEQ bands / ch", "", 1, 31, 1, 0 };
    ValueField peqMinQ   { "Min Q", "", 0.1, 2, 0.1, 1 };
    ValueField peqMaxQ   { "Max Q", "", 2, 40, 0.5, 1 };
    ValueField peqMaxG   { "PEQ max gain", "dB", 0, 20, 0.5, 1 };
    ValueField peqMinG   { "PEQ min gain", "dB", -40, 0, 0.5, 1 };
    ValueField maxDelay  { "Max delay", "ms", 1, 100, 0.5, 1 };
    ValueField delayStep { "Delay step", "ms", 0.001, 1, 0.001, 3 };
    ValueField gainStep  { "Gain step", "dB", 0.01, 1, 0.01, 2 };
    kit::Caption slopesCaption { "Crossover slopes (dB/oct)" };
    std::vector<std::unique_ptr<Chip>> slopeChips;
    ToggleSwitch lrToggle { "Linkwitz-Riley" }, besselToggle { "Bessel" }, allpassToggle { "All-pass" };

    std::unique_ptr<juce::FileChooser> chooser;
    bool updating = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SystemPage)
};
} // namespace fa::ui
