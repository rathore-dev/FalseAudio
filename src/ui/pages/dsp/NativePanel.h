#pragma once

#include "DspCommon.h"
#include "core/bridge/native/MiniDspController.h"
#include "core/bridge/native/Dsp408Controller.h"

namespace fa::ui::dsp
{
/** One USB device found by a scan (miniDSP or Dayton DSP-408). */
struct UsbDevice
{
    enum class Kind { miniDsp, dsp408 };
    Kind kind = Kind::miniDsp;
    juce::String path, name, serial, detail, reason;
    bool supported = false;
    bool needsSampleRate = false;   // C-DSP 8x12 v2 and friends: internal rate is a user setting
};

/** Small sheet asking for the internal sample rate of a miniDSP that does not report it. */
class SampleRateSheet : public juce::Component
{
public:
    explicit SampleRateSheet (const juce::String& deviceName);
    std::function<void (double)> onResult;    // 0 = cancelled
    static constexpr int width = 520, height = 236;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::String deviceName;
    SegmentedControl rates { { "48 kHz", "96 kHz" } };
    FaButton ok { "Connect", FaButton::Style::primary, icons::Id::plug };
    FaButton cancel { "Cancel", FaButton::Style::ghost };
};

/** Direct USB mode: scan list, Connect / Disconnect / Restore backup, device status, and the
    mapping / quantisation report with warnings after a tune. All device IO runs off the
    message thread. */
class NativePanel : public juce::Component,
                    private juce::ListBoxModel
{
public:
    explicit NativePanel (Session& s);
    ~NativePanel() override;

    void scan();                                 // detect() on a background thread
    void refresh();                              // re-read connection state / report (sessionChanged)
    bool isBusy() const noexcept { return busy; }
    void setBusy (bool b, const juce::String& what);
    std::optional<UsbDevice> getSelected() const;
    juce::String getConnectedPath() const;

    std::function<void (const UsbDevice&)> onConnect;
    std::function<void()> onDisconnect;
    std::function<void()> onRestore;
    std::function<void (const juce::String&, AppShell::ToastKind)> toast;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    int getNumRows() override { return (int) devices.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void selectedRowsChanged (int) override { updateButtons(); }
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;

    void updateButtons();
    void updateReport();
    bool isConnected() const;
    juce::String connectedName() const;

    Session& session;
    std::vector<UsbDevice> devices;
    bool scanning = false, busy = false, everScanned = false;
    juce::String busyText, scanText;
    juce::ListBox list;
    FaButton scanBtn    { "Scan for devices", FaButton::Style::secondary, icons::Id::refresh };
    FaButton connectBtn { "Connect",          FaButton::Style::primary,   icons::Id::plug };
    FaButton disconnectBtn { "Disconnect",    FaButton::Style::ghost,     icons::Id::unlink };
    FaButton restoreBtn { "Restore backup",   FaButton::Style::danger,    icons::Id::restore };
    FaButton copyBtn    { "Copy",             FaButton::Style::ghost,     icons::Id::copy };
    juce::TextEditor report;
    juce::Rectangle<int> statusRect, listLabelRect, reportLabelRect;
    juce::String statusTitle, statusLines, statusNote;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NativePanel)
};
} // namespace fa::ui::dsp
