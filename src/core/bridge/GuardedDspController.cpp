#include "GuardedDspController.h"
#include "../tune/CrossoverPlanner.h"
#include <cmath>

namespace fa
{
GuardedDspController::GuardedDspController (DspController& inner, Options o) : wrapped (inner), opt (std::move (o)) {}

void GuardedDspController::note (const juce::String& s)
{
    {
        std::lock_guard<std::mutex> l (m);
        notes.add (s);
    }
    if (onNote)
        onNote (s);
}

juce::StringArray GuardedDspController::takeNotes()
{
    std::lock_guard<std::mutex> l (m);
    auto n = notes;
    notes.clear();
    return n;
}

bool GuardedDspController::sanitize (OutputChannel& ch, juce::StringArray& out) const
{
    bool changed = false;
    auto& s = ch.settings;

    if (ch.role == DriverRole::tweeter && ! ch.isPassiveSet() && ! overrideHp.load())
    {
        const double floorHz = std::max (CrossoverPlanner::safeHighPassHz (ch, opt.planner),
                                         safety::minTweeterHpHz (ch.driver.fsHz));
        if (! s.highPass.enabled || s.highPass.freq < floorHz)
        {
            out.add (ch.label + ": tweeter high-pass raised to " + juce::String (juce::roundToInt (floorHz))
                     + " Hz (protected minimum)");
            s.highPass.enabled = true;
            s.highPass.freq = floorHz;
            changed = true;
        }
    }

    if (s.gainDb > opt.maxGainDb)
    {
        out.add (ch.label + ": gain " + juce::String (s.gainDb, 1) + " dB limited to " + juce::String (opt.maxGainDb, 1) + " dB");
        s.gainDb = opt.maxGainDb;
        changed = true;
    }
    return changed;
}

bool GuardedDspController::applyChannel (const OutputChannel& channel, juce::String& error)
{
    OutputChannel ch = channel;
    juce::StringArray n;
    sanitize (ch, n);
    for (auto& x : n)
        note (x);

    Last prev;
    bool havePrev = false;
    {
        std::lock_guard<std::mutex> l (m);
        auto it = last.find (ch.dspOutput);
        if (it != last.end()) { prev = it->second; havePrev = true; }
    }

    const double finalGain = ch.settings.gainDb;

    // Walk big upward gain changes in capped steps.
    if (havePrev && finalGain > prev.gainDb + opt.maxGainStepDb + 1e-9)
    {
        OutputChannel stepCh = ch;
        for (double g = prev.gainDb + opt.maxGainStepDb; g < finalGain - 1e-9; g += opt.maxGainStepDb)
        {
            stepCh.settings.gainDb = g;
            if (! wrapped.applyChannel (stepCh, error))
                return false;
        }
        note (ch.label + ": gain rise applied in " + juce::String (opt.maxGainStepDb, 1) + " dB steps");
    }

    const bool isTweeter = ch.role == DriverRole::tweeter && ! ch.isPassiveSet();
    const bool xoverChanged = havePrev && (ch.settings.highPass.freq != prev.hpHz || (int) ch.settings.highPass.type != prev.hpType);
    const bool polChanged = havePrev && ch.settings.inverted != prev.inverted;
    bool muted = false;
    if (isTweeter && wrapped.isAutomatic() && ! ch.settings.muted && (xoverChanged || polChanged))
    {
        juce::String ignore;
        muted = wrapped.setMuted (ch, true, ignore);     // controllers without mute simply fail: write anyway
        if (muted)
            note (ch.label + ": muted during crossover/polarity change");
    }

    const bool ok = wrapped.applyChannel (ch, error);
    if (muted)
    {
        juce::String ignore;
        wrapped.setMuted (ch, false, ignore);
    }
    if (ok)
    {
        std::lock_guard<std::mutex> l (m);
        last[ch.dspOutput] = { ch.settings.gainDb, ch.settings.highPass.freq, (int) ch.settings.highPass.type, ch.settings.inverted };
    }
    return ok;
}
} // namespace fa
