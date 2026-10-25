#include "DspCommon.h"

namespace fa::ui::dsp
{
PageSettings::PageSettings()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "DspPage";
    o.filenameSuffix = ".settings";
    o.folderName = "FalseAudio";
    o.osxLibrarySubFolder = "Application Support";
    props = std::make_unique<juce::PropertiesFile> (o);
}

juce::String PageSettings::get (const juce::String& key, const juce::String& fallback) const
{
    return props != nullptr ? props->getValue (key, fallback) : fallback;
}

void PageSettings::set (const juce::String& key, const juce::String& value)
{
    if (props == nullptr)
        return;
    props->setValue (key, value);
    props->saveIfNeeded();
}

//==============================================================================
juce::String methodName (ControlBinding::Method m)
{
    using M = ControlBinding::Method;
    switch (m)
    {
        case M::uiaText:      return "Type value (UI Automation)";
        case M::uiaRange:     return "Set slider / spinner (UI Automation)";
        case M::uiaToggle:    return "Toggle (UI Automation)";
        case M::uiaInvoke:    return "Press (UI Automation)";
        case M::uiaSelect:    return "Pick list item (UI Automation)";
        case M::clickPoint:   return "Click fixed point";
        case M::clickAndType: return "Double-click point, type, Enter";
    }
    return {};
}

juce::StringArray methodNames()
{
    juce::StringArray names;
    for (int i = 0; i <= (int) ControlBinding::Method::clickAndType; ++i)
        names.add (methodName ((ControlBinding::Method) i));
    return names;
}

juce::String describeCaps (const DspCapabilities& c)
{
    juce::String s;
    s << c.peqBandsPerChannel << " PEQ" << juce::String (juce::CharPointer_UTF8 (" · ")) << juce::String (c.sampleRate / 1000.0, 1) << " kHz"
      << juce::String (juce::CharPointer_UTF8 (" · ")) << theme::formatDb (c.peqMinGainDb, 0) << ".." << theme::formatDb (c.peqMaxGainDb, 0)
      << juce::String (juce::CharPointer_UTF8 (" · ")) << juce::String (juce::roundToInt (c.maxDelayMs)) << " ms";
    return s;
}

const juce::StringArray& crossoverRows()
{
    static const juce::StringArray rows { "LR12", "LR24", "LR36", "LR48", "BW6", "BW12", "BW18", "BW24", "BW48", "BS12", "BS24", "OFF" };
    return rows;
}

juce::String crossoverRowDescription (const juce::String& key)
{
    if (key == "OFF")
        return "Filter off / bypass";
    const juce::String slope = key.retainCharacters ("0123456789");
    if (key.startsWith ("LR")) return "Linkwitz-Riley " + slope + " dB/oct";
    if (key.startsWith ("BS")) return "Bessel " + slope + " dB/oct";
    return "Butterworth " + slope + " dB/oct";
}

juce::String describeElement (const uia::ElementInfo& info)
{
    if (! info.valid)
        return "Nothing recognised under the mouse";

    juce::String s = info.controlType.isNotEmpty() ? info.controlType : juce::String ("Element");
    if (info.name.isNotEmpty())         s << " \"" << info.name << "\"";
    if (info.automationId.isNotEmpty()) s << "  id=" << info.automationId;
    if (info.value.isNotEmpty())        s << "  value=" << info.value;

    juce::StringArray pats;
    if (info.supportsValue)     pats.add ("Value");
    if (info.supportsRange)     pats.add ("Range");
    if (info.supportsToggle)    pats.add ("Toggle");
    if (info.supportsInvoke)    pats.add ("Invoke");
    if (info.supportsSelection) pats.add ("Selection");
    if (info.supportsExpand)    pats.add ("Expand");
    if (! pats.isEmpty())
        s << "  [" << pats.joinIntoString (", ") << "]";
    return s;
}

void drawSectionLabel (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area)
{
    g.setColour (theme::textMuted);
    g.setFont (theme::fontMedium (11.0f));
    g.drawText (text.toUpperCase(), area, juce::Justification::centredLeft);
}

juce::File profileFileFor (const juce::String& name)
{
    auto safe = juce::File::createLegalFileName (name.trim());
    if (safe.isEmpty())
        safe = "DSP profile";
    return DspProfile::profilesFolder().getChildFile (safe + ".json");
}

//==============================================================================
void InfoPanel::setText (const juce::String& t, const juce::String& b)
{
    title = t;
    body = b;
    repaint();
}

int InfoPanel::idealHeight (int width) const
{
    juce::AttributedString as;
    as.append (body, theme::font (13.0f), theme::textDim);
    juce::TextLayout tl;
    tl.createLayout (as, (float) std::max (40, width - 2 * theme::pad - 36));
    return (int) std::ceil (tl.getHeight()) + (title.isNotEmpty() ? 46 : 26);
}

void InfoPanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    theme::fillCard (g, r, theme::bg2, theme::border);
    g.setColour (accent.withAlpha (0.9f));
    g.fillRoundedRectangle (r.removeFromLeft (3.0f).reduced (0.0f, 10.0f), 1.5f);

    auto area = getLocalBounds().reduced (theme::pad, 12);
    icons::draw (g, icon, area.removeFromLeft (20).removeFromTop (20).toFloat(), accent);
    area.removeFromLeft (12);

    if (title.isNotEmpty())
    {
        g.setColour (theme::text);
        g.setFont (theme::fontMedium (14.0f));
        g.drawText (title, area.removeFromTop (20), juce::Justification::centredLeft);
        area.removeFromTop (4);
    }

    juce::AttributedString as;
    as.append (body, theme::font (13.0f), theme::textDim);
    juce::TextLayout tl;
    tl.createLayout (as, (float) area.getWidth());
    tl.draw (g, area.toFloat());
}
} // namespace fa::ui::dsp
