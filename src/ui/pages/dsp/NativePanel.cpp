#include "NativePanel.h"

namespace fa::ui::dsp
{
//==============================================================================
SampleRateSheet::SampleRateSheet (const juce::String& name) : deviceName (name)
{
    addAndMakeVisible (rates);
    addAndMakeVisible (ok);
    addAndMakeVisible (cancel);
    ok.onClick = [this] { if (onResult) onResult (rates.getSelected() == 1 ? 96000.0 : 48000.0); };
    cancel.onClick = [this] { if (onResult) onResult (0.0); };
}

void SampleRateSheet::paint (juce::Graphics& g)
{
    theme::fillCard (g, getLocalBounds().toFloat(), theme::bg2, theme::borderHi);
    auto r = getLocalBounds().reduced (theme::padLg);
    g.setColour (theme::text);
    g.setFont (theme::fontBold (17.0f));
    g.drawText ("Internal sample rate of " + deviceName, r.removeFromTop (26), juce::Justification::centredLeft);
    r.removeFromTop (6);
    g.setColour (theme::textDim);
    g.setFont (theme::font (13.0f));
    g.drawFittedText ("This miniDSP does not report its internal rate over USB, but delay and filter coefficients "
                      "depend on it. Pick the rate shown in the miniDSP plugin (Device > Sample rate). A wrong choice "
                      "shifts every delay and crossover.",
                      r.removeFromTop (60), juce::Justification::topLeft, 4);
}

void SampleRateSheet::resized()
{
    auto r = getLocalBounds().reduced (theme::padLg);
    r.removeFromTop (26 + 6 + 60 + 12);
    rates.setBounds (r.removeFromTop (36).withWidth (260));
    auto bar = getLocalBounds().reduced (theme::padLg).removeFromBottom (theme::controlHeight + 4);
    ok.setBounds (bar.removeFromRight (130));
    bar.removeFromRight (8);
    cancel.setBounds (bar.removeFromRight (90));
}

//==============================================================================
NativePanel::NativePanel (Session& s) : session (s)
{
    list.setModel (this);
    list.setRowHeight (56);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    list.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    list.getViewport()->setScrollBarsShown (true, false);
    addAndMakeVisible (list);

    for (auto* b : { &scanBtn, &connectBtn, &disconnectBtn, &restoreBtn, &copyBtn })
        addAndMakeVisible (b);

    scanBtn.onClick = [this] { scan(); };
    connectBtn.onClick = [this]
    {
        if (auto d = getSelected())
            if (onConnect) onConnect (*d);
    };
    disconnectBtn.onClick = [this] { if (onDisconnect) onDisconnect(); };
    restoreBtn.onClick = [this] { if (onRestore) onRestore(); };
    restoreBtn.setTooltip ("Writes the backup taken before the first write back to the device and verifies it.");
    copyBtn.onClick = [this]
    {
        juce::SystemClipboard::copyTextToClipboard (report.getText());
        if (toast) toast ("Report copied", AppShell::ToastKind::good);
    };

    report.setMultiLine (true, false);
    report.setReadOnly (true);
    report.setScrollbarsShown (true);
    report.setCaretVisible (false);
    report.setFont (theme::mono (12.0f));
    report.setColour (juce::TextEditor::backgroundColourId, theme::bg1);
    report.setColour (juce::TextEditor::outlineColourId, theme::border);
    report.setColour (juce::TextEditor::focusedOutlineColourId, theme::accentDim);
    report.setColour (juce::TextEditor::textColourId, theme::text);
    report.setColour (juce::TextEditor::highlightColourId, theme::accent.withAlpha (0.3f));
    addAndMakeVisible (report);

    scanText = "Plug the DSP in over USB and press Scan.";
    refresh();
}

NativePanel::~NativePanel()
{
    list.setModel (nullptr);
}

//==============================================================================
void NativePanel::scan()
{
    if (scanning)
        return;
    scanning = true;
    everScanned = true;
    scanText = "Scanning USB...";
    updateButtons();
    repaint();

    juce::Component::SafePointer<NativePanel> safe (this);
    juce::Thread::launch ([safe]
    {
        std::vector<UsbDevice> found;

        for (auto& d : MiniDspController::detect())
        {
            UsbDevice u;
            u.kind = UsbDevice::Kind::miniDsp;
            u.path = d.path;
            u.name = d.name.isNotEmpty() ? d.name : (d.usbProduct.isNotEmpty() ? d.usbProduct : juce::String ("miniDSP device"));
            u.serial = d.usbSerial.isNotEmpty() ? d.usbSerial : (d.identity.serial != 0 ? juce::String (d.identity.serial) : juce::String());
            u.supported = d.supported;
            u.reason = d.error;
            u.detail = "miniDSP";
            if (d.identity.hwId >= 0)
                u.detail << juce::String (juce::CharPointer_UTF8 (" · hw ")) << d.identity.hwId << juce::String (juce::CharPointer_UTF8 (" · fw ")) << d.identity.fwMajor << "." << d.identity.fwMinor;
            if (d.kind.isNotEmpty())
            {
                u.detail << juce::String (juce::CharPointer_UTF8 (" · ")) << d.kind;
                if (auto* spec = minidsp::byKind (d.kind))
                    u.needsSampleRate = spec->sampleRate <= 0.0;
            }
            found.push_back (u);
        }

        for (auto& d : Dsp408Controller::detect (true))
        {
            UsbDevice u;
            u.kind = UsbDevice::Kind::dsp408;
            u.path = d.path;
            u.name = "Dayton Audio DSP-408";
            u.serial = d.serial;
            u.supported = true;
            u.detail = "USB HID";
            if (d.identity.isNotEmpty()) u.detail << juce::String (juce::CharPointer_UTF8 (" · ")) << d.identity;
            found.push_back (u);
        }

        juce::MessageManager::callAsync ([safe, found]
        {
            if (safe == nullptr)
                return;
            safe->devices = found;
            safe->scanning = false;
            int supported = 0;
            for (auto& d : found) if (d.supported) ++supported;
            safe->scanText = found.empty() ? "No miniDSP or DSP-408 found. Check the USB cable and that the vendor plugin is closed."
                           : juce::String (found.size()) + (found.size() == 1 ? " device" : " devices") + " found, "
                             + juce::String (supported) + " supported";
            safe->list.updateContent();
            int sel = -1;
            const auto connected = safe->getConnectedPath();
            for (size_t i = 0; i < found.size(); ++i)
                if (found[i].path == connected || (sel < 0 && found[i].supported))
                    sel = (int) i;
            safe->list.selectRow (sel);
            safe->updateButtons();
            safe->repaint();
        });
    });
}

void NativePanel::setBusy (bool b, const juce::String& what)
{
    busy = b;
    busyText = what;
    updateButtons();
    repaint();
}

std::optional<UsbDevice> NativePanel::getSelected() const
{
    const int row = list.getSelectedRow();
    if (row >= 0 && row < (int) devices.size())
        return devices[(size_t) row];
    return std::nullopt;
}

bool NativePanel::isConnected() const
{
    const auto k = session.getDspKind();
    return k == Session::DspKind::nativeMiniDsp || k == Session::DspKind::nativeDsp408;
}

juce::String NativePanel::connectedName() const
{
    return isConnected() ? session.getDsp().getName() : juce::String();
}

juce::String NativePanel::getConnectedPath() const
{
    return {};   // the controllers don't expose the HID path; rows are matched by selection instead
}

void NativePanel::refresh()
{
    updateReport();
    updateButtons();
    repaint();
}

void NativePanel::updateButtons()
{
    const bool connected = isConnected();
    const auto sel = getSelected();
    scanBtn.setEnabled (! scanning && ! busy);
    connectBtn.setEnabled (! busy && ! scanning && sel.has_value() && sel->supported && ! session.getAutoTune().isRunning());
    connectBtn.setButtonText (connected ? "Switch device" : "Connect");
    disconnectBtn.setVisible (connected);
    disconnectBtn.setEnabled (! busy && ! session.getAutoTune().isRunning());
    restoreBtn.setVisible (connected);
    restoreBtn.setEnabled (! busy && ! session.getAutoTune().isRunning());
}

void NativePanel::updateReport()
{
    statusTitle.clear();
    statusLines.clear();
    statusNote.clear();
    juce::String text;

    if (! isConnected())
    {
        report.setText ("Connect a device: after Auto Tune the quantisation report (what the device grid changed) "
                        "and any warnings appear here.", false);
        return;
    }

    auto& dsp = session.getDsp();
    juce::StringArray warnings, mapping;
    juce::File snapshot;

    if (auto* mini = dynamic_cast<MiniDspController*> (&dsp))
    {
        const auto id = mini->getIdentity();
        statusTitle = mini->getName();
        statusLines << "Serial " << (id.serial != 0 ? juce::String (id.serial) : juce::String ("-"))
                    << juce::String (juce::CharPointer_UTF8 ("  ·  firmware ")) << id.fwMajor << "." << id.fwMinor
                    << juce::String (juce::CharPointer_UTF8 ("  ·  ")) << mini->getWritesSent() << " writes sent";
        if (auto* dev = mini->getDevice())
            statusLines << juce::String (juce::CharPointer_UTF8 ("  ·  ")) << dev->productName << " (" << mini->getCapabilities().sampleRate / 1000.0 << " kHz)";
        warnings = mini->getWarnings();
        snapshot = mini->getSnapshotFile();
        statusNote = "miniDSP limitation: the vendor plugin keeps its own copy of the settings and will not display "
                     "values written this way. Do not press Sync in the plugin afterwards or it overwrites the tune.";
    }
    else if (auto* d408 = dynamic_cast<Dsp408Controller*> (&dsp))
    {
        statusTitle = d408->getName();
        statusLines << (d408->getIdentity().isNotEmpty() ? d408->getIdentity() : juce::String ("DSP-408"))
                    << juce::String (juce::CharPointer_UTF8 ("  ·  ")) << d408->getBlocksWritten() << " blocks written";
        mapping = d408->getMappingReport();
        snapshot = d408->getSnapshotFile();
        statusNote = "DSP-408 writes are permanent on the device (there is no undo). A backup is taken before the "
                     "first write; Restore backup puts the original state back.";
    }

    if (snapshot != juce::File())
        statusLines << "\nBackup: " << snapshot.getFullPathName();
    else
        statusLines << "\nBackup: taken automatically before the first write";

    if (! warnings.isEmpty())
    {
        text << "WARNINGS (" << warnings.size() << ")\n";
        for (auto& w : warnings) text << "  ! " << w << "\n";
        text << "\n";
    }
    if (! mapping.isEmpty())
    {
        text << "MAPPING / QUANTISATION REPORT (" << mapping.size() << ")\n";
        for (auto& m : mapping) text << "  " << m << "\n";
    }
    if (text.isEmpty())
        text = session.hasTuneResult() ? "No warnings. Every value landed on the device grid as requested."
                                       : "Connected. Run Auto Tune - the report of what the device grid changed appears here.";
    report.setText (text, false);
}

//==============================================================================
void NativePanel::paint (juce::Graphics& g)
{
    drawSectionLabel (g, "USB devices", listLabelRect);
    g.setColour (scanning ? theme::accent : theme::textDim);
    g.setFont (theme::font (12.0f));
    g.drawText (busy ? busyText : scanText, listLabelRect.withTrimmedLeft (100), juce::Justification::centredRight, true);

    if (devices.empty())
    {
        auto area = list.getBounds().reduced (8);
        g.setColour (theme::textMuted);
        g.setFont (theme::font (13.0f));
        g.drawFittedText (scanning ? "Looking for miniDSP and DSP-408 units..."
                          : everScanned ? "Nothing found.\nSupported: miniDSP 2x4 HD, C-DSP 8x12, DDRC-24, Flex, SHD, 2x4, 10x10 HD... and the Dayton Audio DSP-408."
                                        : "Press Scan for devices.\nSupported: miniDSP 2x4 HD, C-DSP 8x12, DDRC-24, Flex, SHD, 2x4, 10x10 HD... and the Dayton Audio DSP-408.",
                          area, juce::Justification::centred, 5);
    }

    if (! statusRect.isEmpty())
    {
        const bool connected = isConnected();
        theme::fillCard (g, statusRect.toFloat(), theme::bg2, connected ? theme::good.withAlpha (0.6f) : theme::border, theme::radiusSm);
        auto inner = statusRect.reduced (14, 10);
        g.setColour (connected ? theme::good : theme::textMuted);
        g.fillEllipse (inner.removeFromLeft (10).withSizeKeepingCentre (8, 8).withY (inner.getY() + 6).toFloat());
        inner.removeFromLeft (8);
        g.setColour (theme::text);
        g.setFont (theme::fontMedium (14.0f));
        g.drawText (connected ? statusTitle : juce::String ("Not connected"), inner.removeFromTop (20), juce::Justification::centredLeft, true);
        g.setColour (theme::textDim);
        g.setFont (theme::font (12.0f));
        g.drawFittedText (connected ? statusLines
                                    : juce::String ("Direct USB writes the tune straight into the DSP - no vendor software, no clicking. "
                                                    "A backup of the current settings is taken before the first write."),
                          inner.removeFromTop (36), juce::Justification::topLeft, 2);
        if (connected && statusNote.isNotEmpty())
        {
            g.setColour (theme::warn.withAlpha (0.95f));
            g.setFont (theme::font (12.0f));
            g.drawFittedText (statusNote, inner, juce::Justification::topLeft, 3);
        }
    }

    drawSectionLabel (g, "Report & warnings", reportLabelRect);
}

void NativePanel::resized()
{
    auto r = getLocalBounds();
    const int gap = theme::grid;

    auto bar = r.removeFromTop (theme::controlHeight);
    scanBtn.setBounds (bar.removeFromLeft (170)); bar.removeFromLeft (6);
    connectBtn.setBounds (bar.removeFromLeft (150)); bar.removeFromLeft (6);
    if (disconnectBtn.isVisible()) { disconnectBtn.setBounds (bar.removeFromLeft (120)); bar.removeFromLeft (6); }
    if (restoreBtn.isVisible())      restoreBtn.setBounds (bar.removeFromRight (150));
    r.removeFromTop (gap + 4);

    statusRect = r.removeFromTop (isConnected() ? 120 : 76);
    r.removeFromTop (gap);

    listLabelRect = r.removeFromTop (18);
    r.removeFromTop (4);
    const int listH = juce::jlimit (120, 4 * 56 + 8, (int) (r.getHeight() * 0.45));
    list.setBounds (r.removeFromTop (listH));
    r.removeFromTop (gap);

    reportLabelRect = r.removeFromTop (18);
    copyBtn.setBounds (reportLabelRect.removeFromRight (80).withHeight (26).translated (0, -4));
    r.removeFromTop (4);
    report.setBounds (r);
}

void NativePanel::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (row < 0 || row >= (int) devices.size())
        return;
    const auto& d = devices[(size_t) row];
    const bool isLive = isConnected() && connectedName().containsIgnoreCase (d.kind == UsbDevice::Kind::dsp408 ? "DSP-408" : "miniDSP")
                        && list.getSelectedRow() == row;

    auto r = juce::Rectangle<int> (0, 0, width, height).reduced (0, 2).toFloat();
    theme::fillCard (g, r, selected ? theme::bg3 : theme::bg2,
                     isLive ? theme::good.withAlpha (0.7f) : selected ? theme::borderHi : theme::border, theme::radiusSm);

    auto inner = r.reduced (12.0f, 8.0f).toNearestInt();
    auto badge = inner.removeFromRight (110);
    const juce::String badgeText = ! d.supported ? "UNSUPPORTED" : isLive ? "CONNECTED" : "RECOMMENDED";
    const auto badgeColour = ! d.supported ? theme::bad : isLive ? theme::good : theme::accent;
    g.setColour (badgeColour.withAlpha (0.15f));
    g.fillRoundedRectangle (badge.withSizeKeepingCentre (104, 20).toFloat(), 10.0f);
    g.setColour (badgeColour);
    g.setFont (theme::fontMedium (10.5f));
    g.drawText (badgeText, badge, juce::Justification::centred);

    g.setColour (theme::text);
    g.setFont (theme::fontMedium (14.0f));
    g.drawText (d.name, inner.removeFromTop (20), juce::Justification::centredLeft, true);

    juce::String sub = d.detail;
    if (d.serial.isNotEmpty()) sub << juce::String (juce::CharPointer_UTF8 (" · serial ")) << d.serial;
    if (! d.supported && d.reason.isNotEmpty()) sub = d.reason;
    g.setColour (d.supported ? theme::textDim : theme::warn);
    g.setFont (theme::font (11.5f));
    g.drawText (sub, inner, juce::Justification::centredLeft, true);
}

void NativePanel::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    if (row >= 0 && row < (int) devices.size() && devices[(size_t) row].supported && connectBtn.isEnabled() && onConnect)
        onConnect (devices[(size_t) row]);
}
} // namespace fa::ui::dsp
