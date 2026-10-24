#pragma once

#include "DspCommon.h"

namespace fa::ui::dsp
{
/** Export buttons: text sheet, REW filter files (one per channel), Equalizer APO, miniDSP
    biquads (one per channel), JSON preset. Uses juce::FileChooser::launchAsync + exporters. */
class ExportPanel : public juce::Component
{
public:
    explicit ExportPanel (Session& s);

    std::function<void (const juce::String&, AppShell::ToastKind)> toast;

    static int idealHeight() { return 22 + 3 * theme::controlHeight + 2 * 6; }
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    enum class Kind { text, rew, apo, miniDsp, json };
    void run (Kind k);
    void saveSingle (const juce::String& title, const juce::String& suggestedName, const juce::String& pattern,
                     std::function<juce::String()> makeText);
    void savePerChannel (const juce::String& title, const juce::String& extension,
                         std::function<juce::String (const OutputChannel&)> makeText);
    void report (bool ok, const juce::String& what);

    Session& session;
    FaButton text    { "Text sheet",       FaButton::Style::secondary, icons::Id::document };
    FaButton rew     { "REW filters",      FaButton::Style::secondary, icons::Id::filter };
    FaButton apo     { "Equalizer APO",    FaButton::Style::secondary, icons::Id::sliders };
    FaButton miniDsp { "miniDSP biquads",  FaButton::Style::secondary, icons::Id::chip };
    FaButton json    { "JSON preset",      FaButton::Style::secondary, icons::Id::save };
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExportPanel)
};
} // namespace fa::ui::dsp
