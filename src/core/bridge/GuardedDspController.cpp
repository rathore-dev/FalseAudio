#include "GuardedDspController.h"
#include "../tune/CrossoverPlanner.h"
#include <cmath>

namespace fa
{
GuardedDspController::GuardedDspController (DspController& inner, Options o) : wrapped (inner), opt (std::move (o)) {}
GuardedDspController::GuardedDspController (std::shared_ptr<DspController> inner, Options o)
    : owner (inner), wrapped (*inner), opt (std::move (o)) {}

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

namespace
{
    bool finiteSpec (const FilterSpec& f) { return std::isfinite (f.freq) && std::isfinite (f.q) && std::isfinite (f.gainDb); }

    bool allFinite (const OutputChannel& ch)
    {
        const auto& s = ch.settings;
        if (! std::isfinite (s.gainDb) || ! std::isfinite (s.delayMs) || ! finiteSpec (s.highPass) || ! finiteSpec (s.lowPass))
            return false;
        for (auto& f : s.peq)
            if (! finiteSpec (f))
                return false;
        return true;
    }
}

SafetyLimits GuardedDspController::getPlannerLimits() const
{
    std::lock_guard<std::mutex> l (m);
    return opt.planner;
}

void GuardedDspController::setPlannerLimits (const SafetyLimits& s)
{
    std::lock_guard<std::mutex> l (m);
    opt.planner = s;
}

bool GuardedDspController::sanitize (OutputChannel& ch, juce::StringArray& out) const
{
    bool changed = false;
    auto& s = ch.settings;
    const auto limits = getPlannerLimits();

    if (ch.role == DriverRole::tweeter && ! ch.isPassiveSet() && ! overrideHp.load())
    {
        const double floorHz = std::max (CrossoverPlanner::safeHighPassHz (ch, limits),
                                         safety::minTweeterHpHz (ch.driver.fsHz));
        if (! s.highPass.enabled || ! std::isfinite (s.highPass.freq) || s.highPass.freq < floorHz)
        {
            out.add (ch.label + ": tweeter high-pass raised to " + juce::String (juce::roundToInt (floorHz))
                     + " Hz (protected minimum)");
            s.highPass.enabled = true;
            s.highPass.freq = floorHz;
            changed = true;
        }
        if (s.highPass.slopeDbPerOct < safety::kMinTweeterHpSlopeDbPerOct)
        {
            out.add (ch.label + ": tweeter high-pass slope raised to " + juce::String (safety::kMinTweeterHpSlopeDbPerOct)
                     + " dB/oct (protected minimum)");
            s.highPass.slopeDbPerOct = safety::kMinTweeterHpSlopeDbPerOct;
            changed = true;
        }
    }

    if (! std::isfinite (s.gainDb) || s.gainDb > opt.maxGainDb)
    {
        out.add (ch.label + ": gain " + juce::String (s.gainDb, 1) + " dB limited to " + juce::String (opt.maxGainDb, 1) + " dB");
        s.gainDb = opt.maxGainDb;
        changed = true;
    }
    return changed;
}

bool GuardedDspController::applyChannel (const OutputChannel& channel, juce::String& error)
{
    if (! allFinite (channel))
    {
        error = channel.label + ": refused - a setting is NaN/infinite";
        note (error);
        return false;
    }

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
    const bool isTweeter = ch.role == DriverRole::tweeter && ! ch.isPassiveSet();
    const bool xoverChanged = havePrev && (ch.settings.highPass.freq != prev.hpHz || (int) ch.settings.highPass.type != prev.hpType);
    const bool polChanged = havePrev && ch.settings.inverted != prev.inverted;
    const bool riskyChange = isTweeter && (xoverChanged || polChanged);
    const bool ramp = havePrev && finalGain > prev.gainDb + opt.maxGainStepDb + 1e-9;

    // A tweeter crossover/polarity change is never written live: mute first. Where the controller cannot
    // mute, the new crossover is written BEFORE any gain increase (at the previous, not higher, gain).
    bool muted = false;
    if (riskyChange && ! ch.settings.muted && wrapped.isAutomatic())
    {
        juce::String ignore;
        muted = wrapped.setMuted (ch, true, ignore);
        if (muted)
            note (ch.label + ": muted during crossover/polarity change");
    }

    OutputChannel writeCh = ch;
    if (muted)
        writeCh.settings.muted = true;      // every write while muted keeps it muted

    if (riskyChange && ! muted && ramp)
    {
        OutputChannel first = writeCh;
        first.settings.gainDb = std::min (prev.gainDb, finalGain);
        if (! wrapped.applyChannel (first, error))
            return false;
    }

    // Walk big upward gain changes in capped steps.
    if (ramp)
    {
        OutputChannel stepCh = writeCh;
        for (double g = prev.gainDb + opt.maxGainStepDb; g < finalGain - 1e-9; g += opt.maxGainStepDb)
        {
            stepCh.settings.gainDb = g;
            if (! wrapped.applyChannel (stepCh, error))
                return false;           // stays muted if we muted it
        }
        note (ch.label + ": gain rise applied in " + juce::String (opt.maxGainStepDb, 1) + " dB steps");
    }

    const bool ok = wrapped.applyChannel (writeCh, error);
    if (muted && ok)
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
