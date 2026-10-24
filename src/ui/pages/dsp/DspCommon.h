#pragma once

#include "../../Page.h"
#include "../../AppShell.h"
#include "../../components/Widgets.h"
#include "core/bridge/DspWindowHost.h"
#include "core/bridge/DspProfile.h"
#include "core/bridge/UIAutomationController.h"

/** Small shared pieces for the DSP page and its helpers. */
namespace fa::ui::dsp
{
    /** Settings that belong to the DSP page only (last exe, last profile...). Kept in its own
        properties file so it never fights the Session's settings file. */
    class PageSettings
    {
    public:
        PageSettings();
        juce::String get (const juce::String& key, const juce::String& fallback = {}) const;
        void set (const juce::String& key, const juce::String& value);

    private:
        std::unique_ptr<juce::PropertiesFile> props;
    };

    juce::String methodName (ControlBinding::Method m);
    juce::StringArray methodNames();                       // in enum order

    /** "10 PEQ · 48 kHz · ±12 dB · 20 ms" */
    juce::String describeCaps (const DspCapabilities& c);

    /** Crossover value-map rows the Teach wizard offers (canonical keys, see crossoverKey()). */
    const juce::StringArray& crossoverRows();
    juce::String crossoverRowDescription (const juce::String& key);

    /** Readable description of a UI Automation element (for the hover readout). */
    juce::String describeElement (const uia::ElementInfo& info);

    /** Draws a small upper-case section label (e.g. "OPTIONS"). */
    void drawSectionLabel (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area);

    /** Safe file name for a profile. */
    juce::File profileFileFor (const juce::String& name);

    /** Multi-paragraph read-only text block with an optional icon, painted from theme tokens. */
    class InfoPanel : public juce::Component
    {
    public:
        explicit InfoPanel (icons::Id icon = icons::Id::info) : icon (icon) {}
        void setText (const juce::String& title, const juce::String& body);
        void setAccent (juce::Colour c) { accent = c; repaint(); }
        void paint (juce::Graphics&) override;
        /** Height needed for a given width. */
        int idealHeight (int width) const;

    private:
        icons::Id icon;
        juce::String title, body;
        juce::Colour accent = theme::accent;
    };
} // namespace fa::ui::dsp
