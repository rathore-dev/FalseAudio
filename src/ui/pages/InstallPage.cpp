#include "InstallPage.h"
#include "install/InstallationAccess.h"

namespace fa::ui
{
InstallPage::InstallPage (Session& s)
    : Page (s),
      checklist (s.getAppDataFolder().getChildFile ("install_checklist.json")),
      setup (s)
{
    addAndMakeVisible (body);
    auto& c = body.content();
    c.addAndMakeVisible (summary);
    c.addAndMakeVisible (wiring);
    c.addAndMakeVisible (checklist);
    c.addAndMakeVisible (gains);
    c.addAndMakeVisible (setup);
    addChildComponent (empty);

    body.layout = [this] (int w) { return layoutContent (w); };

    checklist.onLayoutChanged = [this] { body.relayout(); };
    checklist.onProgressChanged = [this]
    {
        // progress chips in the header
        SummaryHeader::Health h;
        h.stepsDone = checklist.countDone();
        h.stepsTotal = checklist.countTotal();
        h.danger = checklist.countOpen (ikit::Severity::danger);
        h.important = checklist.countOpen (ikit::Severity::important);
        for (auto& w : plan.wires) if (! w.ok) ++h.wireUpgrades;
        for (auto& ch : plan.channels) { h.channelWarnings += ch.warnings.size(); if (ch.isDanger()) ++h.danger; }
        summary.setPlan (plan, installation, gear::GearDatabase::instance(), h);
    };
    gains.onApply = [this] { applySafeLimits(); };
    setup.onSwitchToLive = [this]
    {
        session.setMode (Session::Mode::live);
        if (session.getMode() == Session::Mode::live)
            toast ("Switched to Live mode", AppShell::ToastKind::good);
        else
            toast ("Can't switch mode while Auto Tune is running", AppShell::ToastKind::warn);
    };
    setup.onToast = [this] (const juce::String& m, AppShell::ToastKind k) { toast (m, k); };
    empty.onAction = [this] { if (shell != nullptr) shell->showPage (PageId::car, "equipment"); };

    InstallationStore::instance().addChangeListener (&storeListener);
}

InstallPage::~InstallPage()
{
    InstallationStore::instance().removeChangeListener (&storeListener);
}

void InstallPage::pageShown()
{
    auto& db = gear::GearDatabase::instance();
    if (db.size() == 0)
        db.loadDefaults();
    rebuild();
}

void InstallPage::sessionChanged()
{
    // config / mode changes affect the plan (HP values, mode-gated checks); coalesce bursts
    if (pendingRebuild)
        return;
    pendingRebuild = true;
    juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<InstallPage> (this)]
    {
        if (sp != nullptr)
        {
            sp->pendingRebuild = false;
            sp->rebuild();
        }
    });
}

bool InstallPage::hasEquipment (const install::Installation& inst)
{
    return inst.headUnitId.isNotEmpty() || inst.dspId.isNotEmpty() || inst.dacId.isNotEmpty()
        || ! inst.amps.empty() || ! inst.drivers.empty();
}

void InstallPage::rebuild()
{
    installation = InstallationStore::instance().get();
    auto& db = gear::GearDatabase::instance();
    showingEmpty = ! hasEquipment (installation);

    empty.setVisible (showingEmpty);
    body.setVisible (! showingEmpty);
    if (showingEmpty)
    {
        resized();
        return;
    }

    plan = install::buildPlan (installation, session.getConfig(), db);

    wiring.setChecks (plan.wires, installation.batteryVoltageEngineOn);
    checklist.setInstructions (plan.instructions);
    gains.setPlan (plan.channels, session.getConfig());

    const gear::Product* mic = nullptr;
    for (auto& id : installation.micIds)
        if ((mic = db.find (id)) != nullptr) break;
    setup.refresh (mic, db.find (installation.audioInterfaceId));

    if (checklist.onProgressChanged)
        checklist.onProgressChanged();

    body.relayout();
}

int InstallPage::layoutContent (int width)
{
    const int m = ikit::margin;
    const int w = juce::jmax (320, width - 2 * m);
    int y = ikit::gap2;

    auto place = [&] (juce::Component& c, int h)
    {
        c.setBounds (m, y, w, h);
        y += h + ikit::gap2;
    };

    place (summary, summary.heightFor (w));
    place (wiring, wiring.heightFor (w));
    place (checklist, checklist.heightFor (w));
    place (gains, gains.heightFor (w));
    place (setup, setup.heightFor (w));
    return y + ikit::gap;
}

void InstallPage::applySafeLimits()
{
    if (showingEmpty)
        return;
    auto cfg = session.getConfig();
    install::applyToConfig (plan, installation, gear::GearDatabase::instance(), cfg);
    session.setConfig (cfg);
    const int n = gains.countUnsafeHp();
    toast (n > 0 ? "Safe crossover limits and driver specs applied to " + juce::String (n) + (n == 1 ? " channel" : " channels")
                 : "Driver specs and safe limits applied to the tuning", AppShell::ToastKind::good);
}

void InstallPage::toast (const juce::String& msg, AppShell::ToastKind kind)
{
    if (shell != nullptr)
        shell->toast (msg, kind);
}

void InstallPage::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg1);
}

void InstallPage::resized()
{
    body.setBounds (getLocalBounds());
    empty.setBounds (getLocalBounds().withSizeKeepingCentre (juce::jmin (getWidth() - 2 * ikit::margin, 520), 280));
}
} // namespace fa::ui
