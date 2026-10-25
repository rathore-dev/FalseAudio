#pragma once

#include <JuceHeader.h>
#include "core/install/InstallPlanner.h"

namespace fa::ui
{
/** The one place the UI keeps the user's physical installation (products + wiring).

    Shared by the Equipment page (writes) and the Install & Gains page (reads). Persisted as JSON in
    <appdata>/FalseAudio/installation.json, loaded lazily on first use and saved shortly after every
    set() (coalesced on a timer so rapid edits never hammer the disk).

    Listeners (juce::ChangeListener) are notified on the message thread after every set(). */
class InstallationStore : public juce::ChangeBroadcaster,
                          private juce::Timer
{
public:
    static InstallationStore& instance();

    const install::Installation& get() const noexcept { return installation; }

    /** Replaces the installation, schedules a save and broadcasts a change. */
    void set (const install::Installation& inst);

    /** Convenience: edit in place, then set(). */
    void edit (const std::function<void (install::Installation&)>& fn);

    /** Where the JSON lives. */
    juce::File getFile() const;

    /** Writes immediately (also called from the destructor). */
    void flush();

private:
    InstallationStore();
    ~InstallationStore() override;

    void load();
    void timerCallback() override;

    install::Installation installation;
    bool dirty = false;

    JUCE_DECLARE_NON_COPYABLE (InstallationStore)
};
} // namespace fa::ui
