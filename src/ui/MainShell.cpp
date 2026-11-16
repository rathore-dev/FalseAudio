#include "MainShell.h"
#include "components/Widgets.h"
#include "pages/AutoTunePage.h"
#include "pages/ResultsPage.h"
#include "pages/CarPage.h"
#include "pages/InstallPage.h"
#include "pages/HardwarePage.h"
#include "pages/DspPage.h"
#include "pages/ExpertPage.h"

namespace fa::ui
{
using namespace juce;

namespace
{
    constexpr int numPages = 7;

    struct RailEntry { PageId id; const char* label; const char* tooltip; bool manualTools; };

    /** The rail: six everyday pages, then Expert pinned to the bottom as "manual tools". */
    const RailEntry railOrder[] =
    {
        { PageId::autoTune, "Tune",            "Tune - one click: measure, align, cross over, equalise, verify", false },
        { PageId::results,  "Results",         "Results - score, before / after, diagnostics, settings and exports", false },
        { PageId::car,      "Car",             "Car - speaker layout and the equipment fitted", false },
        { PageId::install,  "Install & Gains", "Install & Gains - wire checks, gain settings and manual adjustments", false },
        { PageId::rig,      "Rig",             "Rig - audio interface, microphones and calibration", false },
        { PageId::dsp,      "DSP",             "DSP - connect, control and send the tune to your DSP", false },
        { PageId::expert,   "Expert",          "Expert - manual tools: Measure, Live RTA, Alignment, Crossovers & EQ", true },
    };

    struct PageAlias { const char* name; PageId id; const char* tab; };

    /** Names understood by the QA hook command `page <name>`: the seven pages, their sub-tabs,
        and every pre-merge page name (so older scripts keep working). */
    const PageAlias pageAliases[] =
    {
        { "tune", PageId::autoTune, "" },      { "autotune", PageId::autoTune, "" },
        { "results", PageId::results, "" },    { "report", PageId::results, "" },   { "insights", PageId::results, "" },
        { "car", PageId::car, "" },            { "layout", PageId::car, "layout" }, { "system", PageId::car, "layout" },
        { "equipment", PageId::car, "equipment" }, { "gear", PageId::car, "equipment" },
        { "install", PageId::install, "" },
        { "rig", PageId::rig, "" },            { "hardware", PageId::rig, "" },
        { "dsp", PageId::dsp, "" },
        { "expert", PageId::expert, "" },      { "measure", PageId::expert, "measure" },
        { "live", PageId::expert, "live" },    { "alignment", PageId::expert, "alignment" },
        { "crossover", PageId::expert, "crossover" }, { "crossovereq", PageId::expert, "crossover" },
    };
}

// =============================================================================
void MainShell::PageHost::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    Path p;
    p.addRoundedRectangle (r.getX(), r.getY(), r.getWidth() + theme::radiusLg, r.getHeight() + theme::radiusLg,
                           theme::radiusLg, theme::radiusLg, true, false, false, false);
    g.setColour (theme::bg1);
    g.fillPath (p);
    g.setColour (theme::border);
    g.strokePath (p, PathStrokeType (1.0f), AffineTransform::translation (0.5f, 0.5f));
}

// =============================================================================
MainShell::MainShell (Session& s)
    : session (s), header (s)
{
    setOpaque (true);
    setWantsKeyboardFocus (true);

    addAndMakeVisible (header);
    addAndMakeVisible (rail);
    addAndMakeVisible (pageHost);
    addAndMakeVisible (overlay);

    buildPages();

    std::vector<NavRail::Item> items;
    for (auto& e : railOrder)
        if (auto* p = getPage (e.id))
            items.push_back ({ e.id, e.label, p->getIcon(), {}, e.tooltip, e.manualTools });
    rail.setItems (std::move (items));
    rail.onSelect = [this] (PageId id) { showPage (id); };

    header.onModeChange = [this] (Session::Mode m) { setMode (m); };

    session.addChangeListener (this);
    session.getAutoTune().addListener (this);

    showPage (PageId::autoTune);

#if FA_QA_HOOKS
    {
        auto args = JUCEApplication::getCommandLineParameterArray();
        const int i = args.indexOf ("--qa-script");
        if (i >= 0 && i + 1 < args.size())
        {
            qaLines.addLines (File (args[i + 1]).loadFileAsString());
            startTimer (50);
        }
    }
#endif
}

MainShell::~MainShell()
{
    stopTimer();
    session.getAutoTune().removeListener (this);
    session.removeChangeListener (this);
    overlay.closeSheet();
    pages.clear();
}

void MainShell::buildPages()
{
    pages.resize (numPages);
    pages[indexOf (PageId::autoTune)] = std::make_unique<AutoTunePage> (session);
    pages[indexOf (PageId::results)]  = std::make_unique<ResultsPage> (session);
    pages[indexOf (PageId::car)]      = std::make_unique<CarPage> (session);        // Layout + Equipment
    pages[indexOf (PageId::install)]  = std::make_unique<InstallPage> (session);
    pages[indexOf (PageId::rig)]      = std::make_unique<HardwarePage> (session);
    pages[indexOf (PageId::dsp)]      = std::make_unique<DspPage> (session);
    pages[indexOf (PageId::expert)]   = std::make_unique<ExpertPage> (session);     // Measure, Live, Alignment, Crossovers & EQ

    for (auto& p : pages)
    {
        p->setShell (this);
        pageHost.addChildComponent (*p);
    }
}

// ---- AppShell -----------------------------------------------------------------
Page* MainShell::getPage (PageId id)
{
    const int i = indexOf (id);
    return (i >= 0 && i < (int) pages.size()) ? pages[(size_t) i].get() : nullptr;
}

void MainShell::refreshHeader()
{
    if (auto* p = hasCurrentPage ? getPage (currentPage) : nullptr)
        header.setPage (p->getTitle(), p->getSubtitle());
}

void MainShell::showPage (PageId id, const String& tab)
{
    auto* next = getPage (id);
    if (next == nullptr)
        return;

    if (tab.isNotEmpty())
        next->showTab (tab);       // before pageShown(), so only the requested sub-tab wakes up

    if (hasCurrentPage && currentPage == id)
    {
        rail.setSelected (id);
        refreshHeader();
        return;
    }

    if (hasCurrentPage)
        if (auto* prev = getPage (currentPage))
        {
            prev->setVisible (false);
            prev->pageHidden();
        }

    currentPage = id;
    hasCurrentPage = true;
    next->setBounds (pageHost.getLocalBounds());
    next->setVisible (true);
    next->toFront (false);
    next->pageShown();

    header.setPage (next->getTitle(), next->getSubtitle());
    rail.setSelected (id);
}

void MainShell::showSheet (std::unique_ptr<Component> content, int width, int height)
{
    overlay.showSheet (std::move (content), width, height);
}

void MainShell::closeSheet()
{
    overlay.closeSheet();
}

void MainShell::toast (const String& message, ToastKind kind)
{
    overlay.toast (message, kind);
}

// ---- layout -------------------------------------------------------------------
void MainShell::paint (Graphics& g)
{
    g.fillAll (theme::bg0);
}

void MainShell::resized()
{
    auto r = getLocalBounds();
    overlay.setBounds (r);
    header.setBounds (r.removeFromTop (theme::headerHeight));
    rail.setBounds (r.removeFromLeft (theme::railWidth));
    pageHost.setBounds (r);
    if (auto* p = hasCurrentPage ? getPage (currentPage) : nullptr)
        p->setBounds (pageHost.getLocalBounds());
}

bool MainShell::keyPressed (const KeyPress& key)
{
    // Ctrl+1..9 / Ctrl+0 jump through the rail order
    if (key.getModifiers().isCtrlDown() && ! overlay.hasSheet())
    {
        const auto c = key.getTextCharacter();
        int idx = -1;
        if (c >= '1' && c <= '9') idx = (int) (c - '1');
        if (c == '0')             idx = 9;
        if (idx >= 0 && idx < (int) std::size (railOrder))
        {
            showPage (railOrder[idx].id);
            return true;
        }
    }
    return false;
}

// ---- session ------------------------------------------------------------------
void MainShell::setMode (Session::Mode m)
{
    if (session.getMode() == m)
        return;
    session.setMode (m);
    if (session.getMode() != m)
    {
        header.refresh();
        toast ("Can't switch mode while Auto Tune is running", ToastKind::warn);
        return;
    }
    toast (m == Session::Mode::live ? "Live mode: measuring through your audio interface"
                                    : "Demo mode: tuning a simulated car", ToastKind::info);
}

void MainShell::changeListenerCallback (ChangeBroadcaster*)
{
    header.refresh();
}

// ---- Auto Tune prompts ----------------------------------------------------------
void MainShell::autoTuneStageChanged (AutoTuneEngine::Stage stage)
{
    using Stage = AutoTuneEngine::Stage;
    if (stage == Stage::idle || stage == Stage::done || stage == Stage::failed || stage == Stage::cancelled)
    {
        header.setTuneStatus ({}, 0.0);
        return;
    }
    const double overall = ((double) ((int) stage - 1)) / (double) AutoTuneEngine::numWorkStages;
    header.setTuneStatus (juce::String (juce::CharPointer_UTF8 ("Auto Tune  ·  ")) + AutoTuneEngine::stageName (stage), overall);
}

void MainShell::autoTuneProgress (AutoTuneEngine::Stage stage, double fraction, const String& message)
{
    const double overall = ((double) ((int) stage - 1) + jlimit (0.0, 1.0, fraction)) / (double) AutoTuneEngine::numWorkStages;
    String text = juce::String (juce::CharPointer_UTF8 ("Auto Tune  ·  ")) + AutoTuneEngine::stageName (stage);
    if (message.isNotEmpty())
        text << juce::String (juce::CharPointer_UTF8 ("  ·  ")) << message;
    header.setTuneStatus (text, overall);
}

void MainShell::autoTuneNeedsUser (const AutoTuneEngine::UserAction& action)
{
    using Kind = AutoTuneEngine::UserAction::Kind;

    icons::Id icon = icons::Id::help;
    String okText = "Continue", cancelText = "Cancel", title = action.title, message = action.message;

    switch (action.kind)
    {
        case Kind::moveMic:
            icon = icons::Id::mic;
            okText = "Done";
            if (title.isEmpty())   title = "Move the microphone";
            if (message.isEmpty()) message = "Place the mic at the next listening position, keep still, then press Done.";
            break;
        case Kind::manualDsp:
            icon = icons::Id::chip;
            okText = "Done";
            if (title.isEmpty())   title = "Apply these settings in your DSP software";
            if (message.isEmpty()) message = "Enter the following in your DSP app, then press Done to continue.";
            break;
        case Kind::confirm:
        default:
            if (title.isEmpty())   title = "Auto Tune needs your OK";
            break;
    }

    auto sheet = std::make_unique<MessageSheet> (title, message, action.steps, okText, cancelText, icon);
    sheet->onResult = [this] (bool proceed)
    {
        session.getAutoTune().respondToUser (proceed);
        if (! proceed)
            toast ("Auto Tune cancelled", ToastKind::warn);
    };

    const int width = action.steps.isEmpty() ? 480 : 560;
    showSheet (std::move (sheet), width, MessageSheet::idealHeight (action.steps.size()));
}

void MainShell::autoTuneFinished (const AutoTuneEngine::Result& r)
{
    header.setTuneStatus ({}, 0.0);
    if (! r.success && r.error.isNotEmpty())
        toast ("Auto Tune stopped: " + r.error, ToastKind::bad);
}

#if FA_QA_HOOKS
// ---- DEV-ONLY QA automation -------------------------------------------------------
Component* MainShell::findQa (Component& root, const String& key)
{
    for (auto* c : root.getChildren())
    {
        if (! c->isVisible())
            continue;
        if (c->getComponentID() == key)
            return c;
        if (auto* b = dynamic_cast<Button*> (c))
            if (b->getButtonText().equalsIgnoreCase (key) || b->getName().equalsIgnoreCase (key))
                return c;
        if (auto* f = findQa (*c, key))
            return f;
    }
    return nullptr;
}

bool MainShell::runQaLine (const String& raw)
{
    const auto line = raw.trim();
    if (line.isEmpty() || line.startsWith ("#"))
        return true;
    const auto cmd = line.upToFirstOccurrenceOf (" ", false, false).toLowerCase();
    const auto arg = line.fromFirstOccurrenceOf (" ", false, false).trim();

    if (cmd == "page")
    {
        // page <name> [tab]   e.g. "page expert alignment"; old page names are aliases
        const auto name = arg.upToFirstOccurrenceOf (" ", false, false);
        const auto tab  = arg.fromFirstOccurrenceOf (" ", false, false).trim();
        for (auto& a : pageAliases)
            if (name.equalsIgnoreCase (a.name))
            {
                showPage (a.id, tab.isNotEmpty() ? tab : String (a.tab));
                return true;
            }
        Logger::writeToLog ("QA: unknown page: " + arg);
        return true;
    }
    if (cmd == "tab")       // tab <name>: sub-tab of the current page (Car, Expert)
    {
        auto* p = hasCurrentPage ? getPage (currentPage) : nullptr;
        if (p == nullptr || ! p->showTab (arg))
            Logger::writeToLog ("QA: unknown tab: " + arg);
        refreshHeader();
        return true;
    }
    if (cmd == "wait")      { qaWaitMs = arg.getIntValue(); return true; }
    if (cmd == "click")
    {
        if (auto* c = findQa (*this, arg))
            if (auto* b = dynamic_cast<Button*> (c)) { b->triggerClick(); return true; }
        Logger::writeToLog ("QA: click target not found: " + arg);
        return true;
    }
    if (cmd == "type" || cmd == "segment")      // type <componentId> <text> | segment <componentId> <index>
    {
        const auto id = arg.upToFirstOccurrenceOf (" ", false, false);
        const auto rest = arg.fromFirstOccurrenceOf (" ", false, false).trim();
        auto* c = findQa (*this, id);
        if (auto* te = dynamic_cast<TextEditor*> (c)) te->setText (rest, true);
        else if (auto* sc = dynamic_cast<SegmentedControl*> (c)) sc->setSelected (rest.getIntValue(), sendNotification);
        else if (auto* cb = dynamic_cast<ComboBox*> (c)) cb->setSelectedId (rest.getIntValue(), sendNotification);
        else Logger::writeToLog ("QA: " + cmd + " target not found: " + id);
        return true;
    }
    if (cmd == "scroll")
    {
        std::function<Viewport* (Component&)> findVp = [&] (Component& r) -> Viewport*
        {
            for (auto* c : r.getChildren())
            {
                if (! c->isVisible()) continue;
                if (auto* v = dynamic_cast<Viewport*> (c)) return v;
                if (auto* f = findVp (*c)) return f;
            }
            return nullptr;
        };
        if (auto* v = findVp (*this)) v->setViewPosition (0, arg.getIntValue());
        return true;
    }
    if (cmd == "mode")      { setMode (arg.equalsIgnoreCase ("live") ? Session::Mode::live : Session::Mode::demo); return true; }
    if (cmd == "autotune")  { String e; if (! session.startAutoTune (e)) Logger::writeToLog ("QA: autotune: " + e); return true; }
    if (cmd == "size")
    {
        if (auto* top = getTopLevelComponent())
        {
            StringArray wh; wh.addTokens (arg, " x", "");
            wh.removeEmptyStrings();
            if (wh.size() >= 2)
                top->setSize (wh[0].getIntValue() + top->getWidth() - getWidth(),
                              wh[1].getIntValue() + top->getHeight() - getHeight());
        }
        return true;
    }
    if (cmd == "snap")
    {
        File f (arg);
        f.getParentDirectory().createDirectory();
        auto img = createComponentSnapshot (getLocalBounds(), true, 1.0f);
        f.deleteFile();
        FileOutputStream os (f);
        if (os.openedOk()) PNGImageFormat().writeImageToStream (img, os);
        return true;
    }
    if (cmd == "quit")      { JUCEApplication::getInstance()->quit(); return false; }
    return true;
}

#endif

#if FA_QA_HOOKS
void MainShell::timerCallback()
{
    if (qaWaitMs > 0) { qaWaitMs -= getTimerInterval(); return; }
    while (qaIndex < qaLines.size() && qaWaitMs <= 0)
        if (! runQaLine (qaLines[qaIndex++]))
            { stopTimer(); return; }
    if (qaIndex >= qaLines.size())
        stopTimer();
}
#else
void MainShell::timerCallback() { stopTimer(); }
#endif
} // namespace fa::ui
