#pragma once

#include <JuceHeader.h>
#include "core/tune/SystemConfig.h"

namespace fa::ui
{
/** Design tokens for the whole app: colours, type, spacing. Everything visual reads from here. */
namespace theme
{
    // ---- Surfaces (deepest to highest) -----------------------------------
    const juce::Colour bg0       { 0xff070A10 };   // window
    const juce::Colour bg1       { 0xff0C111A };   // page
    const juce::Colour bg2       { 0xff111827 };   // card
    const juce::Colour bg3       { 0xff172033 };   // elevated / hover
    const juce::Colour bg4       { 0xff1E2A40 };   // pressed / active chip
    const juce::Colour border    { 0xff1F2B3D };
    const juce::Colour borderHi  { 0xff2C3A52 };

    // ---- Text -------------------------------------------------------------
    const juce::Colour text      { 0xffE7EDF6 };
    const juce::Colour textDim   { 0xff8E9DB5 };
    const juce::Colour textMuted { 0xff566480 };

    // ---- Accent & semantic ------------------------------------------------
    const juce::Colour accent    { 0xff22D3EE };   // electric cyan
    const juce::Colour accentHi  { 0xff67E8F9 };
    const juce::Colour accentDim { 0xff0E7490 };
    const juce::Colour good      { 0xff34D399 };
    const juce::Colour warn      { 0xffFBBF24 };
    const juce::Colour bad       { 0xffF87171 };

    // ---- Driver roles -----------------------------------------------------
    const juce::Colour roleSub       { 0xff9F8CFF };
    const juce::Colour roleMidbass   { 0xffFF9F43 };
    const juce::Colour roleMidrange  { 0xff4ADE80 };
    const juce::Colour roleTweeter   { 0xffFF6B9D };
    const juce::Colour roleFullrange { 0xff60A5FA };

    // ---- Curves -----------------------------------------------------------
    const juce::Colour curveBefore { 0xff6B7A94 };
    const juce::Colour curveAfter  { 0xff22D3EE };
    const juce::Colour curveTarget { 0xffF1F5F9 };
    const juce::Colour curveLeft   { 0xff22D3EE };
    const juce::Colour curveRight  { 0xffF472B6 };
    const juce::Colour curveSum    { 0xffFACC15 };

    // ---- Layout -----------------------------------------------------------
    constexpr int   grid     = 8;
    constexpr int   pad      = 16;
    constexpr int   padLg    = 24;
    constexpr float radius   = 10.0f;
    constexpr float radiusSm = 6.0f;
    constexpr int   railWidth   = 84;
    constexpr int   headerHeight = 52;
    constexpr int   controlHeight = 32;

    juce::Colour roleColour (DriverRole r);
    juce::Colour sideTint (juce::Colour c, Side s);     // right side is slightly shifted
    juce::Colour scoreColour (int score);

    // ---- Type -------------------------------------------------------------
    juce::Font font (float size);
    juce::Font fontMedium (float size);
    juce::Font fontBold (float size);
    juce::Font mono (float size);
    juce::Font display (float size);

    // ---- Formatting helpers ----------------------------------------------
    juce::String formatHz (double hz, int maxDecimals = 1);
    juce::String formatDb (double db, int decimals = 1, bool withSign = true);
    juce::String formatMs (double ms, int decimals = 2);

    // ---- Drawing helpers --------------------------------------------------
    void fillCard (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour fill = bg2,
                   juce::Colour stroke = border, float radius = theme::radius);
    void drawGlow (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, float radius, float spread);
    void drawDashedPath (juce::Graphics& g, const juce::Path& p, float thickness, float dash = 6.0f, float gap = 4.0f);
}
} // namespace fa::ui
