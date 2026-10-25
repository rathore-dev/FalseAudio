#include "ProfilePicker.h"

namespace fa::ui::dsp
{
ProfilePicker::ProfilePicker()
{
    list.setModel (this);
    list.setRowHeight (56);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    list.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    list.getViewport()->setScrollBarsShown (true, false);
    addAndMakeVisible (list);

    for (auto* b : { &teachNew, &use, &edit, &dup, &remove })
        addAndMakeVisible (b);

    teachNew.onClick = [this] { if (onTeachNew) onTeachNew(); };

    use.onClick = [this]
    {
        if (auto* e = selectedEntry())
            if (onUse) onUse (e->profile, e->file);
    };

    edit.onClick = [this]
    {
        if (auto* e = selectedEntry())
            if (onEdit) onEdit (e->profile, e->file);
    };

    dup.onClick = [this]
    {
        if (auto* e = selectedEntry())
        {
            auto copy = e->profile;
            copy.name = e->profile.name + " copy";
            auto f = profileFileFor (copy.name);
            int n = 2;
            while (f.existsAsFile())
            {
                copy.name = e->profile.name + " copy " + juce::String (n++);
                f = profileFileFor (copy.name);
            }
            if (copy.save (f))
            {
                refresh();
                if (toast) toast ("Duplicated as \"" + copy.name + "\"", AppShell::ToastKind::good);
            }
            else if (toast)
                toast ("Could not write " + f.getFullPathName(), AppShell::ToastKind::bad);
        }
    };

    remove.onClick = [this]
    {
        auto* e = selectedEntry();
        if (e == nullptr)
            return;
        const auto file = e->file;
        const auto name = e->profile.name;
        auto doDelete = [this, file, name] (bool yes)
        {
            if (! yes)
                return;
            if (file.deleteFile())
            {
                if (active == file)
                    active = {};
                refresh();
                if (toast) toast ("Deleted profile \"" + name + "\"", AppShell::ToastKind::info);
            }
            else if (toast)
                toast ("Could not delete " + file.getFileName(), AppShell::ToastKind::bad);
        };
        if (confirm != nullptr)
            confirm ("Delete the DSP profile \"" + name + "\"? This cannot be undone.", doDelete);
        else
            doDelete (true);
    };

    refresh();
}

ProfilePicker::~ProfilePicker()
{
    list.setModel (nullptr);
}

void ProfilePicker::refresh()
{
    const auto previouslySelected = selectedEntry() != nullptr ? selectedEntry()->file : juce::File();
    entries.clear();
    for (auto& f : DspProfile::listProfiles())
    {
        Entry e;
        e.file = f;
        if (DspProfile::load (f, e.profile))
            entries.push_back (e);
    }
    std::sort (entries.begin(), entries.end(), [] (auto& a, auto& b) { return a.profile.name.compareIgnoreCase (b.profile.name) < 0; });

    list.updateContent();
    int sel = -1;
    for (size_t i = 0; i < entries.size(); ++i)
        if (entries[i].file == previouslySelected || (sel < 0 && entries[i].file == active))
            sel = (int) i;
    if (sel < 0 && ! entries.empty())
        sel = 0;
    list.selectRow (sel);
    updateButtons();
    repaint();
}

void ProfilePicker::setActive (const juce::File& f)
{
    active = f;
    list.repaint();
    updateButtons();
}

void ProfilePicker::setCanTeach (bool c, const juce::String& reason)
{
    canTeach = c;
    teachBlockedReason = reason;
    updateButtons();
    repaint();
}

const ProfilePicker::Entry* ProfilePicker::selectedEntry() const
{
    const int row = list.getSelectedRow();
    return row >= 0 && row < (int) entries.size() ? &entries[(size_t) row] : nullptr;
}

void ProfilePicker::updateButtons()
{
    const auto* e = selectedEntry();
    const bool has = e != nullptr;
    use.setEnabled (has && canTeach && (e == nullptr || e->file != active));
    use.setButtonText (has && e->file == active ? "Active" : "Use");
    edit.setEnabled (has && canTeach);
    dup.setEnabled (has);
    remove.setEnabled (has);
    teachNew.setEnabled (canTeach);
}

void ProfilePicker::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    drawSectionLabel (g, "Saved DSP profiles", r.removeFromTop (18));

    if (entries.empty())
    {
        auto area = list.getBounds().reduced (8);
        g.setColour (theme::textMuted);
        g.setFont (theme::font (13.0f));
        g.drawFittedText ("No profiles yet.\nAttach your DSP software and teach it once - the profile is reusable for every car with the same DSP.",
                          area, juce::Justification::centred, 4);
    }

    if (! canTeach && teachBlockedReason.isNotEmpty())
    {
        g.setColour (theme::warn.withAlpha (0.9f));
        g.setFont (theme::font (12.0f));
        auto hint = getLocalBounds().removeFromBottom (18);
        g.drawFittedText (teachBlockedReason, hint, juce::Justification::centredLeft, 1);
    }
}

void ProfilePicker::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (22);

    auto bottom = r.removeFromBottom (canTeach ? 32 : 32 + 20);
    if (! canTeach)
        bottom.removeFromBottom (20);
    teachNew.setBounds (bottom);
    r.removeFromBottom (theme::grid);

    auto actions = r.removeFromBottom (theme::controlHeight);
    const int gap = 6;
    const int w = (actions.getWidth() - 3 * gap) / 4;
    use.setBounds (actions.removeFromLeft (w));    actions.removeFromLeft (gap);
    edit.setBounds (actions.removeFromLeft (w));   actions.removeFromLeft (gap);
    dup.setBounds (actions.removeFromLeft (w));    actions.removeFromLeft (gap);
    remove.setBounds (actions);
    r.removeFromBottom (theme::grid);

    list.setBounds (r);
}

void ProfilePicker::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (row < 0 || row >= (int) entries.size())
        return;
    const auto& e = entries[(size_t) row];
    const bool isActive = e.file == active;

    auto r = juce::Rectangle<int> (0, 0, width, height).reduced (0, 2).toFloat();
    theme::fillCard (g, r, selected ? theme::bg3 : theme::bg2, isActive ? theme::accent.withAlpha (0.7f) : (selected ? theme::borderHi : theme::border), theme::radiusSm);

    auto inner = r.reduced (12.0f, 8.0f).toNearestInt();
    auto right = inner.removeFromRight (isActive ? 64 : 0);
    if (isActive)
    {
        g.setColour (theme::accent.withAlpha (0.15f));
        g.fillRoundedRectangle (right.withSizeKeepingCentre (60, 20).toFloat(), 10.0f);
        g.setColour (theme::accent);
        g.setFont (theme::fontMedium (10.5f));
        g.drawText ("ACTIVE", right, juce::Justification::centred);
    }

    g.setColour (theme::text);
    g.setFont (theme::fontMedium (14.0f));
    g.drawText (e.profile.name, inner.removeFromTop (20), juce::Justification::centredLeft, true);

    juce::String sub = e.profile.processName.isNotEmpty() ? e.profile.processName : juce::String ("any process");
    sub << juce::String (juce::CharPointer_UTF8 (" · ")) << (int) e.profile.bindings.size() << " controls · " << describeCaps (e.profile.caps);
    g.setColour (theme::textDim);
    g.setFont (theme::font (11.5f));
    g.drawText (sub, inner, juce::Justification::centredLeft, true);
}

void ProfilePicker::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    if (row >= 0 && row < (int) entries.size() && onUse && canTeach)
        onUse (entries[(size_t) row].profile, entries[(size_t) row].file);
}
} // namespace fa::ui::dsp
