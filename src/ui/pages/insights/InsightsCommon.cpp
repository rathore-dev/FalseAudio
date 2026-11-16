#include "core/tune/Verdict.h"
#include "InsightsCommon.h"

namespace fa::ui::insightsui
{
juce::Colour heatColour (double deltaDb, double goodDb, double badDb)
{
    const double a = std::abs (deltaDb);
    if (a <= goodDb)
        return theme::good;
    const double t = juce::jlimit (0.0, 1.0, (a - goodDb) / std::max (0.01, badDb - goodDb));
    if (t < 0.5)
        return theme::good.interpolatedWith (theme::warn, (float) (t * 2.0));
    return theme::warn.interpolatedWith (theme::bad, (float) ((t - 0.5) * 2.0));
}

juce::Colour thdColour (double percent)
{
    if (percent < 1.0)  return theme::good;
    if (percent < 3.0)  return theme::accent;
    if (percent < 10.0) return theme::warn;
    return theme::bad;
}

juce::Colour snrColour (double snrDb)
{
    if (snrDb >= 45.0) return theme::good;
    if (snrDb >= 30.0) return theme::warn;
    return theme::bad;
}

double thdDb (double percent)
{
    return 20.0 * std::log10 (std::max (percent, 0.001) / 100.0);
}

juce::String formatPercent (double percent)
{
    const int decimals = percent < 1.0 ? 2 : percent < 10.0 ? 1 : 0;
    return juce::String (percent, decimals) + " %";
}

juce::String formatBandHz (double lowHz, double highHz)
{
    if (lowHz <= 0.0 && highHz <= 0.0)
        return "-";
    return theme::formatHz (lowHz, 0).replace (" Hz", "").replace (" kHz", "k") + " - " + theme::formatHz (highHz, 1);
}

juce::String verdictFor (int score)
{
    return fa::verdictLabel (score);
}

juce::String verdictFor (int score, double tonal, double imaging, double clean)
{
    return fa::verdictLabel (score, tonal, imaging, clean);
}

const OutputChannel* findChannel (const SystemConfig& cfg, const juce::String& channelId)
{
    return cfg.find (channelId);
}

juce::Colour channelColour (const SystemConfig& cfg, const juce::String& channelId)
{
    if (auto* ch = cfg.find (channelId))
        return theme::sideTint (theme::roleColour (ch->role), ch->side);
    return theme::textDim;
}

juce::String channelLabel (const SystemConfig& cfg, const juce::String& channelId)
{
    if (auto* ch = cfg.find (channelId))
        return ch->label.isNotEmpty() ? ch->label : ch->id;
    return channelId;
}

FindingStyle styleForFinding (const juce::String& text)
{
    const auto t = text.toLowerCase();
    auto any = [&t] (std::initializer_list<const char*> words)
    {
        for (auto* w : words)
            if (t.contains (w))
                return true;
        return false;
    };

    if (any ({ "clip", "reversed", "high distortion", "distort", "resonan", "noisy", "low snr", "low signal",
               "limited", "weak", "cannot", "could not", "not reach", "rattle", "warning", "too " }))
        return { icons::Id::warning, theme::warn };
    if (any ({ "fixed", "corrected", "clean", "good", "healthy", "within", "no problem", "ok", "matches", "flat" }))
        return { icons::Id::check, theme::good };
    return { icons::Id::info, theme::accent };
}

void drawStat (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& caption,
               const juce::String& value, juce::Colour valueColour, const juce::String& hint)
{
    theme::drawEyebrow (g, caption, area.removeFromTop (14));
    area.removeFromTop (2);
    g.setColour (valueColour);
    g.setFont (theme::display (17.0f));
    g.drawText (value, area.removeFromTop (22), juce::Justification::centredLeft, true);
    if (hint.isNotEmpty())
    {
        g.setColour (theme::textMuted);
        g.setFont (theme::font (11.0f));
        g.drawText (hint, area.removeFromTop (16), juce::Justification::centredLeft, true);
    }
}

static juce::TextLayout layoutFor (const juce::String& text, float width, const juce::Font& font, juce::Colour colour)
{
    juce::AttributedString as;
    as.setJustification (juce::Justification::topLeft);
    as.setWordWrap (juce::AttributedString::byWord);
    as.append (text, font, colour);   // append with font/colour: setColour before append is ignored
    juce::TextLayout tl;
    tl.createLayout (as, std::max (10.0f, width));
    return tl;
}

int wrappedHeight (const juce::String& text, int width, const juce::Font& font)
{
    return (int) std::ceil (layoutFor (text, (float) width, font, theme::text).getHeight());
}

void drawWrapped (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area,
                  const juce::Font& font, juce::Colour colour)
{
    layoutFor (text, (float) area.getWidth(), font, colour).draw (g, area.toFloat());
}

int pillWidth (const juce::String& text)
{
    return (int) std::ceil (theme::textWidth (theme::fontMedium (11.0f), text)) + 18;
}

int drawPill (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& text, juce::Colour colour)
{
    const int w = std::min (area.getWidth(), pillWidth (text));
    auto r = area.withWidth (w).toFloat();
    theme::fillPill (g, r, colour.withAlpha (0.14f), colour.withAlpha (0.45f));
    g.setColour (colour);
    g.setFont (theme::fontMedium (11.0f));
    g.drawText (text, r.toNearestInt(), juce::Justification::centred, true);
    return w;
}
} // namespace fa::ui::insightsui
