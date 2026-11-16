#include "StageTimeline.h"

namespace fa::ui
{
StageTimeline::StageTimeline()
{
    setInterceptsMouseClicks (false, false);
    reset();
}

StageTimeline::~StageTimeline() = default;

int StageTimeline::stageIndex (Stage s)
{
    switch (s)
    {
        case Stage::preflight:  return 0;
        case Stage::protect:    return 1;
        case Stage::measure:    return 2;
        case Stage::crossovers: return 3;
        case Stage::solve:      return 4;
        case Stage::sumCheck:   return 5;
        case Stage::imaging:    return 6;
        case Stage::apply:      return 7;
        case Stage::verify:     return 8;
        case Stage::noise:      return 9;
        case Stage::report:     return 10;
        default:                return -1;
    }
}

juce::String StageTimeline::shortDescription (Stage s)
{
    switch (s)
    {
        case Stage::preflight:  return "Device, DSP link, signal";
        case Stage::protect:    return "Safe crossovers, flat EQ";
        case Stage::measure:    return "Every driver, every seat";
        case Stage::crossovers: return "From measured bandwidth";
        case Stage::solve:      return "Polarity, time, levels, EQ";
        case Stage::sumCheck:   return "Cancellation fix, polarity";
        case Stage::imaging:    return "Centre image, level only";
        case Stage::apply:      return "Write to the DSP";
        case Stage::verify:     return "Re-measure, close the loop";
        case Stage::noise:      return "Hiss and ground-noise check";
        case Stage::report:     return "Score and findings";
        default:                return {};
    }
}

void StageTimeline::reset()
{
    current = Stage::idle;
    outcome = Stage::idle;
    progress.fill (0.0);
    shownFill.fill (0.0f);
    pulse = 0.0f;
    lineFill = 0.0f;
    stopTimer();
    repaint();
}

void StageTimeline::setStage (Stage s)
{
    current = s;
    const int idx = stageIndex (s);
    if (idx >= 0)
    {
        for (int i = 0; i < idx; ++i)
            progress[(size_t) i] = 1.0;
        if (! isTimerRunning())
            startTimerHz (60);
    }
    repaint();
}

void StageTimeline::setProgress (Stage s, double fraction)
{
    const int idx = stageIndex (s);
    if (idx >= 0)
        progress[(size_t) idx] = juce::jlimit (0.0, 1.0, std::max (progress[(size_t) idx], fraction));
}

void StageTimeline::setOutcome (Stage finalStage)
{
    outcome = finalStage;
    if (finalStage == Stage::done)
        progress.fill (1.0);
    startTimerHz (60);
    repaint();
}

void StageTimeline::timerCallback()
{
    bool moving = false;
    for (size_t i = 0; i < progress.size(); ++i)
    {
        const float target = (float) progress[i];
        const float d = target - shownFill[i];
        if (std::abs (d) > 0.002f)
        {
            shownFill[i] += d * 0.18f;
            moving = true;
        }
        else
            shownFill[i] = target;
    }

    const int idx = stageIndex (current);
    float lineTarget = 0.0f;
    if (outcome == Stage::done)
        lineTarget = 1.0f;
    else if (idx >= 0)
        lineTarget = ((float) idx + (float) progress[(size_t) idx]) / (float) (AutoTuneEngine::numWorkStages - 1);
    lineTarget = juce::jlimit (0.0f, 1.0f, lineTarget);
    if (std::abs (lineTarget - lineFill) > 0.001f)
    {
        lineFill += (lineTarget - lineFill) * 0.12f;
        moving = true;
    }

    pulse += 0.045f;
    if (pulse > juce::MathConstants<float>::twoPi)
        pulse -= juce::MathConstants<float>::twoPi;

    const bool running = idx >= 0 && outcome == Stage::idle;
    if (! running && ! moving)
        stopTimer();
    repaint();
}

void StageTimeline::paint (juce::Graphics& g)
{
    constexpr int n = AutoTuneEngine::numWorkStages;
    static const Stage stages[n] = { Stage::preflight, Stage::protect, Stage::measure, Stage::crossovers,
                                     Stage::solve, Stage::sumCheck, Stage::imaging, Stage::apply, Stage::verify,
                                     Stage::noise, Stage::report };

    auto area = getLocalBounds();
    const bool compact = area.getWidth() < 760;
    const float nodeR = compact ? 12.0f : 15.0f;
    const int labelH = compact ? 34 : 40;

    const float nodeY = (float) (area.getY() + (area.getHeight() - labelH) / 2);
    const float x0 = (float) area.getX() + nodeR + 24.0f;
    const float x1 = (float) area.getRight() - nodeR - 24.0f;
    auto xFor = [&] (int i) { return x0 + (x1 - x0) * (float) i / (float) (n - 1); };

    const int activeIdx = stageIndex (current);
    const bool failed = outcome == Stage::failed || outcome == Stage::cancelled;

    // Connector rail
    g.setColour (theme::border);
    g.fillRoundedRectangle (x0, nodeY - 1.5f, x1 - x0, 3.0f, 1.5f);
    const float fillW = (x1 - x0) * lineFill;
    if (fillW > 0.5f)
    {
        juce::ColourGradient grad (theme::accentDim, x0, nodeY, theme::accent, x0 + fillW, nodeY, false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (x0, nodeY - 1.5f, fillW, 3.0f, 1.5f);
        g.setColour (theme::accent.withAlpha (0.25f));
        g.fillRoundedRectangle (x0, nodeY - 3.5f, fillW, 7.0f, 3.5f);
    }

    for (int i = 0; i < n; ++i)
    {
        const float cx = xFor (i);
        const float fill = shownFill[(size_t) i];
        const bool isActive = i == activeIdx && outcome == Stage::idle;
        const bool isDone = fill >= 0.999f && ! isActive;
        const bool stoppedHere = failed && i == activeIdx;

        juce::Rectangle<float> node (cx - nodeR, nodeY - nodeR, nodeR * 2.0f, nodeR * 2.0f);
        const auto colour = stoppedHere ? (outcome == Stage::failed ? theme::bad : theme::warn)
                          : (isDone || isActive) ? theme::accent : theme::borderHi;

        if (isActive)
        {
            const float p = 0.5f + 0.5f * std::sin (pulse);
            g.setColour (theme::accent.withAlpha (0.10f + 0.10f * p));
            g.fillEllipse (node.expanded (8.0f + 5.0f * p));
            g.setColour (theme::accent.withAlpha (0.22f));
            g.fillEllipse (node.expanded (3.0f));
        }

        // Base disc
        g.setColour (theme::bg1);
        g.fillEllipse (node);
        g.setColour (isDone ? theme::accent.withAlpha (0.22f) : stoppedHere ? colour.withAlpha (0.22f) : theme::bg3);
        g.fillEllipse (node);
        g.setColour (colour.withAlpha (isDone || isActive || stoppedHere ? 1.0f : 0.8f));
        g.drawEllipse (node.reduced (0.75f), 1.5f);

        if (isActive && fill > 0.001f)
        {
            juce::Path arc;
            arc.addArc (node.getX() + 1.0f, node.getY() + 1.0f, node.getWidth() - 2.0f, node.getHeight() - 2.0f,
                        0.0f, juce::MathConstants<float>::twoPi * fill, true);
            g.setColour (theme::accentHi);
            g.strokePath (arc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        if (isDone)
            icons::draw (g, icons::Id::check, node.reduced (nodeR * 0.42f), theme::accentHi);
        else if (stoppedHere)
            icons::draw (g, outcome == Stage::failed ? icons::Id::cross : icons::Id::stop, node.reduced (nodeR * 0.42f), colour);
        else
        {
            g.setColour (isActive ? theme::text : theme::textMuted);
            g.setFont (theme::fontBold (compact ? 10.5f : 12.0f));
            g.drawText (juce::String (i + 1), node, juce::Justification::centred);
        }

        // Labels
        const int lw = compact ? 92 : 136;
        juce::Rectangle<int> label ((int) cx - lw / 2, (int) (nodeY + nodeR) + 8, lw, labelH);
        g.setColour (isActive ? theme::text : isDone ? theme::textDim : stoppedHere ? colour : theme::textMuted);
        g.setFont (isActive ? theme::fontBold (12.5f) : theme::fontMedium (12.0f));
        g.drawText (AutoTuneEngine::stageName (stages[i]), label.removeFromTop (18), juce::Justification::centred);
        if (! compact)
        {
            g.setColour (isActive ? theme::textDim : theme::textMuted.withAlpha (0.8f));
            g.setFont (theme::font (10.5f));
            g.drawText (shortDescription (stages[i]), label, juce::Justification::centredTop);
        }
    }
}
} // namespace fa::ui
