#pragma once

#include "../../Page.h"
#include "../../components/Widgets.h"
#include "../../components/Plots.h"

/** Small building blocks shared by the Auto Tune and Report pages: colour lookups, text
    formatting, findings/notes models and a few lightweight presentational components. */
namespace fa::ui::tune
{
    /** Role colour (side-tinted) of a channel id; sensible colours for the engine's pseudo ids
        ("Verify L", "Verify R", "Verify L+R", "Level check"). */
    juce::Colour colourForChannelId (const SystemConfig& cfg, const juce::String& id);
    juce::String labelForChannelId (const SystemConfig& cfg, const juce::String& id);

    /** "3-way + sub", "2-way + stereo sub", "Full range". */
    juce::String layoutSummary (const SystemConfig& cfg);
    juce::String dspKindName (Session& session);
    juce::String formatDuration (double seconds);
    juce::String formatDelayCm (double ms);

    struct Finding
    {
        enum class Kind { problemFixed, warning, info, good };
        Kind kind = Kind::info;
        juce::String title;
        juce::String detail;
        juce::Colour colour;
        icons::Id icon = icons::Id::info;
    };

    /** Turns the solver report into user-facing findings (wiring fixed, dips, low SNR...). */
    std::vector<Finding> buildFindings (const AutoTuneEngine::Result& r, const SystemConfig& cfg);

    struct NoteGroup
    {
        juce::String title;          // channel label or "System"
        juce::Colour colour;
        juce::StringArray lines;
    };

    /** Groups "Channel: text" report notes per channel, in config order. */
    std::vector<NoteGroup> groupNotes (const juce::StringArray& notes, const SystemConfig& cfg);

    /** Offsets that make curves readable regardless of the absolute measurement scale. */
    double referenceLevel (const std::vector<double>& freqs, const std::vector<double>& db, double f1 = 200.0, double f2 = 2000.0);

    /** Builds the before/after/target traces for one side ("L", "R" or "L+R") onto a plot. */
    void populateResultPlot (FrequencyPlot& plot, const AutoTuneEngine::Result& r, int side);
}

namespace fa::ui
{
/** Compact metric: small caption, large tabular value, optional unit and tint. */
class MetricTile : public juce::Component
{
public:
    MetricTile (const juce::String& caption = {}, const juce::String& unit = {});
    void set (const juce::String& value, juce::Colour tint = theme::text, const juce::String& hint = {});
    void setCaption (const juce::String& c) { caption = c; repaint(); }
    void paint (juce::Graphics&) override;
private:
    juce::String caption, unit, value, hint;
    juce::Colour tint = theme::text;
};

/** List of findings with icons. Height grows with content; call getIdealHeight (width). */
class FindingsList : public juce::Component
{
public:
    void setFindings (std::vector<tune::Finding> f);
    int getIdealHeight (int width) const;
    void paint (juce::Graphics&) override;
private:
    std::vector<tune::Finding> findings;
    int rowHeight (const tune::Finding& f, int width) const;
};

/** Simulated SQ judge: big total, one bar per category (tonal, bass, stage, centre, imaging,
    linearity, noise), the refine cost delta and the judge's comments. */
class JudgePanel : public juce::Component
{
public:
    void set (const advanced::JudgeScore& score, double refineCostBefore, double refineCostAfter);
    bool hasData() const noexcept { return ! categories.empty(); }
    int getIdealHeight (int width) const;
    void paint (juce::Graphics&) override;
private:
    struct Category { juce::String name; double value = 0; int weightPercent = 0; };
    std::vector<Category> categories;
    double total = 0, costBefore = 0, costAfter = 0;
    juce::StringArray comments;
    int columns (int width) const { return width >= 720 ? 2 : 1; }
    int barsHeight (int width) const;
    int commentsHeight (int width) const;
};

/** "What changed" list: channel groups with coloured stripes and bullet lines. */
class NotesView : public juce::Component
{
public:
    void setGroups (std::vector<tune::NoteGroup> g);
    int getIdealHeight (int width) const;
    void paint (juce::Graphics&) override;
private:
    std::vector<tune::NoteGroup> groups;
    int groupHeight (const tune::NoteGroup& g, int width) const;
};
} // namespace fa::ui
