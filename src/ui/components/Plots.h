#pragma once

#include "../Page.h"

/** Graph components. API is a CONTRACT (pages code against it); implementation is owned by the
    plots worker. */
namespace fa::ui
{
/** One curve on a FrequencyPlot. */
struct PlotTrace
{
    juce::String id;                 // stable key (replacing a trace with the same id updates it)
    juce::String label;              // legend text
    juce::Colour colour = theme::accent;
    std::vector<double> freqs;       // Hz, ascending
    std::vector<double> values;      // dB (magnitude), degrees (phase) or ms (group delay)
    std::vector<double> alpha;       // optional per-point 0..1 opacity (e.g. coherence), empty = 1
    float thickness = 2.0f;
    bool dashed = false;
    bool fillToBottom = false;       // gradient area fill under the curve
    bool visible = true;
    bool bars = false;               // draw as RTA bars (each point = one band)

    static PlotTrace fromResponse (const juce::String& id, const juce::String& label, juce::Colour c,
                                   const FrequencyResponse& r, double smoothingFraction = 0.0);
    static PlotTrace fromPhase (const juce::String& id, const juce::String& label, juce::Colour c,
                                const FrequencyResponse& r);
    static PlotTrace fromDb (const juce::String& id, const juce::String& label, juce::Colour c,
                             const std::vector<double>& freqs, const std::vector<double>& db);
};

/** Log-frequency plot (default 10 Hz..24 kHz) with decade/third-octave grid, axis labels,
    smooth antialiased curves, hover crosshair readout (frequency + every visible trace value),
    mouse-wheel zoom and drag pan on both axes, double-click to reset, and a legend row of
    clickable chips that toggle visibility.

    Optional editable nodes (used by the EQ editor): draggable handles; drag = freq/gain,
    wheel or shift-drag = Q. */
class FrequencyPlot : public juce::Component
{
public:
    enum class Units { dB, degrees, ms, coherence };

    FrequencyPlot();
    ~FrequencyPlot() override;

    void setUnits (Units u);
    void setRanges (double fMin, double fMax, double yMin, double yMax);   // also the reset view
    void setTrace (const PlotTrace& t);          // add or replace by id
    void removeTrace (const juce::String& id);
    void clearTraces();
    void setTraceVisible (const juce::String& id, bool visible);
    void setShowLegend (bool b);
    void setTitle (const juce::String& t);       // small caption top-left inside the plot

    /** Shaded vertical band, e.g. a crossover region. */
    void setBands (std::vector<std::tuple<double, double, juce::Colour>> regions);

    struct Node
    {
        juce::String id;
        double freq = 1000.0, value = 0.0, q = 1.0;
        juce::Colour colour = theme::accent;
        bool selected = false;
    };
    void setNodes (std::vector<Node> nodes);
    std::function<void (const Node&)> onNodeChanged;     // while dragging
    std::function<void (const juce::String&)> onNodeSelected;
    std::function<void (double freq, double value)> onBackgroundDoubleClick; // e.g. add EQ band

    // coordinate helpers (public for overlays)
    float xForFreq (double f) const;
    float yForValue (double v) const;
    double freqForX (float x) const;
    double valueForY (float y) const;
    juce::Rectangle<float> getPlotArea() const;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

/** Time-domain plot for impulse/step responses: x in ms, y linear (normalised) or dB envelope. */
class TimePlot : public juce::Component
{
public:
    TimePlot();
    ~TimePlot() override;
    struct Trace { juce::String id, label; juce::Colour colour; std::vector<float> samples; double sampleRate = 48000; double startMs = 0; };
    void setTraces (std::vector<Trace> traces);
    void setRangeMs (double t0, double t1);
    void setDbMode (bool envelopeDb);
    /** Vertical marker lines (e.g. detected arrival) */
    void setMarkers (std::vector<std::pair<double, juce::Colour>> msMarkers);
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

/** Animated circular score gauge 0..100 with label underneath. */
class ScoreRing : public juce::Component, private juce::Timer
{
public:
    ScoreRing();
    void setScore (int score, bool animate = true);
    void setCaption (const juce::String& c);
    void paint (juce::Graphics&) override;
private:
    void timerCallback() override;
    float shown = 0, target = 0;
    juce::String caption;
};

/** Top-down car silhouette with speaker positions coloured by role. Highlights a channel id;
    optional concentric arrival rings (ms per channel) for the Alignment page. */
class CarView : public juce::Component
{
public:
    CarView();
    void setConfig (const SystemConfig& cfg);
    void setHighlight (const juce::String& channelId);
    void setArrivals (std::map<juce::String, double> arrivalMs);
    void setSeat (int seat); // 0 driver, 1 passenger, 2 centre
    std::function<void (const juce::String& channelId)> onChannelClicked;
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
private:
    SystemConfig config;
    juce::String highlight, hover;
    std::map<juce::String, double> arrivals;
    int seat = 0;
    std::map<juce::String, juce::Point<float>> speakerPositions() const;
};
} // namespace fa::ui
