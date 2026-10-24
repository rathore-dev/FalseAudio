#pragma once

#include "../Page.h"
#include "../AppShell.h"
#include "../components/Widgets.h"
#include "dsp/DspCommon.h"
#include "dsp/ProfilePicker.h"
#include "dsp/TeachWizard.h"
#include "dsp/ManualPanel.h"
#include "dsp/ExportPanel.h"

namespace fa::ui
{
/** DSP page: choose how FalseAudio controls the DSP (virtual / manual guided / automatic),
    embed the vendor's DSP software, teach and manage automation profiles, export the tune. */
class DspPage : public Page,
                private juce::Timer
{
public:
    explicit DspPage (Session& s);
    ~DspPage() override;

    juce::String getTitle() const override    { return "DSP"; }
    juce::String getSubtitle() const override { return "Control your DSP software"; }
    icons::Id getIcon() const override        { return icons::Id::chip; }

    void pageShown() override;
    void pageHidden() override;
    void sessionChanged() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    enum UiMode { modeVirtual = 0, modeManual = 1, modeAutomatic = 2 };

    void timerCallback() override;

    // mode
    void userChoseMode (int index);
    void syncModeFromSession();
    void refreshChips();
    void updateSidePanel();

    // embedding
    void showAttachMenu();
    void attachWindow (const DspWindowHost::WindowInfo& w);
    void launchDsp();
    void detachDsp();
    void hostStateChanged();
    void showHost (bool show);

    // automation profiles
    void requestControl (const DspProfile& profile, const juce::File& file);
    void activateProfile (const DspProfile& profile, const juce::File& file);
    void revokeControl();
    void startTeaching (const DspProfile& existing, const juce::File& file);
    void stopTeaching();

    void notify (const juce::String& message, AppShell::ToastKind kind = AppShell::ToastKind::info);
    void confirm (const juce::String& title, const juce::String& message, const juce::String& okText,
                  std::function<void (bool)> answer, const juce::StringArray& steps = {},
                  const juce::String& cancelText = "Cancel");

    // ---- state
    dsp::PageSettings settings;
    int uiMode = modeVirtual;
    bool controlAllowed = false;
    bool teaching = false;
    DspProfile activeProfile;
    juce::File activeProfileFile;
    bool floatedForAutoTune = false;
    DspWindowHost::WindowInfo floatedWindow;
    bool launching = false;
    juce::uint32 launchStarted = 0;
    std::unique_ptr<juce::FileChooser> chooser;

    // ---- top: mode
    Card modeCard { "Control mode" };
    SegmentedControl modeControl { { "Virtual (demo)", "Manual guided", "Automatic (UI Automation)" } };
    Chip modeChip, dspChip, attachChip, consentChip;
    juce::Label explanation;

    // ---- main: embedded DSP software
    Card hostCard { "DSP software", "Attach a running window or launch your vendor tool" };
    FaButton attachBtn { "Attach window...", FaButton::Style::secondary, icons::Id::link };
    FaButton launchBtn { "Launch DSP software...", FaButton::Style::secondary, icons::Id::open };
    FaButton detachBtn { "Detach", FaButton::Style::ghost, icons::Id::unlink };
    FaButton allowBtn  { "Allow control", FaButton::Style::primary, icons::Id::check };
    FaButton revokeBtn { "Revoke control", FaButton::Style::ghost, icons::Id::eyeOff };
    FaButton panelBtn  { "Hide panel", FaButton::Style::ghost, icons::Id::maximise };   // wide view for big vendor tools
    bool sidePanelHidden = false;
    DspWindowHost host;
    EmptyState emptyHost { icons::Id::external, "No DSP software attached",
                           "Attach your DSP vendor software and it appears right here, inside FalseAudio.",
                           "Attach window..." };

    // ---- side panel
    Card sideCard { "Virtual rig" };
    dsp::InfoPanel virtualInfo { icons::Id::car };
    FaButton tryAutoTune { "Go to Auto Tune", FaButton::Style::primary, icons::Id::bolt };
    dsp::ManualPanel manualPanel;
    dsp::ProfilePicker profilePicker;
    dsp::ExportPanel exportPanel;
    dsp::TeachWizard wizard;      // after host: destroyed before it

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DspPage)
};
} // namespace fa::ui
