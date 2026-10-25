#include "MeasurePage.h"

namespace fa::ui
{
namespace
{
    constexpr int toolbarHeight = 64;
    constexpr int libraryWidth  = 300;
    constexpr int detailsHeight = 64;
    constexpr int viewRowHeight = 44;

    juce::String feedLabel (int index)
    {
        switch (index)
        {
            case 0:  return "L";
            case 1:  return "R";
            default: return "L+R";
        }
    }

    Feed feedFor (int index)
    {
        switch (index)
        {
            case 0:  return Feed::left;
            case 1:  return Feed::right;
            default: return Feed::both;
        }
    }

    void styleEditor (juce::TextEditor& e)
    {
        e.setColour (juce::TextEditor::backgroundColourId, theme::bg1);
        e.setColour (juce::TextEditor::textColourId, theme::text);
        e.setColour (juce::TextEditor::outlineColourId, theme::border);
        e.setColour (juce::TextEditor::focusedOutlineColourId, theme::accent);
        e.setColour (juce::TextEditor::highlightColourId, theme::accentDim);
        e.setColour (juce::CaretComponent::caretColourId, theme::accent);
        e.setFont (theme::font (13.0f));
        e.setIndents (10, 0);
        e.setJustification (juce::Justification::centredLeft);
        e.setSelectAllWhenFocused (true);
    }
}

//==============================================================================
MeasurePage::MeasurePage (Session& s) : Page (s)
{
    // ---- toolbar ----------------------------------------------------------
    addAndMakeVisible (feedSelector);
    feedSelector.setSelected (2);

    styleEditor (labelField);
    labelField.setTextToShowWhenEmpty ("Label (optional)", theme::textMuted);
    labelField.setTooltip ("Name stored with the measurement, e.g. 'FL tweeter raw'");
    addAndMakeVisible (labelField);

    measureButton.setTooltip ("Play a log sweep through the selected feed and capture the response");
    measureButton.onClick = [this] { startMeasurement(); };
    addAndMakeVisible (measureButton);

    progress.setVisible (false);
    addChildComponent (progress);

    smoothingSelector.setSelected (4);
    smoothingSelector.onChange = [this] (int) { rebuildView(); };
    addAndMakeVisible (smoothingSelector);

    targetToggle.setToggleState (true, juce::dontSendNotification);
    targetToggle.onClick = [this] { rebuildView(); };
    addAndMakeVisible (targetToggle);

    // ---- library ----------------------------------------------------------
    library.onSelect = [this] (const juce::String& id)
    {
        selectedId = id;
        refreshLibrary();
        rebuildView();
        rebuildDetails();
    };
    library.onVisibilityChanged = [this] (const juce::String& id, bool visible)
    {
        stateFor (id).visible = visible;
        refreshLibrary();
        rebuildView();
    };
    library.onDelete = [this] (const juce::String& id)
    {
        session.removeMeasurement (id);
    };
    library.onClearAll = [this]
    {
        if (session.getMeasurements().empty())
            return;

        auto sheet = std::make_unique<MessageSheet> ("Clear library?",
                                                     "This removes every measurement in the session, including the ones captured by Auto Tune. The tune result itself is kept.",
                                                     juce::StringArray{}, "Clear all", "Keep", icons::Id::trash);
        sheet->onResult = [this] (bool ok) { if (ok) session.clearMeasurements(); };
        if (shell != nullptr)
            shell->showSheet (std::move (sheet), 440, MessageSheet::idealHeight (0));
        else
            session.clearMeasurements();
    };
    addAndMakeVisible (library);

    // ---- main -------------------------------------------------------------
    viewSelector.onChange = [this] (int i) { setView ((View) i); };
    addAndMakeVisible (viewSelector);

    freqPlot.setUnits (FrequencyPlot::Units::dB);
    freqPlot.setRanges (10.0, 24000.0, -40.0, 20.0);
    freqPlot.setShowLegend (true);
    addAndMakeVisible (freqPlot);

    timePlot.setRangeMs (-2.0, 20.0);
    addChildComponent (timePlot);

    addChildComponent (waterfall);

    addAndMakeVisible (details);

    refreshLibrary();
    rebuildView();
    rebuildDetails();
}

MeasurePage::~MeasurePage()
{
    stopTimer();
}

//==============================================================================
void MeasurePage::paint (juce::Graphics& g)
{
    // toolbar card
    theme::fillCard (g, toolbarArea.toFloat(), theme::bg2, theme::border);

    auto labels = toolbarArea.reduced (theme::pad, 0);
    g.setFont (theme::fontMedium (10.0f));
    g.setColour (theme::textMuted);

    auto captionFor = [&] (juce::Component& c, const juce::String& text)
    {
        auto b = c.getBounds();
        g.drawText (text.toUpperCase(), b.getX(), toolbarArea.getY() + 6, b.getWidth(), 12, juce::Justification::centredLeft);
    };
    captionFor (feedSelector, "Feed");
    captionFor (labelField, "Label");
    captionFor (smoothingSelector, "Smoothing");

    // status text next to the Measure button
    if (statusText.isNotEmpty())
    {
        g.setColour (statusColour);
        g.setFont (theme::font (12.0f));
        g.drawFittedText (statusText, statusArea, juce::Justification::centredLeft, 1);
    }

    // main card
    theme::fillCard (g, mainCardArea.toFloat(), theme::bg2, theme::border);
    g.setColour (theme::border);
    g.fillRect (mainCardArea.getX() + 1, mainCardArea.getY() + viewRowHeight, mainCardArea.getWidth() - 2, 1);
}

void MeasurePage::resized()
{
    auto r = getLocalBounds().reduced (theme::padLg, theme::pad);

    // ---- toolbar ----------------------------------------------------------
    toolbarArea = r.removeFromTop (toolbarHeight);
    {
        auto t = toolbarArea.reduced (theme::pad, 0).withTrimmedTop (22).withHeight (theme::controlHeight);
        const bool narrow = toolbarArea.getWidth() < 1100;

        feedSelector.setBounds (t.removeFromLeft (150));
        t.removeFromLeft (theme::pad);
        labelField.setBounds (t.removeFromLeft (narrow ? 150 : 200));
        t.removeFromLeft (theme::pad);
        measureButton.setBounds (t.removeFromLeft (128));
        t.removeFromLeft (theme::pad);

        auto right = t.removeFromRight (narrow ? 420 : 470);
        targetToggle.setBounds (right.removeFromRight (90));
        right.removeFromRight (theme::pad);
        smoothingSelector.setBounds (right);

        progress.setBounds (t.withHeight (4).withY (t.getCentreY() + 10).withTrimmedRight (theme::pad));
        statusArea = t.withTrimmedRight (theme::pad).withHeight (16).withY (t.getCentreY() - 12);
    }

    r.removeFromTop (theme::pad);

    // ---- library ----------------------------------------------------------
    const int libW = juce::jlimit (240, 340, r.getWidth() / 4);
    library.setBounds (r.removeFromLeft (libW));
    r.removeFromLeft (theme::pad);

    // ---- main -------------------------------------------------------------
    details.setBounds (r.removeFromBottom (detailsHeight));
    r.removeFromBottom (theme::grid);
    mainCardArea = r;

    auto viewRow = r.removeFromTop (viewRowHeight).reduced (theme::pad, 6);
    viewSelector.setBounds (viewRow.removeFromLeft (juce::jmin (viewRow.getWidth(), 640)));

    auto plotArea = r.reduced (1, 1).withTrimmedTop (4);
    freqPlot.setBounds (plotArea);
    timePlot.setBounds (plotArea);
    waterfall.setBounds (plotArea);
}

void MeasurePage::pageShown()
{
    refreshLibrary();
    rebuildView();
    rebuildDetails();
    if (measuring || session.isMeasuring())
        startTimerHz (20);
}

void MeasurePage::pageHidden()
{
    if (! measuring)
        stopTimer();
}

void MeasurePage::sessionChanged()
{
    // the measurement set may have changed (manual, auto-tune, delete, clear)
    const auto& ms = session.getMeasurements();
    if (selectedId.isNotEmpty() && findMeasurement (selectedId) == nullptr)
        selectedId = {};
    if (selectedId.isEmpty() && ! ms.empty())
        selectedId = ms.back().id;

    refreshLibrary();
    rebuildView();
    rebuildDetails();
}

//==============================================================================
void MeasurePage::timerCallback()
{
    if (! measuring)
    {
        stopTimer();
        return;
    }

    const double elapsed = (juce::Time::getMillisecondCounter() - measureStarted) * 0.001;
    const double total = session.getAutoTuneOptions().sweepSeconds + 1.6;   // sweep + tail + processing
    progress.setProgress (juce::jlimit (0.02, 0.97, elapsed / total));
    statusText = "Sweeping " + feedLabel (feedSelector.getSelected()) + "  \xC2\xB7  " + juce::String (elapsed, 1) + " s";
    statusColour = theme::accent;
    repaint (statusArea);
}

void MeasurePage::startMeasurement()
{
    if (measuring || session.isMeasuring())
        return;

    juce::String whyNot;
    if (! session.getSource().isReady (whyNot))
    {
        if (shell != nullptr)
            shell->toast ("Can't measure: " + whyNot, AppShell::ToastKind::warn);
        statusText = whyNot;
        statusColour = theme::warn;
        repaint (statusArea);
        return;
    }

    measuring = true;
    measureStarted = juce::Time::getMillisecondCounter();
    measureButton.setEnabled (false);
    measureButton.setButtonText ("Measuring\xE2\x80\xA6");
    progress.setProgress (0.0);
    progress.setVisible (true);
    startTimerHz (20);

    const auto feed = feedFor (feedSelector.getSelected());
    auto label = labelField.getText().trim();
    if (label.isEmpty())
        label = feedLabel (feedSelector.getSelected());

    juce::Component::SafePointer<MeasurePage> safe (this);
    session.measureOnce (feed, label, [safe] (const std::vector<Measurement>& ms, const juce::String& error)
    {
        if (safe.getComponent() == nullptr)
            return;

        auto& self = *safe.getComponent();
        self.measuring = false;
        self.stopTimer();
        self.progress.setVisible (false);
        self.measureButton.setEnabled (true);
        self.measureButton.setButtonText ("Measure");

        if (error.isNotEmpty())
        {
            self.statusText = error;
            self.statusColour = theme::bad;
            if (self.shell != nullptr)
                self.shell->toast ("Measurement failed: " + error, AppShell::ToastKind::bad);
        }
        else if (! ms.empty())
        {
            const auto& m = ms.back();
            self.selectedId = m.id;
            self.statusText = "Captured  \xC2\xB7  SNR " + juce::String (juce::roundToInt (m.snrDb)) + " dB  \xC2\xB7  peak "
                            + juce::String (m.peakDbFS, 1) + " dBFS" + (m.clipped ? "  \xC2\xB7  CLIPPED" : "");
            self.statusColour = m.clipped ? theme::bad : (m.snrDb < 25.0 ? theme::warn : theme::good);
            if (self.shell != nullptr)
            {
                if (m.clipped)
                    self.shell->toast ("Input clipped - lower the sweep level or mic gain", AppShell::ToastKind::warn);
                else
                    self.shell->toast (juce::String (ms.size()) + (ms.size() == 1 ? " measurement captured" : " measurements captured"),
                                       AppShell::ToastKind::good);
            }
            // addMeasurement already triggered sessionChanged(); make sure the selection shows
            self.refreshLibrary();
            self.rebuildView();
            self.rebuildDetails();
        }
        self.repaint();
    });
}

//==============================================================================
juce::Colour MeasurePage::paletteColour (int index)
{
    static const juce::Colour palette[] = {
        theme::accent, theme::roleTweeter, theme::curveSum, theme::roleMidrange,
        theme::roleSub, theme::roleMidbass, theme::roleFullrange, theme::curveTarget.darker (0.2f)
    };
    constexpr int n = (int) (sizeof (palette) / sizeof (palette[0]));
    const int wrap = index / n;
    return palette[index % n].withRotatedHue (0.03f * (float) wrap);
}

MeasurePage::ItemState& MeasurePage::stateFor (const juce::String& id)
{
    auto it = itemStates.find (id);
    if (it == itemStates.end())
        it = itemStates.emplace (id, ItemState { paletteColour (paletteCursor++), true }).first;
    return it->second;
}

void MeasurePage::syncColours()
{
    // forget state for measurements that no longer exist
    for (auto it = itemStates.begin(); it != itemStates.end();)
    {
        if (findMeasurement (it->first) == nullptr) it = itemStates.erase (it);
        else ++it;
    }
    for (const auto& m : session.getMeasurements())
        stateFor (m.id);
}

const Measurement* MeasurePage::findMeasurement (const juce::String& id) const
{
    for (const auto& m : session.getMeasurements())
        if (m.id == id)
            return &m;
    return nullptr;
}

const Measurement* MeasurePage::selectedMeasurement() const
{
    return findMeasurement (selectedId);
}

bool MeasurePage::isFromTune (const juce::String& id) const
{
    if (! session.hasTuneResult())
        return false;
    for (const auto& m : session.getTuneResult().measurements)
        if (m.id == id)
            return true;
    return false;
}

juce::String MeasurePage::displayName (const Measurement& m) const
{
    if (m.name.isNotEmpty())
        return m.name;
    if (m.channelId.isNotEmpty())
        return m.channelId;
    return "Measurement";
}

FrequencyResponse MeasurePage::smoothed (const FrequencyResponse& r) const
{
    switch (smoothingSelector.getSelected())
    {
        case 0:  return r;
        case 1:  return smoothFractionalOctave (r, 48.0);
        case 2:  return smoothFractionalOctave (r, 24.0);
        case 3:  return smoothFractionalOctave (r, 12.0);
        case 4:  return smoothFractionalOctave (r, 6.0);
        case 5:  return smoothFractionalOctave (r, 3.0);
        case 6:  return smoothFractionalOctave (r, 1.0);
        case 7:  return smoothPsychoacoustic (r);
        default: return smoothVariable (r);
    }
}

//==============================================================================
void MeasurePage::refreshLibrary()
{
    syncColours();

    std::vector<MeasurementLibrary::Item> items;
    const auto& ms = session.getMeasurements();
    items.reserve (ms.size());

    // newest first
    for (auto it = ms.rbegin(); it != ms.rend(); ++it)
    {
        const auto& m = *it;
        const auto& st = stateFor (m.id);
        MeasurementLibrary::Item item;
        item.id = m.id;
        item.name = displayName (m);
        item.channel = m.channelId.isNotEmpty() ? m.channelId : juce::String ("-");
        item.micPosition = m.micPosition;
        item.snrDb = m.snrDb;
        item.time = m.timestamp;
        item.colour = st.colour;
        item.visible = st.visible;
        item.fromTune = isFromTune (m.id);
        item.clipped = m.clipped;
        items.push_back (std::move (item));
    }
    library.setItems (items, selectedId);
}

void MeasurePage::setView (View v)
{
    view = v;
    viewSelector.setSelected ((int) v);
    rebuildView();
}

void MeasurePage::ensureCache (const Measurement& m)
{
    if (cache.id != m.id)
        cache = Cache { m.id };

    if (view == View::distortion && ! cache.haveDistortion)
    {
        const double fs = m.sampleRate > 0 ? m.sampleRate : 48000.0;
        cache.distortion = analysis::harmonicDistortion (m, session.getAutoTuneOptions().sweepSeconds,
                                                         10.0, std::min (24000.0, fs * 0.48), 6);
        cache.haveDistortion = true;
    }
    if (view == View::waterfall && ! cache.haveWaterfall)
    {
        cache.waterfall = analysis::spectralDecay (m, 300.0, 30, 20.0, 20000.0);
        cache.haveWaterfall = true;
    }
    if (view == View::step && ! cache.haveStep)
    {
        cache.step = analysis::stepResponse (m);
        cache.haveStep = true;
    }
    if (view == View::etc && ! cache.haveEtc)
    {
        cache.etc = analysis::energyTimeCurve (m);
        cache.haveEtc = true;
    }
}

void MeasurePage::rebuildView()
{
    const auto& ms = session.getMeasurements();
    const auto* sel = selectedMeasurement();

    const bool useFreq = view == View::spl || view == View::phase || view == View::groupDelay || view == View::distortion;
    const bool useTime = view == View::impulse || view == View::etc || view == View::step;
    freqPlot.setVisible (useFreq);
    timePlot.setVisible (useTime);
    waterfall.setVisible (view == View::waterfall);

    const bool smoothingApplies = view == View::spl || view == View::phase || view == View::groupDelay;
    smoothingSelector.setEnabled (smoothingApplies);
    smoothingSelector.setAlpha (smoothingApplies ? 1.0f : 0.45f);
    targetToggle.setEnabled (view == View::spl);
    targetToggle.setAlpha (view == View::spl ? 1.0f : 0.45f);

    // collect the visible measurements (library order = newest first; draw oldest first so the newest is on top)
    std::vector<const Measurement*> shown;
    for (const auto& m : ms)
    {
        auto it = itemStates.find (m.id);
        if (it != itemStates.end() && it->second.visible)
            shown.push_back (&m);
    }

    juce::String rangeKey = juce::String ((int) view);

    if (useFreq)
    {
        freqPlot.clearTraces();

        if (view == View::distortion)
        {
            freqPlot.setUnits (FrequencyPlot::Units::dB);
            freqPlot.setTitle (sel != nullptr ? "Harmonic distortion  \xC2\xB7  " + displayName (*sel) : juce::String ("Harmonic distortion"));
            if (sel != nullptr)
            {
                ensureCache (*sel);
                const auto& d = cache.distortion;
                if (d.valid && ! d.freqs.empty())
                {
                    std::vector<double> thdDb (d.thdPercent.size());
                    for (size_t i = 0; i < thdDb.size(); ++i)
                        thdDb[i] = 20.0 * std::log10 (std::max (d.thdPercent[i], 1.0e-4) / 100.0);

                    auto thd = PlotTrace::fromDb ("thd", "THD", theme::text, d.freqs, thdDb);
                    thd.thickness = 2.5f;
                    freqPlot.setTrace (thd);

                    static const juce::Colour hc[] = { theme::accent, theme::roleMidrange, theme::warn, theme::roleTweeter, theme::roleSub };
                    for (size_t h = 0; h < d.harmonicDb.size() && h < 4; ++h)
                    {
                        auto t = PlotTrace::fromDb ("h" + juce::String (h + 2), "H" + juce::String (h + 2), hc[h], d.freqs, d.harmonicDb[h]);
                        t.thickness = 1.6f;
                        freqPlot.setTrace (t);
                    }
                }
                else
                {
                    freqPlot.setTitle ("Harmonic distortion  \xC2\xB7  not available for this measurement");
                }
            }
            rangeKey << ":dist";
            if (rangeKey != lastRangeKey)
                freqPlot.setRanges (20.0, 20000.0, -90.0, 0.0);
        }
        else
        {
            freqPlot.setUnits (view == View::spl ? FrequencyPlot::Units::dB
                             : view == View::phase ? FrequencyPlot::Units::degrees
                                                   : FrequencyPlot::Units::ms);
            freqPlot.setTitle (view == View::spl ? "SPL" : view == View::phase ? "Phase" : "Group delay");

            double maxLevel = -1.0e9, anchorMeasured = 0.0;
            bool haveAnchor = false;

            for (const auto* m : shown)
            {
                const auto& st = itemStates[m->id];
                const bool isSel = m->id == selectedId;
                const auto r = smoothed (m->response);
                if (r.isEmpty())
                    continue;

                PlotTrace t;
                if (view == View::spl)
                {
                    t = PlotTrace::fromResponse (m->id, displayName (*m), st.colour, r);
                    for (size_t i = 0; i < r.size(); ++i)
                        if (r.freqs[i] >= 30.0 && r.freqs[i] <= 16000.0)
                            maxLevel = std::max (maxLevel, t.values[i]);
                    if (isSel || ! haveAnchor)
                    {
                        anchorMeasured = averageLevelDb (r, 300.0, 3000.0);
                        haveAnchor = true;
                    }
                }
                else if (view == View::phase)
                {
                    t = PlotTrace::fromPhase (m->id, displayName (*m), st.colour, r);
                }
                else
                {
                    t = PlotTrace::fromDb (m->id, displayName (*m), st.colour, r.freqs, groupDelayMs (r));
                }
                t.thickness = isSel ? 2.4f : 1.5f;
                if (! isSel && shown.size() > 1)
                    t.alpha.assign (t.values.size(), 0.6);
                freqPlot.setTrace (t);
            }

            if (view == View::spl && targetToggle.getToggleState() && haveAnchor)
            {
                const auto& f = analysisFrequencies();
                auto levels = session.getConfig().target.levels (f);
                double mean = 0.0; int n = 0;
                for (size_t i = 0; i < f.size(); ++i)
                    if (f[i] >= 300.0 && f[i] <= 3000.0) { mean += levels[i]; ++n; }
                const double offset = anchorMeasured - (n > 0 ? mean / n : 0.0);
                for (auto& v : levels) v += offset;
                auto t = PlotTrace::fromDb ("target", "Target", theme::curveTarget, f, levels);
                t.dashed = true;
                t.thickness = 1.5f;
                freqPlot.setTrace (t);
            }

            // Range: fit once per (view, visible set) so zoom/pan is not reset on every refresh
            for (const auto* m : shown) rangeKey << m->id.substring (0, 8);
            if (rangeKey != lastRangeKey)
            {
                if (view == View::spl)
                {
                    if (maxLevel > -1.0e8)
                    {
                        const double top = std::ceil ((maxLevel + 6.0) / 10.0) * 10.0;
                        freqPlot.setRanges (10.0, 24000.0, top - 80.0, top);
                    }
                    else
                        freqPlot.setRanges (10.0, 24000.0, -40.0, 20.0);
                }
                else if (view == View::phase)
                    freqPlot.setRanges (10.0, 24000.0, -180.0, 180.0);
                else
                    freqPlot.setRanges (10.0, 24000.0, -5.0, 25.0);
            }
        }
    }
    else if (useTime)
    {
        std::vector<TimePlot::Trace> traces;
        std::vector<std::pair<double, juce::Colour>> markers;
        double onsetSel = 0.0;
        bool haveSel = false;

        auto addTrace = [&] (const Measurement& m, std::vector<float> samples, bool isSel)
        {
            TimePlot::Trace t;
            t.id = m.id;
            t.label = displayName (m);
            t.colour = isSel ? itemStates[m.id].colour : itemStates[m.id].colour.withAlpha (0.55f);
            t.sampleRate = m.sampleRate;
            t.startMs = -1000.0 * m.zeroSample / std::max (1.0, m.sampleRate);
            t.samples = std::move (samples);
            traces.push_back (std::move (t));
        };

        if (view == View::impulse)
        {
            timePlot.setDbMode (false);
            for (const auto* m : shown)
                addTrace (*m, m->ir, m->id == selectedId);
        }
        else if (sel != nullptr)
        {
            ensureCache (*sel);
            if (view == View::step)
            {
                timePlot.setDbMode (false);
                addTrace (*sel, cache.step, true);
            }
            else
            {
                timePlot.setDbMode (true);
                // ETC is delivered in dB; the plot's dB mode takes linear samples and draws the envelope
                std::vector<float> lin (cache.etc.size());
                for (size_t i = 0; i < lin.size(); ++i)
                    lin[i] = (float) magFromDb (cache.etc[i]);
                addTrace (*sel, lin, true);
            }
        }

        if (sel != nullptr)
        {
            onsetSel = sel->onsetMs;
            haveSel = true;
            markers.push_back ({ sel->onsetMs, itemStates[sel->id].colour.withAlpha (0.7f) });
        }
        if (view == View::impulse)
            markers.push_back ({ 0.0, theme::textMuted });

        timePlot.setTraces (std::move (traces));
        timePlot.setMarkers (std::move (markers));

        rangeKey << ":" << (haveSel ? juce::String (onsetSel, 1) : juce::String ("none"));
        if (rangeKey != lastRangeKey)
        {
            const double t0 = haveSel ? onsetSel : 0.0;
            if (view == View::impulse)      timePlot.setRangeMs (t0 - 2.0, t0 + 12.0);
            else if (view == View::step)    timePlot.setRangeMs (t0 - 1.0, t0 + 10.0);
            else                            timePlot.setRangeMs (t0 - 5.0, t0 + 60.0);
        }
    }
    else // waterfall
    {
        if (sel != nullptr)
        {
            ensureCache (*sel);
            waterfall.setData (cache.waterfall, itemStates[sel->id].colour,
                               "Spectral decay  \xC2\xB7  " + displayName (*sel) + "  \xC2\xB7  0-300 ms");
        }
        else
            waterfall.clear();
    }

    lastRangeKey = rangeKey;
    repaint();
}

void MeasurePage::rebuildDetails()
{
    const auto* m = selectedMeasurement();
    if (m == nullptr)
    {
        details.setTitle ("No measurement selected");
        details.setStats ({});
        return;
    }

    details.setTitle (displayName (*m) + "  \xC2\xB7  " + m->channelId, itemStates[m->id].colour);

    std::vector<StatStrip::Stat> stats;
    stats.push_back ({ "Onset", juce::String (m->onsetMs, 2), "ms" });
    stats.push_back ({ "Distance", juce::String (juce::roundToInt (msToMetres (m->onsetMs) * 100.0)), "cm" });
    stats.push_back ({ "Peak", juce::String (m->peakDbFS, 1), "dBFS",
                       m->peakDbFS > -3.0 ? theme::warn : theme::text });
    stats.push_back ({ "SNR", juce::String (juce::roundToInt (m->snrDb)), "dB",
                       m->snrDb >= 40.0 ? theme::good : (m->snrDb >= 25.0 ? theme::warn : theme::bad) });
    stats.push_back ({ "Length", juce::String ((double) m->ir.size() / std::max (1.0, m->sampleRate), 1), "s" });
    stats.push_back ({ "Rate", juce::String (m->sampleRate / 1000.0, 1), "kHz" });

    StatStrip::Stat status;
    status.label = "Status";
    status.chip = true;
    if (m->clipped)       { status.value = "CLIPPED"; status.colour = theme::bad; }
    else if (m->snrDb < 25.0) { status.value = "NOISY"; status.colour = theme::warn; }
    else                  { status.value = "CLEAN"; status.colour = theme::good; }
    stats.push_back (status);

    details.setStats (std::move (stats));
}
} // namespace fa::ui
