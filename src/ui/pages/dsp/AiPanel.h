#pragma once

#include "DspCommon.h"
#include "AiOperatorFacade.h"

namespace fa::ui::dsp
{
/** "AI operator (any software)" panel: API key, model + budget cap, consent + STOP, live activity
    (steps, action log, thumbnail, cost) and the learned map. Talks only to an AiOperatorFacade. */
class AiPanel : public juce::Component
{
public:
    AiPanel();
    ~AiPanel() override;

    /** Binds the panel to an operator (not owned). Pass nullptr to unbind. Replaces callbacks. */
    void setFacade (AiOperatorFacade* f);

    /** The window the AI may operate: the attached DSP software ("" = none attached). */
    void setTarget (const juce::String& processName, const juce::String& windowTitle);

    /** Kill switch; safe to call any time (mode change, page hidden, shutdown). */
    void stopNow();
    bool isRunning() const;

    std::function<void (const juce::String&, AppShell::ToastKind)> toast;

    void resized() override;
    void visibilityChanged() override;

private:
    class Body;
    juce::Viewport viewport;
    std::unique_ptr<Body> body;
    AiOperatorFacade* facade = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AiPanel)
};
} // namespace fa::ui::dsp
