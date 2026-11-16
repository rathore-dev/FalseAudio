#include "AiProfileStore.h"

namespace fa
{
AiProfileStore::AiProfileStore (juce::File folder) : dir (std::move (folder))
{
    if (dir == juce::File())
        dir = defaultFolder();
}

juce::File AiProfileStore::defaultFolder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("FalseAudio").getChildFile ("profiles");
}

juce::File AiProfileStore::fileFor (const juce::String& title) const
{
    auto name = juce::File::createLegalFileName (title.trim().isEmpty() ? juce::String ("dsp") : title.trim());
    return dir.getChildFile ("ai_" + name.substring (0, 80) + ".json");
}

bool AiProfileStore::save (const DspProfile& profile, const juce::String& windowTitle) const
{
    return profile.save (fileFor (windowTitle));
}

bool AiProfileStore::load (const juce::String& windowTitle, DspProfile& out) const
{
    const auto f = fileFor (windowTitle);
    return f.existsAsFile() && DspProfile::load (f, out);
}

bool AiProfileStore::remove (const juce::String& windowTitle) const
{
    return fileFor (windowTitle).deleteFile();
}
} // namespace fa
