#include "SafetyGuard.h"
#include "../tune/CrossoverPlanner.h"
#include <cmath>

namespace fa
{
SafetyGuard::SafetyGuard (Limits l, DspCapabilities c) : limits (std::move (l)), caps (std::move (c)) {}

void SafetyGuard::kill (const juce::String& why)
{
    std::lock_guard<std::mutex> lock (m);
    killWhy = why;
    killed = true;
}

juce::String SafetyGuard::killReason() const
{
    std::lock_guard<std::mutex> lock (m);
    return killWhy;
}

void SafetyGuard::resetSession()
{
    std::lock_guard<std::mutex> lock (m);
    killed = false;
    killWhy = {};
    actions = 0;
    usd = 0.0;
    tokens = 0;
    muted.clear();
    gains.clear();
}

SafetyGuard::Verdict SafetyGuard::checkBudget() const
{
    if (killed.load())
        return Verdict::deny ("Kill switch active: " + killReason());
    if (actions.load() >= limits.maxActions)
        return Verdict::deny ("Action budget exhausted (" + juce::String (limits.maxActions) + " actions).");
    std::lock_guard<std::mutex> lock (m);
    if (usd >= limits.maxUsd)
        return Verdict::deny ("Cost budget exhausted (" + juce::String (limits.maxUsd, 2) + " USD).");
    if (tokens >= limits.maxTokens)
        return Verdict::deny ("Token budget exhausted.");
    return Verdict::allow();
}

SafetyGuard::Verdict SafetyGuard::beginAction (const juce::String& what)
{
    auto v = checkBudget();
    if (! v)
        return v;
    ++actions;
    juce::ignoreUnused (what);
    return Verdict::allow();
}

void SafetyGuard::addUsage (const ClaudeUsage& u, double cost)
{
    std::lock_guard<std::mutex> lock (m);
    usd += cost;
    tokens += u.total();
}

double SafetyGuard::usdSpent() const { std::lock_guard<std::mutex> lock (m); return usd; }
juce::int64 SafetyGuard::tokensUsed() const { std::lock_guard<std::mutex> lock (m); return tokens; }

//==============================================================================
SafetyGuard::Verdict SafetyGuard::checkPointInWindow (juce::Point<int> rel, juce::Rectangle<int> w)
{
    if (w.isEmpty())
        return Verdict::deny ("No window attached.");
    if (rel.x < 0 || rel.y < 0 || rel.x >= w.getWidth() || rel.y >= w.getHeight())
        return Verdict::deny ("Point (" + juce::String (rel.x) + "," + juce::String (rel.y)
                              + ") is outside the attached window (" + juce::String (w.getWidth()) + "x"
                              + juce::String (w.getHeight()) + ").");
    return Verdict::allow();
}

SafetyGuard::Verdict SafetyGuard::checkElementInWindow (juce::Rectangle<int> e, juce::Rectangle<int> w)
{
    if (w.isEmpty())
        return Verdict::deny ("No window attached.");
    if (! w.expanded (2).contains (e.getCentre()))
        return Verdict::deny ("Element is outside the attached window.");
    return Verdict::allow();
}

SafetyGuard::Verdict SafetyGuard::checkKeyCombo (const juce::String& combo)
{
    const auto c = combo.toLowerCase().removeCharacters (" ");
    const auto parts = juce::StringArray::fromTokens (c, "+", "");
    const bool alt = parts.contains ("alt"), ctrl = parts.contains ("ctrl") || parts.contains ("control");
    const bool win = parts.contains ("win") || parts.contains ("super") || parts.contains ("meta");
    if (win)
        return Verdict::deny ("Windows-key combinations are not allowed.");
    if (alt && (parts.contains ("f4") || parts.contains ("tab") || parts.contains ("esc")))
        return Verdict::deny ("That key combination could leave the attached window.");
    if (ctrl && alt && (parts.contains ("delete") || parts.contains ("del")))
        return Verdict::deny ("Not allowed.");
    if (ctrl && (parts.contains ("w") || parts.contains ("q")))
        return Verdict::deny ("Closing combinations are not allowed.");
    if (parts.isEmpty() || c.isEmpty())
        return Verdict::deny ("Empty key combination.");
    return Verdict::allow();
}

//==============================================================================
void SafetyGuard::setChannels (const std::vector<OutputChannel>& list)
{
    std::lock_guard<std::mutex> lock (m);
    channels.clear();
    for (auto& ch : list)
    {
        ChannelInfo ci;
        ci.role = ch.role;
        ci.label = ch.label;
        ci.protectedHp = CrossoverPlanner::safeHighPassHz (ch, limits.planner);
        channels[ch.dspOutput] = ci;
    }
}

double SafetyGuard::protectedHpHz (const OutputChannel& ch) const
{
    return CrossoverPlanner::safeHighPassHz (ch, limits.planner);
}

void SafetyGuard::noteMute (int output, bool isMutedNow) { std::lock_guard<std::mutex> lock (m); muted[output] = isMutedNow; }
bool SafetyGuard::isMuted (int output) const
{
    std::lock_guard<std::mutex> lock (m);
    auto it = muted.find (output);
    return it != muted.end() && it->second;
}
void SafetyGuard::noteGain (int output, double g) { std::lock_guard<std::mutex> lock (m); gains[output] = g; }
bool SafetyGuard::knownGain (int output, double& g) const
{
    std::lock_guard<std::mutex> lock (m);
    auto it = gains.find (output);
    if (it == gains.end())
        return false;
    g = it->second;
    return true;
}

//==============================================================================
bool SafetyGuard::sanitizeChannel (OutputChannel& ch, juce::StringArray& notes)
{
    bool changed = false;
    auto& s = ch.settings;
    const juce::String who = ch.label.isNotEmpty() ? ch.label : ch.id;

    // Gain: range, then step cap against the last known gain (or the first-write cap).
    {
        const double lo = caps.minChannelGainDb, hi = caps.maxChannelGainDb;
        double g = juce::jlimit (lo, hi, s.gainDb);
        double prev = 0.0;
        if (knownGain (ch.dspOutput, prev))
        {
            const double step = juce::jlimit (-limits.maxGainStepDb, limits.maxGainStepDb, g - prev);
            g = prev + step;
        }
        else if (g > limits.maxFirstGainDb)
            g = limits.maxFirstGainDb;
        g = juce::jlimit (lo, hi, g);
        if (std::abs (g - s.gainDb) > 1.0e-9)
        {
            notes.add (who + ": gain " + juce::String (s.gainDb, 1) + " dB limited to " + juce::String (g, 1) + " dB (per-step cap).");
            s.gainDb = g;
            changed = true;
        }
    }

    const double delay = juce::jlimit (0.0, caps.maxDelayMs, s.delayMs);
    if (delay != s.delayMs)
    {
        notes.add (who + ": delay limited to " + juce::String (delay, 2) + " ms.");
        s.delayMs = delay;
        changed = true;
    }

    // Protected high-pass: never below the planner's limit for the driver (tweeters above all).
    if (ch.hasHighPass() && ch.role != DriverRole::subwoofer)
    {
        const double minHp = protectedHpHz (ch);
        if (ch.role == DriverRole::tweeter && (! s.highPass.enabled || s.highPass.freq < minHp))
        {
            notes.add (who + ": high-pass raised to protected " + juce::String (juce::roundToInt (minHp)) + " Hz.");
            s.highPass.enabled = true;
            s.highPass.freq = minHp;
            if (s.highPass.slopeDbPerOct < 12)
                s.highPass.slopeDbPerOct = 12;
            changed = true;
        }
        else if (s.highPass.enabled && s.highPass.freq < minHp && ch.role != DriverRole::tweeter)
        {
            // Other drivers: protect too (excursion), same planner rule.
            notes.add (who + ": high-pass raised to " + juce::String (juce::roundToInt (minHp)) + " Hz (driver limit).");
            s.highPass.freq = minHp;
            changed = true;
        }
    }

    auto clampFreq = [&] (FilterSpec& f, const juce::String& what)
    {
        const double fr = juce::jlimit (limits.minFreqHz, limits.maxFreqHz, f.freq);
        if (fr != f.freq)
        {
            notes.add (who + ": " + what + " frequency limited to " + juce::String (fr, 0) + " Hz.");
            f.freq = fr;
            changed = true;
        }
    };
    clampFreq (s.highPass, "high-pass");
    clampFreq (s.lowPass, "low-pass");

    for (size_t i = 0; i < s.peq.size(); ++i)
    {
        auto& f = s.peq[i];
        const juce::String w = "EQ" + juce::String ((int) i + 1);
        clampFreq (f, w);
        const double g = juce::jlimit (caps.peqMinGainDb, std::min (caps.peqMaxGainDb, limits.planner.maxBoostDb + 6.0), f.gainDb);
        const double q = juce::jlimit (caps.peqMinQ, caps.peqMaxQ, f.q);
        if (g != f.gainDb || q != f.q)
        {
            notes.add (who + ": " + w + " gain/Q limited.");
            f.gainDb = g;
            f.q = q;
            changed = true;
        }
    }
    return changed;
}

std::vector<double> SafetyGuard::gainSteps (int output, double target) const
{
    std::vector<double> steps;
    double cur = 0.0;
    if (! knownGain (output, cur))
        return steps;
    target = juce::jlimit (caps.minChannelGainDb, caps.maxChannelGainDb, target);
    while (std::abs (target - cur) > limits.maxGainStepDb + 1e-9 && steps.size() < 64)
    {
        cur += target > cur ? limits.maxGainStepDb : -limits.maxGainStepDb;
        steps.push_back (cur);
    }
    return steps;
}

bool SafetyGuard::tweeterNeedsMuteFirst (const OutputChannel& prev, bool havePrev, const OutputChannel& next) const
{
    if (next.role != DriverRole::tweeter)
        return false;
    if (! havePrev)
        return true;
    auto differs = [] (const FilterSpec& a, const FilterSpec& b)
    {
        return a.enabled != b.enabled || (a.enabled && (std::abs (a.freq - b.freq) > 1e-6 || a.slopeDbPerOct != b.slopeDbPerOct
                                                       || a.alignment != b.alignment));
    };
    return differs (prev.settings.highPass, next.settings.highPass) || differs (prev.settings.lowPass, next.settings.lowPass)
           || prev.settings.inverted != next.settings.inverted;
}

//==============================================================================
bool SafetyGuard::isKnownParam (const juce::String& p)
{
    static const char* const names[] = { "nav", "save", "gain", "delay", "polarity", "mute", "hp.freq", "hp.type", "lp.freq", "lp.type",
                                         "peq.freq", "peq.gain", "peq.q" };
    for (auto* n : names)
        if (p == n)
            return true;
    return false;
}

SafetyGuard::Verdict SafetyGuard::checkToolWrite (const juce::String& param, int output, double value)
{
    if (! isKnownParam (param))
        return Verdict::deny ("Unknown param \"" + param + "\". Declare what the control is: nav, save, gain, delay, polarity, mute, "
                              "hp.freq, hp.type, lp.freq, lp.type, peq.freq, peq.gain or peq.q.");

    if (param == "nav" || param == "save")   // navigation / store buttons change no audio value
        return Verdict::allow();

    ChannelInfo ci;
    bool known = false;
    {
        std::lock_guard<std::mutex> lock (m);
        auto it = channels.find (output);
        if (it != channels.end()) { ci = it->second; known = true; }
    }
    if (! known)
        return Verdict::deny ("Output " + juce::String (output + 1) + " is not part of this system.");

    const bool numeric = std::isfinite (value);
    const bool tweeter = ci.role == DriverRole::tweeter;

    if (tweeter && (param.startsWith ("hp.") || param.startsWith ("lp.") || param == "polarity") && ! isMuted (output))
        return Verdict::deny ("Mute output " + juce::String (output + 1) + " (param \"mute\") before changing its crossover or polarity: it drives a tweeter.");

    if (param == "hp.freq" && numeric)
    {
        if (tweeter && value < ci.protectedHp)
            return Verdict::deny ("Tweeter high-pass must be at least " + juce::String (juce::roundToInt (ci.protectedHp)) + " Hz.");
        if (value < limits.minFreqHz || value > limits.maxFreqHz)
            return Verdict::deny ("Frequency out of range.");
    }
    else if (param == "lp.freq" && numeric)
    {
        if (value < limits.minFreqHz || value > limits.maxFreqHz)
            return Verdict::deny ("Frequency out of range.");
    }
    else if (param == "gain" && numeric)
    {
        if (value < caps.minChannelGainDb || value > caps.maxChannelGainDb)
            return Verdict::deny ("Gain must be within " + juce::String (caps.minChannelGainDb, 0) + " .. "
                                  + juce::String (caps.maxChannelGainDb, 0) + " dB.");
        double prev = 0.0;
        if (knownGain (output, prev))
        {
            if (std::abs (value - prev) > limits.maxGainStepDb + 1e-9)
                return Verdict::deny ("Gain step too large: current " + juce::String (prev, 1) + " dB, max step "
                                      + juce::String (limits.maxGainStepDb, 1) + " dB.");
        }
        else if (value > limits.maxFirstGainDb)
            return Verdict::deny ("Current gain unknown: the first value written may not exceed "
                                  + juce::String (limits.maxFirstGainDb, 1) + " dB. Read the control first (read_value) or write lower.");
    }
    else if (param == "delay" && numeric)
    {
        if (value < 0.0)
            return Verdict::deny ("Delay cannot be negative.");
    }
    else if (param == "peq.gain" && numeric)
    {
        if (value < caps.peqMinGainDb || value > caps.peqMaxGainDb)
            return Verdict::deny ("EQ gain out of range.");
    }
    else if (param == "peq.q" && numeric)
    {
        if (value < caps.peqMinQ || value > caps.peqMaxQ)
            return Verdict::deny ("EQ Q out of range.");
    }
    else if (param == "peq.freq" && numeric)
    {
        if (value < limits.minFreqHz || value > limits.maxFreqHz)
            return Verdict::deny ("Frequency out of range.");
    }
    return Verdict::allow();
}
} // namespace fa
