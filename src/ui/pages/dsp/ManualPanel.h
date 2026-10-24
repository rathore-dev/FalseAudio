#pragma once

#include "DspCommon.h"

namespace fa::ui::dsp
{
/** Manual guided mode: explains the checklist flow and previews the current config's values
    for one channel (ManualDspController::describeChannel) in a monospace card with Copy. */
class ManualPanel : public juce::Component
{
public:
    explicit ManualPanel (Session& s);

    void refresh();     // call on sessionChanged
    std::function<void (const juce::String&, AppShell::ToastKind)> toast;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void updatePreview();

    Session& session;
    InfoPanel info { icons::Id::document };
    juce::ComboBox channelBox;
    juce::TextEditor preview;
    FaButton copy { "Copy", FaButton::Style::ghost, icons::Id::copy };
    FaButton copyAll { "Copy all channels", FaButton::Style::ghost, icons::Id::document };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ManualPanel)
};
} // namespace fa::ui::dsp
