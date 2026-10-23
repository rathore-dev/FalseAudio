#include "Theme.h"

namespace fa::ui::theme
{
juce::Colour roleColour (DriverRole r)
{
    switch (r)
    {
        case DriverRole::subwoofer: return roleSub;
        case DriverRole::midbass:   return roleMidbass;
        case DriverRole::midrange:  return roleMidrange;
        case DriverRole::tweeter:   return roleTweeter;
        case DriverRole::fullrange: return roleFullrange;
    }
    return text;
}

juce::Colour sideTint (juce::Colour c, Side s)
{
    if (s == Side::right)
        return c.withRotatedHue (0.04f).brighter (0.15f);
    if (s == Side::mono)
        return c;
    return c;
}

juce::Colour scoreColour (int score)
{
    if (score >= 85) return good;
    if (score >= 65) return accent;
    if (score >= 45) return warn;
    return bad;
}

static const juce::String sans     = "Segoe UI";
static const juce::String sansMono = "Consolas";

juce::Font font (float size)        { return juce::Font (juce::FontOptions (sans, "Regular", size)); }
juce::Font fontMedium (float size)  { return juce::Font (juce::FontOptions (sans, "Semibold", size)); }
juce::Font fontBold (float size)    { return juce::Font (juce::FontOptions (sans, "Bold", size)); }
juce::Font mono (float size)        { return juce::Font (juce::FontOptions (sansMono, "Regular", size)); }
juce::Font display (float size)     { return juce::Font (juce::FontOptions (sans, "Semibold", size)).withExtraKerningFactor (-0.02f); }

juce::String formatHz (double hz, int maxDecimals)
{
    if (hz >= 1000.0)
    {
        const double k = hz / 1000.0;
        juce::String s = juce::String (k, k < 10.0 ? std::min (2, maxDecimals + 1) : 1);
        if (s.contains ("."))
            s = s.trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
        return s + " kHz";
    }
    juce::String s = juce::String (hz, hz < 100.0 ? maxDecimals : 0);
    if (s.contains ("."))
        s = s.trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
    return s + " Hz";
}

juce::String formatDb (double db, int decimals, bool withSign)
{
    juce::String s = juce::String (std::abs (db) < 0.05 ? 0.0 : db, decimals);
    if (withSign && db > 0.049)
        s = "+" + s;
    return s + " dB";
}

juce::String formatMs (double ms, int decimals)
{
    return juce::String (ms, decimals) + " ms";
}

void fillCard (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour fill, juce::Colour stroke, float rad)
{
    g.setColour (fill);
    g.fillRoundedRectangle (r, rad);
    // subtle top highlight for depth
    g.setColour (juce::Colours::white.withAlpha (0.025f));
    g.drawRoundedRectangle (r.reduced (1.0f), rad - 1.0f, 1.0f);
    g.setColour (stroke);
    g.drawRoundedRectangle (r.reduced (0.5f), rad, 1.0f);
}

void drawGlow (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, float rad, float spread)
{
    const int steps = 6;
    for (int i = steps; i >= 1; --i)
    {
        const float t = (float) i / (float) steps;
        g.setColour (c.withAlpha (0.045f * (1.0f - t) + 0.01f));
        g.fillRoundedRectangle (r.expanded (spread * t), rad + spread * t);
    }
}

void drawDashedPath (juce::Graphics& g, const juce::Path& p, float thickness, float dash, float gap)
{
    const float lengths[] = { dash, gap };
    juce::Path dashed;
    juce::PathStrokeType (thickness).createDashedStroke (dashed, p, lengths, 2);
    g.fillPath (dashed);
}
} // namespace fa::ui::theme
