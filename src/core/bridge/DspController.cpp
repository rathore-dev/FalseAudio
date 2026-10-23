#include "DspController.h"

namespace fa
{
bool DspController::applyAll (const SystemConfig& cfg, juce::String& error)
{
    if (! beginBatch (error))
        return false;
    for (auto& ch : cfg.channels)
        if (! applyChannel (ch, error))
            return false;
    return endBatch (error);
}

bool DspController::soloChannels (const SystemConfig& cfg, const juce::StringArray& ids, juce::String& error)
{
    if (! beginBatch (error))
        return false;
    for (auto& ch : cfg.channels)
        if (! setMuted (ch, ! ids.contains (ch.id), error))
            return false;
    return endBatch (error);
}

//==============================================================================
ManualDspController::ManualDspController (DspCapabilities c, AskUser a)
    : caps (std::move (c)), askUser (std::move (a))
{
}

juce::StringArray ManualDspController::describeChannel (const OutputChannel& ch)
{
    const auto& s = ch.settings;
    juce::StringArray lines;
    const juce::String out = "Output " + juce::String (ch.dspOutput + 1) + " (" + ch.label + ")";

    lines.add (out + ": Gain " + juce::String (s.gainDb, 1) + " dB, Delay " + juce::String (s.delayMs, 2)
               + " ms (" + juce::String (msToMetres (s.delayMs) * 100.0, 1) + " cm), Phase "
               + (s.inverted ? "180 deg" : "0 deg") + (s.muted ? ", MUTED" : ""));

    if (s.highPass.enabled)
        lines.add ("    HP: " + juce::String (s.highPass.freq, 0) + " Hz, " + juce::String (s.highPass.slopeDbPerOct)
                   + " dB/oct " + toString (s.highPass.alignment));
    if (s.lowPass.enabled)
        lines.add ("    LP: " + juce::String (s.lowPass.freq, 0) + " Hz, " + juce::String (s.lowPass.slopeDbPerOct)
                   + " dB/oct " + toString (s.lowPass.alignment));

    for (size_t i = 0; i < s.peq.size(); ++i)
        lines.add ("    EQ " + juce::String ((int) i + 1) + ": " + s.peq[i].describe());

    return lines;
}

bool ManualDspController::beginBatch (juce::String&)
{
    batching = true;
    pending.clear();
    pendingTitle = "Update your DSP";
    return true;
}

bool ManualDspController::endBatch (juce::String& error)
{
    batching = false;
    return flush (pendingTitle, error);
}

bool ManualDspController::flush (const juce::String& title, juce::String& error)
{
    if (pending.isEmpty())
        return true;

    const auto steps = pending;
    pending.clear();

    if (askUser == nullptr || ! askUser (title, steps))
    {
        error = "Cancelled by user.";
        return false;
    }
    return true;
}

bool ManualDspController::applyChannel (const OutputChannel& ch, juce::String& error)
{
    pending.addArray (describeChannel (ch));
    return batching ? true : flush ("Update " + ch.label, error);
}

bool ManualDspController::soloChannels (const SystemConfig& cfg, const juce::StringArray& ids, juce::String& error)
{
    juce::StringArray playing, muted;
    for (auto& ch : cfg.channels)
        (ids.contains (ch.id) ? playing : muted).add (ch.label + " (out " + juce::String (ch.dspOutput + 1) + ")");

    pending.add ("PLAY ONLY:  " + playing.joinIntoString (", "));
    if (! muted.isEmpty())
        pending.add ("MUTE:       " + muted.joinIntoString (", "));
    pendingTitle = "Set up the next measurement";
    return batching ? true : flush (pendingTitle, error);
}

bool ManualDspController::setMuted (const OutputChannel& ch, bool muted, juce::String& error)
{
    pending.add ((muted ? "MUTE   " : "UNMUTE ") + juce::String ("output ") + juce::String (ch.dspOutput + 1) + " (" + ch.label + ")");
    if (! batching)
    {
        pendingTitle = muted ? "Mute a channel" : "Unmute a channel";
        return flush (pendingTitle, error);
    }
    pendingTitle = "Set up the next measurement";
    return true;
}
} // namespace fa
