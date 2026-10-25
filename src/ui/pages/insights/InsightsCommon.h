#pragma once

#include "../../Page.h"
#include "../../AppShell.h"
#include "../../components/Widgets.h"
#include "../../components/Plots.h"
#include "core/tune/insights/TuneInsights.h"

/** Small shared helpers for the Insights page: colour scales (heat / distortion / SNR), number
    formatting, channel lookups and a few paint utilities. UI only - all maths lives in
    core/tune/insights. */
namespace fa::ui::insightsui
{
    constexpr int gap         = theme::pad;
    constexpr int cardHeaderH = 48;

    /** Green when |delta| <= goodDb, through amber to red at |delta| >= badDb. */
    juce::Colour heatColour (double deltaDb, double goodDb = 1.0, double badDb = 6.0);
    /** Severity colour for a THD percentage (< 1 % clean ... >= 10 % bad). */
    juce::Colour thdColour (double percent);
    juce::Colour snrColour (double snrDb);

    /** THD as dB relative to the fundamental: 1 % = -40 dB, 10 % = -20 dB. Used for all THD plots
        so the log-like scale shows the quiet parts as well as the peaks. */
    double thdDb (double percent);

    juce::String formatPercent (double percent);
    juce::String formatBandHz (double lowHz, double highHz);
    juce::String verdictFor (int score);

    const OutputChannel* findChannel (const SystemConfig& cfg, const juce::String& channelId);
    juce::Colour channelColour (const SystemConfig& cfg, const juce::String& channelId);
    juce::String channelLabel (const SystemConfig& cfg, const juce::String& channelId);

    struct FindingStyle { icons::Id icon; juce::Colour colour; };
    /** Icon + colour for a plain-language finding, chosen from its wording. */
    FindingStyle styleForFinding (const juce::String& text);

    /** Caption (eyebrow) above a large tabular value, optional hint underneath. */
    void drawStat (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& caption,
                   const juce::String& value, juce::Colour valueColour, const juce::String& hint = {});

    /** Wrapped paragraph helpers. */
    int  wrappedHeight (const juce::String& text, int width, const juce::Font& font);
    void drawWrapped (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area,
                      const juce::Font& font, juce::Colour colour);

    /** Pill with text; returns the width used. */
    int drawPill (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& text, juce::Colour colour);
    int pillWidth (const juce::String& text);
}
