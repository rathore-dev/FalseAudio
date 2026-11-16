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

juce::Array<juce::File> AiProfileStore::resolve (const juce::String& windowTitle) const
{
    juce::Array<juce::File> found;
    const auto exact = fileFor (windowTitle);
    DspProfile p;
    if (exact.existsAsFile() && DspProfile::load (exact, p) && p.aiLearned)
        found.add (exact);
    for (auto& f : dir.findChildFiles (juce::File::findFiles, false, "ai_*.json"))
        if (f != exact && DspProfile::load (f, p) && p.aiLearned && p.windowTitleContains.isNotEmpty()
            && windowTitle.containsIgnoreCase (p.windowTitleContains))
            found.add (f);
    return found;
}

bool AiProfileStore::loadFor (const juce::String& windowTitle, DspProfile& out) const
{
    for (auto& f : resolve (windowTitle))
        if (DspProfile::load (f, out))
            return true;
    return false;
}

bool AiProfileStore::removeFor (const juce::String& windowTitle) const
{
    bool all = true;
    for (auto& f : resolve (windowTitle))
        all = f.deleteFile() && all;
    return all;
}
} // namespace fa
