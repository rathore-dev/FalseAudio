#pragma once

#include "../Page.h"
#include "../AppShell.h"
#include "../components/Widgets.h"
#include "../components/Plots.h"
#include "install/InstallKit.h"
#include "install/SummaryHeader.h"
#include "install/WiringCard.h"
#include "install/ChecklistCard.h"
#include "install/GainTable.h"
#include "install/MeasurementSetup.h"

namespace fa::ui
{
/** Install & Gains: every manual setting, exactly. Built from install::buildPlan() for the
    installation chosen on the Equipment page: summary numbers, wire checks, a tickable
    step-by-step checklist, the per-channel gain table and the measurement setup guide. */
class InstallPage : public Page
{
public:
    explicit InstallPage (Session& s);
    ~InstallPage() override;

    juce::String getTitle() const override    { return "Install & Gains"; }
    juce::String getSubtitle() const override { return "Wire checks, gain settings and every manual adjustment"; }
    icons::Id getIcon() const override        { return icons::Id::sliders; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void pageShown() override;
    void sessionChanged() override;

private:
    /** Listens to the InstallationStore (Page already owns the Session listener privately). */
    struct StoreListener : public juce::ChangeListener
    {
        explicit StoreListener (InstallPage& o) : owner (o) {}
        void changeListenerCallback (juce::ChangeBroadcaster*) override { owner.rebuild(); }
        InstallPage& owner;
    };
    StoreListener storeListener { *this };

    void rebuild();
    int layoutContent (int width);
    void applySafeLimits();
    void toast (const juce::String& msg, AppShell::ToastKind kind = AppShell::ToastKind::info);
    static bool hasEquipment (const install::Installation& inst);

    ikit::ScrollBody body;
    SummaryHeader summary;
    WiringCard wiring;
    ChecklistCard checklist;
    GainTable gains;
    MeasurementSetup setup;
    EmptyState empty { icons::Id::plug, "No equipment selected yet",
                       "Pick your head unit, DSP, amplifiers, speakers and wiring under Car > Equipment - "
                       "this page then works out every setting for you.", "Go to Equipment" };

    install::Plan plan;
    install::Installation installation;
    bool showingEmpty = true;
    bool pendingRebuild = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InstallPage)
};
} // namespace fa::ui
