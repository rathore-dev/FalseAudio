#include "DspPage.h"
#include "dsp/WindowPicker.h"

namespace fa::ui
{
namespace
{
    const char* kLastExeKey     = "dsp.lastExe";
    const char* kLastProfileKey = "dsp.lastProfile";
    const char* kUiModeKey      = "dsp.uiMode";
    const char* kWideKey        = "dsp.wideView";

    juce::String explanationFor (int mode)
    {
        switch (mode)
        {
            case 0: return "A simulated car with realistic drivers and a virtual DSP. Every step of Auto Tune runs against it, "
                           "so you can learn the whole workflow without hardware. Nothing to connect.";
            case 1: return "Works with every DSP ever made. FalseAudio measures and computes the tune, then shows each change as "
                           "a checklist - you type the values into your DSP software and press Done.";
            case 3: return "Recommended when your DSP is supported: FalseAudio talks to it over USB directly - no vendor software, "
                           "no clicking, every value lands exactly. Supported: miniDSP units (2x4 HD, C-DSP 8x12, DDRC, Flex, SHD...) "
                           "and the Dayton Audio DSP-408. A backup of the DSP is saved before the first write.";
            default: return "FalseAudio drives your DSP vendor software itself through Windows UI Automation, using a profile you "
                            "teach once per DSP model. Attach the software below, pick or teach a profile, then allow control.";
        }
    }
}

DspPage::DspPage (Session& s)
    : Page (s), nativePanel (s), manualPanel (s), exportPanel (s), wizard (s, host)
{
    // ---- direct USB card
    addChildComponent (nativeCard);
    nativeCard.addAndMakeVisible (nativePanel);
    nativePanel.toast = [this] (const juce::String& m, AppShell::ToastKind k) { notify (m, k); };
    nativePanel.onConnect = [this] (const dsp::UsbDevice& d) { connectNative (d); };
    nativePanel.onDisconnect = [this] { disconnectNative(); };
    nativePanel.onRestore = [this] { restoreNativeBackup(); };

    // ---- mode card
    addAndMakeVisible (modeCard);
    modeCard.addAndMakeVisible (modeControl);
    modeControl.onChange = [this] (int i) { userChoseMode (i); };
    explanation.setFont (theme::font (13.0f));
    explanation.setColour (juce::Label::textColourId, theme::textDim);
    explanation.setJustificationType (juce::Justification::topLeft);
    explanation.setBorderSize ({ 0, 0, 0, 0 });
    explanation.setMinimumHorizontalScale (1.0f);
    modeCard.addAndMakeVisible (explanation);
    for (auto* c : { &modeChip, &dspChip, &attachChip, &consentChip })
        modeCard.addAndMakeVisible (c);
    consentChip.onClick = [this]
    {
        if (controlAllowed) revokeControl();
        else if (activeProfileFile != juce::File()) requestControl (activeProfile, activeProfileFile);
    };

    // ---- host card
    addAndMakeVisible (hostCard);
    for (auto* b : { &attachBtn, &launchBtn, &detachBtn, &allowBtn, &revokeBtn, &panelBtn })
        hostCard.addAndMakeVisible (b);

    panelBtn.setTooltip ("Give the DSP software the whole page width (vendor tools are often wider than the view).");
    panelBtn.onClick = [this]
    {
        sidePanelHidden = ! sidePanelHidden;
        settings.set (kWideKey, sidePanelHidden ? "1" : "0");
        updateSidePanel();
    };
    sidePanelHidden = settings.get (kWideKey, "0") == "1";
    hostCard.addChildComponent (host);
    hostCard.addAndMakeVisible (emptyHost);

    attachBtn.onClick = [this] { showAttachMenu(); };
    emptyHost.onAction = [this] { showAttachMenu(); };
    launchBtn.onClick = [this] { launchDsp(); };
    detachBtn.onClick = [this] { detachDsp(); };
    allowBtn.onClick = [this]
    {
        if (activeProfileFile != juce::File())
            requestControl (activeProfile, activeProfileFile);
        else
            notify ("Pick or teach a DSP profile first.", AppShell::ToastKind::warn);
    };
    revokeBtn.onClick = [this] { revokeControl(); };
    host.onStateChanged = [this] { hostStateChanged(); };

    // ---- side card
    addAndMakeVisible (sideCard);
    virtualInfo.setText ("Nothing to set up",
                         "The virtual car reacts instantly to every setting, so Auto Tune runs end-to-end in seconds. "
                         "Switch to Manual or Automatic when your real DSP is connected.");
    sideCard.addChildComponent (virtualInfo);
    sideCard.addChildComponent (tryAutoTune);
    nativeInfo.setAccent (theme::warn);
    nativeInfo.setText ("Good to know",
                        "miniDSP: the vendor plugin keeps its own copy of the settings, so it will not display values written "
                        "this way - and pressing Sync in the plugin overwrites them. "
                        "DSP-408: every write is permanent on the device; the backup taken before the first write is your undo "
                        "(Restore backup). Close the vendor software while FalseAudio is connected.");
    sideCard.addChildComponent (nativeInfo);
    tryAutoTune.onClick = [this] { if (shell != nullptr) shell->showPage (PageId::autoTune); };

    sideCard.addChildComponent (manualPanel);
    manualPanel.toast = [this] (const juce::String& m, AppShell::ToastKind k) { notify (m, k); };

    sideCard.addChildComponent (profilePicker);
    profilePicker.toast = [this] (const juce::String& m, AppShell::ToastKind k) { notify (m, k); };
    profilePicker.onUse = [this] (const DspProfile& p, const juce::File& f) { requestControl (p, f); };
    profilePicker.onEdit = [this] (const DspProfile& p, const juce::File& f) { startTeaching (p, f); };
    profilePicker.onTeachNew = [this] { startTeaching ({}, {}); };
    profilePicker.confirm = [this] (const juce::String& message, std::function<void (bool)> answer)
    {
        confirm ("Delete profile", message, "Delete", std::move (answer));
    };

    sideCard.addAndMakeVisible (exportPanel);
    exportPanel.toast = [this] (const juce::String& m, AppShell::ToastKind k) { notify (m, k); };

    sideCard.addChildComponent (wizard);
    wizard.toast = [this] (const juce::String& m, AppShell::ToastKind k) { notify (m, k); };
    wizard.onCancelled = [this] { stopTeaching(); };
    wizard.onFinished = [this] (const DspProfile& p, const juce::File& f, bool activate)
    {
        stopTeaching();
        profilePicker.refresh();
        if (activate)
            requestControl (p, f);
    };

    // restore
    uiMode = juce::jlimit (0, 3, settings.get (kUiModeKey, "0").getIntValue());
    syncModeFromSession();
    const auto lastProfile = juce::File (settings.get (kLastProfileKey));
    if (lastProfile.existsAsFile())
    {
        DspProfile p;
        if (DspProfile::load (lastProfile, p))
        {
            activeProfile = p;
            activeProfileFile = lastProfile;    // remembered, but control is only granted after "Allow control"
        }
    }

    refreshChips();
    updateSidePanel();
    startTimer (500);
}

DspPage::~DspPage()
{
    stopTimer();
    wizard.stop();
    host.onStateChanged = nullptr;
    host.detach();
}

//==============================================================================
void DspPage::pageShown()
{
    if (floatedForAutoTune && ! session.getAutoTune().isRunning())
    {
        floatedForAutoTune = false;
        juce::String err;
        if (! host.attach (floatedWindow, err) && err.isNotEmpty())
            notify (err, AppShell::ToastKind::warn);
    }
    showHost (host.isAttached());
    profilePicker.refresh();
    manualPanel.refresh();
    refreshChips();
}

void DspPage::pageHidden()
{
    // The foreign HWND is a child of our top-level window: make sure it hides with the page.
    if (teaching)
        wizard.stop();

    if (host.isAttached() && session.getAutoTune().isRunning() && session.getDspKind() == Session::DspKind::uiAutomation)
    {
        // Auto Tune is driving it: float it back to the desktop so clicks/typing still land.
        floatedWindow = host.getAttached();
        floatedForAutoTune = true;
        host.detach();
    }
    showHost (false);
}

void DspPage::showHost (bool show)
{
    host.setVisible (show);
    host.resized();                         // DspWindowHost::reposition() honours isShowing(): hides/shows the HWND now
    emptyHost.setVisible (! show);
}

void DspPage::timerCallback()
{
    // Watchdog: if Auto Tune starts driving the DSP while this page is hidden, the embedded
    // window would be invisible - float it out to the desktop until the run ends.
    const bool running = session.getAutoTune().isRunning();
    if (! isShowing() && running && host.isAttached() && session.getDspKind() == Session::DspKind::uiAutomation)
    {
        floatedWindow = host.getAttached();
        floatedForAutoTune = true;
        host.detach();
    }
    else if (floatedForAutoTune && ! running && isShowing())
    {
        pageShown();
    }

    if (launching && juce::Time::getMillisecondCounter() - launchStarted > 21000)
    {
        launching = false;
        if (! host.isAttached())
            notify ("The DSP software did not show a window within 20 s. Use \"Attach window...\" once it is open.", AppShell::ToastKind::warn);
    }
}

void DspPage::sessionChanged()
{
    syncModeFromSession();
    manualPanel.refresh();
    nativePanel.refresh();          // picks up warnings / mapping report after a tune
    refreshChips();
    updateSidePanel();
}

//==============================================================================
void DspPage::connectNative (const dsp::UsbDevice& device)
{
    if (nativePanel.isBusy() || session.getAutoTune().isRunning())
        return;
    if (! device.supported)
    {
        notify ("That device is not supported: " + device.reason, AppShell::ToastKind::warn);
        return;
    }

    auto proceed = [this, device]
    {
        if (device.needsSampleRate)
        {
            if (shell == nullptr) { connectNativeWithRate (device, 48000.0); return; }
            auto sheet = std::make_unique<dsp::SampleRateSheet> (device.name);
            sheet->onResult = [this, device] (double rate)
            {
                if (shell != nullptr) shell->closeSheet();
                if (rate > 0.0) connectNativeWithRate (device, rate);
            };
            shell->showSheet (std::move (sheet), dsp::SampleRateSheet::width, dsp::SampleRateSheet::height);
        }
        else
            connectNativeWithRate (device, 0.0);
    };

    if (usbAllowed)
    {
        proceed();
        return;
    }

    const bool is408 = device.kind == dsp::UsbDevice::Kind::dsp408;
    juce::StringArray what {
        "Read the current settings and save a backup to " + DspProfile::profilesFolder().getParentDirectory().getChildFile ("backups").getFullPathName() + " before the first write",
        "Write gain, delay, polarity, crossovers, EQ and mute - only to documented addresses, never presets or firmware",
        is408 ? juce::String ("DSP-408 writes are permanent on the device: Restore backup is the undo")
              : juce::String ("The miniDSP plugin will not show these values (it keeps its own copy) - keep it closed"),
        "Stop immediately when you cancel Auto Tune or press Disconnect"
    };
    confirm ("Allow FalseAudio to write to " + device.name + "?",
             "Direct USB mode changes the DSP itself, with no vendor software in between. FalseAudio will:",
             "Allow and connect",
             [this, proceed] (bool yes)
             {
                 if (! yes) { notify ("Not connected - staying in manual guided mode.", AppShell::ToastKind::warn); return; }
                 usbAllowed = true;
                 proceed();
             },
             what, "Not now");
}

void DspPage::connectNativeWithRate (const dsp::UsbDevice& device, double sampleRate)
{
    if (session.getMode() != Session::Mode::live)
        session.setMode (Session::Mode::live);

    nativePanel.setBusy (true, "Connecting to " + device.name + " and taking a backup...");
    refreshChips();

    // The controllers block on USB IO (handshake + reading every output record), so build and
    // connect them on a worker thread and hand the result to the Session on the message thread.
    juce::Component::SafePointer<DspPage> safe (this);
    const bool is408 = device.kind == dsp::UsbDevice::Kind::dsp408;
    juce::Thread::launch ([safe, device, sampleRate, is408]
    {
        juce::String error;
        std::unique_ptr<DspController> controller;
        if (is408)
        {
            Dsp408Controller::Options o;
            o.devicePath = device.path;
            auto c = std::make_unique<Dsp408Controller> (o);
            if (c->connect (error)) controller = std::move (c);
        }
        else
        {
            MiniDspController::Options o;
            o.devicePath = device.path;
            o.sampleRateOverride = sampleRate;
            auto c = std::make_unique<MiniDspController> (o);
            if (c->connect (error)) controller = std::move (c);
        }

        // unique_ptr is move-only; stash it in a shared_ptr for the std::function below
        auto holder = std::make_shared<std::unique_ptr<DspController>> (std::move (controller));
        juce::MessageManager::callAsync ([safe, holder, error, is408, device]
        {
            if (safe == nullptr)
                return;
            safe->nativePanel.setBusy (false, {});
            if (*holder == nullptr)
            {
                safe->notify ("Could not connect to " + device.name + ": " + error, AppShell::ToastKind::bad);
                safe->refreshChips();
                return;
            }
            auto cfg = safe->session.getConfig();
            cfg.caps = (*holder)->getCapabilities();
            safe->session.setConfig (cfg);
            safe->session.setDspController (std::move (*holder), is408 ? Session::DspKind::nativeDsp408 : Session::DspKind::nativeMiniDsp);
            safe->uiMode = modeNative;
            safe->settings.set (kUiModeKey, "3");
            safe->notify ("Connected: " + device.name + " - backup taken, ready for Auto Tune.", AppShell::ToastKind::good);
            safe->syncModeFromSession();
            safe->nativePanel.refresh();
            safe->refreshChips();
            safe->updateSidePanel();
        });
    });
}

void DspPage::disconnectNative()
{
    if (! isNativeKind() || nativePanel.isBusy() || session.getAutoTune().isRunning())
        return;
    session.useManualDsp();     // destroys the controller (closes the USB handle)
    notify ("Disconnected. Auto Tune will show a checklist until you connect again.");
    nativePanel.refresh();
    refreshChips();
}

void DspPage::restoreNativeBackup()
{
    if (! isNativeKind() || nativePanel.isBusy() || session.getAutoTune().isRunning())
        return;

    auto& dsp = session.getDsp();
    auto* mini = dynamic_cast<MiniDspController*> (&dsp);
    auto* d408 = dynamic_cast<Dsp408Controller*> (&dsp);
    if (mini == nullptr && d408 == nullptr)
        return;
    const auto snapshot = mini != nullptr ? mini->getSnapshotFile() : d408->getSnapshotFile();

    confirm ("Restore the backup?",
             "This writes the settings captured when FalseAudio connected back into the DSP, replacing the tune." +
                 juce::String (snapshot != juce::File() ? "\n\nBackup: " + snapshot.getFullPathName() : juce::String()),
             "Restore backup",
             [this] (bool yes)
             {
                 if (! yes || ! isNativeKind() || nativePanel.isBusy() || session.getAutoTune().isRunning())
                     return;
                 // re-fetch: the controller may have been swapped while the sheet was open
                 auto* mini = dynamic_cast<MiniDspController*> (&session.getDsp());
                 auto* d408 = dynamic_cast<Dsp408Controller*> (&session.getDsp());
                 nativePanel.setBusy (true, "Restoring backup...");
                 refreshChips();
                 juce::Component::SafePointer<DspPage> safe (this);
                 juce::Thread::launch ([safe, mini, d408]
                 {
                     juce::String error;
                     bool ok = false;
                     if (mini != nullptr)
                     {
                         const auto f = mini->getSnapshotFile();
                         ok = f.existsAsFile() ? mini->restoreSnapshot (f, error) : (error = "No backup was taken yet (nothing has been written).", false);
                     }
                     else if (d408 != nullptr)
                         ok = d408->restoreSnapshot (error);

                     juce::MessageManager::callAsync ([safe, ok, error]
                     {
                         if (safe == nullptr) return;
                         safe->nativePanel.setBusy (false, {});
                         safe->notify (ok ? "Backup restored and verified." : "Restore failed: " + error,
                                       ok ? AppShell::ToastKind::good : AppShell::ToastKind::bad);
                         safe->nativePanel.refresh();
                         safe->refreshChips();
                     });
                 });
             });
}

//==============================================================================
void DspPage::userChoseMode (int index)
{
    if (session.getAutoTune().isRunning() || nativePanel.isBusy())
    {
        notify (nativePanel.isBusy() ? "Wait for the USB operation to finish." : "Finish or cancel Auto Tune before changing the DSP mode.",
                AppShell::ToastKind::warn);
        syncModeFromSession();
        return;
    }

    uiMode = index;
    settings.set (kUiModeKey, juce::String (index));

    switch (index)
    {
        case modeVirtual:
            session.setMode (Session::Mode::demo);
            break;
        case modeManual:
            if (teaching) stopTeaching();
            session.setMode (Session::Mode::live);
            if (session.getDspKind() != Session::DspKind::manual)
                session.useManualDsp();
            break;
        case modeAutomatic:
            session.setMode (Session::Mode::live);
            if (isNativeKind())
                session.useManualDsp();          // drop the USB controller; the profile takes over below
            if (controlAllowed && activeProfileFile != juce::File() && session.getDspKind() != Session::DspKind::uiAutomation)
                activateProfile (activeProfile, activeProfileFile);
            else if (session.getDspKind() != Session::DspKind::uiAutomation && ! controlAllowed)
                notify (host.isAttached() ? "Pick a profile and press Allow control." : "Attach your DSP software, then pick or teach a profile.");
            break;
        case modeNative:
            if (teaching) stopTeaching();
            session.setMode (Session::Mode::live);
            if (! isNativeKind())
            {
                if (session.getDspKind() != Session::DspKind::manual)
                    session.useManualDsp();      // until Connect succeeds, Auto Tune falls back to the checklist
                notify ("Plug the DSP in over USB, scan, then press Connect.");
                nativePanel.scan();
            }
            break;
    }
    refreshChips();
    updateSidePanel();
}

void DspPage::syncModeFromSession()
{
    switch (session.getDspKind())
    {
        case Session::DspKind::virtualDemo:  uiMode = modeVirtual; break;
        case Session::DspKind::uiAutomation: uiMode = modeAutomatic; break;
        case Session::DspKind::nativeMiniDsp:
        case Session::DspKind::nativeDsp408: uiMode = modeNative; break;
        case Session::DspKind::manual:       if (uiMode == modeVirtual) uiMode = modeManual; break;
    }
    modeControl.setSelected (uiMode, juce::dontSendNotification);
}

bool DspPage::isNativeKind() const
{
    const auto k = session.getDspKind();
    return k == Session::DspKind::nativeMiniDsp || k == Session::DspKind::nativeDsp408;
}

void DspPage::refreshChips()
{
    const bool live = session.getMode() == Session::Mode::live;
    modeChip.set (live ? "LIVE" : "DEMO", live ? theme::good : theme::accent);

    switch (session.getDspKind())
    {
        case Session::DspKind::virtualDemo:  dspChip.set ("Virtual rig", theme::accent); break;
        case Session::DspKind::manual:       dspChip.set ("Manual (guided)", theme::warn); break;
        case Session::DspKind::uiAutomation: dspChip.set ("Auto: " + session.getDsp().getName(), theme::good); break;
        case Session::DspKind::nativeMiniDsp:
        case Session::DspKind::nativeDsp408: dspChip.set ("USB: " + session.getDsp().getName(), theme::good); break;
    }

    if (host.isAttached())
        attachChip.set ("Attached: " + host.getAttached().processName, theme::good);
    else if (launching)
        attachChip.set ("Launching...", theme::warn);
    else if (floatedForAutoTune)
        attachChip.set ("On desktop (Auto Tune)", theme::accent);
    else
        attachChip.set ("Not attached", theme::textMuted);

    const bool uia = session.getDspKind() == Session::DspKind::uiAutomation;
    if (uiMode == modeNative)
        consentChip.set (isNativeKind() ? "Connected - backup taken" : nativePanel.isBusy() ? "Working..." : "Not connected",
                         isNativeKind() ? theme::good : nativePanel.isBusy() ? theme::warn : theme::textMuted);
    else
        consentChip.set (controlAllowed && uia ? "Control allowed" : activeProfileFile != juce::File() ? "Control not allowed" : "No profile",
                         controlAllowed && uia ? theme::good : theme::textMuted);
    consentChip.setVisible (uiMode == modeAutomatic || uiMode == modeNative);
    attachChip.setVisible (uiMode != modeNative);

    hostCard.setTitle ("DSP software", host.isAttached() ? host.getAttached().processName + juce::String (juce::CharPointer_UTF8 ("  ·  ")) + host.getAttached().title
                                                           : juce::String ("Attach a running window or launch your vendor tool"));
    hostCard.setHighlight (uia && controlAllowed ? theme::good.withAlpha (0.6f) : juce::Colours::transparentBlack);

    detachBtn.setEnabled (host.isAttached());
    launchBtn.setEnabled (! launching);
    const bool showAllow = uiMode == modeAutomatic && ! (uia && controlAllowed);
    allowBtn.setVisible (showAllow);
    revokeBtn.setVisible (uiMode == modeAutomatic && uia && controlAllowed);
    allowBtn.setEnabled (host.isAttached() && activeProfileFile != juce::File());

    profilePicker.setActive (uia ? activeProfileFile : juce::File());
    profilePicker.setCanTeach (host.isAttached(), host.isAttached() ? juce::String() : "Attach your DSP software to teach or use a profile.");

    nativeCard.setHighlight (isNativeKind() ? theme::good.withAlpha (0.6f) : juce::Colours::transparentBlack);
    nativeCard.setTitle ("Direct USB", isNativeKind() ? session.getDsp().getName() + juce::String (juce::CharPointer_UTF8 ("  ·  connected"))
                                                      : juce::String ("Writes the tune straight into the DSP - no vendor software"));

    modeCard.repaint();
    resized();
}

void DspPage::updateSidePanel()
{
    const bool v = uiMode == modeVirtual && ! teaching;
    const bool m = uiMode == modeManual && ! teaching;
    const bool a = uiMode == modeAutomatic && ! teaching;
    const bool n = uiMode == modeNative && ! teaching;

    virtualInfo.setVisible (v);
    tryAutoTune.setVisible (v);
    manualPanel.setVisible (m);
    profilePicker.setVisible (a);
    nativeInfo.setVisible (n);
    wizard.setVisible (teaching);
    exportPanel.setVisible (! teaching);

    // Direct USB replaces the embedded-software card with the device panel
    nativeCard.setVisible (n);
    hostCard.setVisible (! n);
    if (n) showHost (false);
    else   showHost (host.isAttached() && isShowing());

    const bool wide = sidePanelHidden && ! teaching && ! n;   // the wizard always needs its panel
    sideCard.setVisible (! wide);
    panelBtn.setButtonText (wide ? "Show panel" : "Hide panel");
    panelBtn.setIcon (wide ? icons::Id::restore : icons::Id::maximise);
    panelBtn.setEnabled (! teaching);

    sideCard.setTitle (teaching ? "Teach wizard" : v ? "Virtual rig" : m ? "Manual guided" : n ? "Direct USB" : "Automation profiles",
                       teaching ? "Click controls inside the DSP software" : v ? "Demo mode" : m ? "Checklist during Auto Tune"
                                : n ? "Limitations & backup" : "Per DSP model");
    explanation.setText (explanationFor (uiMode), juce::dontSendNotification);
    resized();
}

//==============================================================================
void DspPage::showAttachMenu()
{
    dsp::showWindowPicker (attachBtn, [this] (const DspWindowHost::WindowInfo& w) { attachWindow (w); });
}

void DspPage::attachWindow (const DspWindowHost::WindowInfo& w)
{
    juce::String err;
    if (! host.attach (w, err))
    {
        notify (err, AppShell::ToastKind::bad);
        return;
    }
    notify ("Attached " + w.processName, AppShell::ToastKind::good);
}

void DspPage::launchDsp()
{
    auto start = juce::File (settings.get (kLastExeKey));
    if (! start.existsAsFile())
        start = juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory);

    chooser = std::make_unique<juce::FileChooser> ("Choose your DSP software", start, "*.exe");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto exe = fc.getResult();
                              if (exe == juce::File())
                                  return;
                              settings.set (kLastExeKey, exe.getFullPathName());
                              juce::String err;
                              if (! host.launchAndAttach (exe, err))
                              {
                                  notify (err, AppShell::ToastKind::bad);
                                  return;
                              }
                              launching = true;
                              launchStarted = juce::Time::getMillisecondCounter();
                              notify ("Starting " + exe.getFileName() + " - waiting for its window...");
                              refreshChips();
                          });
}

void DspPage::detachDsp()
{
    if (teaching)
        stopTeaching();
    host.detach();
    notify ("DSP software detached - it is back on your desktop.");
}

void DspPage::hostStateChanged()
{
    launching = false;
    showHost (host.isAttached() && isShowing());
    wizard.windowChanged();
    refreshChips();
    if (host.isAttached() && uiMode == modeAutomatic && ! teaching)
        profilePicker.refresh();
}

//==============================================================================
void DspPage::requestControl (const DspProfile& profile, const juce::File& file)
{
    if (! host.isAttached())
    {
        notify ("Attach your DSP software first.", AppShell::ToastKind::warn);
        return;
    }
    if (controlAllowed)
    {
        activateProfile (profile, file);
        return;
    }

    const auto app = host.getAttached().processName;
    juce::StringArray what {
        "Click, type and pick list items inside " + app + " - only there, never anywhere else",
        "Change gain, delay, polarity, crossovers, EQ and mute as the tune requires",
        "Show you a checklist for anything the profile \"" + profile.name + "\" has not taught",
        "Stop immediately when you cancel Auto Tune or press Revoke control"
    };
    confirm ("Allow FalseAudio to control " + app + "?",
             "To apply the tune automatically, FalseAudio will operate your DSP software like a person at the keyboard. It will:",
             "Allow control",
             [this, profile, file] (bool yes)
             {
                 if (! yes)
                 {
                     notify ("Control not allowed - staying in manual guided mode.", AppShell::ToastKind::warn);
                     return;
                 }
                 controlAllowed = true;
                 activateProfile (profile, file);
             },
             what, "Not now");
}

void DspPage::activateProfile (const DspProfile& profile, const juce::File& file)
{
    activeProfile = profile;
    activeProfileFile = file;
    settings.set (kLastProfileKey, file.getFullPathName());

    if (session.getMode() != Session::Mode::live)
        session.setMode (Session::Mode::live);
    session.useUIAutomationDsp (profile, [this] { return host.getAttached().handle; });
    uiMode = modeAutomatic;
    settings.set (kUiModeKey, "2");
    notify ("Automatic control active: " + profile.name, AppShell::ToastKind::good);
    syncModeFromSession();
    refreshChips();
    updateSidePanel();
}

void DspPage::revokeControl()
{
    controlAllowed = false;
    if (session.getDspKind() == Session::DspKind::uiAutomation)
        session.useManualDsp();
    notify ("Control revoked. Auto Tune will show a checklist instead.");
    refreshChips();
}

void DspPage::startTeaching (const DspProfile& existing, const juce::File& file)
{
    if (! host.isAttached())
    {
        notify ("Attach your DSP software first.", AppShell::ToastKind::warn);
        return;
    }
    teaching = true;
    updateSidePanel();
    wizard.start (existing, file);
}

void DspPage::stopTeaching()
{
    wizard.stop();
    teaching = false;
    updateSidePanel();
    refreshChips();
}

//==============================================================================
void DspPage::notify (const juce::String& message, AppShell::ToastKind kind)
{
    if (shell != nullptr)
        shell->toast (message, kind);
}

void DspPage::confirm (const juce::String& title, const juce::String& message, const juce::String& okText,
                       std::function<void (bool)> answer, const juce::StringArray& steps, const juce::String& cancelText)
{
    if (shell == nullptr)
    {
        auto opts = juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::QuestionIcon).withTitle (title)
                        .withMessage (message + (steps.isEmpty() ? "" : "\n\n" + steps.joinIntoString ("\n")))
                        .withButton (okText).withButton ("Cancel").withAssociatedComponent (this);
        juce::NativeMessageBox::showAsync (opts, [answer] (int r) { if (answer) answer (r == 0); });   // 0 = first button
        return;
    }
    auto sheet = std::make_unique<MessageSheet> (title, message, steps, okText, cancelText, icons::Id::chip);
    auto* raw = sheet.get();
    raw->onResult = [answer] (bool ok) { if (answer) answer (ok); };
    shell->showSheet (std::move (sheet), 560, MessageSheet::idealHeight (steps.size()));
}

//==============================================================================
void DspPage::paint (juce::Graphics&) {}

void DspPage::resized()
{
    auto r = getLocalBounds().reduced (theme::padLg, theme::pad);
    if (r.isEmpty())
        return;

    const int gap = theme::pad;
    const int sideW = juce::jlimit (340, 460, (int) (r.getWidth() * 0.32));

    // ---- mode card
    const int modeH = 136;
    modeCard.setBounds (r.removeFromTop (modeH));
    {
        auto c = modeCard.getContentArea();
        auto top = c.removeFromTop (36);
        auto chipsCol = top.removeFromRight (std::min (360, top.getWidth() / 2));
        modeControl.setBounds (top.removeFromLeft (std::min (520, top.getWidth())).withHeight (34));

        // chips: right-aligned row
        int x = chipsCol.getRight();
        for (auto* chip : { &consentChip, &attachChip, &dspChip, &modeChip })
        {
            if (! chip->isVisible()) continue;
            const int w = std::min (chip->getIdealWidth(), 190);
            chip->setBounds (x - w, chipsCol.getY() + 5, w, 24);
            x -= w + 6;
        }
        c.removeFromTop (6);
        explanation.setBounds (c);
    }
    r.removeFromTop (gap);

    // ---- side card (right)
    if (sideCard.isVisible())
    {
        auto side = r.removeFromRight (sideW);
        sideCard.setBounds (side);
        r.removeFromRight (gap);
    }
    {
        auto c = sideCard.getContentArea();
        if (teaching)
        {
            wizard.setBounds (c);
        }
        else
        {
            auto ex = c.removeFromBottom (dsp::ExportPanel::idealHeight());
            exportPanel.setBounds (ex);
            c.removeFromBottom (theme::pad);
            // divider drawn by card border; content above
            if (uiMode == modeVirtual)
            {
                virtualInfo.setBounds (c.removeFromTop (std::min (virtualInfo.idealHeight (c.getWidth()), c.getHeight() - 48)));
                c.removeFromTop (theme::pad);
                tryAutoTune.setBounds (c.removeFromTop (40).withWidth (std::min (220, c.getWidth())));
            }
            else if (uiMode == modeNative)
                nativeInfo.setBounds (c.removeFromTop (std::min (nativeInfo.idealHeight (c.getWidth()), c.getHeight())));
            else if (uiMode == modeManual)
                manualPanel.setBounds (c);
            else
                profilePicker.setBounds (c);
        }
    }

    // ---- main card: direct USB panel or embedded DSP software
    nativeCard.setBounds (r);
    nativePanel.setBounds (nativeCard.getContentArea());
    hostCard.setBounds (r);
    {
        auto c = hostCard.getContentArea();
        auto bar = c.removeFromTop (theme::controlHeight);
        attachBtn.setBounds (bar.removeFromLeft (150)); bar.removeFromLeft (6);
        launchBtn.setBounds (bar.removeFromLeft (190)); bar.removeFromLeft (6);
        detachBtn.setBounds (bar.removeFromLeft (96));
        panelBtn.setBounds (bar.removeFromRight (120)); bar.removeFromRight (6);
        if (revokeBtn.isVisible()) revokeBtn.setBounds (bar.removeFromRight (150));
        if (allowBtn.isVisible())  allowBtn.setBounds (bar.removeFromRight (140));
        c.removeFromTop (theme::grid);
        host.setBounds (c);
        emptyHost.setBounds (c);
    }
}
} // namespace fa::ui
