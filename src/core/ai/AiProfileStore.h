#pragma once

#include "../bridge/DspProfile.h"

namespace fa
{
/** Saves and finds AI-learned maps, in the normal DspProfile JSON format, under
    %APPDATA%\FalseAudio\profiles (one file per vendor window title). The classic
    UIAutomationController can load these files too. */
class AiProfileStore
{
public:
    explicit AiProfileStore (juce::File folder = {});

    static juce::File defaultFolder();
    juce::File getFolder() const { return dir; }

    /** File name derived from the window title (sanitised), e.g. "ai_Helix PC-Tool.json". */
    juce::File fileFor (const juce::String& windowTitle) const;

    bool save (const DspProfile& profile, const juce::String& windowTitle) const;
    bool load (const juce::String& windowTitle, DspProfile& out) const;
    bool remove (const juce::String& windowTitle) const;

    /** THE resolver for "which map belongs to this window": the file saved under the exact title, else any ai_*.json whose
        saved identifying fragment (DspProfile::windowTitleContains, chosen by the AI) is part of the title. Load, delete and
        summary all go through it, so Relearn / Delete act on the very file that gets replayed. */
    juce::Array<juce::File> resolve (const juce::String& windowTitle) const;
    bool loadFor (const juce::String& windowTitle, DspProfile& out) const;
    /** Deletes every file resolve() finds. True if at least one was removed (or none existed). */
    bool removeFor (const juce::String& windowTitle) const;

private:
    juce::File dir;
};
} // namespace fa
