#include "TuneIdleView.h"
#include "StageTimeline.h"

namespace fa::ui
{
namespace
{
    constexpr int inner = 32;
    constexpr int stepsW = 300;
    constexpr int columnGap = 40;
    constexpr int buttonH = 56;
    constexpr int buttonW = 268;

    const juce::String headline = "Tune the whole car in one click.";
    const juce::String promise  = "FalseAudio measures every driver at the listening position, fixes polarity, "
                                  "aligns arrival times, sets crossovers, equalises both sides to the target and "
                                  "verifies the result - automatically, with the tweeters protected throughout.";

    int textHeight (const juce::Font& f, const juce::String& s, int width)
    {
        juce::AttributedString as;
        as.append (s, f);
        juce::TextLayout tl;
        tl.createLayout (as, (float) std::max (10, width));
        return (int) std::ceil (tl.getHeight());
    }

    void drawText (juce::Graphics& g, const juce::Font& f, juce::Colour c, const juce::String& s, juce::Rectangle<int> r)
    {
        juce::AttributedString as;
        as.append (s, f, c);
        juce::TextLayout tl;
        tl.createLayout (as, (float) r.getWidth());
        tl.draw (g, r.toFloat());
    }

    struct Layout
    {
        juce::Rectangle<int> hero, eyebrow, title, promiseRect, button, lastRow, steps, chips, note, optionsHeader, optionsPanel;
        int totalHeight = 0;
    };

    Layout computeLayout (int width, bool twoCol, const juce::String& demoNote, float openAmount, int optionsH, bool hasLast)
    {
        Layout L;
        const int contentW = width - inner * 2;
        const int heroW = twoCol ? contentW - stepsW - columnGap : contentW;
        const int textW = std::min (heroW, 640);

        int y = inner;
        L.eyebrow = { inner, y, textW, 18 };
        y += 18 + 12;
        const auto titleFont = theme::display (width >= 1100 ? 38.0f : 32.0f);
        const int titleH = textHeight (titleFont, headline, textW);
        L.title = { inner, y, textW, titleH };
        y += titleH + 14;
        const int promiseH = textHeight (theme::font (15.0f), promise, textW);
        L.promiseRect = { inner, y, textW, promiseH };
        y += promiseH + 28;
        L.button = { inner, y, std::min (buttonW, heroW), buttonH };
        y += buttonH;
        if (hasLast)
        {
            y += 14;
            L.lastRow = { inner, y, textW, 30 };
            y += 30;
        }
        const int heroBottom = y;

        const int stepsH = 26 + AutoTuneEngine::numWorkStages * 46;
        if (twoCol)
        {
            L.steps = { width - inner - stepsW, inner, stepsW, stepsH };
            // Balance the hero text block vertically against the taller steps column.
            const int dy = std::max (0, (inner + stepsH - heroBottom) / 2);
            for (auto* r : { &L.eyebrow, &L.title, &L.promiseRect, &L.button, &L.lastRow })
                r->translate (0, dy);
            y = std::max (heroBottom, inner + stepsH);
        }
        else
        {
            y += 28;
            L.steps = { inner, y, contentW, stepsH };
            y += stepsH;
        }
        L.hero = { inner, inner, heroW, heroBottom - inner };

        y += 28;
        L.chips = { inner, y, contentW, 28 };
        y += 28;

        if (demoNote.isNotEmpty())
        {
            y += 16;
            const int noteH = std::max (40, textHeight (theme::font (13.0f), demoNote, contentW - 56) + 20);
            L.note = { inner, y, contentW, noteH };
            y += noteH;
        }

        y += 20;
        L.optionsHeader = { inner, y, contentW, 32 };
        y += 32;
        const int panelH = (int) std::round (optionsH * openAmount);
        L.optionsPanel = { inner, y + 8, contentW, panelH };
        y += panelH > 0 ? panelH + 8 : 0;
        L.totalHeight = y + inner;
        return L;
    }
}

TuneIdleView::TuneIdleView (Session& s) : session (s), options (s.getAutoTuneOptions())
{
    startButton.setHero (true);
    addAndMakeVisible (startButton);
    addAndMakeVisible (optionsButton);
    addChildComponent (lastReportButton);
    addChildComponent (options);
    options.setInterceptsMouseClicks (true, true);

    startButton.onClick = [this] { if (onStart) onStart(); };
    lastReportButton.onClick = [this] { if (onViewReport) onViewReport(); };
    optionsButton.onClick = [this] { setOptionsOpen (! optionsOpen); };
    refresh();
}

TuneIdleView::~TuneIdleView() = default;

void TuneIdleView::setOptionsOpen (bool open)
{
    optionsOpen = open;
    optionsButton.setIcon (open ? icons::Id::chevronDown : icons::Id::chevronRight);
    options.setVisible (true);
    startTimerHz (60);
}

void TuneIdleView::timerCallback()
{
    const float target = optionsOpen ? 1.0f : 0.0f;
    const float d = target - openAmount;
    if (std::abs (d) < 0.01f)
    {
        openAmount = target;
        stopTimer();
        options.setVisible (optionsOpen);
    }
    else
        openAmount += d * 0.22f;

    options.setAlpha (openAmount);
    if (onHeightChanged != nullptr)
        onHeightChanged();            // the page re-sizes us inside its viewport from idealHeight()
    resized();
    repaint();
}

void TuneIdleView::refresh()
{
    chips.clear();
    const auto& cfg = session.getConfig();
    const bool demo = session.getMode() == Session::Mode::demo;

    auto add = [this] (const juce::String& text, juce::Colour c)
    {
        auto chip = std::make_unique<Chip> (text, c);
        addAndMakeVisible (*chip);
        chips.push_back (std::move (chip));
    };

    add (tune::layoutSummary (cfg), theme::roleMidrange);
    add (juce::String ((int) cfg.channels.size()) + " outputs", theme::textDim);
    add (demo ? "Demo" : "Live", demo ? theme::warn : theme::good);
    add (tune::dspKindName (session), theme::accent);
    const int mics = std::max (1, session.getSource().numMicrophones());
    add (juce::String (mics) + (mics == 1 ? " mic" : " mics"), theme::roleFullrange);
    add ("Target: " + cfg.target.name, theme::curveTarget.withAlpha (0.85f));
    add ("Seat: " + juce::String (cfg.listeningSeat == 0 ? "driver" : cfg.listeningSeat == 1 ? "passenger" : "centre"), theme::textDim);

    demoNote = demo ? "Demo mode tunes a virtual car: a simulated " + tune::layoutSummary (cfg).toLowerCase()
                      + " install with realistic reflections and one hidden wiring mistake for the tuner to find. "
                        "No hardware is touched. Switch to Live in the header to tune a real system."
                    : juce::String();

    lastLine = session.hasTuneResult() ? "Last tune scored " + juce::String (session.getTuneResult().score) + " / 100"
                                       : juce::String();
    lastReportButton.setVisible (lastLine.isNotEmpty());

    options.setTargetName (cfg.target.name);
    resized();
    repaint();
}

int TuneIdleView::idealHeight (int width) const
{
    const auto L = computeLayout (width, twoColumn (width), demoNote, openAmount, TuneOptionsPanel::idealHeight (width - inner * 2),
                                  lastLine.isNotEmpty());
    return L.totalHeight;
}

int TuneIdleView::heroBlockHeight (int width) const
{
    return computeLayout (width, twoColumn (width), demoNote, 0.0f, 0, lastLine.isNotEmpty()).hero.getHeight();
}

void TuneIdleView::resized()
{
    const int w = getWidth();
    const auto L = computeLayout (w, twoColumn (w), demoNote, openAmount, TuneOptionsPanel::idealHeight (w - inner * 2),
                                  lastLine.isNotEmpty());
    if (lastLine.isNotEmpty())
    {
        const int tw = (int) std::ceil (juce::GlyphArrangement::getStringWidth (theme::font (13.0f), lastLine)) + 14;
        lastReportButton.setBounds (L.lastRow.withX (L.lastRow.getX() + tw).withWidth (160));
    }
    heroArea = L.hero;
    stepsArea = L.steps;
    chipsArea = L.chips;
    noteArea = L.note;
    optionsHeader = L.optionsHeader;

    startButton.setBounds (L.button);
    optionsButton.setBounds (L.optionsHeader.withWidth (118));

    int x = L.chips.getX();
    for (auto& c : chips)
    {
        const int cw = c->getIdealWidth();
        c->setBounds (x, L.chips.getY(), cw, L.chips.getHeight());
        x += cw + 8;
    }

    options.setBounds (L.optionsPanel.withHeight (TuneOptionsPanel::idealHeight (w - inner * 2)));
}

void TuneIdleView::paint (juce::Graphics& g)
{
    auto card = getLocalBounds().toFloat();
    theme::fillCard (g, card, theme::bg2, theme::border, theme::radius);

    // Atmosphere: a soft accent glow behind the headline, clipped to the card.
    {
        juce::Graphics::ScopedSaveState ss (g);
        juce::Path clip;
        clip.addRoundedRectangle (card, theme::radius);
        g.reduceClipRegion (clip);
        juce::ColourGradient glow (theme::accent.withAlpha (0.16f), (float) inner + 120.0f, (float) inner + 40.0f,
                                   theme::accent.withAlpha (0.0f), (float) inner + 520.0f, (float) inner + 320.0f, true);
        g.setGradientFill (glow);
        g.fillRect (card);
        // faint grid lines, instrument-like
        g.setColour (juce::Colours::white.withAlpha (0.018f));
        for (float x = card.getX(); x < card.getRight(); x += 48.0f)
            g.fillRect (x, card.getY(), 1.0f, card.getHeight());
    }

    const int w = getWidth();
    const auto L = computeLayout (w, twoColumn (w), demoNote, openAmount, TuneOptionsPanel::idealHeight (w - inner * 2),
                                  lastLine.isNotEmpty());

    g.setColour (theme::accent);
    g.setFont (theme::fontBold (11.0f));
    g.drawText ("ONE-CLICK AUTOMATIC TUNING", L.eyebrow, juce::Justification::centredLeft);

    drawText (g, theme::display (w >= 1100 ? 38.0f : 32.0f), theme::text, headline, L.title);
    drawText (g, theme::font (15.0f), theme::textDim, promise, L.promiseRect);

    if (lastLine.isNotEmpty())
    {
        const int score = session.hasTuneResult() ? session.getTuneResult().score : 0;
        auto row = L.lastRow;
        auto dot = row.removeFromLeft (14).withSizeKeepingCentre (8, 8).toFloat();
        g.setColour (theme::scoreColour (score));
        g.fillEllipse (dot);
        g.setColour (theme::textDim);
        g.setFont (theme::font (13.0f));
        g.drawText (lastLine, row, juce::Justification::centredLeft);
    }

    // "What happens" steps
    {
        auto r = L.steps;
        g.setColour (theme::textMuted);
        g.setFont (theme::fontBold (11.0f));
        g.drawText ("WHAT HAPPENS", r.removeFromTop (26), juce::Justification::centredLeft);

        static const AutoTuneEngine::Stage stages[] = {
            AutoTuneEngine::Stage::preflight, AutoTuneEngine::Stage::protect, AutoTuneEngine::Stage::measure,
            AutoTuneEngine::Stage::crossovers, AutoTuneEngine::Stage::solve, AutoTuneEngine::Stage::sumCheck,
            AutoTuneEngine::Stage::imaging, AutoTuneEngine::Stage::apply, AutoTuneEngine::Stage::verify,
            AutoTuneEngine::Stage::noise, AutoTuneEngine::Stage::report };

        const float railX = (float) r.getX() + 14.0f;
        g.setColour (theme::border);
        g.fillRect (railX - 0.5f, (float) r.getY() + 14.0f, 1.0f, (float) ((AutoTuneEngine::numWorkStages - 1) * 46));

        for (int i = 0; i < AutoTuneEngine::numWorkStages; ++i)
        {
            auto row = r.removeFromTop (46);
            juce::Rectangle<float> dot (railX - 11.0f, (float) row.getY() + 3.0f, 22.0f, 22.0f);
            g.setColour (theme::bg3);
            g.fillEllipse (dot);
            g.setColour (theme::borderHi);
            g.drawEllipse (dot.reduced (0.5f), 1.0f);
            g.setColour (theme::accent);
            g.setFont (theme::fontBold (10.5f));
            g.drawText (juce::String (i + 1), dot, juce::Justification::centred);

            auto text = row.withTrimmedLeft (38);
            g.setColour (theme::text);
            g.setFont (theme::fontMedium (13.0f));
            g.drawText (AutoTuneEngine::stageName (stages[i]), text.removeFromTop (18), juce::Justification::centredLeft);
            g.setColour (theme::textMuted);
            g.setFont (theme::font (11.5f));
            g.drawText (StageTimeline::shortDescription (stages[i]), text.removeFromTop (16), juce::Justification::centredLeft);
        }
    }

    if (demoNote.isNotEmpty())
    {
        auto n = L.note.toFloat();
        g.setColour (theme::warn.withAlpha (0.08f));
        g.fillRoundedRectangle (n, theme::radiusSm);
        g.setColour (theme::warn.withAlpha (0.35f));
        g.drawRoundedRectangle (n.reduced (0.5f), theme::radiusSm, 1.0f);
        icons::draw (g, icons::Id::info, juce::Rectangle<float> (n.getX() + 16.0f, n.getY() + 11.0f, 18.0f, 18.0f), theme::warn);
        drawText (g, theme::font (13.0f), theme::textDim, demoNote, L.note.reduced (46, 10).withLeft (L.note.getX() + 46));
    }

    // Options header rule
    g.setColour (theme::border);
    g.fillRect (L.optionsHeader.getX(), L.optionsHeader.getY() - 10, L.optionsHeader.getWidth(), 1);
    g.setColour (theme::textMuted);
    g.setFont (theme::font (12.0f));
    g.drawText ("Defaults suit most cars", L.optionsHeader.withTrimmedLeft (126), juce::Justification::centredLeft);
}
} // namespace fa::ui
