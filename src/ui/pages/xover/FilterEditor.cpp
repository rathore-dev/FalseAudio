#include "FilterEditor.h"

namespace fa::ui
{
namespace
{
    juce::String shortAlignment (Alignment a)
    {
        switch (a)
        {
            case Alignment::butterworth:   return "BW";
            case Alignment::linkwitzRiley: return "LR";
            case Alignment::bessel:        return "Bessel";
        }
        return "?";
    }
}

FilterEditor::FilterEditor (bool isHighPass)
    : highPass (isHighPass),
      freq ("Frequency", "Hz", 10.0, 24000.0, 1.0, 0)
{
    spec = FilterSpec::crossover (highPass ? FilterType::highPass : FilterType::lowPass, highPass ? 80.0 : 3000.0);

    enable.setTooltip (highPass ? "Enable the high-pass filter" : "Enable the low-pass filter");
    enable.onClick = [this] { spec.enabled = enable.getToggleState(); refreshEnablement(); emit(); };
    addAndMakeVisible (enable);

    freq.setValue (spec.freq);
    freq.onChange = [this] (double v) { spec.freq = v; emit(); };
    addAndMakeVisible (freq);

    slope.onChange = [this] (int i)
    {
        if (i >= 0 && i < (int) slopes.size())
        {
            spec.slopeDbPerOct = slopes[(size_t) i];
            // LR needs an even order; fall back to Butterworth for odd slopes
            if (spec.alignment == Alignment::linkwitzRiley && (spec.slopeDbPerOct / 6) % 2 != 0)
            {
                spec.alignment = Alignment::butterworth;
                setSpec (spec, accent);
            }
            emit();
        }
    };
    addAndMakeVisible (slope);

    alignment.onChange = [this] (int i)
    {
        if (i >= 0 && i < (int) alignments.size())
        {
            spec.alignment = alignments[(size_t) i];
            emit();
        }
    };
    addAndMakeVisible (alignment);

    setCapabilities ({});
}

void FilterEditor::setCapabilities (const DspCapabilities& caps)
{
    slopes = caps.availableSlopes;
    if (slopes.empty())
        slopes = { 6, 12, 18, 24, 36, 48 };

    alignments = { Alignment::butterworth };
    if (caps.hasLinkwitzRiley) alignments.push_back (Alignment::linkwitzRiley);
    if (caps.hasBessel)        alignments.push_back (Alignment::bessel);

    juce::StringArray slopeNames, alignNames;
    for (int s : slopes) slopeNames.add (juce::String (s));
    for (auto a : alignments) alignNames.add (shortAlignment (a));
    slope.setOptions (slopeNames);
    alignment.setOptions (alignNames);

    setSpec (spec, accent);
}

void FilterEditor::setSpec (const FilterSpec& s, juce::Colour c)
{
    spec = s;
    accent = c;
    enable.setToggleState (spec.enabled, juce::dontSendNotification);
    freq.setValue (spec.freq);

    int si = 0;
    for (size_t i = 0; i < slopes.size(); ++i)
        if (slopes[i] == spec.slopeDbPerOct) si = (int) i;
    slope.setSelected (si);

    int ai = 0;
    for (size_t i = 0; i < alignments.size(); ++i)
        if (alignments[i] == spec.alignment) ai = (int) i;
    alignment.setSelected (ai);

    refreshEnablement();
    repaint();
}

FilterSpec FilterEditor::getSpec() const
{
    return spec;
}

void FilterEditor::setCompact (bool c)
{
    if (compact == c) return;
    compact = c;
    resized();
    repaint();
}

void FilterEditor::emit()
{
    repaint();
    if (onChange)
        onChange (spec);
}

void FilterEditor::refreshEnablement()
{
    const float a = spec.enabled ? 1.0f : 0.45f;
    freq.setAlpha (a);
    slope.setAlpha (a);
    alignment.setAlpha (a);
}

void FilterEditor::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    theme::fillCard (g, r.toFloat(), theme::bg2, spec.enabled ? accent.withAlpha (0.35f) : theme::border);

    auto header = r.removeFromTop (headerH()).reduced (theme::pad, 0);
    g.setColour (theme::text);
    g.setFont (theme::fontMedium (13.0f));
    g.drawText (highPass ? "High-pass" : "Low-pass", header, juce::Justification::centredLeft);

    // summary text (e.g. "LR 24 @ 80 Hz")
    g.setColour (spec.enabled ? accent : theme::textMuted);
    g.setFont (theme::mono (11.0f));
    const juce::String summary = spec.enabled ? (shortAlignment (spec.alignment) + " " + juce::String (spec.slopeDbPerOct) + "  @ " + theme::formatHz (spec.freq, 0))
                                              : juce::String ("bypassed");
    g.drawText (summary, header.withTrimmedRight (56), juce::Justification::centredRight);

    auto cap = [&] (juce::Component& c, const juce::String& text)
    {
        g.setColour (theme::textMuted);
        g.setFont (theme::fontMedium (10.0f));
        g.drawText (text.toUpperCase(), c.getX(), c.getY() - 13, c.getWidth(), 12, juce::Justification::centredLeft);
    };
    cap (slope, "Slope dB/oct");
    cap (alignment, "Alignment");
}

void FilterEditor::resized()
{
    auto r = getLocalBounds().reduced (theme::pad, 0);
    auto header = r.removeFromTop (headerH());
    enable.setBounds (header.removeFromRight (48).withSizeKeepingCentre (48, 28));

    freq.setBounds (r.removeFromTop (theme::controlHeight));
    r.removeFromTop (gapH());
    slope.setBounds (r.removeFromTop (theme::controlHeight));
    r.removeFromTop (gapH());
    alignment.setBounds (r.removeFromTop (theme::controlHeight));
}
} // namespace fa::ui
