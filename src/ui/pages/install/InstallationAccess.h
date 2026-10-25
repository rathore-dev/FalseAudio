#pragma once

/** The Install page reads the current install::Installation from the store owned by the Equipment
    page (src/ui/pages/gear/InstallationStore.h, API: instance().get() / .set(), ChangeBroadcaster).
    Until that header lands this falls back to a tiny file-backed store with the identical API so
    the page still compiles and can be exercised. */
#if __has_include("../gear/InstallationStore.h")
 #include "../gear/InstallationStore.h"
#else
 #include <JuceHeader.h>
 #include "core/install/InstallPlanner.h"

namespace fa::ui
{
class InstallationStore : public juce::ChangeBroadcaster
{
public:
    static InstallationStore& instance()
    {
        static InstallationStore s;
        return s;
    }

    const install::Installation& get() const noexcept { return inst; }

    void set (const install::Installation& i)
    {
        inst = i;
        file().replaceWithText (juce::JSON::toString (inst.toVar(), true));
        sendChangeMessage();
    }

private:
    InstallationStore()
    {
        if (file().existsAsFile())
            inst = install::Installation::fromVar (juce::JSON::parse (file()));
    }
    static juce::File file()
    {
        auto f = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                     .getChildFile ("FalseAudio").getChildFile ("installation.json");
        f.getParentDirectory().createDirectory();
        return f;
    }
    install::Installation inst;
};
} // namespace fa::ui
#endif
