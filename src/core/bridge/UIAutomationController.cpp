#include "UIAutomationController.h"

namespace fa
{
UIAutomationController::UIAutomationController (DspProfile p, WindowProvider w, ManualDspController::AskUser a)
    : profile (std::move (p)), windowProvider (std::move (w)), askUser (std::move (a))
{
}

bool UIAutomationController::connect (juce::String& error)
{
    window = windowProvider != nullptr ? windowProvider() : nullptr;
    if (window == nullptr)
    {
        error = "Attach your DSP software in the DSP tab first (" + profile.name + ").";
        return false;
    }
    if (! automation.isAvailable())
    {
        error = "Windows UI Automation is not available on this system.";
        return false;
    }
    selectedChannel = -1;
    return true;
}

bool UIAutomationController::isConnected() const
{
    return window != nullptr;
}

double UIAutomationController::convertDelay (double ms, const juce::String& unit, double fs)
{
    if (unit == "cm")      return ms * speedOfSound / 10.0;
    if (unit == "m")       return ms * speedOfSound / 1000.0;
    if (unit == "in")      return ms * speedOfSound / 10.0 / 2.54;
    if (unit == "samples") return std::round (ms * fs / 1000.0);
    return ms;
}

bool UIAutomationController::perform (const ControlBinding& b, const juce::String& text, double numeric, bool toggle,
                                      juce::String& error)
{
    using M = ControlBinding::Method;
    bool ok = true;

    const auto clickPos = [&]
    {
        const auto r = uia::Automation::windowScreenBounds (window);
        return juce::Point<int> (r.getX() + (int) std::round (b.relX * r.getWidth()),
                                 r.getY() + (int) std::round (b.relY * r.getHeight()));
    };

    switch (b.method)
    {
        case M::uiaText:   ok = automation.setText (window, b.path, text, error); break;
        case M::uiaRange:  ok = automation.setRange (window, b.path, numeric, error); break;
        case M::uiaToggle: ok = automation.setToggle (window, b.path, toggle != b.invertToggle, error); break;
        case M::uiaInvoke: ok = automation.invoke (window, b.path, error); break;
        case M::uiaSelect:
        {
            auto it = b.valueMap.find (text);
            ok = automation.selectItem (window, b.path, it != b.valueMap.end() ? it->second : text, error);
            break;
        }
        case M::clickPoint:
            uia::Automation::clickAt (clickPos());
            break;
        case M::clickAndType:
            uia::Automation::clickAt (clickPos(), true);
            uia::Automation::pressKeyCombo ('A', true);
            uia::Automation::typeText (text, true);
            break;
    }

    juce::Thread::sleep (std::max (0, b.settleMs));
    return ok;
}

bool UIAutomationController::selectChannel (int channel, juce::String& error)
{
    if (channel == selectedChannel)
        return true;
    if (auto* b = profile.find ("channel.select", channel))
    {
        if (! perform (*b, {}, 0.0, true, error))
            return false;
    }
    selectedChannel = channel;
    return true;
}

bool UIAutomationController::setNumeric (const juce::String& key, int channel, int band, double value,
                                         const juce::String& label, juce::String& error)
{
    const auto* b = profile.find (key, channel, band);
    if (b == nullptr)
    {
        manualLines.add (label + " = " + juce::String (value, 2));
        return true;
    }
    const double v = value * b->scale + b->offset;
    return perform (*b, juce::String (v, b->decimals), v, false, error);
}

bool UIAutomationController::setToggle (const juce::String& key, int channel, bool on, const juce::String& label, juce::String& error)
{
    const auto* b = profile.find (key, channel);
    if (b == nullptr)
    {
        manualLines.add (label + " = " + (on ? "ON" : "OFF"));
        return true;
    }
    if (b->method == ControlBinding::Method::clickPoint)
    {
        // State can't be read on custom controls; ask for confirmation instead of guessing.
        manualLines.add (label + " = " + (on ? "ON" : "OFF") + " (check)");
        return true;
    }
    return perform (*b, {}, 0.0, on, error);
}

bool UIAutomationController::setCrossover (const juce::String& prefix, int channel, const FilterSpec& f,
                                           const juce::String& label, juce::String& error)
{
    const juce::String typeKey = f.enabled ? crossoverKey (f) : juce::String ("OFF");

    if (const auto* tb = profile.find (prefix + ".type", channel))
    {
        if (tb->valueMap.empty() || tb->valueMap.count (typeKey) > 0)
        {
            if (! perform (*tb, typeKey, 0.0, f.enabled, error))
                return false;
        }
        else
        {
            manualLines.add (label + " type = " + (f.enabled ? toString (f.alignment) + " " + juce::String (f.slopeDbPerOct) + " dB/oct" : "OFF"));
        }
    }
    else
    {
        manualLines.add (label + " type = " + (f.enabled ? toString (f.alignment) + " " + juce::String (f.slopeDbPerOct) + " dB/oct" : "OFF"));
    }

    if (f.enabled)
        return setNumeric (prefix + ".freq", channel, -1, f.freq, label + " frequency (Hz)", error);
    return true;
}

bool UIAutomationController::applyChannel (const OutputChannel& ch, juce::String& error)
{
    const int c = ch.dspOutput;
    const auto& s = ch.settings;
    const juce::String who = ch.label + " (out " + juce::String (c + 1) + ")";

    if (! selectChannel (c, error))
        return false;

    bool ok = setNumeric ("gain", c, -1, s.gainDb, who + " gain (dB)", error);

    if (ok)
    {
        const auto* db = profile.find ("delay", c);
        const juce::String unit = db != nullptr ? db->unit : juce::String ("ms");
        ok = setNumeric ("delay", c, -1, convertDelay (s.delayMs, unit, profile.caps.sampleRate),
                         who + " delay (" + (unit.isEmpty() ? "ms" : unit) + ")", error);
    }

    if (ok) ok = setToggle ("polarity", c, s.inverted, who + " polarity invert", error);
    if (ok && ch.hasHighPass()) ok = setCrossover ("hp", c, s.highPass, who + " high-pass", error);
    if (ok && ch.role == DriverRole::subwoofer) ok = setCrossover ("hp", c, s.highPass, who + " subsonic", error);
    if (ok && ch.hasLowPass())  ok = setCrossover ("lp", c, s.lowPass, who + " low-pass", error);

    // EQ: write every band the DSP has, flattening unused ones.
    const int bands = profile.caps.peqBandsPerChannel;
    for (int b = 0; ok && b < bands; ++b)
    {
        const bool used = b < (int) s.peq.size();
        if (! used && profile.find ("peq.gain", c, b) == nullptr)
            continue; // nothing to flatten automatically; manual lines only for used bands

        if (const auto* sel = profile.find ("peq.select", c, b))
            ok = perform (*sel, {}, 0.0, true, error);

        const auto f = used ? s.peq[(size_t) b] : FilterSpec::peq (1000.0, 1.0, 0.0);
        const juce::String bandLabel = who + " EQ" + juce::String (b + 1);
        if (ok) ok = setNumeric ("peq.freq", c, b, f.freq, bandLabel + " freq (Hz)", error);
        if (ok) ok = setNumeric ("peq.gain", c, b, f.gainDb, bandLabel + " gain (dB)", error);
        if (ok) ok = setNumeric ("peq.q",    c, b, f.q, bandLabel + " Q", error);
    }

    if (! ok)
        return false;
    return batching ? true : flushManual (error);
}

bool UIAutomationController::setMuted (const OutputChannel& ch, bool muted, juce::String& error)
{
    if (! selectChannel (ch.dspOutput, error))
        return false;
    if (! setToggle ("mute", ch.dspOutput, muted, ch.label + " (out " + juce::String (ch.dspOutput + 1) + ") mute", error))
        return false;
    return batching ? true : flushManual (error);
}

bool UIAutomationController::beginBatch (juce::String&)
{
    batching = true;
    manualLines.clear();
    return true;
}

bool UIAutomationController::endBatch (juce::String& error)
{
    batching = false;
    if (const auto* save = profile.find ("save"))
        if (! perform (*save, {}, 0.0, true, error))
            return false;
    return flushManual (error);
}

bool UIAutomationController::flushManual (juce::String& error)
{
    if (manualLines.isEmpty())
        return true;
    const auto lines = manualLines;
    manualLines.clear();
    if (askUser == nullptr || ! askUser ("These settings are not automated yet - please set them", lines))
    {
        error = "Cancelled by user.";
        return false;
    }
    return true;
}

//==============================================================================
TeachSession::TeachSession (void* w) : window (w) {}

TeachSession::~TeachSession()
{
    stopTimer();
}

void TeachSession::capture (TeachSlot::Kind k, Callback cb)
{
    kind = k;
    callback = std::move (cb);
    wasDown = uia::Automation::isMouseButtonDown();
    startTimerHz (60);
}

void TeachSession::cancel()
{
    stopTimer();
    callback = nullptr;
}

uia::ElementInfo TeachSession::hoverInfo() const
{
    return automation.elementAt (uia::Automation::mousePosition(), window);
}

void TeachSession::timerCallback()
{
    const bool down = uia::Automation::isMouseButtonDown();
    const auto pos = uia::Automation::mousePosition();
    const auto bounds = uia::Automation::windowScreenBounds (window);

    if (down && ! wasDown)
        downPos = pos;

    if (! down && wasDown && bounds.contains (downPos) && bounds.contains (pos) && callback != nullptr)
    {
        const auto info = automation.elementAt (pos, window);

        ControlBinding b;
        b.relX = bounds.getWidth()  > 0 ? (double) (pos.x - bounds.getX()) / bounds.getWidth()  : 0.5;
        b.relY = bounds.getHeight() > 0 ? (double) (pos.y - bounds.getY()) / bounds.getHeight() : 0.5;
        b.path = info.path;
        b.capturedDescription = info.valid ? info.controlType + (info.name.isNotEmpty() ? " \"" + info.name + "\"" : juce::String())
                                           : juce::String ("point ") + juce::String (b.relX, 3) + ", " + juce::String (b.relY, 3);

        using M = ControlBinding::Method;
        const bool hasPath = info.valid && ! info.path.isEmpty();
        switch (kind)
        {
            case TeachSlot::Kind::numeric:
                b.method = hasPath && info.supportsValue ? M::uiaText
                         : hasPath && info.supportsRange ? M::uiaRange : M::clickAndType;
                break;
            case TeachSlot::Kind::toggle:
                b.method = hasPath && info.supportsToggle ? M::uiaToggle : M::clickPoint;
                break;
            case TeachSlot::Kind::select:
                b.method = hasPath && (info.supportsExpand || info.supportsSelection || info.controlType == "ComboBox")
                               ? M::uiaSelect : M::clickPoint;
                break;
            case TeachSlot::Kind::action:
                b.method = hasPath && (info.supportsInvoke || info.supportsSelection) ? M::uiaInvoke : M::clickPoint;
                break;
        }

        auto cb = std::move (callback);
        callback = nullptr;
        stopTimer();
        cb (b, info);
        return;
    }

    wasDown = down;
}
} // namespace fa
