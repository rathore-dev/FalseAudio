#include "ScoreStrip.h"

namespace fa::ui
{
namespace
{
    constexpr int ringsCardH = 236;
    constexpr int judgeW     = 380;
    constexpr int judgeRowH  = 30;
    constexpr int judgeTop   = 54;

    double errorToScore (double errDb)      { return juce::jlimit (0.0, 100.0, 100.0 - errDb * 14.0); }
    const juce::Font commentFont()          { return theme::font (11.5f); }

    juce::String judgeDetail (double score)
    {
        if (score >= 90) return "reference";
        if (score >= 80) return "excellent";
        if (score >= 65) return "good";
        if (score >= 45) return "fair";
        return "weak";
    }
}

ScoreStrip::ScoreStrip()
{
    for (auto* r : { &overall, &tonal, &imaging, &clean })
        addAndMakeVisible (r);
    overall.setCaption ("TUNE SCORE");
    tonal.setCaption ("TONAL");
    imaging.setCaption ("IMAGING");
    clean.setCaption ("CLEANLINESS");
}

void ScoreStrip::set (const AutoTuneEngine::Result& r, const insights::Insights& in)
{
    overallScore = r.score;
    verdict = insightsui::verdictFor (r.score, in.tonalBalanceScore, in.imagingScore, in.cleanlinessScore);

    juce::StringArray lines;
    for (int i = 0; i < in.summary.size() && i < 3; ++i)
        lines.add (in.summary[i]);
    if (lines.isEmpty())
        lines.add ("Average deviation from target " + juce::String (0.5 * (r.leftErrorDb + r.rightErrorDb), 1)
                   + " dB, left/right match " + juce::String (r.matchDb, 1) + " dB.");
    summary = lines.joinIntoString ("  ");

    overall.setScore (r.score, true);
    tonal.setScore (juce::roundToInt (in.tonalBalanceScore), true);
    imaging.setScore (juce::roundToInt (in.imagingScore), true);
    clean.setScore (juce::roundToInt (in.cleanlinessScore), true);

    // Simulated SQ judge (advanced::judge) when the tune carries one; insight rows otherwise.
    const auto& sq = r.sqScore;
    if (! sq.categories.empty() || sq.total > 0.0)
    {
        std::vector<JudgeRow> jr;
        for (const auto& [name, score] : sq.categories)
            jr.push_back ({ name, score, judgeDetail (score) });
        judgeTotal = sq.total;
        setJudgeRows (std::move (jr), sq.comments);
        return;
    }

    std::vector<JudgeRow> def;
    def.push_back ({ "Tonal balance",   in.tonalBalanceScore,       "vs target curve" });
    def.push_back ({ "Imaging",         in.imagingScore,            "L/R match, sum dips" });
    def.push_back ({ "Cleanliness",     in.cleanlinessScore,        "distortion + noise" });
    def.push_back ({ "Left accuracy",   errorToScore (r.leftErrorDb),  juce::String (r.leftErrorDb, 1) + " dB RMS" });
    def.push_back ({ "Right accuracy",  errorToScore (r.rightErrorDb), juce::String (r.rightErrorDb, 1) + " dB RMS" });
    def.push_back ({ "L / R symmetry",  errorToScore (r.matchDb),      juce::String (r.matchDb, 1) + " dB" });
    judgeTotal = -1.0;
    setJudgeRows (std::move (def));
}

void ScoreStrip::setJudgeRows (std::vector<JudgeRow> newRows, juce::StringArray newComments)
{
    rows = std::move (newRows);
    comments = std::move (newComments);
    resized();
    repaint();
}

int ScoreStrip::judgeWidth (int width) const noexcept
{
    return stacked (width) ? width : std::min (judgeW, width / 3);
}

int ScoreStrip::judgeHeight (int width) const
{
    int h = judgeTop + (int) rows.size() * judgeRowH + theme::pad;
    if (! comments.isEmpty())
    {
        h += 10;
        const int textW = judgeWidth (width) - 2 * theme::pad - 22;
        for (const auto& c : comments)
            h += std::max (16, insightsui::wrappedHeight (c, textW, commentFont())) + 4;
    }
    return h;
}

int ScoreStrip::idealHeight (int width) const
{
    if (stacked (width))
        return ringsCardH + insightsui::gap + judgeHeight (width);
    return std::max (ringsCardH, judgeHeight (width));
}

void ScoreStrip::resized()
{
    auto a = getLocalBounds();
    if (stacked (a.getWidth()))
    {
        ringsArea = a.removeFromTop (ringsCardH);
        a.removeFromTop (insightsui::gap);
        judgeArea = a;
    }
    else
    {
        judgeArea = a.removeFromRight (judgeWidth (a.getWidth()));
        a.removeFromRight (insightsui::gap);
        ringsArea = a.withHeight (ringsCardH);
    }

    auto inner = ringsArea.reduced (theme::pad);
    inner.removeFromTop (22);                       // eyebrow
    const int big = std::min (inner.getHeight(), 172);
    overall.setBounds (inner.removeFromLeft (big).withSizeKeepingCentre (big, big));
    inner.removeFromLeft (insightsui::gap);

    const int wantText = std::min (260, std::max (120, inner.getWidth() / 3));
    textArea = inner.removeFromLeft (wantText);
    inner.removeFromLeft (insightsui::gap);
    dividerX = inner.getX();
    inner.removeFromLeft (insightsui::gap);

    const int ringSize = juce::jlimit (56, 132, std::min ((inner.getWidth() - 2 * insightsui::gap) / 3, inner.getHeight()));
    const int totalW = ringSize * 3 + 2 * insightsui::gap;
    inner = inner.withSizeKeepingCentre (totalW, inner.getHeight());
    for (auto* r : { &tonal, &imaging, &clean })
    {
        r->setBounds (inner.removeFromLeft (ringSize).withSizeKeepingCentre (ringSize, ringSize));
        inner.removeFromLeft (insightsui::gap);
    }
}

void ScoreStrip::paint (juce::Graphics& g)
{
    // ---- rings card --------------------------------------------------------
    theme::fillCard (g, ringsArea.toFloat());
    theme::drawEyebrow (g, "SCORES", ringsArea.reduced (theme::pad).removeFromTop (16));

    auto t = textArea;
    g.setColour (theme::scoreColour (overallScore));
    g.setFont (theme::display (22.0f));
    g.drawText (verdict, t.removeFromTop (30), juce::Justification::centredLeft, true);
    t.removeFromTop (4);
    insightsui::drawWrapped (g, summary, t, theme::font (12.5f), theme::textDim);

    g.setColour (theme::border);
    g.fillRect (dividerX, ringsArea.getY() + 48, 1, ringsArea.getHeight() - 64);

    // ---- judge sheet -------------------------------------------------------
    theme::fillCard (g, judgeArea.toFloat());
    auto j = judgeArea.reduced (theme::pad);
    auto eyebrowRow = j.removeFromTop (16);
    if (judgeTotal >= 0.0)
    {
        // total on the right of the eyebrow, in a score-coloured pill
        const int total = juce::roundToInt (judgeTotal);
        const auto txt = "SQ JUDGE  " + juce::String (total) + " / 100";
        const int pw = insightsui::pillWidth (txt);
        auto pill = eyebrowRow.removeFromRight (pw).withHeight (20).withY (eyebrowRow.getY() - 2);
        insightsui::drawPill (g, pill, txt, theme::scoreColour (total));
    }
    theme::drawEyebrow (g, "JUDGE SHEET", eyebrowRow);
    g.setColour (theme::textMuted);
    g.setFont (theme::font (11.5f));
    g.drawText (judgeTotal >= 0.0 ? "Simulated competition judge on the final settings"
                                  : "How a competition judge would hear this tune",
                j.removeFromTop (18), juce::Justification::centredLeft, true);
    j.removeFromTop (judgeTop - 34 - theme::pad);

    const int nameW  = std::min (136, j.getWidth() / 3);
    const int valueW = 40;
    const int detailW = std::min (104, j.getWidth() / 4);

    for (const auto& row : rows)
    {
        auto r = j.removeFromTop (judgeRowH);
        const int score = juce::roundToInt (row.score);
        const auto c = theme::scoreColour (score);

        g.setColour (theme::text);
        g.setFont (theme::font (12.5f));
        g.drawText (row.name, r.removeFromLeft (nameW), juce::Justification::centredLeft, true);

        auto detail = r.removeFromRight (detailW);
        g.setColour (theme::textMuted);
        g.setFont (theme::mono (11.0f));
        g.drawText (row.detail, detail, juce::Justification::centredRight, true);

        auto value = r.removeFromRight (valueW);
        g.setColour (c);
        g.setFont (theme::fontMedium (12.5f));
        g.drawText (juce::String (score), value, juce::Justification::centredRight, true);

        auto bar = r.reduced (8, 0).withSizeKeepingCentre (r.getWidth() - 16, 6).toFloat();
        g.setColour (theme::bg4);
        g.fillRoundedRectangle (bar, 3.0f);
        auto fill = bar.withWidth (bar.getWidth() * (float) juce::jlimit (0.0, 1.0, row.score / 100.0));
        if (fill.getWidth() > 0.5f)
        {
            theme::drawGlow (g, fill, c.withAlpha (0.35f), 3.0f, 4.0f);
            g.setColour (c);
            g.fillRoundedRectangle (fill, 3.0f);
        }
    }

    if (! comments.isEmpty())
    {
        j.removeFromTop (10);
        for (const auto& c : comments)
        {
            const auto style = insightsui::styleForFinding (c);
            const int h = std::max (16, insightsui::wrappedHeight (c, j.getWidth() - 22, commentFont()));
            auto line = j.removeFromTop (h);
            icons::draw (g, style.icon, line.removeFromLeft (16).withHeight (16).toFloat().reduced (1.0f), style.colour);
            line.removeFromLeft (6);
            insightsui::drawWrapped (g, c, line, commentFont(), theme::textDim);
            j.removeFromTop (4);
        }
    }
}
} // namespace fa::ui
