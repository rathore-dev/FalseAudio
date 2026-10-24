#include "TuneOptionsPanel.h"
#include "core/tune/TargetLibrary.h"

namespace fa::ui
{
namespace
{
    constexpr int fieldH = 56;
    constexpr int toggleH = 28;
    constexpr int numFields = 5;
}

TuneOptionsPanel::TuneOptionsPanel (AutoTuneEngine::Options& o) : options (o)
{
    for (auto* f : { &micPositions, &sweepSeconds, &sweepLevel, &verifyPasses, &refineIters })
        addAndMakeVisible (f);
    for (auto* t : { &matchRight, &refineXo, &bandTargets, &competition })
        addAndMakeVisible (t);

    micPositions.onChange = [this] (double v) { options.micPositions = juce::roundToInt (v); };
    sweepSeconds.onChange = [this] (double v) { options.sweepSeconds = v; };
    sweepLevel.onChange   = [this] (double v) { options.sweepLevelDbFS = v; };
    verifyPasses.onChange = [this] (double v) { options.maxVerifyPasses = juce::roundToInt (v); };
    refineIters.onChange  = [this] (double v) { options.refineIterations = juce::roundToInt (v); };
    competition.onClick   = [this] { options.competitionRefine = competition.getToggleState(); refineIters.setEnabled (options.competitionRefine); };
    matchRight.onClick    = [this] { options.matchRightToLeft = matchRight.getToggleState(); };
    refineXo.onClick      = [this] { options.refineCrossovers = refineXo.getToggleState(); };
    bandTargets.onClick   = [this] { options.useBandTargets = bandTargets.getToggleState(); };

    refresh();
}

void TuneOptionsPanel::refresh()
{
    micPositions.setValue (options.micPositions);
    sweepSeconds.setValue (options.sweepSeconds);
    sweepLevel.setValue (options.sweepLevelDbFS);
    verifyPasses.setValue (options.maxVerifyPasses);
    refineIters.setValue (options.refineIterations);
    refineIters.setEnabled (options.competitionRefine);
    competition.setToggleState (options.competitionRefine, juce::dontSendNotification);
    matchRight.setToggleState (options.matchRightToLeft, juce::dontSendNotification);
    refineXo.setToggleState (options.refineCrossovers, juce::dontSendNotification);
    bandTargets.setToggleState (options.useBandTargets, juce::dontSendNotification);
    bandTargets.setEnabled (bandTargetsAvailable);
    bandTargets.setAlpha (bandTargetsAvailable ? 1.0f : 0.45f);
}

void TuneOptionsPanel::setTargetName (const juce::String& targetName)
{
    const auto* entry = TargetLibrary::findByName (targetName);
    bandTargetsAvailable = entry != nullptr && entry->hasBands();
    bandTargets.setTooltip (bandTargetsAvailable ? juce::String()
                                                 : "The current target curve has no per-band levels; pick a built-in house curve to enable this.");
    refresh();
}

void TuneOptionsPanel::setEnabledState (bool enabled)
{
    for (auto* c : getChildren())
        c->setEnabled (enabled);
    setAlpha (enabled ? 1.0f : 0.55f);
}

int TuneOptionsPanel::idealHeight (int width)
{
    const int cols = width >= 720 ? 4 : 2;
    const int rows = (numFields + cols - 1) / cols;
    return rows * (fieldH + 26) + 8 + toggleH * 4 + 24 + 16;
}

juce::String TuneOptionsPanel::hintFor (int column) const
{
    switch (column)
    {
        case 0:  return "Head positions averaged per driver";
        case 1:  return "Longer = more noise rejection";
        case 2:  return "Auto-adjusted if clipping or noisy";
        case 3:  return "Closed-loop correction rounds";
        default: return "200 = ~90% of the gain of 400, half the time";
    }
}

void TuneOptionsPanel::paint (juce::Graphics& g)
{
    const int cols = getWidth() >= 720 ? 4 : 2;
    const int colW = (getWidth() - (cols - 1) * theme::pad) / cols;
    const int rows = (numFields + cols - 1) / cols;

    g.setFont (theme::font (11.0f));
    g.setColour (theme::textMuted);
    for (int i = 0; i < numFields; ++i)
    {
        const int r = i / cols, c = i % cols;
        juce::Rectangle<int> hint (c * (colW + theme::pad), r * (fieldH + 26) + fieldH + 4, colW, 18);
        g.drawText (hintFor (i), hint, juce::Justification::centredLeft, true);
    }

    const int y = rows * (fieldH + 26) + 2;
    g.setColour (theme::border);
    g.fillRect (0, y, getWidth(), 1);
}

void TuneOptionsPanel::resized()
{
    const int cols = getWidth() >= 720 ? 4 : 2;
    const int colW = (getWidth() - (cols - 1) * theme::pad) / cols;
    ValueField* fields[] = { &micPositions, &sweepSeconds, &sweepLevel, &verifyPasses, &refineIters };
    for (int i = 0; i < numFields; ++i)
    {
        const int r = i / cols, c = i % cols;
        fields[i]->setBounds (c * (colW + theme::pad), r * (fieldH + 26), colW, fieldH);
    }

    const int rows = (numFields + cols - 1) / cols;
    int y = rows * (fieldH + 26) + 12;
    matchRight.setBounds (0, y, getWidth(), toggleH);
    y += toggleH + 6;
    refineXo.setBounds (0, y, getWidth(), toggleH);
    y += toggleH + 6;
    bandTargets.setBounds (0, y, getWidth(), toggleH);
    y += toggleH + 6;
    competition.setBounds (0, y, getWidth(), toggleH);
}
} // namespace fa::ui
