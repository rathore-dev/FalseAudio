#pragma once

#include "../../Page.h"
#include "../../AppShell.h"
#include "../../components/Widgets.h"
#include "core/gear/Gear.h"
#include "core/install/InstallPlanner.h"
#include <optional>

/** Helpers owned by the Equipment page: product formatting (chips, spec sheets), category
    metadata, a background database loader, and a few tiny layout widgets. Header-only. */
namespace fa::ui::gearkit
{
constexpr int gap     = theme::grid;        // 8
constexpr int gap2    = theme::pad;         // 16
constexpr int margin  = theme::padLg;       // 24
constexpr int btnH    = theme::controlHeight;
constexpr int fieldH  = 56;
constexpr int cardChrome = 48 + theme::pad; // Card title row + bottom padding

//==============================================================================
// Category metadata
//==============================================================================
inline juce::String categoryLabel (gear::Category c)
{
    using C = gear::Category;
    switch (c)
    {
        case C::headUnit:        return "Head unit";
        case C::dsp:             return "DSP";
        case C::dspAmp:          return "DSP amplifier";
        case C::dac:             return "DAC";
        case C::ampMulti:        return "Multi-channel amp";
        case C::ampMono:         return "Mono amp";
        case C::driverTweeter:   return "Tweeter";
        case C::driverMidrange:  return "Midrange";
        case C::driverMidbass:   return "Midbass";
        case C::driverCoax:      return "Coaxial";
        case C::componentSet:    return "Component set";
        case C::subwoofer:       return "Subwoofer";
        case C::powerWire:       return "Power wire";
        case C::speakerWire:     return "Speaker wire";
        case C::rca:             return "RCA cable";
        case C::fuseHolder:      return "Fuse holder";
        case C::battery:         return "Battery";
        case C::alternator:      return "Alternator";
        case C::audioInterface:  return "Audio interface";
        case C::measurementMic:  return "Measurement mic";
    }
    return "Product";
}

inline icons::Id categoryIcon (gear::Category c)
{
    using C = gear::Category;
    switch (c)
    {
        case C::headUnit:        return icons::Id::radio;
        case C::dsp: case C::dspAmp: case C::dac:
                                 return icons::Id::chip;
        case C::ampMulti: case C::ampMono:
                                 return icons::Id::power;
        case C::driverTweeter: case C::driverMidrange: case C::driverMidbass:
        case C::driverCoax: case C::componentSet: case C::subwoofer:
                                 return icons::Id::speaker;
        case C::powerWire: case C::speakerWire: case C::rca: case C::fuseHolder:
                                 return icons::Id::plug;
        case C::battery: case C::alternator:
                                 return icons::Id::bolt;
        case C::audioInterface:  return icons::Id::sliders;
        case C::measurementMic:  return icons::Id::mic;
    }
    return icons::Id::speaker;
}

inline bool isDriverCategory (gear::Category c)
{
    using C = gear::Category;
    return c == C::driverTweeter || c == C::driverMidrange || c == C::driverMidbass
        || c == C::driverCoax || c == C::componentSet || c == C::subwoofer;
}

inline bool isAmpCategory (gear::Category c)
{
    return c == gear::Category::ampMulti || c == gear::Category::ampMono || c == gear::Category::dspAmp;
}

/** Which product categories make sense for a config channel role. First = preferred. */
inline std::vector<gear::Category> categoriesForRole (DriverRole r)
{
    using C = gear::Category;
    switch (r)
    {
        case DriverRole::subwoofer: return { C::subwoofer };
        case DriverRole::midbass:   return { C::driverMidbass, C::componentSet, C::driverCoax };
        case DriverRole::midrange:  return { C::driverMidrange, C::componentSet };
        case DriverRole::tweeter:   return { C::driverTweeter, C::componentSet };
        case DriverRole::fullrange: return { C::driverCoax, C::componentSet, C::driverMidbass };
    }
    return { C::driverMidbass };
}

//==============================================================================
// Number formatting
//==============================================================================
inline juce::String num (double v, int maxDecimals = 1)
{
    if (std::abs (v - std::round (v)) < 1e-6)
        return juce::String ((int) std::round (v));
    return juce::String (v, maxDecimals);
}

inline juce::String awgLabel (double awg)
{
    if (awg <= -1.0) return "2/0 AWG";
    if (awg <= 0.0)  return "1/0 AWG";
    return num (awg, 0) + " AWG";
}

inline juce::String ohms (double v) { return num (v, 1) + " Ω"; }

/** The amp's headline power claim: prefers 4 ohm, then 2, then 1. */
inline juce::String ampPowerLine (const gear::AmpSpecs& a)
{
    for (auto key : { "4ohm", "2ohm", "1ohm" })
    {
        const auto it = a.rmsPerChannelW.find (key);
        if (it != a.rmsPerChannelW.end() && it->second > 0)
        {
            juce::String s = num (it->second, 0) + " W";
            if (a.channels > 1) s << " × " << a.channels;
            s << " @ " << juce::String (key).replace ("ohm", " Ω");
            return s;
        }
    }
    return a.channels > 0 ? juce::String (a.channels) + " ch" : juce::String();
}

//==============================================================================
// Spec chips (short) and spec sheet (long)
//==============================================================================
/** Up to ~6 short facts for result rows and slot cards. */
inline juce::StringArray specChips (const gear::Product& p)
{
    juce::StringArray out;
    auto add = [&out] (const juce::String& s) { if (s.isNotEmpty() && out.size() < 6) out.add (s); };

    if (p.driver)
    {
        const auto& d = *p.driver;
        if (d.sizeInches > 0)  add (num (d.sizeInches) + "\"");
        if (d.voiceCoils > 1)  add ("DVC " + (d.coilOhms > 0 ? num (d.coilOhms) : juce::String()) + " Ω");
        else if (d.nominalOhms > 0) add (ohms (d.nominalOhms));
        if (d.powerRmsW > 0)   add (num (d.powerRmsW, 0) + " W RMS");
        if (d.fsHz > 0)        add ("Fs " + num (d.fsHz, 0) + " Hz");
        if (d.sensitivityDb > 0) add (num (d.sensitivityDb) + " dB");
        if (d.xmaxMm > 0)      add ("Xmax " + num (d.xmaxMm) + " mm");
    }
    if (p.amp)
    {
        const auto& a = *p.amp;
        if (a.channels > 0) add (juce::String (a.channels) + " ch");
        add (ampPowerLine (a).upToFirstOccurrenceOf (" ×", false, false)
                 + (a.rmsPerChannelW.empty() ? "" : " /ch"));
        if (a.amplifierClass.isNotEmpty()) add ("Class " + a.amplifierClass);
        if (a.minStableOhms > 0) add (ohms (a.minStableOhms) + " stable");
        if (a.inputSensitivityMaxV > 0) add (num (a.inputSensitivityMinV) + "–" + num (a.inputSensitivityMaxV) + " V in");
    }
    if (p.source)
    {
        const auto& s = *p.source;
        if (s.preoutVoltageV > 0) add (num (s.preoutVoltageV) + " V pre-out");
        if (s.preoutChannels > 0) add (juce::String (s.preoutChannels) + " pre-outs");
        if (s.outputs > 0 && s.inputs > 0) add (juce::String (s.inputs) + " in / " + juce::String (s.outputs) + " out");
        else if (s.outputs > 0) add (juce::String (s.outputs) + " out");
        if (s.peqBandsPerChannel > 0) add (juce::String (s.peqBandsPerChannel) + " PEQ/ch");
        else if (s.eqBands > 0) add (juce::String (s.eqBands) + "-band EQ");
        if (s.timeAlignment) add ("Time align");
        if (s.software.isNotEmpty()) add (s.software);
    }
    if (p.wire)
    {
        const auto& w = *p.wire;
        if (w.awg != 0 || w.mm2 == 0) add (awgLabel (w.awg));
        else add (num (w.mm2) + " mm²");
        if (w.material.isNotEmpty()) add (w.material);
        if (w.ampacityA > 0) add (num (w.ampacityA, 0) + " A");
    }
    if (p.audioInterface)
    {
        const auto& i = *p.audioInterface;
        if (i.inputs > 0) add (juce::String (i.inputs) + " in / " + juce::String (i.outputs) + " out");
        if (i.micPreamps > 0) add (juce::String (i.micPreamps) + " mic pre");
        if (i.phantomPower) add ("48 V");
        if (i.hasLoopback) add ("Loopback");
        if (i.asio) add ("ASIO");
    }
    if (p.mic)
    {
        const auto& m = *p.mic;
        if (m.type.isNotEmpty()) add (m.type);
        if (m.needsPhantom) add ("Needs 48 V");
        if (m.calFileAvailable) add ("Cal file");
        if (m.maxSplDb > 0) add (num (m.maxSplDb, 0) + " dB max");
    }
    return out;
}

/** Short one-line summary ("6.5\" · 4 Ω · 80 W RMS"). */
inline juce::String specsLine (const gear::Product& p)
{
    return specChips (p).joinIntoString ("  |  ");
}

struct SpecRow { juce::String label, value; bool isGroup = false; };

/** The full spec sheet, grouped. */
inline std::vector<SpecRow> specSheet (const gear::Product& p)
{
    std::vector<SpecRow> rows;
    auto group = [&rows] (const juce::String& g) { rows.push_back ({ g, {}, true }); };
    auto row = [&rows] (const juce::String& l, const juce::String& v) { if (v.isNotEmpty()) rows.push_back ({ l, v, false }); };
    auto n = [] (double v, const juce::String& unit, int dec = 1) { return v > 0 ? num (v, dec) + unit : juce::String(); };
    auto yes = [] (bool b) { return b ? juce::String ("Yes") : juce::String(); };

    group ("Product");
    row ("Brand", p.brand);
    row ("Model", p.model);
    row ("Series", p.series);
    row ("Year", p.year > 0 ? juce::String (p.year) : juce::String());
    row ("Category", categoryLabel (p.category));

    if (p.driver)
    {
        const auto& d = *p.driver;
        group ("Driver");
        row ("Size", n (d.sizeInches, "\""));
        row ("Impedance", d.voiceCoils > 1 ? "DVC " + n (d.coilOhms, " Ω") : n (d.nominalOhms, " Ω"));
        row ("Re", n (d.reOhms, " Ω", 2));
        row ("Power RMS", n (d.powerRmsW, " W", 0));
        row ("Power peak", n (d.powerPeakW, " W", 0));
        row ("Sensitivity", d.sensitivityDb > 0 ? num (d.sensitivityDb) + " dB " + d.sensitivityRef : juce::String());
        row ("Response", d.freqLowHz > 0 ? num (d.freqLowHz, 0) + " – " + num (d.freqHighHz, 0) + " Hz" : juce::String());
        row ("Recommended HP", n (d.recommendedHpHz, " Hz", 0));
        row ("Recommended LP", n (d.recommendedLpHz, " Hz", 0));
        group ("Thiele / Small");
        row ("Fs", n (d.fsHz, " Hz"));
        row ("Qts", n (d.qts, "", 3));
        row ("Qes", n (d.qes, "", 3));
        row ("Qms", n (d.qms, "", 2));
        row ("Vas", n (d.vasLitres, " L"));
        row ("Xmax", n (d.xmaxMm, " mm"));
        row ("Sd", n (d.sdCm2, " cm²"));
        row ("BL", n (d.bl, " T.m"));
        row ("Mms", n (d.mmsGrams, " g"));
        row ("Le", n (d.leMh, " mH", 2));
        group ("Mounting");
        row ("Depth", n (d.mountingDepthMm, " mm", 0));
        row ("Cut-out", n (d.cutoutMm, " mm", 0));
    }
    if (p.amp)
    {
        const auto& a = *p.amp;
        group ("Amplifier");
        row ("Channels", a.channels > 0 ? juce::String (a.channels) : juce::String());
        row ("Class", a.amplifierClass);
        for (auto& [k, w] : a.rmsPerChannelW)
            row ("RMS @ " + juce::String (k).replace ("bridged", "bridged ").replace ("ohm", " Ω"), n (w, " W", 0));
        row ("Min load", n (a.minStableOhms, " Ω"));
        row ("Input sensitivity", a.inputSensitivityMaxV > 0 ? num (a.inputSensitivityMinV, 2) + " – " + num (a.inputSensitivityMaxV) + " V" : juce::String());
        row ("High-level input", n (a.highLevelInputMaxV, " V max"));
        row ("S/N", n (a.snrDb, " dB", 0));
        row ("THD", n (a.thdPercent, " %", 3));
        row ("Damping factor", n (a.dampingFactor, "", 0));
        row ("Efficiency", n (a.efficiencyPercent, " %", 0));
        row ("Fuse", n (a.fuseRatingA, " A", 0));
        juce::StringArray feats;
        if (a.hasHpf) feats.add ("HPF");
        if (a.hasLpf) feats.add ("LPF");
        if (a.hasSubsonic) feats.add ("Subsonic");
        if (a.hasBassBoost) feats.add ("Bass boost" + (a.bassBoostMaxDb > 0 ? " +" + num (a.bassBoostMaxDb, 0) + " dB" : juce::String()));
        row ("On-board filters", feats.joinIntoString (", "));
    }
    if (p.source)
    {
        const auto& s = *p.source;
        group (p.category == gear::Category::headUnit ? "Source" : "Processor");
        row ("Pre-out voltage", n (s.preoutVoltageV, " V"));
        row ("Pre-out channels", s.preoutChannels > 0 ? juce::String (s.preoutChannels) : juce::String());
        row ("Output impedance", n (s.outputImpedanceOhms, " Ω", 0));
        row ("Volume steps", s.maxVolumeStep > 0 ? juce::String (s.maxVolumeStep) : juce::String());
        row ("Clean up to step", s.cleanVolumeStep > 0 ? juce::String (s.cleanVolumeStep) : juce::String());
        row ("S/N", n (s.snrDb, " dB", 0));
        row ("Inputs", s.inputs > 0 ? juce::String (s.inputs) : juce::String());
        row ("Outputs", s.outputs > 0 ? juce::String (s.outputs) : juce::String());
        row ("Max input", n (s.maxInputVoltageV, " V"));
        row ("PEQ per channel", s.peqBandsPerChannel > 0 ? juce::String (s.peqBandsPerChannel) + " bands" : juce::String());
        row ("EQ", s.eqBands > 0 ? juce::String (s.eqBands) + " bands " + s.eqType : juce::String());
        row ("Max delay", n (s.maxDelayMs, " ms"));
        row ("Sample rate", s.sampleRateHz > 0 ? num (s.sampleRateHz / 1000.0) + " kHz" : juce::String());
        row ("Time alignment", yes (s.timeAlignment));
        row ("Crossovers", yes (s.crossovers));
        row ("Software", s.software + (s.softwareExe.isNotEmpty() ? " (" + s.softwareExe + ")" : juce::String()));
    }
    if (p.wire)
    {
        const auto& w = *p.wire;
        group ("Conductor");
        row ("Gauge", (w.awg != 0 || w.mm2 == 0) ? awgLabel (w.awg) : juce::String());
        row ("Cross-section", n (w.mm2, " mm²"));
        row ("Material", w.material);
        row ("Resistance", w.ohmsPerMetre > 0 ? juce::String (w.ohmsPerMetre * 1000.0, 2) + " mΩ/m" : juce::String());
        row ("Ampacity", n (w.ampacityA, " A", 0));
    }
    if (p.audioInterface)
    {
        const auto& i = *p.audioInterface;
        group ("Interface");
        row ("Inputs / outputs", i.inputs > 0 ? juce::String (i.inputs) + " / " + juce::String (i.outputs) : juce::String());
        row ("Mic preamps", i.micPreamps > 0 ? juce::String (i.micPreamps) : juce::String());
        row ("Phantom power", yes (i.phantomPower));
        row ("Loopback", yes (i.hasLoopback));
        row ("ASIO", yes (i.asio));
        row ("Max gain", n (i.maxGainDb, " dB", 0));
        row ("Max output", n (i.maxOutputDbu, " dBu"));
        row ("Sample rates", i.sampleRates.joinIntoString (", "));
        row ("Connection", i.connection);
    }
    if (p.mic)
    {
        const auto& m = *p.mic;
        group ("Microphone");
        row ("Type", m.type);
        row ("Needs phantom", yes (m.needsPhantom));
        row ("Sensitivity", n (m.sensitivityMvPa, " mV/Pa"));
        row ("Max SPL", n (m.maxSplDb, " dB", 0));
        row ("Calibration file", m.calFileAvailable ? (m.calFileUrl.isNotEmpty() ? m.calFileUrl : "Available") : juce::String());
    }
    if (p.notes.isNotEmpty())
    {
        group ("Notes");
        row ("", p.notes);
    }
    if (p.sourceUrl.isNotEmpty())
    {
        group ("Source");
        row ("", p.sourceUrl);
    }
    return rows;
}

//==============================================================================
// Background database loader (shared by Equipment and Install pages)
//==============================================================================
/** Loads GearDatabase::loadDefaults() once on a background thread. Callbacks run on the
    message thread. Safe to call ensureLoaded() repeatedly. */
class GearLoader : private juce::Thread,
                   private juce::AsyncUpdater
{
public:
    static GearLoader& instance()
    {
        static GearLoader l;
        return l;
    }

    bool isLoaded() const noexcept  { return loaded.load(); }
    bool isLoading() const noexcept { return isThreadRunning(); }

    /** Starts loading if needed; `onDone` is called (on the message thread) when the database is
        ready - immediately if it already is. */
    void ensureLoaded (std::function<void()> onDone)
    {
        if (! loaded.load() && ! isThreadRunning() && gear::GearDatabase::instance().size() > 0)
            loaded.store (true);              // someone else loaded it synchronously
        if (loaded.load())
        {
            if (onDone) onDone();
            return;
        }
        if (onDone)
            callbacks.push_back (std::move (onDone));
        if (! isThreadRunning())
            startThread();
    }

    /** True when it is safe to query the database from the message thread. */
    static bool ready() { return instance().isLoaded(); }

private:
    GearLoader() : juce::Thread ("GearDatabase loader") {}
    ~GearLoader() override { stopThread (5000); }

    void run() override
    {
        gear::GearDatabase::instance().loadDefaults();
        loaded.store (true);
        triggerAsyncUpdate();
    }

    void handleAsyncUpdate() override
    {
        auto cbs = std::move (callbacks);
        callbacks.clear();
        for (auto& cb : cbs)
            if (cb) cb();
    }

    std::atomic<bool> loaded { false };
    std::vector<std::function<void()>> callbacks;   // message thread only
};

//==============================================================================
// Tiny layout widgets
//==============================================================================
/** Vertically scrolling page body; `layout` returns the content height for a width. */
class ScrollBody : public juce::Component
{
public:
    ScrollBody()
    {
        viewport.setViewedComponent (&contentComp, false);
        viewport.setScrollBarsShown (true, false, true, false);
        viewport.setScrollBarThickness (8);
        addAndMakeVisible (viewport);
    }
    juce::Component& content() noexcept { return contentComp; }
    juce::Viewport& getViewport() noexcept { return viewport; }
    std::function<int (int width)> layout;

    void resized() override { viewport.setBounds (getLocalBounds()); relayout(); }
    void relayout()
    {
        const int w = juce::jmax (320, viewport.getWidth() - viewport.getScrollBarThickness());
        const int h = layout ? layout (w) : 0;
        contentComp.setSize (w, juce::jmax (h, viewport.getHeight()));
    }
private:
    juce::Viewport viewport;
    juce::Component contentComp;
};

/** Section heading: eyebrow + title + one-line description, with an optional right slot. */
class SectionHeader : public juce::Component
{
public:
    SectionHeader (const juce::String& eyebrow, const juce::String& title, const juce::String& hint)
        : eyebrowText (eyebrow), titleText (title), hintText (hint) {}

    static constexpr int height = 56;

    void setHint (const juce::String& h) { hintText = h; repaint(); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        theme::drawEyebrow (g, eyebrowText, r.removeFromTop (14), theme::accent);
        g.setColour (theme::text);
        g.setFont (theme::fontBold (17.0f));
        g.drawText (titleText, r.removeFromTop (24), juce::Justification::centredLeft, false);
        g.setColour (theme::textDim);
        g.setFont (theme::font (12.5f));
        g.drawText (hintText, r, juce::Justification::topLeft, true);
    }
private:
    juce::String eyebrowText, titleText, hintText;
};

class Caption : public juce::Component
{
public:
    explicit Caption (const juce::String& t = {}, juce::Colour c = theme::textMuted) : text (t), colour (c) {}
    void setText (const juce::String& t) { text = t; repaint(); }
    void paint (juce::Graphics& g) override
    {
        g.setColour (colour);
        g.setFont (theme::fontBold (10.5f));
        g.drawText (text.toUpperCase(), getLocalBounds(), juce::Justification::centredLeft, false);
    }
private:
    juce::String text;
    juce::Colour colour;
};

class Note : public juce::Component
{
public:
    explicit Note (const juce::String& t = {}, float size = 12.5f, juce::Colour c = theme::textDim)
        : text (t), fontSize (size), colour (c) {}
    void setText (const juce::String& t) { text = t; repaint(); }
    void setTextColour (juce::Colour c) { colour = c; repaint(); }
    void paint (juce::Graphics& g) override
    {
        g.setColour (colour);
        g.setFont (theme::font (fontSize));
        g.drawFittedText (text, getLocalBounds(), juce::Justification::topLeft, 20, 1.0f);
    }
    int heightFor (int width) const
    {
        juce::GlyphArrangement ga;
        ga.addJustifiedText (theme::font (fontSize), text, 0.0f, 0.0f, (float) width, juce::Justification::topLeft);
        return (int) std::ceil (ga.getBoundingBox (0, -1, true).getBottom()) + 4;
    }
private:
    juce::String text;
    float fontSize;
    juce::Colour colour;
};

inline void styleEditor (juce::TextEditor& e, const juce::String& placeholder = {})
{
    e.setColour (juce::TextEditor::backgroundColourId, theme::bg3);
    e.setColour (juce::TextEditor::outlineColourId, theme::border);
    e.setColour (juce::TextEditor::focusedOutlineColourId, theme::accent);
    e.setColour (juce::TextEditor::textColourId, theme::text);
    e.setColour (juce::TextEditor::highlightColourId, theme::accentDim);
    e.setColour (juce::CaretComponent::caretColourId, theme::accent);
    e.setFont (theme::font (13.0f));
    e.setIndents (8, 6);
    e.setSelectAllWhenFocused (true);
    if (placeholder.isNotEmpty())
        e.setTextToShowWhenEmpty (placeholder, theme::textMuted);
}

inline void styleCombo (juce::ComboBox& c)
{
    c.setColour (juce::ComboBox::backgroundColourId, theme::bg3);
    c.setColour (juce::ComboBox::outlineColourId, theme::border);
    c.setColour (juce::ComboBox::textColourId, theme::text);
    c.setColour (juce::ComboBox::arrowColourId, theme::textDim);
    c.setColour (juce::ComboBox::focusedOutlineColourId, theme::accent);
    c.setColour (juce::ComboBox::buttonColourId, theme::bg3);
}

/** Draws a row of small chips from the left; returns the x where it stopped. */
inline int drawChips (juce::Graphics& g, const juce::StringArray& chips, juce::Rectangle<int> area,
                      juce::Colour colour = theme::textDim, float fontSize = 11.0f)
{
    const auto f = theme::fontMedium (fontSize);
    g.setFont (f);
    int x = area.getX();
    for (auto& c : chips)
    {
        const int w = (int) std::ceil (theme::textWidth (f, c)) + 14;
        if (x + w > area.getRight())
            break;
        juce::Rectangle<float> r ((float) x, (float) area.getY(), (float) w, (float) area.getHeight());
        g.setColour (colour.withAlpha (0.10f));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (colour.withAlpha (0.35f));
        g.drawRoundedRectangle (r.reduced (0.5f), r.getHeight() * 0.5f, 1.0f);
        g.setColour (colour);
        g.drawText (c, r.toNearestInt(), juce::Justification::centred, false);
        x += w + 6;
    }
    return x;
}

/** Small "verified" badge: check mark in a good-coloured disc. */
inline void drawVerified (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour (theme::good.withAlpha (0.18f));
    g.fillEllipse (r);
    g.setColour (theme::good);
    icons::draw (g, icons::Id::check, r.reduced (r.getWidth() * 0.25f), theme::good);
}

/** Animated loading shimmer band drawn over a rounded rect (phase 0..1). */
inline void drawShimmer (juce::Graphics& g, juce::Rectangle<float> r, float phase, float radius = theme::radiusSm)
{
    juce::Graphics::ScopedSaveState ss (g);
    juce::Path clip;
    clip.addRoundedRectangle (r, radius);
    g.reduceClipRegion (clip);
    g.setColour (theme::bg3);
    g.fillRect (r);
    const float bandW = r.getWidth() * 0.45f;
    const float x = r.getX() - bandW + (r.getWidth() + bandW * 2.0f) * phase;
    juce::ColourGradient grad (theme::bg3, x, 0.0f, theme::bg3, x + bandW, 0.0f, false);
    grad.addColour (0.5, theme::bg4.brighter (0.08f));
    g.setGradientFill (grad);
    g.fillRect (r);
}
} // namespace fa::ui::gearkit
