#pragma once

#include "DspCommon.h"

namespace fa::ui::dsp
{
/** Lists the saved DSP profiles (DspProfile::listProfiles) with Use / Edit / Duplicate / Delete
    and a "Teach new profile" action. */
class ProfilePicker : public juce::Component,
                      private juce::ListBoxModel
{
public:
    ProfilePicker();
    ~ProfilePicker() override;

    void refresh();
    void setActive (const juce::File& profileFile);
    void setCanTeach (bool canTeach, const juce::String& reasonIfNot);

    std::function<void (const DspProfile&, const juce::File&)> onUse;
    std::function<void (const DspProfile&, const juce::File&)> onEdit;
    std::function<void()> onTeachNew;
    /** Asks the host page to confirm a deletion (sheet); the page calls back with the answer. */
    std::function<void (const juce::String& message, std::function<void (bool)> answer)> confirm;
    std::function<void (const juce::String&, AppShell::ToastKind)> toast;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Entry
    {
        juce::File file;
        DspProfile profile;
    };

    int getNumRows() override { return (int) entries.size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void selectedRowsChanged (int) override { updateButtons(); }

    const Entry* selectedEntry() const;
    void updateButtons();

    std::vector<Entry> entries;
    juce::File active;
    juce::ListBox list;
    FaButton teachNew { "Teach new profile", FaButton::Style::primary, icons::Id::plus };
    FaButton use      { "Use",       FaButton::Style::secondary, icons::Id::play };
    FaButton edit     { "Re-teach",  FaButton::Style::ghost,     icons::Id::sliders };
    FaButton dup      { "Duplicate", FaButton::Style::ghost,     icons::Id::copy };
    FaButton remove   { "Delete",    FaButton::Style::danger,    icons::Id::trash };
    bool canTeach = false;
    juce::String teachBlockedReason;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProfilePicker)
};
} // namespace fa::ui::dsp
