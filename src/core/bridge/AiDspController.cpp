#include "AiDspController.h"
#include "SafetyRules.h"
#include "UIAutomationController.h"
#include <cmath>

namespace fa
{
namespace
{
    double leadingNumber (const juce::String& text)
    {
        const auto t = text.trim().replaceCharacter (',', '.');
        if (t.isEmpty())
            return std::numeric_limits<double>::quiet_NaN();
        const auto c = t[0];
        if (! (juce::CharacterFunctions::isDigit (c) || c == '-' || c == '+' || c == '.'))
            return std::numeric_limits<double>::quiet_NaN();
        return t.getDoubleValue();
    }
}

AiDspController::AiDspController (Options o)
    : opts (std::move (o)), store (opts.profilesFolder)
{
    if (opts.apiKey == nullptr)
        opts.apiKey = [] { return ApiKeyStore().resolve(); };
}

juce::String AiDspController::getName() const
{
    return hasMap() ? "AI operator - " + profile.name : juce::String ("AI operator");
}

bool AiDspController::loadMap()
{
    DspProfile p;
    if (store.load (windowTitle, p) && p.aiLearned)
    {
        profile = p;
        return true;
    }
    // The window title often contains a project name: match on the saved identifying fragment.
    for (auto& f : store.getFolder().findChildFiles (juce::File::findFiles, false, "ai_*.json"))
        if (DspProfile::load (f, p) && p.aiLearned && p.windowTitleContains.isNotEmpty()
            && windowTitle.containsIgnoreCase (p.windowTitleContains))
        {
            profile = p;
            return true;
        }
    return false;
}

bool AiDspController::saveMap()
{
    const auto key = profile.windowTitleContains.isNotEmpty() ? profile.windowTitleContains : windowTitle;
    return store.save (profile, key);
}

bool AiDspController::connect (juce::String& error)
{
    backend = opts.backend != nullptr ? opts.backend : makeUiaWindowBackend (opts.windowProvider);
    if (backend == nullptr || ! backend->isAttached())
    {
        error = "Attach your DSP software in the DSP tab first.";
        return false;
    }
    windowTitle = backend->windowTitle();
    guard = std::make_unique<SafetyGuard> (opts.limits, opts.caps);
    guard->setChannels (opts.outputs);
    if (loadMap())
        log ("Loaded learned map (" + juce::String ((int) profile.bindings.size()) + " controls) for \"" + profile.name + "\".");
    else
        log ("No learned map for this window yet; the AI must learn it first.");
    selectedChannel = -1;
    lastApplied.clear();
    connected = true;
    return true;
}

void AiDspController::kill (const juce::String& why)
{
    if (guard != nullptr)
        guard->kill (why);
    if (auto* a = activeAgent.load())
        a->cancel();
}

void AiDspController::resetSafety()
{
    if (guard != nullptr)
        guard->resetSession();
}

//==============================================================================
std::unique_ptr<LlmClient> AiDspController::makeLlmClient() const
{
    if (opts.provider == LlmProvider::ollama)
        return std::make_unique<OllamaClient> (opts.ollama, opts.transport);
    return std::make_unique<ClaudeClient> (opts.client, opts.transport, opts.apiKey, opts.sleep);
}

DspOperatorAgent::Options AiDspController::effectiveAgentOptions() const
{
    auto a = opts.agent;
    if (opts.provider == LlmProvider::ollama)
    {
        a.compact = true;
        a.maxTurns = std::min (a.maxTurns, 30);
        a.maxScreenshots = std::min (a.maxScreenshots, 6);
        a.screenshotMaxEdge = std::min (a.screenshotMaxEdge, 768);
        a.treeMaxNodes = std::min (a.treeMaxNodes, 150);
        a.treeMaxDepth = std::min (a.treeMaxDepth, 8);
    }
    return a;
}

bool AiDspController::learn (juce::String& error)
{
    if (! connected || guard == nullptr)
    {
        error = "Not connected.";
        return false;
    }
    if (! opts.allowLearning)
    {
        error = "AI learning is disabled.";
        return false;
    }

    auto client = makeLlmClient();
    DspOperatorAgent agent (*client, *backend, *guard, effectiveAgentOptions());
    activeAgent = &agent;
    juce::StringArray focus;
    for (auto& k : badKeys)
        focus.add (k);
    const auto res = agent.learn (opts.outputs, opts.caps, profile, focus);
    activeAgent = nullptr;

    ++stats.learnRuns;
    stats.usage += res.usage;
    stats.costUsd += res.costUsd;

    if (! res.profile.bindings.empty())
    {
        profile = res.profile;
        if (profile.windowTitleContains.isEmpty())
            profile.windowTitleContains = windowTitle;
        saveMap();
    }
    if (! res.success)
    {
        error = res.message;
        return false;
    }
    badKeys.clear();
    selectedChannel = -1;
    return true;
}

bool AiDspController::tryHeal (juce::String& error)
{
    if (! opts.allowLearning || stats.heals >= opts.maxHeals || guard->isKilled())
        return false;
    ++stats.heals;
    log ("Self-heal: asking the AI to re-learn " + juce::String ((int) badKeys.size()) + " control(s).");
    juce::String e;
    const bool ok = learn (e);
    if (! ok)
        error = "Self-heal failed: " + e;
    return ok;
}

//==============================================================================
bool AiDspController::performBinding (const ControlBinding& b, const juce::String& key, const juce::String& text,
                                      double numeric, bool toggleValue, juce::String& error)
{
    using M = ControlBinding::Method;
    if (auto v = guard->beginAction ("write " + key); ! v)
    {
        error = v.reason;
        return false;
    }

    const bool click = b.method == M::clickPoint || b.method == M::clickAndType;
    if (! click)
    {
        uia::ElementInfo info;
        if (! backend->find (b.path, info))
        {
            error = "Control for \"" + key + "\" not found (" + b.path.describe() + ").";
            badKeys.insert (key);
            return false;
        }
        if (auto v = SafetyGuard::checkElementInWindow (info.screenBounds, backend->windowBounds()); ! v)
        {
            error = v.reason;
            return false;
        }
    }

    bool ok = true;
    const auto wb = backend->windowBounds();
    const juce::Point<int> clickPos (juce::roundToInt (b.relX * wb.getWidth()), juce::roundToInt (b.relY * wb.getHeight()));

    switch (b.method)
    {
        case M::uiaText:   ok = backend->setText (b.path, text, error); break;
        case M::uiaRange:  ok = backend->setRange (b.path, numeric, error); break;
        case M::uiaToggle: ok = backend->setToggle (b.path, toggleValue != b.invertToggle, error); break;
        case M::uiaInvoke: ok = backend->invoke (b.path, error); break;
        case M::uiaSelect:
        {
            auto it = b.valueMap.find (text);
            ok = backend->selectItem (b.path, it != b.valueMap.end() ? it->second : text, error);
            break;
        }
        case M::clickPoint:
            if (auto v = SafetyGuard::checkPointInWindow (clickPos, wb); ! v) { error = v.reason; return false; }
            backend->clickAt (clickPos, false);
            break;
        case M::clickAndType:
        {
            if (auto v = SafetyGuard::checkPointInWindow (clickPos, wb); ! v) { error = v.reason; return false; }
            backend->clickAt (clickPos, true);
            juce::String ke;
            backend->keyCombo ("ctrl+a", ke);
            backend->typeText (text, true);
            break;
        }
    }
    ++stats.writes;
    if (! ok)
    {
        badKeys.insert (key);
        return false;
    }

    const int settle = std::max (b.settleMs, opts.settleMs);
    if (settle > 0)
        juce::Thread::sleep (settle);

    // ---- read back ----
    bool verified = false, mismatch = false;
    juce::String detail;
    if (b.method == M::uiaText || b.method == M::uiaRange)
    {
        const auto read = backend->readValue (b.path);
        const double r = leadingNumber (read);
        if (std::isfinite (r))
        {
            const double tolExact = 0.5 * std::pow (10.0, -b.decimals) + 1.0e-9;
            const double tolQuant = std::max (tolExact, 0.02 * std::abs (numeric));   // DSPs quantise (freq grids, steps)
            verified = true;
            if (std::abs (r - numeric) > tolQuant)
            {
                mismatch = true;
                detail = "wrote " + juce::String (numeric, b.decimals) + ", read back \"" + read + "\"";
            }
        }
    }
    else if (b.method == M::uiaToggle)
    {
        bool st = false;
        if (backend->readToggle (b.path, st))
        {
            verified = true;
            if (st != (toggleValue != b.invertToggle))
            {
                mismatch = true;
                detail = "toggle reads " + juce::String (st ? "on" : "off");
            }
        }
    }
    else if (b.method == M::uiaSelect)
    {
        auto it = b.valueMap.find (text);
        const auto want = it != b.valueMap.end() ? it->second : text;
        const auto read = backend->readValue (b.path);
        if (read.isNotEmpty())
        {
            verified = true;
            if (! read.containsIgnoreCase (want))
            {
                mismatch = true;
                detail = "selected \"" + want + "\", read back \"" + read + "\"";
            }
        }
    }

    if (mismatch)
    {
        ++stats.mismatches;
        badKeys.insert (key);
        error = "Read-back mismatch for " + key + ": " + detail;
        return false;
    }
    ++(verified ? stats.verifiedWrites : stats.unverifiedWrites);
    return true;
}

bool AiDspController::selectChannel (int channel, juce::String& error)
{
    if (channel == selectedChannel)
        return true;
    if (const auto* b = profile.find ("channel.select", channel))
        if (! performBinding (*b, DspProfile::keyFor ("channel.select", channel), {}, 0.0, true, error))
            return false;
    selectedChannel = channel;
    return true;
}

AiDspController::Write AiDspController::writeNumeric (const juce::String& key, int channel, int band, double value,
                                                      bool allowMissingIfNeutral, double neutral, juce::String& error)
{
    const auto* b = profile.find (key, channel, band);
    if (b == nullptr)
    {
        if (allowMissingIfNeutral && std::abs (value - neutral) < 1.0e-9)
            return Write::ok;
        badKeys.insert (DspProfile::keyFor (key, channel, band));
        error = "No control mapped for " + key + " (output " + juce::String (channel + 1) + ").";
        return Write::notBound;
    }
    const double v = value * b->scale + b->offset;
    // find() picks the most specific key; report that one if it fails
    const juce::String used = profile.bindings.count (DspProfile::keyFor (key, channel, band)) ? DspProfile::keyFor (key, channel, band)
                            : profile.bindings.count (DspProfile::keyFor (key, channel)) ? DspProfile::keyFor (key, channel)
                            : profile.bindings.count (DspProfile::keyFor (key, -1, band)) ? DspProfile::keyFor (key, -1, band) : key;
    return performBinding (*b, used, juce::String (v, b->decimals), v, false, error) ? Write::ok : Write::failed;
}

AiDspController::Write AiDspController::writeToggle (const juce::String& key, int channel, bool on, bool neutral, juce::String& error)
{
    const auto* b = profile.find (key, channel);
    if (b == nullptr)
    {
        if (on == neutral)
            return Write::ok;
        badKeys.insert (DspProfile::keyFor (key, channel));
        error = "No control mapped for " + key + " (output " + juce::String (channel + 1) + ").";
        return Write::notBound;
    }
    const juce::String used = profile.bindings.count (DspProfile::keyFor (key, channel)) ? DspProfile::keyFor (key, channel) : key;
    return performBinding (*b, used, {}, 0.0, on, error) ? Write::ok : Write::failed;
}

AiDspController::Write AiDspController::writeCrossover (const juce::String& prefix, int channel, const FilterSpec& f, juce::String& error)
{
    const juce::String typeKey = f.enabled ? crossoverKey (f) : juce::String ("OFF");
    if (const auto* tb = profile.find (prefix + ".type", channel))
    {
        if (tb->valueMap.empty() || tb->valueMap.count (typeKey) > 0)
        {
            const juce::String used = profile.bindings.count (DspProfile::keyFor (prefix + ".type", channel)) ? DspProfile::keyFor (prefix + ".type", channel) : prefix + ".type";
            if (! performBinding (*tb, used, typeKey, 0.0, f.enabled, error))
                return Write::failed;
        }
        else
            log ("Output " + juce::String (channel + 1) + ": the " + prefix + " type " + typeKey + " is not in the learned value map; left as is.");
    }
    else
        log ("Output " + juce::String (channel + 1) + ": no " + prefix + ".type control mapped; filter type left as is.");

    if (f.enabled)
        return writeNumeric (prefix + ".freq", channel, -1, f.freq, false, 0.0, error);
    return Write::ok;
}

bool AiDspController::setMutedOnce (int output, bool muted, juce::String& error)
{
    if (! selectChannel (output, error))
        return false;
    if (writeToggle ("mute", output, muted, false, error) != Write::ok)
        return false;
    guard->noteMute (output, muted);
    return true;
}

bool AiDspController::applyOnce (const OutputChannel& in, juce::String& error)
{
    if (guard->isKilled())
    {
        error = "Kill switch active: " + guard->killReason();
        return false;
    }

    const int c = in.dspOutput;
    if (! selectChannel (c, error))
        return false;

    // Learn the real current gain (if readable) so the per-step cap is relative to the device.
    double known = 0.0;
    if (! guard->knownGain (c, known))
        if (const auto* gb = profile.find ("gain", c))
            if (gb->method == ControlBinding::Method::uiaText || gb->method == ControlBinding::Method::uiaRange)
            {
                const double r = leadingNumber (backend->readValue (gb->path));
                if (std::isfinite (r))
                    guard->noteGain (c, (r - gb->offset) / (gb->scale != 0.0 ? gb->scale : 1.0));
            }

    // Walk large gain changes in capped steps (each step is its own guarded, read-back write).
    // Absolute ceiling first (no boost), then ramp toward the clamped target.
    for (double step : guard->gainSteps (c, std::min (in.settings.gainDb, safety::kMaxOutputGainDb)))
    {
        if (writeNumeric ("gain", c, -1, step, true, 0.0, error) != Write::ok)
            return false;
        guard->noteGain (c, step);
    }

    OutputChannel ch = in;
    juce::StringArray notes;
    guard->sanitizeChannel (ch, notes);
    for (auto& n : notes)
    {
        lastNotes.add (n);
        log ("Safety: " + n);
    }
    const auto& s = ch.settings;
    const juce::String who = ch.label + " (out " + juce::String (c + 1) + ")";

    const auto prev = lastApplied.find (c);
    const bool needMute = guard->tweeterNeedsMuteFirst (prev != lastApplied.end() ? prev->second : ch, prev != lastApplied.end(), ch);
    bool autoMuted = false;
    if (needMute && ! guard->isMuted (c))
    {
        juce::String e;
        if (! setMutedOnce (c, true, e))
        {
            error = "Refusing to change the crossover of " + who + " (tweeter) because it cannot be muted first: " + e;
            return false;
        }
        autoMuted = true;
    }

    auto ok = [&] (Write w) { return w == Write::ok; };
    bool good = ok (writeNumeric ("gain", c, -1, s.gainDb, true, 0.0, error));

    if (good)
    {
        const auto* db = profile.find ("delay", c);
        const juce::String unit = db != nullptr ? db->unit : juce::String ("ms");
        good = ok (writeNumeric ("delay", c, -1, UIAutomationController::convertDelay (s.delayMs, unit, profile.caps.sampleRate),
                                 true, 0.0, error));
    }
    if (good) good = ok (writeToggle ("polarity", c, s.inverted, false, error));
    if (good && ch.hasHighPass()) good = ok (writeCrossover ("hp", c, s.highPass, error));
    if (good && ch.role == DriverRole::subwoofer) good = ok (writeCrossover ("hp", c, s.highPass, error));
    if (good && ch.hasLowPass())  good = ok (writeCrossover ("lp", c, s.lowPass, error));

    const int bands = profile.caps.peqBandsPerChannel;
    for (int b = 0; good && b < bands; ++b)
    {
        const bool used = b < (int) s.peq.size();
        if (! used && profile.find ("peq.gain", c, b) == nullptr)
            continue;
        if (const auto* sel = profile.find ("peq.select", c, b))
            good = performBinding (*sel, DspProfile::keyFor ("peq.select", c, b), {}, 0.0, true, error);
        const auto f = used ? s.peq[(size_t) b] : FilterSpec::peq (1000.0, 1.0, 0.0);
        if (good) good = ok (writeNumeric ("peq.freq", c, b, f.freq, false, 0.0, error));
        if (good) good = ok (writeNumeric ("peq.gain", c, b, f.gainDb, false, 0.0, error));
        if (good) good = ok (writeNumeric ("peq.q", c, b, f.q, false, 0.0, error));
    }

    if (! good)
        return false;           // a muted tweeter stays muted on failure

    if (autoMuted)
    {
        juce::String e;
        if (! setMutedOnce (c, false, e))
        {
            error = "Could not unmute " + who + ": " + e;
            return false;
        }
    }
    lastApplied[c] = ch;
    guard->noteGain (c, s.gainDb);
    return true;
}

bool AiDspController::applyChannel (const OutputChannel& channel, juce::String& error)
{
    if (! connected)
    {
        error = "AI operator is not connected.";
        return false;
    }
    lastNotes.clear();
    badKeys.clear();

    if (! hasMap())
    {
        juce::String e;
        if (! learn (e))
        {
            error = "No control map yet and the AI could not learn one: " + e;
            return false;
        }
    }

    juce::String e;
    bool ok = applyOnce (channel, e);
    if (! ok && ! badKeys.empty() && ! guard->isKilled())
    {
        juce::String he;
        if (tryHeal (he))
        {
            e.clear();
            ok = applyOnce (channel, e);
        }
        else if (he.isNotEmpty())
            e += " (" + he + ")";
    }
    if (! ok)
        error = e;
    return ok;
}

bool AiDspController::setMuted (const OutputChannel& channel, bool muted, juce::String& error)
{
    if (! connected)
    {
        error = "AI operator is not connected.";
        return false;
    }
    if (guard->isKilled())
    {
        error = "Kill switch active: " + guard->killReason();
        return false;
    }
    badKeys.clear();
    if (! hasMap())
    {
        juce::String e;
        if (! learn (e))
        {
            error = "No control map yet and the AI could not learn one: " + e;
            return false;
        }
    }
    juce::String e;
    bool ok = setMutedOnce (channel.dspOutput, muted, e);
    if (! ok && ! badKeys.empty())
    {
        juce::String he;
        if (tryHeal (he))
        {
            e.clear();
            ok = setMutedOnce (channel.dspOutput, muted, e);
        }
    }
    if (! ok)
        error = e;
    return ok;
}

bool AiDspController::soloChannels (const SystemConfig& cfg, const juce::StringArray& ids, juce::String& error)
{
    // Tweeters are muted first, then the rest, then the soloed channels are unmuted last.
    if (! beginBatch (error))
        return false;
    for (int pass = 0; pass < 2; ++pass)
        for (auto& ch : cfg.channels)
        {
            const bool mute = ! ids.contains (ch.id);
            if (mute != (pass == 0))
                continue;
            if (! setMuted (ch, mute, error))
                return false;
        }
    return endBatch (error);
}

bool AiDspController::endBatch (juce::String& error)
{
    batching = false;
    if (const auto* save = profile.find ("save"))
        return performBinding (*save, "save", {}, 0.0, true, error);
    return true;
}
} // namespace fa
