#pragma once

#include "DspCommon.h"
#include "CapabilitiesEditor.h"
#include <optional>

namespace fa::ui::dsp
{
/** Side-panel wizard that builds a DspProfile by watching the user click controls inside the
    embedded DSP software (TeachSession). Stages: setup (layout choices) -> one capture step per
    control (per channel / per band where needed) -> capabilities -> name & save. */
class TeachWizard : public juce::Component
{
public:
    TeachWizard (Session& s, DspWindowHost& host);
    ~TeachWizard() override;

    /** Starts the wizard for the currently attached window. `existing` pre-fills bindings and
        capabilities (re-teach); pass a default profile for a new one. */
    void start (const DspProfile& existing = {}, const juce::File& existingFile = {});
    /** Cancels capture, stops the hover poller. Safe to call any time. */
    void stop();
    bool isActive() const noexcept { return active; }

    /** Call when the hosted window attaches/detaches. */
    void windowChanged();

    std::function<void (const DspProfile&, const juce::File&, bool activateNow)> onFinished;
    std::function<void()> onCancelled;
    std::function<void (const juce::String&, AppShell::ToastKind)> toast;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    enum class Stage { setup, capture, caps, finish };

    struct Step
    {
        const TeachSlot* slot = nullptr;
        int channel = -1, band = -1;
        juce::String key, title;
        bool skipped = false;
    };

    struct Body : public juce::Component
    {
        explicit Body (TeachWizard& o) : owner (o) {}
        void paint (juce::Graphics& g) override { owner.paintBody (g); }
        TeachWizard& owner;
    };

    class HoverPoller;

    // flow
    void setStage (Stage s);
    void buildSteps();
    void enterStep (int index);
    void beginCapture();
    void onCaptured (const ControlBinding& b, const uia::ElementInfo& info);
    void commitStep();
    void applyOptionsTo (ControlBinding& b) const;
    void loadOptionsFrom (const ControlBinding& b);
    void runTest();
    void testFinished (bool ok, const juce::String& error);
    void finish();
    void cancel();
    const Step* currentStep() const { return stepIndex >= 0 && stepIndex < (int) steps.size() ? &steps[(size_t) stepIndex] : nullptr; }
    juce::String channelLabel (int dspOutput) const;
    int countTaught() const;
    void updateButtons();

    // layout / paint
    void layoutBody();
    void paintBody (juce::Graphics& g);
    void paintSetup (juce::Graphics& g);
    void paintCapture (juce::Graphics& g);
    void paintCaps (juce::Graphics& g);
    void paintFinish (juce::Graphics& g);

    Session& session;
    DspWindowHost& host;

    bool active = false;
    Stage stage = Stage::setup;
    DspProfile profile;
    juce::File profileFile;
    std::vector<Step> steps;
    int stepIndex = -1;
    std::unique_ptr<TeachSession> teach;
    std::optional<ControlBinding> captured;
    uia::ElementInfo capturedInfo;
    juce::String hoverText { "Move the mouse over the DSP software..." };
    bool capturing = false;
    bool testing = false;
    juce::String testResult;
    bool testOk = false;

    // chrome
    juce::Viewport viewport;
    Body body { *this };
    FaButton cancelBtn { "Cancel",  FaButton::Style::ghost };
    FaButton backBtn   { "Back",    FaButton::Style::ghost };
    FaButton skipBtn   { "Skip",    FaButton::Style::secondary };
    FaButton retakeBtn { "Retake",  FaButton::Style::secondary, icons::Id::refresh };
    FaButton nextBtn   { "Next",    FaButton::Style::primary, icons::Id::chevronRight };

    // setup
    InfoPanel setupInfo { icons::Id::target };
    SegmentedControl channelLayout { { "Channel selector + shared controls", "Own controls per channel" } };
    SegmentedControl bandLayout    { { "Band selector + shared fields", "Own fields per band" } };
    ValueField setupBands { "PEQ bands per channel", "", 1, 31, 1, 0 };
    ToggleSwitch essentialOnly { "Essential controls only (skip Store/Save)" };

    // capture
    juce::ComboBox methodBox, unitBox, testSelect;
    ValueField decimalsField { "Decimals", "", 0, 6, 1, 0 };
    ValueField scaleField    { "Scale", "x", -1000, 1000, 0.01, 3 };
    ValueField offsetField   { "Offset", "+", -100000, 100000, 0.1, 2 };
    ValueField settleField   { "Settle", "ms", 0, 2000, 10, 0 };
    ValueField testValue     { "Test value", "", -100000, 100000, 0.1, 2 };
    ToggleSwitch invertToggle { "Invert (checkbox is ON for normal)" };
    ToggleSwitch testToggle   { "Test state ON" };
    FaButton testBtn { "Test", FaButton::Style::secondary, icons::Id::play };
    juce::OwnedArray<juce::TextEditor> mapEditors;
    juce::Rectangle<int> promptRect, hoverRect, capturedRect, optionsRect, testRect, mapRect;

    // caps / finish
    CapabilitiesEditor capsEditor;
    juce::TextEditor nameEditor;
    ToggleSwitch activateNow { "Activate this profile now (asks for permission)" };

    std::unique_ptr<HoverPoller> poller;    // declared last: destroyed first

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TeachWizard)
};
} // namespace fa::ui::dsp
