#pragma once

#include "../Page.h"
#include "../AppShell.h"
#include "../components/Widgets.h"
#include "../components/Plots.h"
#include "system/PageKit.h"
#include "alignment/ChannelRow.h"
#include "alignment/ArrivalTimeline.h"

namespace fa::ui
{
/** Alignment page: car view with arrival rings, per-channel delay / polarity / gain / mute,
    arrival timeline, zero-all and tape-measure helpers. */
class AlignmentPage : public Page
{
public:
    explicit AlignmentPage (Session& s);
    ~AlignmentPage() override;

    juce::String getTitle() const override    { return "Alignment"; }
    juce::String getSubtitle() const override { return "Delay, polarity and level per channel"; }
    icons::Id getIcon() const override        { return icons::Id::clock; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void pageShown() override;
    void sessionChanged() override;

private:
    class TableHeader : public juce::Component
    {
    public:
        void paint (juce::Graphics&) override;
    };

    int layoutContent (int width);
    void refreshFromSession();
    void rebuildRows();
    void rebuildTape();
    void updateTimeline();
    void selectChannel (const juce::String& id);
    void editConfig (std::function<void (SystemConfig&)> edit);
    void applyTape();
    void toast (const juce::String& msg, AppShell::ToastKind kind = AppShell::ToastKind::info);
    std::map<juce::String, double> arrivals() const;

    kit::ScrollBody body;

    Card carCard { "Arrival rings", "Time from each driver to the seat" };
    CarView car;
    kit::Note carHint;

    Card tableCard { "Channels", "" };
    FaButton zeroBtn { "Zero all delays", FaButton::Style::secondary, icons::Id::refresh };
    FaButton tapeBtn { "Distances from tape measure", FaButton::Style::secondary, icons::Id::move };
    TableHeader header;
    std::vector<std::unique_ptr<AlignmentRow>> rows;
    juce::String selectedId;

    Card timelineCard { "Arrival timeline", "" };
    ArrivalTimeline timeline;

    Card tapeCard { "Tape measure", "Driver centre to ear, in cm" };
    std::vector<std::unique_ptr<ValueField>> tapeFields;
    std::vector<juce::String> tapeIds;
    FaButton tapeApply { "Apply delays", FaButton::Style::primary, icons::Id::check };
    FaButton tapeCancel { "Cancel", FaButton::Style::ghost };
    kit::Note tapePreview;
    bool tapeVisible = false;

    Card noteCard { "Process rule", "" };
    kit::Note noteText { "Delay only corrects distance: every driver should arrive at the same instant, with the farthest "
                         "driver at zero delay. Once arrivals match, the stereo image is moved with LEVEL, never with delay - "
                         "adding delay to pull the centre image over also smears the crossovers and the midbass impact. "
                         "Polarity flips fix reversed wiring; a flip that \"sounds better\" usually means a crossover phase "
                         "problem instead. Auto Tune measures all of this for you; edit here only to fine-tune." };

    bool updating = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AlignmentPage)
};
} // namespace fa::ui
