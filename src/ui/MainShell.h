#pragma once

#include "AppShell.h"
#include "shell/NavRail.h"
#include "shell/HeaderBar.h"
#include "shell/OverlayLayer.h"

namespace fa::ui
{
/** The window's content: header bar, navigation rail, page area and overlay (sheets + toasts).
    Owns every page. Also the single place that answers AutoTuneEngine's user prompts
    (move the mic / apply settings on a manual DSP / confirm) with a MessageSheet. */
class MainShell : public AppShell,
                  private juce::ChangeListener,
                  private AutoTuneEngine::Listener,
                  private juce::Timer
{
public:
    explicit MainShell (Session& session);
    ~MainShell() override;

    // AppShell
    void showPage (PageId id, const juce::String& tab = {}) override;
    void refreshHeader() override;
    Page* getPage (PageId id) override;
    void showSheet (std::unique_ptr<juce::Component> content, int width, int height) override;
    void closeSheet() override;
    void toast (const juce::String& message, ToastKind kind = ToastKind::info) override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    /** Hosts the current page on a raised, rounded workspace surface. */
    class PageHost : public juce::Component
    {
    public:
        void paint (juce::Graphics&) override;
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void autoTuneStageChanged (AutoTuneEngine::Stage) override;
    void autoTuneProgress (AutoTuneEngine::Stage, double, const juce::String&) override;
    void autoTuneNeedsUser (const AutoTuneEngine::UserAction&) override;
    void autoTuneFinished (const AutoTuneEngine::Result&) override;

    // DEV-ONLY automation (inert unless launched with --qa-script <file>)
    void timerCallback() override;
    bool runQaLine (const juce::String& line);
    juce::Component* findQa (juce::Component& root, const juce::String& key);
    juce::StringArray qaLines;
    int qaIndex = 0;
    int qaWaitMs = 0;

    void buildPages();
    void setMode (Session::Mode m);
    static int indexOf (PageId id) noexcept { return (int) id; }

    Session& session;
    std::vector<std::unique_ptr<Page>> pages;     // indexed by PageId
    PageId currentPage = PageId::autoTune;
    bool hasCurrentPage = false;

    NavRail rail;
    HeaderBar header;
    PageHost pageHost;
    OverlayLayer overlay;
    juce::TooltipWindow tooltips { this, 500 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainShell)
};
} // namespace fa::ui
