#include "ManualPanel.h"

namespace fa::ui::dsp
{
ManualPanel::ManualPanel (Session& s) : session (s)
{
    info.setText ("How manual guided mode works",
                  "Works with every DSP. FalseAudio measures and computes the tune, then shows each change as a "
                  "checklist. You type the values into your DSP software, press Done, and the next measurement starts. "
                  "Below is a live preview of what you would type for the current settings.");
    addAndMakeVisible (info);

    channelBox.onChange = [this] { updatePreview(); };
    addAndMakeVisible (channelBox);

    preview.setMultiLine (true, false);
    preview.setReadOnly (true);
    preview.setScrollbarsShown (true);
    preview.setCaretVisible (false);
    preview.setFont (theme::mono (12.5f));
    preview.setColour (juce::TextEditor::backgroundColourId, theme::bg1);
    preview.setColour (juce::TextEditor::outlineColourId, theme::border);
    preview.setColour (juce::TextEditor::focusedOutlineColourId, theme::accentDim);
    preview.setColour (juce::TextEditor::textColourId, theme::text);
    preview.setColour (juce::TextEditor::highlightColourId, theme::accent.withAlpha (0.3f));
    addAndMakeVisible (preview);

    copy.onClick = [this]
    {
        juce::SystemClipboard::copyTextToClipboard (preview.getText());
        if (toast) toast ("Channel values copied", AppShell::ToastKind::good);
    };
    addAndMakeVisible (copy);

    copyAll.onClick = [this]
    {
        juce::String all;
        for (auto& ch : session.getConfig().channels)
        {
            all << ch.label << " (out " << ch.dspOutput + 1 << ")\n";
            for (auto& line : ManualDspController::describeChannel (ch))
                all << "  " << line << "\n";
            all << "\n";
        }
        juce::SystemClipboard::copyTextToClipboard (all);
        if (toast) toast ("All channels copied", AppShell::ToastKind::good);
    };
    addAndMakeVisible (copyAll);

    refresh();
}

void ManualPanel::refresh()
{
    const auto& chans = session.getConfig().channels;
    const auto previous = channelBox.getSelectedId();
    channelBox.clear (juce::dontSendNotification);
    for (size_t i = 0; i < chans.size(); ++i)
        channelBox.addItem (chans[i].label + "  (out " + juce::String (chans[i].dspOutput + 1) + ")", (int) i + 1);
    if (previous > 0 && previous <= (int) chans.size())
        channelBox.setSelectedId (previous, juce::dontSendNotification);
    else if (! chans.empty())
        channelBox.setSelectedId (1, juce::dontSendNotification);
    updatePreview();
}

void ManualPanel::updatePreview()
{
    const auto& chans = session.getConfig().channels;
    const int idx = channelBox.getSelectedId() - 1;
    if (idx < 0 || idx >= (int) chans.size())
    {
        preview.setText ("No output channels configured.\nSet up your system on the System page.", false);
        return;
    }
    const auto lines = ManualDspController::describeChannel (chans[(size_t) idx]);
    preview.setText (lines.joinIntoString ("\n"), false);
}

void ManualPanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    r.removeFromTop (info.getHeight() + theme::grid);
    drawSectionLabel (g, "Preview: values for one output", r.removeFromTop (18));
}

void ManualPanel::resized()
{
    auto r = getLocalBounds();
    info.setBounds (r.removeFromTop (std::min (info.idealHeight (r.getWidth()), r.getHeight() / 2)));
    r.removeFromTop (theme::grid);
    r.removeFromTop (22);

    auto row = r.removeFromTop (theme::controlHeight);
    channelBox.setBounds (row);
    r.removeFromTop (theme::grid);

    auto bottom = r.removeFromBottom (theme::controlHeight);
    copy.setBounds (bottom.removeFromLeft (90));
    bottom.removeFromLeft (6);
    copyAll.setBounds (bottom.removeFromLeft (170));
    r.removeFromBottom (theme::grid);
    preview.setBounds (r);
}
} // namespace fa::ui::dsp
