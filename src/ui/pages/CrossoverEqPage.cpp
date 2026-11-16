#include "CrossoverEqPage.h"
#include "../components/DspSend.h"
#include "core/dsp/Spectrum.h"

namespace fa::ui
{
namespace
{
    constexpr int leftColumnW   = 300;
    constexpr int plotHeaderH   = 48;

    juce::String shortLabel (const OutputChannel& ch)
    {
        const auto side = ch.side == Side::left ? "L " : (ch.side == Side::right ? "R " : "");
        return side + toString (ch.role);
    }

    /** Normalises a response so its 300 Hz..3 kHz mean sits at 0 dB. */
    FrequencyResponse normalised (FrequencyResponse r)
    {
        if (r.isEmpty()) return r;
        const double mean = averageLevelDb (r, 300.0, 3000.0);
        r.scale (magFromDb (-mean));
        return r;
    }
}

//==============================================================================
CrossoverEqPage::CrossoverEqPage (Session& s) : Page (s)
{
    // ---- left column ------------------------------------------------------
    channelList.onSelect = [this] (const juce::String& id) { selectChannel (id); };
    addAndMakeVisible (channelList);

    highPassEditor.onChange = [this] (const FilterSpec& f) { modifySelected ([&] (ChannelSettings& st) { st.highPass = f; }); };
    addAndMakeVisible (highPassEditor);

    lowPassEditor.onChange = [this] (const FilterSpec& f) { modifySelected ([&] (ChannelSettings& st) { st.lowPass = f; }); };
    addAndMakeVisible (lowPassEditor);

    // ---- plot -------------------------------------------------------------
    plot.setUnits (FrequencyPlot::Units::dB);
    plot.setRanges (10.0, 24000.0, -36.0, 18.0);
    plot.setShowLegend (true);
    plot.setTitle ("Electrical response");
    plot.onNodeSelected = [this] (const juce::String& id)
    {
        selectedBand = id.fromFirstOccurrenceOf ("band", false, false).getIntValue();
        peqTable.setSelected (selectedBand);
        rebuildNodes();
    };
    plot.onNodeChanged = [this] (const FrequencyPlot::Node& n)
    {
        const int idx = n.id.fromFirstOccurrenceOf ("band", false, false).getIntValue();
        const auto* ch = selectedChannel();
        if (ch == nullptr || idx < 0 || idx >= (int) ch->settings.peq.size())
            return;
        selectedBand = idx;
        modifySelected ([&] (ChannelSettings& st)
        {
            auto& b = st.peq[(size_t) idx];
            b.freq = n.freq;
            b.gainDb = n.value;
            b.q = n.q;
            b = clampBand (b);
        }, false);
    };
    plot.onBackgroundDoubleClick = [this] (double f, double v) { addBand (f, v); };
    addAndMakeVisible (plot);

    showAllToggle.setToggleState (true, juce::dontSendNotification);
    showAllToggle.onClick = [this] { rebuildPlot(); };
    addAndMakeVisible (showAllToggle);

    sumToggle.setToggleState (false, juce::dontSendNotification);
    sumToggle.setTooltip ("Electrical sum of the selected channel's side (ideal crossover check)");
    sumToggle.onClick = [this] { rebuildPlot(); };
    addAndMakeVisible (sumToggle);

    predictedToggle.setToggleState (false, juce::dontSendNotification);
    predictedToggle.setTooltip ("Last tune's measurement of this channel multiplied by the current filters (normalised)");
    predictedToggle.onClick = [this] { rebuildPlot(); };
    addAndMakeVisible (predictedToggle);

    copyButton.setTooltip ("Mirror every left channel's filters, EQ, gain and polarity onto the matching right channel (delays are kept)");
    copyButton.onClick = [this] { copyLeftToRight(); };
    addAndMakeVisible (copyButton);

    sendButton.setTooltip ("Write the current crossovers, EQ, delay and gain of every channel to the DSP");
    sendButton.onClick = [this] { if (pendingWrite) timerCallback(); sendConfigToDsp (session, shell); };
    addAndMakeVisible (sendButton);

    // ---- PEQ table --------------------------------------------------------
    peqTable.onBandChanged = [this] (int idx, const FilterSpec& f)
    {
        selectedBand = idx;
        modifySelected ([&] (ChannelSettings& st)
        {
            if (idx >= 0 && idx < (int) st.peq.size())
                st.peq[(size_t) idx] = clampBand (f);
        });
    };
    peqTable.onBandDeleted = [this] (int idx)
    {
        modifySelected ([&] (ChannelSettings& st)
        {
            if (idx >= 0 && idx < (int) st.peq.size())
                st.peq.erase (st.peq.begin() + idx);
        });
        if (selectedBand >= idx) selectedBand = juce::jmax (-1, selectedBand - 1);
        refreshEditors();
    };
    peqTable.onBandSelected = [this] (int idx)
    {
        selectedBand = idx;
        peqTable.setSelected (idx);
        rebuildNodes();
    };
    peqTable.onAddBand = [this] { addBand (1000.0, 0.0); };
    peqTable.onResetAll = [this]
    {
        const auto* ch = selectedChannel();
        if (ch == nullptr || ch->settings.peq.empty()) return;
        modifySelected ([] (ChannelSettings& st) { st.peq.clear(); });
        selectedBand = -1;
        refreshEditors();
        if (shell != nullptr) shell->toast ("EQ cleared on " + ch->label, AppShell::ToastKind::info);
    };
    addAndMakeVisible (peqTable);

    graphicBands.onChanged = [this] (const std::vector<FilterSpec>& f, bool finished)
    {
        modifySelected ([&] (ChannelSettings& st) { st.peq = f; }, finished);
    };
    graphicBands.onResetAll = [this] { peqTable.onResetAll(); };
    addChildComponent (graphicBands);

    refreshAll();
}

CrossoverEqPage::~CrossoverEqPage()
{
    stopTimer();
    if (pendingWrite)
        session.updateChannel (pendingChannel);
}

//==============================================================================
void CrossoverEqPage::paint (juce::Graphics& g)
{
    theme::fillCard (g, plotCardArea.toFloat(), theme::bg2, theme::border);
    g.setColour (theme::border);
    g.fillRect (plotCardArea.getX() + 1, plotCardArea.getY() + plotHeaderH, plotCardArea.getWidth() - 2, 1);

    if (const auto* ch = selectedChannel())
    {
        auto cardCopy = plotCardArea;   // paint() must not shift the member
        auto header = cardCopy.removeFromTop (plotHeaderH).reduced (theme::pad, 0);
        const auto c = colourFor (*ch);
        auto dot = header.removeFromLeft (10).withSizeKeepingCentre (10, 10).toFloat();
        g.setColour (c);
        g.fillEllipse (dot);
        theme::drawGlow (g, dot, c, 5.0f, 6.0f);
        header.removeFromLeft (10);
        g.setColour (theme::text);
        g.setFont (theme::fontMedium (14.0f));
        g.drawFittedText (ch->label, header.removeFromLeft (juce::jmin (260, header.getWidth() / 3)), juce::Justification::centredLeft, 1);
    }
}

void CrossoverEqPage::resized()
{
    auto r = getLocalBounds().reduced (theme::padLg, theme::pad);

    // ---- left column ------------------------------------------------------
    auto left = r.removeFromLeft (juce::jlimit (260, leftColumnW, r.getWidth() / 4));
    r.removeFromLeft (theme::pad);
    lowPassEditor.setBounds (left.removeFromBottom (FilterEditor::idealHeight));
    left.removeFromBottom (theme::pad);
    highPassEditor.setBounds (left.removeFromBottom (FilterEditor::idealHeight));
    left.removeFromBottom (theme::pad);
    channelList.setBounds (left);

    // ---- plot card + PEQ table -------------------------------------------
    const int tableH = juce::jlimit (180, 320, r.getHeight() * 2 / 5);
    const auto tableArea = r.removeFromBottom (tableH);
    peqTable.setBounds (tableArea);
    graphicBands.setBounds (tableArea);
    r.removeFromBottom (theme::pad);
    plotCardArea = r;

    auto header = r.removeFromTop (plotHeaderH).reduced (theme::pad, 8);
    header.removeFromLeft (juce::jmin (280, header.getWidth() / 3));   // title area (painted)
    sendButton.setBounds (header.removeFromRight (124));
    header.removeFromRight (theme::grid);
    copyButton.setBounds (header.removeFromRight (120));
    header.removeFromRight (theme::pad);
    const bool wide = header.getWidth() > 420;
    predictedToggle.setBounds (header.removeFromRight (wide ? 160 : 0));
    predictedToggle.setVisible (wide);
    header.removeFromRight (theme::grid);
    sumToggle.setBounds (header.removeFromRight (100));
    header.removeFromRight (theme::grid);
    showAllToggle.setBounds (header.removeFromRight (120));

    plot.setBounds (r.reduced (1, 1).withTrimmedTop (4));
}

void CrossoverEqPage::pageShown()
{
    refreshAll();
}

void CrossoverEqPage::sessionChanged()
{
    if (applyingEdit)
        return;
    refreshAll();
}

void CrossoverEqPage::timerCallback()
{
    stopTimer();
    if (pendingWrite)
    {
        pendingWrite = false;
        applyingEdit = true;
        session.updateChannel (pendingChannel);
        applyingEdit = false;
        channelList.setChannels (session.getConfig(), selectedId);
        rebuildNodes();   // snap the dragged node to the DSP-clamped values
    }
}

//==============================================================================
const OutputChannel* CrossoverEqPage::selectedChannel() const
{
    if (pendingWrite && pendingChannel.id == selectedId)
        return &pendingChannel;
    return session.getConfig().find (selectedId);
}

juce::Colour CrossoverEqPage::colourFor (const OutputChannel& ch) const
{
    return theme::sideTint (theme::roleColour (ch.role), ch.side);
}

FilterSpec CrossoverEqPage::clampBand (FilterSpec b) const
{
    const auto& caps = session.getConfig().caps;
    b.freq = juce::jlimit (10.0, 24000.0, b.freq);
    b.gainDb = juce::jlimit (caps.peqMinGainDb, caps.peqMaxGainDb, b.gainDb);
    b.q = juce::jlimit (caps.peqMinQ, caps.peqMaxQ, b.q);
    if (caps.freqStepHz > 0.0)
        b.freq = std::round (b.freq / caps.freqStepHz) * caps.freqStepHz;
    return b;
}

void CrossoverEqPage::selectChannel (const juce::String& id)
{
    if (pendingWrite)
        timerCallback();
    selectedId = id;
    selectedBand = -1;
    refreshAll();
}

void CrossoverEqPage::refreshAll()
{
    const auto& cfg = session.getConfig();
    if (cfg.find (selectedId) == nullptr)
        selectedId = cfg.channels.empty() ? juce::String() : cfg.channels.front().id;

    channelList.setChannels (cfg, selectedId);
    highPassEditor.setCapabilities (cfg.caps);
    lowPassEditor.setCapabilities (cfg.caps);
    refreshEditors();
    rebuildPlot();
}

void CrossoverEqPage::refreshEditors()
{
    const auto* ch = selectedChannel();
    const bool have = ch != nullptr;
    highPassEditor.setEnabled (have);
    lowPassEditor.setEnabled (have);
    peqTable.setEnabled (have);
    copyButton.setEnabled (have);

    if (! have)
    {
        peqTable.setBands ({}, session.getConfig().caps, theme::accent, -1);
        repaint();
        return;
    }

    const auto c = colourFor (*ch);
    highPassEditor.setSpec (ch->settings.highPass, c);
    lowPassEditor.setSpec (ch->settings.lowPass, c);
    if (selectedBand >= (int) ch->settings.peq.size())
        selectedBand = (int) ch->settings.peq.size() - 1;
    peqTable.setBands (ch->settings.peq, session.getConfig().caps, c, selectedBand);
    graphicBands.setBands (ch->settings.peq, session.getConfig().caps, c);
    peqTable.setVisible (! isGraphic());
    graphicBands.setVisible (isGraphic());
    repaint();
}

void CrossoverEqPage::rebuildPlot()
{
    const auto& cfg = session.getConfig();
    const auto& f = analysisFrequencies();
    const double fs = cfg.caps.sampleRate > 0 ? cfg.caps.sampleRate : 48000.0;
    const auto* sel = selectedChannel();
    const bool showAll = showAllToggle.getToggleState();

    plot.clearTraces();

    std::vector<std::tuple<double, double, juce::Colour>> bands;

    for (const auto& chRef : cfg.channels)
    {
        const bool isSel = chRef.id == selectedId;
        const auto& ch = (isSel && sel != nullptr) ? *sel : chRef;
        if (! isSel && ! showAll)
            continue;

        auto t = PlotTrace::fromResponse (ch.id, shortLabel (ch), colourFor (ch), ch.settings.response (f, fs, false));
        t.thickness = isSel ? 2.6f : 1.4f;
        if (! isSel)
            t.alpha.assign (t.values.size(), 0.5);
        else
            t.fillToBottom = true;
        plot.setTrace (t);
    }

    if (sel != nullptr)
    {
        // EQ-only curve of the selected channel (dashed) so the PEQ shape is readable under the crossover
        std::vector<FilterSpec> eq;
        for (const auto& b : sel->settings.peq) if (b.enabled) eq.push_back (b);
        if (! eq.empty())
        {
            auto t = PlotTrace::fromResponse ("eq", "EQ only", colourFor (*sel).brighter (0.4f), chainResponse (eq, f, fs));
            t.dashed = true;
            t.thickness = 1.3f;
            plot.setTrace (t);
        }

        // crossover region shading
        const auto& st = sel->settings;
        if (st.highPass.enabled && st.highPass.freq > 10.5)
            bands.push_back ({ st.highPass.freq * 0.5, st.highPass.freq, colourFor (*sel).withAlpha (0.06f) });
        if (st.lowPass.enabled && st.lowPass.freq < 23000.0)
            bands.push_back ({ st.lowPass.freq, st.lowPass.freq * 2.0, colourFor (*sel).withAlpha (0.06f) });

        // electrical sum of this side (shows crossover summation errors)
        if (sumToggle.getToggleState())
        {
            FrequencyResponse sum;
            for (const auto& ch : cfg.channels)
            {
                if (ch.side != sel->side) continue;
                const auto& use = ch.id == sel->id ? *sel : ch;
                auto r = use.settings.response (f, fs, true);
                if (sum.isEmpty()) sum = r; else sum += r;
            }
            if (! sum.isEmpty())
            {
                auto t = PlotTrace::fromResponse ("sum", juce::String (sel->side == Side::right ? "R" : "L") + " sum", theme::curveSum, sum);
                t.thickness = 1.6f;
                plot.setTrace (t);
            }
        }

        // predicted acoustic: last tune's raw measurement of this channel x current filters
        if (predictedToggle.getToggleState() && session.hasTuneResult())
        {
            const Measurement* m = nullptr;
            for (const auto& cand : session.getTuneResult().measurements)
                if (cand.channelId == sel->id)
                    m = &cand;
            if (m != nullptr && ! m->response.isEmpty())
            {
                auto pred = normalised (smoothFractionalOctave (m->response, 6.0).resampled (f) * sel->settings.response (f, fs, false));
                auto t = PlotTrace::fromResponse ("pred", "Predicted acoustic", theme::curveTarget, pred);
                t.thickness = 1.4f;
                t.alpha.assign (t.values.size(), 0.8);
                plot.setTrace (t);
            }
        }
    }

    plot.setBands (std::move (bands));
    rebuildNodes();
}

void CrossoverEqPage::rebuildNodes()
{
    const auto* sel = selectedChannel();
    std::vector<FrequencyPlot::Node> nodes;
    if (sel != nullptr && ! isGraphic())
    {
        const auto c = colourFor (*sel);
        for (size_t i = 0; i < sel->settings.peq.size(); ++i)
        {
            const auto& b = sel->settings.peq[i];
            FrequencyPlot::Node n;
            n.id = "band" + juce::String (i);
            n.freq = b.freq;
            n.value = b.gainDb;
            n.q = b.q;
            n.colour = b.enabled ? c : theme::textMuted;
            n.selected = (int) i == selectedBand;
            nodes.push_back (n);
        }
    }
    plot.setNodes (std::move (nodes));
}

//==============================================================================
void CrossoverEqPage::commit (const OutputChannel& ch, bool immediate)
{
    pendingChannel = ch;
    if (immediate)
    {
        pendingWrite = false;
        stopTimer();
        applyingEdit = true;
        session.updateChannel (ch);
        applyingEdit = false;
        refreshAll();
    }
    else
    {
        pendingWrite = true;
        if (! isTimerRunning())
            startTimer (80);
        // light refresh while dragging: traces + table, no node rebuild (the plot owns the drag)
        const auto& cfg = session.getConfig();
        const auto& f = analysisFrequencies();
        auto t = PlotTrace::fromResponse (ch.id, shortLabel (ch), colourFor (ch), ch.settings.response (f, cfg.caps.sampleRate, false));
        t.thickness = 2.6f;
        t.fillToBottom = true;
        plot.setTrace (t);
        peqTable.setBands (ch.settings.peq, cfg.caps, colourFor (ch), selectedBand);
        graphicBands.setBands (ch.settings.peq, cfg.caps, colourFor (ch));
    }
}

void CrossoverEqPage::modifySelected (const std::function<void (ChannelSettings&)>& fn, bool immediate)
{
    const auto* sel = selectedChannel();
    if (sel == nullptr)
        return;
    OutputChannel ch = *sel;
    fn (ch.settings);
    commit (ch, immediate);
}

void CrossoverEqPage::addBand (double freq, double gainDb)
{
    const auto* sel = selectedChannel();
    if (sel == nullptr || isGraphic())
        return;

    const auto& caps = session.getConfig().caps;
    if ((int) sel->settings.peq.size() >= caps.peqBandsPerChannel)
    {
        if (shell != nullptr)
            shell->toast ("This DSP allows " + juce::String (caps.peqBandsPerChannel) + " bands per channel", AppShell::ToastKind::warn);
        return;
    }

    const int newIndex = (int) sel->settings.peq.size();
    auto band = clampBand (FilterSpec::peq (freq, 2.0, gainDb));
    modifySelected ([&] (ChannelSettings& st) { st.peq.push_back (band); });
    selectedBand = newIndex;
    refreshEditors();
    rebuildNodes();
}

void CrossoverEqPage::copyLeftToRight()
{
    auto cfg = session.getConfig();
    int copied = 0;

    std::vector<size_t> usedRight;
    for (const auto& l : cfg.channels)
    {
        if (l.side != Side::left) continue;
        for (size_t i = 0; i < cfg.channels.size(); ++i)
        {
            auto& r = cfg.channels[i];
            if (r.side != Side::right || r.role != l.role) continue;
            if (std::find (usedRight.begin(), usedRight.end(), i) != usedRight.end()) continue;
            usedRight.push_back (i);

            const double keepDelay = r.settings.delayMs;
            const bool keepMute = r.settings.muted;
            r.settings = l.settings;
            r.settings.delayMs = keepDelay;
            r.settings.muted = keepMute;
            ++copied;
            break;
        }
    }

    if (copied == 0)
    {
        if (shell != nullptr) shell->toast ("No matching right channels found", AppShell::ToastKind::warn);
        return;
    }

    applyingEdit = true;
    for (const auto& ch : cfg.channels)
        if (ch.side == Side::right)
            session.updateChannel (ch);
    applyingEdit = false;
    refreshAll();

    if (shell != nullptr)
        shell->toast ("Copied filters and EQ to " + juce::String (copied) + (copied == 1 ? " right channel" : " right channels"), AppShell::ToastKind::good);
}
} // namespace fa::ui
