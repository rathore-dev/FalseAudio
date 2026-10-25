#pragma once

#include "InstallKit.h"
#include "core/guide/SetupGuide.h"
#include <atomic>
#include <functional>
#include <memory>

namespace fa::ui
{
/** Measurement setup: head-centred mic placement diagram (top + side view, numbered dots),
    interface setup steps for the selected interface/mic, and two live checks (gain staging,
    background noise) that run on a background thread with cancel. */
class MeasurementSetup : public Card
{
public:
    explicit MeasurementSetup (Session& session);
    ~MeasurementSetup() override;

    void refresh (const gear::Product* mic, const gear::Product* audioInterface);
    int heightFor (int width) const;

    std::function<void()> onSwitchToLive;
    std::function<void (const juce::String&, AppShell::ToastKind)> onToast;

    void resized() override;

private:
    // ---- mic placement diagram
    class MicDiagram : public juce::Component
    {
    public:
        guide::MicPlacement placement;
        int heightFor (int width) const;
        void paint (juce::Graphics&) override;
    private:
        void drawView (juce::Graphics&, juce::Rectangle<float> area, bool topView) const;
        static int viewSize (int width);
    };

    // ---- interface steps
    class StepList : public juce::Component
    {
    public:
        std::vector<guide::Step> steps;
        int heightFor (int width) const;
        int stepHeight (const guide::Step& s, int width) const;
        void paint (juce::Graphics&) override;
    };

    // ---- one live check (button + status + result)
    class CheckRow : public juce::Component
    {
    public:
        CheckRow (const juce::String& name, const juce::String& hint, icons::Id icon);
        ~CheckRow() override;

        std::function<guide::Step (std::atomic<bool>&)> job;    // runs on the background thread
        std::function<void (const guide::Step&)> onFinished;
        void setEnabledForMode (bool live, const juce::String& whyNot);
        bool isRunning() const noexcept;
        int heightFor (int width) const;

        void resized() override;
        void paint (juce::Graphics&) override;

    private:
        class Worker;
        void start();
        void cancelRun();
        void finished (const guide::Step& s);

        juce::String name, hint, disabledReason;
        icons::Id icon;
        guide::Step result;
        bool hasResult = false, live = false;
        FaButton button { "Run", FaButton::Style::secondary, icons::Id::play };
        std::unique_ptr<Worker> thread;
    };

    int diagramWidth (int width) const;
    bool twoColumns (int width) const { return width >= 1000; }

    Session& session;
    MicDiagram diagram;
    StepList steps;
    ikit::Note orientation { "", 12.5f, theme::text };
    ikit::Note demoNote { "These checks need Live mode: in Demo the microphones are simulated.", 12.5f, theme::warn };
    FaButton liveBtn { "Switch to Live", FaButton::Style::primary, icons::Id::plug };
    CheckRow gainCheck  { "Gain staging check",     "Plays pink noise at the sweep level and checks the mic peak, RMS and noise floor.", icons::Id::activity };
    CheckRow noiseCheck { "Background noise check", "Measures the cabin noise per octave; run with the engine and HVAC as you'll measure.", icons::Id::mic };
    bool demo = true;
};
} // namespace fa::ui
