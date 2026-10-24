#pragma once

#include "../Page.h"
#include "../AppShell.h"
#include "../components/Widgets.h"
#include "../components/Plots.h"
#include "report/ReportDocument.h"

namespace fa::ui
{
/** Printable-looking report of the last tune with export buttons (text sheet, REW, Equalizer
    APO, JSON preset) and Save Session. Empty state until a tune has completed. */
class ReportPage : public Page
{
public:
    explicit ReportPage (Session& s);
    ~ReportPage() override;

    juce::String getTitle() const override    { return "Report"; }
    juce::String getSubtitle() const override { return "Results of the last tune"; }
    icons::Id getIcon() const override        { return icons::Id::document; }

    void pageShown() override;
    void sessionChanged() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    enum class Export { text, rew, apo, json, session };

    void refresh();
    void exportAs (Export kind);
    void writeAndToast (const juce::File& f, const juce::String& content, const juce::String& what);
    juce::String defaultBaseName() const;

    bool shownResult = false;
    int shownResultStamp = -1;

    juce::Viewport viewport;
    ReportDocument document;
    EmptyState empty { icons::Id::document, "No report yet",
                       "Run Auto Tune to see scores, before/after curves and every DSP setting here.",
                       "Go to Auto Tune" };

    FaButton textButton    { "Text sheet", FaButton::Style::secondary, icons::Id::copy };
    FaButton rewButton     { "REW filters", FaButton::Style::secondary, icons::Id::download };
    FaButton apoButton     { "Equalizer APO", FaButton::Style::secondary, icons::Id::download };
    FaButton jsonButton    { "JSON preset", FaButton::Style::secondary, icons::Id::download };
    FaButton sessionButton { "Save session", FaButton::Style::primary, icons::Id::save };

    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReportPage)
};
} // namespace fa::ui
