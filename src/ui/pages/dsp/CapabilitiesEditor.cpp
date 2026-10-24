#include "CapabilitiesEditor.h"

namespace fa::ui::dsp
{
static const int kSlopes[] { 6, 12, 18, 24, 36, 48 };

CapabilitiesEditor::CapabilitiesEditor()
{
    for (auto* f : { &bands, &minQ, &maxQ, &minGain, &maxGain, &maxDelay, &delayStep, &gainStep, &minChGain, &maxChGain })
    {
        addAndMakeVisible (f);
        f->onChange = [this] (double) { notify(); };
    }

    for (auto sr : { 44100, 48000, 96000, 192000 })
        sampleRate.addItem (juce::String (sr / 1000.0, 1) + " kHz", sr);
    sampleRate.setSelectedId (48000, juce::dontSendNotification);
    sampleRate.onChange = [this] { notify(); };
    addAndMakeVisible (sampleRate);

    for (int s : kSlopes)
    {
        auto* t = slopeToggles.add (new ToggleSwitch (juce::String (s) + " dB"));
        t->setToggleState (true, juce::dontSendNotification);
        t->onClick = [this] { notify(); };
        addAndMakeVisible (t);
    }

    for (auto* t : { &hasLR, &hasBessel, &hasAllPass })
    {
        t->onClick = [this] { notify(); };
        addAndMakeVisible (t);
    }
    hasLR.setToggleState (true, juce::dontSendNotification);
    hasBessel.setToggleState (true, juce::dontSendNotification);
}

void CapabilitiesEditor::setCapabilities (const DspCapabilities& c)
{
    bands.setValue (c.peqBandsPerChannel);
    minQ.setValue (c.peqMinQ);
    maxQ.setValue (c.peqMaxQ);
    minGain.setValue (c.peqMinGainDb);
    maxGain.setValue (c.peqMaxGainDb);
    maxDelay.setValue (c.maxDelayMs);
    delayStep.setValue (c.delayStepMs);
    gainStep.setValue (c.gainStepDb);
    minChGain.setValue (c.minChannelGainDb);
    maxChGain.setValue (c.maxChannelGainDb);
    sampleRate.setSelectedId ((int) c.sampleRate, juce::dontSendNotification);
    if (sampleRate.getSelectedId() == 0)
        sampleRate.setSelectedId (48000, juce::dontSendNotification);

    for (int i = 0; i < slopeToggles.size(); ++i)
        slopeToggles[i]->setToggleState (std::find (c.availableSlopes.begin(), c.availableSlopes.end(), kSlopes[i]) != c.availableSlopes.end(),
                                         juce::dontSendNotification);
    hasLR.setToggleState (c.hasLinkwitzRiley, juce::dontSendNotification);
    hasBessel.setToggleState (c.hasBessel, juce::dontSendNotification);
    hasAllPass.setToggleState (c.hasAllPass, juce::dontSendNotification);
}

DspCapabilities CapabilitiesEditor::getCapabilities() const
{
    DspCapabilities c;
    c.peqBandsPerChannel = (int) bands.getValue();
    c.peqMinQ = minQ.getValue();
    c.peqMaxQ = std::max (maxQ.getValue(), c.peqMinQ + 0.1);
    c.peqMinGainDb = minGain.getValue();
    c.peqMaxGainDb = maxGain.getValue();
    c.maxDelayMs = maxDelay.getValue();
    c.delayStepMs = delayStep.getValue();
    c.gainStepDb = gainStep.getValue();
    c.minChannelGainDb = minChGain.getValue();
    c.maxChannelGainDb = maxChGain.getValue();
    c.sampleRate = sampleRate.getSelectedId() > 0 ? (double) sampleRate.getSelectedId() : 48000.0;
    c.availableSlopes.clear();
    for (int i = 0; i < slopeToggles.size(); ++i)
        if (slopeToggles[i]->getToggleState())
            c.availableSlopes.push_back (kSlopes[i]);
    if (c.availableSlopes.empty())
        c.availableSlopes = { 12, 24 };
    c.hasLinkwitzRiley = hasLR.getToggleState();
    c.hasBessel = hasBessel.getToggleState();
    c.hasAllPass = hasAllPass.getToggleState();
    return c;
}

int CapabilitiesEditor::idealHeight() const
{
    // 3 section labels (18 + 6) + 6 rows of fields (32 + 8) + 2 rows of slopes (28 + 6) + 1 row alignments (28)
    return 3 * 24 + 6 * 40 + 2 * 34 + 28 + 16;
}

void CapabilitiesEditor::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    drawSectionLabel (g, "Equaliser & ranges", r.removeFromTop (18));
    r.removeFromTop (6 + 6 * 40);
    drawSectionLabel (g, "Crossover slopes available", r.removeFromTop (18));
    r.removeFromTop (6 + 2 * 34);
    drawSectionLabel (g, "Filter alignments available", r.removeFromTop (18));
}

void CapabilitiesEditor::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (24);

    juce::Component* fields[] { &bands, &sampleRate, &minQ, &maxQ, &minGain, &maxGain,
                                &maxDelay, &delayStep, &gainStep, &minChGain, &maxChGain, nullptr };
    const int cols = 2, gap = theme::grid;
    const int colW = (r.getWidth() - gap) / cols;
    for (int i = 0; i < 12; i += cols)
    {
        auto row = r.removeFromTop (theme::controlHeight);
        for (int c = 0; c < cols; ++c)
        {
            auto cell = row.removeFromLeft (colW);
            row.removeFromLeft (gap);
            if (auto* comp = fields[i + c])
                comp->setBounds (cell);
        }
        r.removeFromTop (gap);
    }

    r.removeFromTop (24);
    const int scols = 3;
    const int sw = (r.getWidth() - 2 * gap) / scols;
    for (int i = 0; i < slopeToggles.size(); i += scols)
    {
        auto row = r.removeFromTop (28);
        for (int c = 0; c < scols && i + c < slopeToggles.size(); ++c)
        {
            slopeToggles[i + c]->setBounds (row.removeFromLeft (sw));
            row.removeFromLeft (gap);
        }
        r.removeFromTop (6);
    }

    r.removeFromTop (24);
    auto row = r.removeFromTop (28);
    hasLR.setBounds (row.removeFromLeft (sw));      row.removeFromLeft (gap);
    hasBessel.setBounds (row.removeFromLeft (sw));  row.removeFromLeft (gap);
    hasAllPass.setBounds (row.removeFromLeft (sw));
}
} // namespace fa::ui::dsp
