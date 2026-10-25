#include "InstallationStore.h"

namespace fa::ui
{
InstallationStore& InstallationStore::instance()
{
    static InstallationStore store;
    return store;
}

InstallationStore::InstallationStore()
{
    load();
}

InstallationStore::~InstallationStore()
{
    stopTimer();
    if (dirty)
        flush();
}

juce::File InstallationStore::getFile() const
{
    auto folder = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("FalseAudio");
    folder.createDirectory();
    return folder.getChildFile ("installation.json");
}

void InstallationStore::load()
{
    const auto f = getFile();
    if (! f.existsAsFile())
        return;

    const auto v = juce::JSON::parse (f);
    if (v.isObject())
        installation = install::Installation::fromVar (v);
}

void InstallationStore::set (const install::Installation& inst)
{
    installation = inst;
    dirty = true;
    startTimer (600);               // coalesce bursts of edits into one write
    sendChangeMessage();
}

void InstallationStore::edit (const std::function<void (install::Installation&)>& fn)
{
    auto copy = installation;
    fn (copy);
    set (copy);
}

void InstallationStore::timerCallback()
{
    stopTimer();
    flush();
}

void InstallationStore::flush()
{
    if (! dirty)
        return;
    dirty = false;

    const auto f = getFile();
    juce::TemporaryFile tmp (f);
    {
        juce::FileOutputStream out (tmp.getFile());
        if (! out.openedOk())
            return;
        juce::JSON::writeToStream (out, installation.toVar(), juce::JSON::FormatOptions().withSpacing (juce::JSON::Spacing::multiLine));
    }
    tmp.overwriteTargetFileWithTemporary();
}
} // namespace fa::ui
