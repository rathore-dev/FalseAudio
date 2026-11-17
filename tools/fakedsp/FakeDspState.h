#pragma once

// Dev/test only. Shared by FakeDspConsole (writes the state file) and the FakeDsp live harness (reads it).
// The AI operator never sees this: it only gets the window.

#include <juce_core/juce_core.h>
#include <vector>

namespace fakedsp
{
constexpr int numOutputs = 8;
constexpr int numPeqBands = 5;
static const char* const windowTitle = "FakeDSP PC-Tool 1.0";

/** Text of the filter combo entries ("Off" first), as a vendor tool would show them. */
inline juce::StringArray filterChoices()
{
    juce::StringArray s;
    s.add ("Off");
    for (int sl : { 12, 18, 24, 48 }) s.add ("Butterworth " + juce::String (sl) + " dB/oct");
    for (int sl : { 12, 24, 36, 48 }) s.add ("Linkwitz-Riley " + juce::String (sl) + " dB/oct");
    for (int sl : { 12, 24 })         s.add ("Bessel " + juce::String (sl) + " dB/oct");
    return s;
}

struct Filter
{
    int choice = 0;            // index into filterChoices(); 0 = Off
    double freq = 1000.0;
    /** "BW"/"LR"/"BS" + slope, "" when off. */
    juce::String key() const
    {
        const auto t = filterChoices()[choice];
        if (choice == 0) return {};
        const auto pre = t.startsWith ("Butter") ? "BW" : t.startsWith ("Linkwitz") ? "LR" : "BS";
        return juce::String (pre) + juce::StringArray::fromTokens (t, " ", "")[1];
    }
};

struct Peq { double freq = 1000.0, gain = 0.0, q = 1.0; };

struct Output
{
    double gain = 0.0, delayMs = 0.0;
    bool inverted = false, muted = false;
    Filter hp, lp;
    Peq peq[numPeqBands];
};

struct State
{
    int selected = 0;                 // 0-based
    int storeCount = 0;
    Output out[numOutputs];

    juce::var toVar() const
    {
        auto* root = new juce::DynamicObject();
        root->setProperty ("selected", selected);
        root->setProperty ("storeCount", storeCount);
        juce::Array<juce::var> arr;
        for (auto& o : out)
        {
            auto* d = new juce::DynamicObject();
            d->setProperty ("gain", o.gain);
            d->setProperty ("delayMs", o.delayMs);
            d->setProperty ("inverted", o.inverted);
            d->setProperty ("muted", o.muted);
            for (int k = 0; k < 2; ++k)
            {
                auto& f = k == 0 ? o.hp : o.lp;
                auto* fd = new juce::DynamicObject();
                fd->setProperty ("choice", f.choice);
                fd->setProperty ("key", f.key());
                fd->setProperty ("freq", f.freq);
                d->setProperty (k == 0 ? "hp" : "lp", juce::var (fd));
            }
            juce::Array<juce::var> pq;
            for (auto& b : o.peq)
            {
                auto* bd = new juce::DynamicObject();
                bd->setProperty ("freq", b.freq);
                bd->setProperty ("gain", b.gain);
                bd->setProperty ("q", b.q);
                pq.add (juce::var (bd));
            }
            d->setProperty ("peq", pq);
            arr.add (juce::var (d));
        }
        root->setProperty ("outputs", arr);
        return juce::var (root);
    }

    static bool fromVar (const juce::var& v, State& s)
    {
        auto* arr = v["outputs"].getArray();
        if (arr == nullptr || (int) arr->size() < numOutputs)
            return false;
        s.selected = (int) v["selected"];
        s.storeCount = (int) v["storeCount"];
        for (int i = 0; i < numOutputs; ++i)
        {
            const auto& d = (*arr)[i];
            auto& o = s.out[i];
            o.gain = d["gain"]; o.delayMs = d["delayMs"];
            o.inverted = (bool) d["inverted"]; o.muted = (bool) d["muted"];
            for (int k = 0; k < 2; ++k)
            {
                auto& f = k == 0 ? o.hp : o.lp;
                const auto& fd = d[k == 0 ? "hp" : "lp"];
                f.choice = (int) fd["choice"]; f.freq = fd["freq"];
            }
            if (auto* pq = d["peq"].getArray())
                for (int b = 0; b < numPeqBands && b < (int) pq->size(); ++b)
                {
                    o.peq[b].freq = (*pq)[b]["freq"]; o.peq[b].gain = (*pq)[b]["gain"]; o.peq[b].q = (*pq)[b]["q"];
                }
        }
        return true;
    }

    bool save (const juce::File& f) const
    {
        const auto tmp = f.getSiblingFile (f.getFileName() + ".tmp");
        if (! tmp.replaceWithText (juce::JSON::toString (toVar(), false)))
            return false;
        return tmp.moveFileTo (f);
    }

    static bool load (const juce::File& f, State& s)
    {
        if (! f.existsAsFile())
            return false;
        juce::var v;
        if (! juce::JSON::parse (f.loadFileAsString(), v).wasOk())
            return false;
        return fromVar (v, s);
    }
};
} // namespace fakedsp
