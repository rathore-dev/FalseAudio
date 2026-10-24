#include "AutoTunePage.h"

namespace fa::ui
{
AutoTunePage::AutoTunePage (Session& s)
    : Page (s), idleView (s), runningView (s), resultView (s)
{
    idleViewport.setViewedComponent (&idleView, false);
    idleViewport.setScrollBarsShown (true, false);
    idleViewport.setScrollBarThickness (8);
    addAndMakeVisible (idleViewport);
    addChildComponent (runningView);
    addChildComponent (resultView);

    idleView.onStart = [this] { startTune(); };
    idleView.onHeightChanged = [this] { resized(); };
    idleView.onViewReport = [this] { if (shell != nullptr) shell->showPage (PageId::report); };
    runningView.onCancel = [this] { cancelTune(); };
    resultView.onRunAgain = [this] { setState (State::idle); };
    resultView.onViewReport = [this] { if (shell != nullptr) shell->showPage (PageId::report); };

    session.getAutoTune().addListener (this);

    if (session.getAutoTune().isRunning())
    {
        runningView.begin();
        setState (State::running);
    }
    else if (session.hasTuneResult())
    {
        resultView.showResult (session.getTuneResult());
        setState (State::done);
    }
}

AutoTunePage::~AutoTunePage()
{
    session.getAutoTune().removeListener (this);
}

//==============================================================================
void AutoTunePage::setState (State s)
{
    state = s;
    idleViewport.setVisible (s == State::idle);
    runningView.setVisible (s == State::running);
    resultView.setVisible (s == State::done || s == State::failed);
    if (s == State::idle)
        idleView.refresh();
    resized();
    repaint();
}

void AutoTunePage::startTune()
{
    juce::String error;
    if (! session.startAutoTune (error))
    {
        if (shell != nullptr)
            shell->toast (error, AppShell::ToastKind::bad);
        return;
    }
    lastStage = AutoTuneEngine::Stage::preflight;
    runningView.begin();
    setState (State::running);
}

void AutoTunePage::cancelTune()
{
    session.getAutoTune().cancel();
    runningView.setProgress (lastStage, 0.0, "Cancelling - finishing the current sweep safely");
    if (shell != nullptr)
        shell->toast ("Cancelling auto tune", AppShell::ToastKind::warn);
}

//==============================================================================
void AutoTunePage::autoTuneStageChanged (AutoTuneEngine::Stage s)
{
    using Stage = AutoTuneEngine::Stage;
    if (s == Stage::done || s == Stage::failed || s == Stage::cancelled || s == Stage::idle)
        return;                                   // outcome handled in autoTuneFinished

    lastStage = s;
    if (state != State::running)
    {
        runningView.begin();                      // started from elsewhere (e.g. a shortcut)
        setState (State::running);
    }
    runningView.setStage (s);
}

void AutoTunePage::autoTuneProgress (AutoTuneEngine::Stage s, double fraction, const juce::String& message)
{
    if (state == State::running)
        runningView.setProgress (s, fraction, message);
}

void AutoTunePage::autoTuneMeasurement (const Measurement& m)
{
    if (state == State::running)
        runningView.addMeasurement (m);
}

void AutoTunePage::autoTuneFinished (const AutoTuneEngine::Result& r)
{
    const auto finalStage = session.getAutoTune().getStage();
    runningView.setOutcome (finalStage);

    if (r.success)
    {
        resultView.showResult (r);
        setState (State::done);
        if (shell != nullptr)
            shell->toast ("Auto tune complete - score " + juce::String (r.score) + " / 100",
                          r.score >= 65 ? AppShell::ToastKind::good : AppShell::ToastKind::warn);
    }
    else
    {
        const bool cancelled = finalStage == AutoTuneEngine::Stage::cancelled;
        resultView.showFailure (r.error, cancelled, lastStage, r.seconds);
        setState (State::failed);
        if (shell != nullptr && ! cancelled)
            shell->toast ("Auto tune stopped: " + r.error, AppShell::ToastKind::bad);
    }
}

//==============================================================================
void AutoTunePage::pageShown()
{
    if (state == State::idle)
        idleView.refresh();
}

void AutoTunePage::sessionChanged()
{
    if (state == State::idle)
        idleView.refresh();
}

void AutoTunePage::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg1);
}

void AutoTunePage::resized()
{
    const auto area = contentBounds();
    idleViewport.setBounds (area);
    int w = idleViewport.getWidth();
    int h = idleView.idealHeight (w);
    if (h > idleViewport.getHeight())                     // leave room for the scrollbar only when it shows
    {
        w -= idleViewport.getScrollBarThickness() + 6;
        h = idleView.idealHeight (w);
    }
    idleView.setSize (w, std::max (area.getHeight(), h));
    runningView.setBounds (area);
    resultView.setBounds (area);
}
} // namespace fa::ui
