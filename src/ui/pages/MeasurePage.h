#pragma once

#include "../Page.h"
#include "../AppShell.h"
#include "../components/Widgets.h"
#include "../components/Plots.h"
#include "measure/MeasurementLibrary.h"
#include "measure/WaterfallView.h"
#include "measure/StatStrip.h"
#include "core/measure/Analysis.h"
#include "core/dsp/Spectrum.h"

namespace fa::ui
{
/** Manual sweep measurements and their analysis: SPL, phase, group delay, impulse, ETC,
    step, harmonic distortion and spectral decay. The library on the left shows every
    measurement in the session (including the ones captured by Auto Tune). */
class MeasurePage : public Page,
                    private juce::Timer
{
public:
    explicit MeasurePage (Session& s);
    ~MeasurePage() override;

    juce::String getTitle() const override    { return "Measure"; }
    juce::String getSubtitle() const override { return "Sweep measurements and analysis"; }
    icons::Id getIcon() const override        { return icons::Id::waveform; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void pageShown() override;
    void pageHidden() override;
    void sessionChanged() override;

private:
    enum class View { spl, phase, groupDelay, impulse, etc, step, distortion, waterfall };

    struct ItemState
    {
        juce::Colour colour;
        bool visible = true;
    };

    // ---- toolbar ----
    SegmentedControl feedSelector { { "L", "R", "L+R" } };
    juce::TextEditor labelField;
    FaButton measureButton { "Measure", FaButton::Style::primary, icons::Id::play };
    ProgressBarFa progress;
    SegmentedControl smoothingSelector { { "Off", "1/48", "1/24", "1/12", "1/6", "1/3", "1/1", "Psy", "Var" } };
    ToggleSwitch targetToggle { "Target" };
    FaButton exportButton { "Export", FaButton::Style::secondary, icons::Id::download };
    std::unique_ptr<juce::FileChooser> chooser;
    void exportSelected();

    // ---- library ----
    MeasurementLibrary library;

    // ---- main ----
    SegmentedControl viewSelector { { "SPL", "Phase", "Group delay", "Impulse", "ETC", "Step", "Distortion", "Waterfall" } };
    FrequencyPlot freqPlot;
    TimePlot timePlot;
    WaterfallView waterfall;
    StatStrip details;

    std::map<juce::String, ItemState> itemStates;
    juce::String selectedId;
    int paletteCursor = 0;
    View view = View::spl;
    juce::String lastRangeKey;

    // analysis cache for the selected measurement (expensive views)
    struct Cache
    {
        juce::String id;
        bool haveDistortion = false, haveWaterfall = false, haveStep = false, haveEtc = false;
        analysis::Distortion distortion;
        analysis::Waterfall waterfall;
        std::vector<float> step, etc;
    } cache;

    // measuring state
    bool measuring = false;
    juce::uint32 measureStarted = 0;
    juce::String statusText;
    juce::Colour statusColour = theme::textDim;

    juce::Rectangle<int> toolbarArea, mainCardArea, statusArea;

    void timerCallback() override;
    void startMeasurement();
    void refreshLibrary();
    void rebuildView();
    void rebuildDetails();
    void setView (View v);
    void ensureCache (const Measurement& m);
    void syncColours();

    const Measurement* selectedMeasurement() const;
    const Measurement* findMeasurement (const juce::String& id) const;
    ItemState& stateFor (const juce::String& id);
    FrequencyResponse smoothed (const FrequencyResponse& r) const;
    juce::String displayName (const Measurement& m) const;
    bool isFromTune (const juce::String& id) const;

    static juce::Colour paletteColour (int index);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeasurePage)
};
} // namespace fa::ui
