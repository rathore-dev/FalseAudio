#include "SystemPage.h"

namespace fa::ui
{
namespace
{
    constexpr int kSlopes[] = { 6, 12, 18, 24, 36, 48 };

    /** Height a Card adds around its content area (title row + paddings). */
    int cardChrome (Card& c)
    {
        const auto saved = c.getBounds();
        c.setSize (400, 300);
        const int chrome = 300 - c.getContentArea().getHeight();
        c.setBounds (saved);
        return chrome;
    }

    std::vector<double> previewFrequencies()
    {
        static const auto f = makeLogFrequencies (15.0, 22000.0, 24);
        return f;
    }
}

//==============================================================================
SystemPage::SystemPage (Session& s) : Page (s)
{
    addAndMakeVisible (body);
    auto& c = body.content();
    body.layout = [this] (int w) { return layoutContent (w); };

    for (auto* card : { &layoutCard, &carCard, &channelsCard, &targetCard, &safetyCard, &capsCard })
        c.addAndMakeVisible (card);

    // ---- layout wizard
    for (auto* w : std::initializer_list<juce::Component*> { &presetCaption, &presetBox, &waysCaption, &waysSeg, &subCaption, &subSeg,
                                                             &seatCaption, &seatSeg, &steerCaption, &steerSeg,
                                                             &layoutSummary, &layoutHint })
        layoutCard.addAndMakeVisible (w);
    layoutSummary.setTextColour (theme::text);
    kit::styleCombo (presetBox);
    presetBox.setTextWhenNothingSelected ("Choose a topology...");
    presetBox.setTooltip ("Ready-made speaker topologies: active, passive component sets, full-range or a single driver.");
    for (int i = 0; i <= (int) SystemConfig::Preset::singleDriver; ++i)
        presetBox.addItem (SystemConfig::presetName ((SystemConfig::Preset) i), i + 1);
    presetBox.onChange = [this]
    {
        if (updating || presetBox.getSelectedId() <= 0)
            return;
        applyPreset ((SystemConfig::Preset) (presetBox.getSelectedId() - 1));
    };
    waysSeg.onChange = [this] (int) { applyLayout(); };
    subSeg.onChange  = [this] (int) { applyLayout(); };
    seatSeg.onChange = [this] (int seat)
    {
        editConfig ([seat] (SystemConfig& cfg) { cfg.listeningSeat = seat; });
    };
    steerSeg.onChange = [this] (int i)
    {
        if (updating) return;
        kit::setRightHandDrive (session, i == 1);
        car.setRightHandDrive (i == 1);
    };

    // ---- car + channels
    carCard.addAndMakeVisible (car);
    car.onChannelClicked = [this] (const juce::String& id) { selectChannel (id); };

    channelsCard.addAndMakeVisible (channelViewport);
    channelViewport.setViewedComponent (&channelList, false);
    channelViewport.setScrollBarsShown (true, false, true, false);
    channelViewport.setScrollBarThickness (6);

    // ---- target
    targetCard.addAndMakeVisible (targetPlot);
    targetPlot.setRanges (20.0, 20000.0, -15.0, 15.0);
    targetPlot.setShowLegend (true);
    targetPlot.setTitle ("Target preview");
    for (auto* w : std::initializer_list<juce::Component*> { &tuneCaption, &bassBoost, &transLow, &transHigh,
                                                             &trebleTilt, &tiltStart, &tuneHint, &targetChip })
        targetCard.addAndMakeVisible (w);

    bassBoost.onChange  = [this] (double v) { editTarget ([v] (TargetCurve& t) { t.bassBoostDb = v; }); };
    transLow.onChange   = [this] (double v) { editTarget ([v] (TargetCurve& t) { t.transitionLowHz = v; t.transitionHighHz = juce::jmax (t.transitionHighHz, v + 10.0); }); };
    transHigh.onChange  = [this] (double v) { editTarget ([v] (TargetCurve& t) { t.transitionHighHz = v; t.transitionLowHz = juce::jmin (t.transitionLowHz, v - 10.0); }); };
    trebleTilt.onChange = [this] (double v) { editTarget ([v] (TargetCurve& t) { t.trebleTiltDb = v; }); };
    tiltStart.onChange  = [this] (double v) { editTarget ([v] (TargetCurve& t) { t.tiltStartHz = v; }); };

    // ---- safety
    for (auto* w : std::initializer_list<juce::Component*> { &maxBoost, &lfBoost, &sweepLevel, &tweeterHp,
                                                             &minTwHp, &minMidHp, &minMbHp, &safetyHint })
        safetyCard.addAndMakeVisible (w);
    maxBoost.onChange   = [this] (double v) { editConfig ([v] (SystemConfig& c) { c.safety.maxBoostDb = v; }); };
    lfBoost.onChange    = [this] (double v) { editConfig ([v] (SystemConfig& c) { c.safety.maxLowFreqBoostDb = v; }); };
    sweepLevel.onChange = [this] (double v) { editConfig ([v] (SystemConfig& c) { c.safety.sweepLevelDbFS = v; }); };
    tweeterHp.onChange  = [this] (double v) { editConfig ([v] (SystemConfig& c) { c.safety.tweeterMinHpFactor = v; }); };
    minTwHp.onChange    = [this] (double v) { editConfig ([v] (SystemConfig& c) { c.safety.minTweeterHpHz = v; }); };
    minMidHp.onChange   = [this] (double v) { editConfig ([v] (SystemConfig& c) { c.safety.minMidrangeHpHz = v; }); };
    minMbHp.onChange    = [this] (double v) { editConfig ([v] (SystemConfig& c) { c.safety.minMidbassHpHz = v; }); };

    // ---- caps
    for (auto* w : std::initializer_list<juce::Component*> { &peqBands, &peqMinQ, &peqMaxQ, &peqMaxG, &peqMinG,
                                                             &maxDelay, &delayStep, &gainStep, &slopesCaption,
                                                             &lrToggle, &besselToggle, &allpassToggle })
        capsCard.addAndMakeVisible (w);
    peqBands.onChange  = [this] (double v) { editConfig ([v] (SystemConfig& c) { c.caps.peqBandsPerChannel = (int) v; }); };
    peqMinQ.onChange   = [this] (double v) { editConfig ([v] (SystemConfig& c) { c.caps.peqMinQ = v; }); };
    peqMaxQ.onChange   = [this] (double v) { editConfig ([v] (SystemConfig& c) { c.caps.peqMaxQ = v; }); };
    peqMaxG.onChange   = [this] (double v) { editConfig ([v] (SystemConfig& c) { c.caps.peqMaxGainDb = v; }); };
    peqMinG.onChange   = [this] (double v) { editConfig ([v] (SystemConfig& c) { c.caps.peqMinGainDb = v; }); };
    maxDelay.onChange  = [this] (double v) { editConfig ([v] (SystemConfig& c) { c.caps.maxDelayMs = v; }); };
    delayStep.onChange = [this] (double v) { editConfig ([v] (SystemConfig& c) { c.caps.delayStepMs = v; }); };
    gainStep.onChange  = [this] (double v) { editConfig ([v] (SystemConfig& c) { c.caps.gainStepDb = v; }); };
    lrToggle.onClick      = [this] { const bool b = lrToggle.getToggleState();      editConfig ([b] (SystemConfig& c) { c.caps.hasLinkwitzRiley = b; }); };
    besselToggle.onClick  = [this] { const bool b = besselToggle.getToggleState();  editConfig ([b] (SystemConfig& c) { c.caps.hasBessel = b; }); };
    allpassToggle.onClick = [this] { const bool b = allpassToggle.getToggleState(); editConfig ([b] (SystemConfig& c) { c.caps.hasAllPass = b; }); };

    for (int slope : kSlopes)
    {
        auto chip = std::make_unique<Chip> (juce::String (slope), theme::textMuted);
        chip->onClick = [this, slope]
        {
            editConfig ([slope] (SystemConfig& c)
            {
                auto& v = c.caps.availableSlopes;
                auto it = std::find (v.begin(), v.end(), slope);
                if (it != v.end()) { if (v.size() > 1) v.erase (it); }
                else { v.push_back (slope); std::sort (v.begin(), v.end()); }
            });
        };
        capsCard.addAndMakeVisible (*chip);
        slopeChips.push_back (std::move (chip));
    }

    rebuildTargetTiles();
    refreshFromSession();
}

SystemPage::~SystemPage() = default;

//==============================================================================
void SystemPage::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg1);
}

void SystemPage::resized()
{
    body.setBounds (getLocalBounds());
}

void SystemPage::pageShown()
{
    rebuildTargetTiles();   // user curves may have been added elsewhere
    refreshFromSession();
}

void SystemPage::sessionChanged()
{
    refreshFromSession();
}

//==============================================================================
int SystemPage::waysOf (const SystemConfig& cfg)
{
    int n = 0;
    for (auto& ch : cfg.channels)
        if (ch.side == Side::left && ! (ch.role == DriverRole::subwoofer && ch.id.startsWith ("SUB")))
            ++n;
    return juce::jlimit (2, 4, n);
}

int SystemPage::subModeOf (const SystemConfig& cfg)
{
    int subs = 0;
    for (auto& ch : cfg.channels)
        if (ch.id.startsWith ("SUB"))
            ++subs;
    return subs == 0 ? 0 : (subs == 1 ? 1 : 2);
}

void SystemPage::applyLayout()
{
    if (updating)
        return;
    const int ways = waysSeg.getSelected() + 2;
    const int sub  = subSeg.getSelected();
    session.setLayout (ways, sub > 0, sub == 2);
    toast (juce::String (ways) + "-way" + (sub == 0 ? "" : sub == 1 ? " + mono sub" : " + stereo subs") + " layout created");
}

void SystemPage::applyPreset (SystemConfig::Preset p)
{
    if (updating)
        return;
    const auto& old = session.getConfig();
    auto fresh = SystemConfig::makePreset (p);
    fresh.name = old.name;
    fresh.caps = old.caps;
    fresh.safety = old.safety;
    fresh.target = old.target;
    session.setConfig (fresh);   // also rebuilds the demo rig, so Demo mode follows the topology
    toast (SystemConfig::presetName (p) + " topology created");
}

void SystemPage::editConfig (std::function<void (SystemConfig&)> edit)
{
    if (updating)
        return;
    auto cfg = session.getConfig();
    edit (cfg);
    session.setConfig (cfg);
}

void SystemPage::editTarget (std::function<void (TargetCurve&)> edit)
{
    if (updating)
        return;
    auto t = session.getConfig().target;
    if (! t.points.empty())
        return;
    edit (t);
    session.setTarget (t);
}

void SystemPage::selectChannel (const juce::String& id)
{
    selectedId = id;
    car.setHighlight (id);
    for (auto& cc : channelCards)
    {
        cc->setSelected (cc->getChannelId() == id);
        if (cc->getChannelId() == id)
        {
            const auto r = cc->getBounds();
            const auto view = channelViewport.getViewArea();
            if (r.getY() < view.getY() || r.getBottom() > view.getBottom())
                channelViewport.setViewPosition (0, juce::jmax (0, r.getY() - kit::gap));
        }
    }
}

void SystemPage::toast (const juce::String& msg, AppShell::ToastKind kind)
{
    if (shell != nullptr)
        shell->toast (msg, kind);
}

//==============================================================================
void SystemPage::rebuildChannelCards()
{
    const auto& cfg = session.getConfig();

    bool same = channelCards.size() == cfg.channels.size();
    for (size_t i = 0; same && i < cfg.channels.size(); ++i)
        same = channelCards[i]->getChannelId() == cfg.channels[i].id;

    if (same)
    {
        for (size_t i = 0; i < cfg.channels.size(); ++i)
            channelCards[i]->update (cfg.channels[i]);
        return;
    }

    channelCards.clear();
    for (auto& ch : cfg.channels)
    {
        auto card = std::make_unique<ChannelCard> (ch,
            [this] (const OutputChannel& edited)
            {
                if (updating)
                    return;
                // A topology change must rebuild the demo rig too: go through setConfig.
                const auto* cur = session.getConfig().find (edited.id);
                if (cur != nullptr && (cur->kind != edited.kind || cur->role != edited.role))
                    editConfig ([edited] (SystemConfig& c) { if (auto* o = c.find (edited.id)) *o = edited; });
                else
                    session.updateChannel (edited);
            },
            [this] (const juce::String& id) { selectChannel (id); });
        channelList.addAndMakeVisible (*card);
        channelCards.push_back (std::move (card));
    }
    if (cfg.find (selectedId) == nullptr)
        selectedId = cfg.channels.empty() ? juce::String() : cfg.channels.front().id;
    body.relayout();
}

void SystemPage::rebuildTargetTiles()
{
    tiles.clear();
    auto add = [this] (std::unique_ptr<TargetTile> t)
    {
        t->onClick = [this] (TargetTile& tile)
        {
            if (tile.isImportTile()) { importTarget(); return; }
            session.setTarget (tile.getEntry().curve);
            toast ("Target: " + tile.getEntry().curve.name, AppShell::ToastKind::good);
        };
        targetCard.addAndMakeVisible (*t);
        tiles.push_back (std::move (t));
    };

    for (auto& e : TargetLibrary::builtIn())
        add (std::make_unique<TargetTile> (e));
    for (auto& e : TargetLibrary::userCurves())
        add (std::make_unique<TargetTile> (e));
    add (std::make_unique<TargetTile>());
    body.relayout();
}

void SystemPage::importTarget()
{
    chooser = std::make_unique<juce::FileChooser> ("Import target curve",
                                                   juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
                                                   "*.txt;*.csv;*.frd;*.req;*.tgt");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (! file.existsAsFile())
            return;
        TargetLibrary::Entry entry;
        juce::String error;
        if (! TargetLibrary::import (file, entry, error))
        {
            toast (error.isEmpty() ? "Could not read " + file.getFileName() : error, AppShell::ToastKind::bad);
            return;
        }
        rebuildTargetTiles();
        session.setTarget (entry.curve);
        toast ("Imported " + entry.curve.name, AppShell::ToastKind::good);
    });
}

void SystemPage::updateTargetPlot()
{
    const auto& t = session.getConfig().target;
    const auto freqs = previewFrequencies();

    targetPlot.clearTraces();
    auto main = PlotTrace::fromDb ("target", t.name, theme::curveTarget, freqs, t.levels (freqs));
    main.thickness = 2.4f;
    main.fillToBottom = true;
    targetPlot.setTrace (main);

    if (auto* entry = TargetLibrary::findByName (t.name))
    {
        for (auto& [role, band] : entry->bands)
        {
            auto tr = PlotTrace::fromDb ("band." + toString (role), toString (role) + " target",
                                         theme::roleColour (role), freqs, band.levels (freqs));
            tr.dashed = true;
            tr.thickness = 1.5f;
            targetPlot.setTrace (tr);
        }
    }

    for (auto& tile : tiles)
        tile->setSelected (! tile->isImportTile() && tile->getEntry().curve.name == t.name);

    const bool parametric = t.points.empty();
    for (auto* f : { &bassBoost, &transLow, &transHigh, &trebleTilt, &tiltStart })
    {
        f->setEnabled (parametric);
        f->setAlpha (parametric ? 1.0f : 0.4f);
    }
    bassBoost.setValue (t.bassBoostDb);
    transLow.setValue (t.transitionLowHz);
    transHigh.setValue (t.transitionHighHz);
    trebleTilt.setValue (t.trebleTiltDb);
    tiltStart.setValue (t.tiltStartHz);

    bool modified = false;
    if (auto* entry = TargetLibrary::findByName (t.name))
    {
        const auto& o = entry->curve;
        modified = parametric && (std::abs (o.bassBoostDb - t.bassBoostDb) > 1e-6 || std::abs (o.transitionLowHz - t.transitionLowHz) > 1e-6
                                  || std::abs (o.transitionHighHz - t.transitionHighHz) > 1e-6 || std::abs (o.trebleTiltDb - t.trebleTiltDb) > 1e-6
                                  || std::abs (o.tiltStartHz - t.tiltStartHz) > 1e-6);
    }
    targetChip.set (modified ? "Fine-tuned" : (parametric ? "Parametric" : "Measured points"),
                    modified ? theme::warn : (parametric ? theme::accent : theme::roleFullrange));
    targetChip.setSize (targetChip.getIdealWidth(), 20);

    tuneHint.setText (parametric
        ? "Drag or type to shape the house curve. Bass boost sits below the transition; the tilt lowers treble "
          "smoothly from the tilt start to 20 kHz. Pick a tile again to reset."
        : "This curve is defined by measured points and cannot be shaped here. Choose a parametric preset "
          "(Car Reference, Harman, Flat) to fine-tune.");
}

void SystemPage::refreshFromSession()
{
    const juce::ScopedValueSetter<bool> svs (updating, true);
    const auto& cfg = session.getConfig();

    waysSeg.setSelected (waysOf (cfg) - 2);
    subSeg.setSelected (subModeOf (cfg));
    seatSeg.setSelected (juce::jlimit (0, 2, cfg.listeningSeat));

    int maxOut = 0;
    for (auto& ch : cfg.channels)
        maxOut = juce::jmax (maxOut, ch.dspOutput + 1);
    int nPassive = 0, nFull = 0;
    for (auto& ch : cfg.channels)
    {
        nPassive += ch.isPassiveSet() ? 1 : 0;
        nFull += ch.role == DriverRole::fullrange ? 1 : 0;
    }
    layoutSummary.setText (juce::String (cfg.channels.size()) + " channels" + kit::sep() + juce::String (maxOut) + " DSP outputs"
                           + kit::sep() + (cfg.hasSubwoofer() ? "sub" : "no sub")
                           + (nPassive > 0 ? kit::sep() + juce::String (nPassive) + " passive set" + (nPassive > 1 ? "s" : "") : juce::String())
                           + (nFull > 0 ? kit::sep() + juce::String (nFull) + " full range" : juce::String()));
    presetBox.setSelectedId (0, juce::dontSendNotification);
    layoutHint.setText ("Changing the layout or preset rebuilds the channel list with fresh defaults. Driver details, "
                        "crossovers and EQ for the old layout are discarded. Set a channel's type to Passive set when a component "
                        "set or coax runs from one amp channel: the tuner then skips crossover and tweeter work for it.");
    channelsCard.setTitle ("Channels", juce::String (cfg.channels.size()) + " outputs");

    steerSeg.setSelected (kit::isRightHandDrive (session) ? 1 : 0);
    car.setRightHandDrive (kit::isRightHandDrive (session));
    car.setConfig (cfg);
    car.setSeat (cfg.listeningSeat);
    rebuildChannelCards();
    selectChannel (selectedId);
    updateTargetPlot();

    maxBoost.setValue (cfg.safety.maxBoostDb);
    lfBoost.setValue (cfg.safety.maxLowFreqBoostDb);
    sweepLevel.setValue (cfg.safety.sweepLevelDbFS);
    tweeterHp.setValue (cfg.safety.tweeterMinHpFactor);
    minTwHp.setValue (cfg.safety.minTweeterHpHz);
    minMidHp.setValue (cfg.safety.minMidrangeHpHz);
    minMbHp.setValue (cfg.safety.minMidbassHpHz);

    peqBands.setValue (cfg.caps.peqBandsPerChannel);
    peqMinQ.setValue (cfg.caps.peqMinQ);
    peqMaxQ.setValue (cfg.caps.peqMaxQ);
    peqMaxG.setValue (cfg.caps.peqMaxGainDb);
    peqMinG.setValue (cfg.caps.peqMinGainDb);
    maxDelay.setValue (cfg.caps.maxDelayMs);
    delayStep.setValue (cfg.caps.delayStepMs);
    gainStep.setValue (cfg.caps.gainStepDb);
    lrToggle.setToggleState (cfg.caps.hasLinkwitzRiley, juce::dontSendNotification);
    besselToggle.setToggleState (cfg.caps.hasBessel, juce::dontSendNotification);
    allpassToggle.setToggleState (cfg.caps.hasAllPass, juce::dontSendNotification);

    for (size_t i = 0; i < slopeChips.size(); ++i)
    {
        const int slope = kSlopes[i];
        const bool on = std::find (cfg.caps.availableSlopes.begin(), cfg.caps.availableSlopes.end(), slope) != cfg.caps.availableSlopes.end();
        slopeChips[i]->set (juce::String (slope), on ? theme::accent : theme::textMuted);
        slopeChips[i]->setAlpha (on ? 1.0f : 0.55f);
    }
    repaint();
}

//==============================================================================
int SystemPage::layoutContent (int w)
{
    using namespace kit;
    const int innerW = w - 2 * margin;
    const bool wide = innerW >= 1024;
    const int chrome = cardChrome (layoutCard);
    int y = margin;

    // ---- Row A: layout | car | channels -----------------------------------
    const int leftW  = juce::jlimit (260, 320, innerW / 4);
    const int rightW = wide ? juce::jlimit (360, 420, innerW / 3) : 0;
    const int rowH   = 656;

    layoutCard.setBounds (margin, y, leftW, rowH);
    {
        auto a = layoutCard.getContentArea();
        presetCaption.setBounds (a.removeFromTop (16)); a.removeFromTop (6);
        presetBox.setBounds (a.removeFromTop (btnH));   a.removeFromTop (gap2);
        waysCaption.setBounds (a.removeFromTop (16)); a.removeFromTop (6);
        waysSeg.setBounds (a.removeFromTop (btnH));   a.removeFromTop (gap2);
        subCaption.setBounds (a.removeFromTop (16));  a.removeFromTop (6);
        subSeg.setBounds (a.removeFromTop (btnH));    a.removeFromTop (gap2);
        seatCaption.setBounds (a.removeFromTop (16)); a.removeFromTop (6);
        seatSeg.setBounds (a.removeFromTop (btnH));   a.removeFromTop (gap2);
        steerCaption.setBounds (a.removeFromTop (16)); a.removeFromTop (6);
        steerSeg.setBounds (a.removeFromTop (btnH));  a.removeFromTop (gap2 + gap);
        layoutSummary.setBounds (a.removeFromTop (20)); a.removeFromTop (gap);
        layoutHint.setBounds (a.removeFromTop (layoutHint.heightFor (a.getWidth())));
    }

    const int carX = margin + leftW + gap2;
    const int carW = innerW - leftW - gap2 - (wide ? rightW + gap2 : 0);
    carCard.setBounds (carX, y, carW, rowH);
    car.setBounds (carCard.getContentArea().reduced (gap));

    auto layoutChannels = [this] (juce::Rectangle<int> area, int cols) -> int
    {
        const int listW = area.getWidth() - (cols == 1 ? channelViewport.getScrollBarThickness() + 2 : 0);
        const int cellW = (listW - gap * (cols - 1)) / juce::jmax (1, cols);
        int x = 0, yy = 0, col = 0;
        for (auto& cc : channelCards)
        {
            cc->setBounds (x, yy, cellW, ChannelCard::height);
            if (++col == cols) { col = 0; x = 0; yy += ChannelCard::height + gap; }
            else x += cellW + gap;
        }
        const int n = (int) channelCards.size();
        const int rows = (n + cols - 1) / juce::jmax (1, cols);
        const int h = juce::jmax (0, rows * ChannelCard::height + (rows - 1) * gap);
        channelList.setSize (listW, h);
        return h;
    };

    if (wide)
    {
        channelsCard.setBounds (w - margin - rightW, y, rightW, rowH);
        channelViewport.setBounds (channelsCard.getContentArea());
        layoutChannels (channelViewport.getBounds(), 1);
        y += rowH + gap2;
    }
    else
    {
        y += rowH + gap2;
        const int cols = innerW >= 700 ? 2 : 1;
        channelsCard.setBounds (margin, y, innerW, 200);
        const int h = layoutChannels (channelsCard.getContentArea(), cols);
        channelsCard.setSize (innerW, chrome + h);
        channelViewport.setBounds (channelsCard.getContentArea());
        y += channelsCard.getHeight() + gap2;
    }

    // ---- Row B: target curve ---------------------------------------------
    targetCard.setBounds (margin, y, innerW, 400);
    {
        auto a = targetCard.getContentArea();
        const int cw = a.getWidth();
        std::vector<juce::Component*> items;
        for (auto& t : tiles) items.push_back (t.get());
        const int tilesH = gridLayout (a, items, TargetTile::minWidth, TargetTile::height, gap, gap);
        int yy = a.getY() + tilesH + gap2;

        const int plotH = juce::jlimit (260, 360, (int) (cw * 0.28));
        targetPlot.setBounds (a.getX(), yy, cw, plotH);
        targetChip.setBounds (targetCard.getHeaderActionArea().removeFromRight (targetChip.getIdealWidth()).withSizeKeepingCentre (targetChip.getIdealWidth(), 22));
        yy += plotH + gap2;

        tuneCaption.setBounds (a.getX(), yy, cw, 16);
        yy += 22;
        const int fw = juce::jmin (150, (cw - 4 * gap) / 5);
        int x = a.getX();
        for (auto* f : { &bassBoost, &transLow, &transHigh, &trebleTilt, &tiltStart })
        {
            f->setBounds (x, yy, fw, fieldH);
            x += fw + gap;
        }
        const int hintX = x + gap;
        const int hintW = juce::jmax (120, a.getRight() - hintX);
        if (hintW >= 220)
        {
            tuneHint.setBounds (hintX, yy, hintW, fieldH);
            yy += fieldH;
        }
        else
        {
            yy += fieldH + gap;
            tuneHint.setBounds (a.getX(), yy, cw, tuneHint.heightFor (cw));
            yy += tuneHint.getHeight();
        }
        targetCard.setSize (innerW, chrome + (yy - a.getY()));
        y += targetCard.getHeight() + gap2;
    }

    // ---- Row C: safety | capabilities ------------------------------------
    const bool twoCols = innerW >= 820;
    const int colW = twoCols ? (innerW - gap2) / 2 : innerW;

    safetyCard.setBounds (margin, y, colW, 300);
    int safetyH = 0;
    {
        auto a = safetyCard.getContentArea();
        std::vector<juce::Component*> items { &maxBoost, &lfBoost, &sweepLevel, &tweeterHp, &minTwHp, &minMidHp, &minMbHp };
        int yy = a.getY() + gridLayout (a, items, 120, fieldH, gap, gap) + gap2;
        safetyHint.setBounds (a.getX(), yy, a.getWidth(), safetyHint.heightFor (a.getWidth()));
        yy += safetyHint.getHeight();
        safetyH = chrome + (yy - a.getY());
    }

    const int capsX = twoCols ? margin + colW + gap2 : margin;
    const int capsY = twoCols ? y : y + safetyH + gap2;
    capsCard.setBounds (capsX, capsY, colW, 300);
    int capsH = 0;
    {
        auto a = capsCard.getContentArea();
        std::vector<juce::Component*> items { &peqBands, &peqMinQ, &peqMaxQ, &peqMaxG, &peqMinG, &maxDelay, &delayStep, &gainStep };
        int yy = a.getY() + gridLayout (a, items, 120, fieldH, gap, gap) + gap2;
        slopesCaption.setBounds (a.getX(), yy, a.getWidth(), 16);
        yy += 22;
        int x = a.getX();
        for (auto& chip : slopeChips)
        {
            const int cwid = juce::jmax (40, chip->getIdealWidth());
            chip->setBounds (x, yy, cwid, 24);
            x += cwid + gap;
        }
        yy += 24 + gap2;
        const int tw = juce::jmax (120, (a.getWidth() - 2 * gap) / 3);
        lrToggle.setBounds (a.getX(), yy, tw, 24);
        besselToggle.setBounds (a.getX() + tw + gap, yy, tw, 24);
        allpassToggle.setBounds (a.getX() + 2 * (tw + gap), yy, tw, 24);
        yy += 24;
        capsH = chrome + (yy - a.getY());
    }

    if (twoCols)
    {
        const int h = juce::jmax (safetyH, capsH);
        safetyCard.setSize (colW, h);
        capsCard.setSize (colW, h);
        y += h;
    }
    else
    {
        safetyCard.setSize (colW, safetyH);
        capsCard.setSize (colW, capsH);
        y = capsY + capsH;
    }

    return y + margin;
}
} // namespace fa::ui
