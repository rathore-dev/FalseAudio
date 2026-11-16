#include "LivePage.h"
#include "system/PageKit.h"

namespace fa::ui
{
namespace
{
    constexpr int topRowHeight   = 40;
    constexpr int rightColumnW   = 312;
    constexpr int controlsHeight = 48;
    constexpr int splHeight      = 236;

    int bandsPerOctaveFor (int index)
    {
        switch (index)
        {
            case 0:  return 3;
            case 1:  return 6;
            case 2:  return 12;
            default: return 24;
        }
    }

    double averagingSecondsFor (int index)
    {
        switch (index)
        {
            case 0:  return 2.5;
            case 1:  return 1.0;
            default: return 0.25;
        }
    }

    LiveAnalyzer::Weighting weightingFor (int index)
    {
        switch (index)
        {
            case 0:  return LiveAnalyzer::Weighting::A;
            case 1:  return LiveAnalyzer::Weighting::C;
            default: return LiveAnalyzer::Weighting::Z;
        }
    }
}

//==============================================================================
LivePage::LivePage (Session& s) : Page (s)
{
    // ---- top row ----------------------------------------------------------
    modeSelector.onChange = [this] (int i) { setMode ((Mode) i); };
    addAndMakeVisible (modeSelector);

    micChip.setTooltip ("The analyzer microphone is chosen on the Rig page - click to open it");
    micChip.onClick = [this] { if (shell != nullptr) shell->showPage (PageId::rig); };
    addAndMakeVisible (micChip);

    banner.onAction = [this] { session.setMode (Session::Mode::live); };
    addChildComponent (banner);

    // ---- RTA controls -----------------------------------------------------
    rtaDisplay.setSelected (0);
    rtaDisplay.onChange = [this] (int) { applyAnalyzerSettings(); };
    addAndMakeVisible (rtaDisplay);

    rtaResolution.setSelected (1);
    rtaResolution.onChange = [this] (int) { rebuildBands(); applyAnalyzerSettings(); };
    addAndMakeVisible (rtaResolution);

    rtaAveraging.setSelected (1);
    rtaAveraging.onChange = [this] (int) { applyAnalyzerSettings(); };
    addAndMakeVisible (rtaAveraging);

    peakHoldToggle.setToggleState (true, juce::dontSendNotification);
    peakHoldToggle.onClick = [this]
    {
        if (! peakHoldToggle.getToggleState())
        {
            peakHold.clear();
            rtaPlot.removeTrace ("peak");
        }
    };
    addAndMakeVisible (peakHoldToggle);

    captureButton.setTooltip ("Freeze the current spectrum as a reference overlay");
    captureButton.onClick = [this]
    {
        PlotTrace t;
        if (! snapshotFreqs.empty())
        {
            t = PlotTrace::fromDb ("snap", "Snapshot", theme::curveSum, snapshotFreqs, snapshotDb);
            t.thickness = 1.5f;
            t.dashed = true;
            haveSnapshot = true;
            rtaPlot.setTrace (t);
            if (shell != nullptr) shell->toast ("Snapshot captured", AppShell::ToastKind::info);
        }
    };
    addAndMakeVisible (captureButton);

    clearButton.setTooltip ("Clear peak hold, snapshot and averaging");
    clearButton.onClick = [this]
    {
        peakHold.clear();
        haveSnapshot = false;
        rtaPlot.removeTrace ("peak");
        rtaPlot.removeTrace ("snap");
        session.getAnalyzer().reset();
    };
    addAndMakeVisible (clearButton);

    rtaPlot.setUnits (FrequencyPlot::Units::dB);
    rtaPlot.setRanges (20.0, 20000.0, -100.0, 0.0);
    rtaPlot.setTitle ("RTA");
    addAndMakeVisible (rtaPlot);

    // ---- Transfer function ------------------------------------------------
    tfMagnitude.setUnits (FrequencyPlot::Units::dB);
    tfMagnitude.setRanges (20.0, 20000.0, -30.0, 30.0);
    tfMagnitude.setTitle (juce::String (juce::CharPointer_UTF8 ("Magnitude  \xC2\xB7  opacity = coherence")));
    addChildComponent (tfMagnitude);

    tfPhase.setUnits (FrequencyPlot::Units::degrees);
    tfPhase.setRanges (20.0, 20000.0, -180.0, 180.0);
    tfPhase.setTitle ("Phase");
    tfPhase.setShowLegend (false);
    addChildComponent (tfPhase);

    tfCoherence.setUnits (FrequencyPlot::Units::coherence);
    tfCoherence.setRanges (20.0, 20000.0, 0.0, 1.0);
    tfCoherence.setTitle ("Coherence");
    tfCoherence.setShowLegend (false);
    addChildComponent (tfCoherence);

    findDelayButton.setTooltip ("Measure the delay between the generator reference and the mic and compensate it");
    findDelayButton.onClick = [this]
    {
        if (! liveAvailable) return;
        delayMs = session.getAnalyzer().findDelay (micIndex);
        haveDelay = true;
        delayChip.set ("Delay " + theme::formatMs (delayMs) + juce::String (juce::CharPointer_UTF8 (" ms  \xC2\xB7  ")) + juce::String (juce::roundToInt (msToMetres (delayMs) * 100.0)) + " cm", theme::accent);
        resized();
        if (shell != nullptr) shell->toast ("Delay compensation set to " + theme::formatMs (delayMs) + " ms", AppShell::ToastKind::good);
    };
    addChildComponent (findDelayButton);

    clearDelayButton.onClick = [this]
    {
        session.getAnalyzer().setDelayCompensation (0.0);
        haveDelay = false;
        delayMs = 0.0;
        delayChip.set ("No delay compensation", theme::textDim);
        resized();
    };
    addChildComponent (clearDelayButton);
    addChildComponent (delayChip);

    // ---- right column -----------------------------------------------------
    addAndMakeVisible (spl);
    generator.onChange = [this] (const GeneratorSettings&) { pushGenerator(); };
    addAndMakeVisible (generator);

    rebuildBands();
    updateAvailability();
    setMode (Mode::rta);
}

LivePage::~LivePage()
{
    stopTimer();
}

//==============================================================================
void LivePage::paint (juce::Graphics& g)
{
    theme::fillCard (g, mainCardArea.toFloat(), theme::bg2, theme::border);
    g.setColour (theme::border);
    g.fillRect (mainCardArea.getX() + 1, mainCardArea.getY() + controlsHeight, mainCardArea.getWidth() - 2, 1);

    if (! liveAvailable)
    {
        // dim the plots a touch so the page reads as "not running"
        g.setColour (theme::bg1.withAlpha (0.35f));
        g.fillRoundedRectangle (mainCardArea.withTrimmedTop (controlsHeight).toFloat().reduced (1.0f), theme::radius);
    }
}

void LivePage::resized()
{
    auto r = getLocalBounds().reduced (theme::padLg, theme::pad);

    // ---- top row ----------------------------------------------------------
    auto top = r.removeFromTop (topRowHeight);
    const int controlY = top.getY() + (topRowHeight - theme::controlHeight) / 2;
    modeSelector.setBounds (top.removeFromLeft (260).withHeight (theme::controlHeight).withY (controlY));
    top.removeFromLeft (theme::pad);

    {
        const int cw = 176;
        micChip.setBounds (top.removeFromLeft (cw).withHeight (24).withY (top.getY() + (topRowHeight - 24) / 2));
        top.removeFromLeft (theme::pad);
    }
    banner.setBounds (top.withHeight (36).withY (top.getY() + 2));

    r.removeFromTop (theme::pad);

    // ---- right column -----------------------------------------------------
    auto right = r.removeFromRight (juce::jmin (rightColumnW, r.getWidth() / 3));
    r.removeFromRight (theme::pad);
    spl.setBounds (right.removeFromTop (splHeight));
    right.removeFromTop (theme::pad);
    generator.setBounds (right);

    // ---- main card --------------------------------------------------------
    mainCardArea = r;
    controlsRowArea = r.removeFromTop (controlsHeight).reduced (theme::pad, 8);

    layoutMode();
}

void LivePage::layoutMode()
{
    auto c = controlsRowArea;
    auto plots = mainCardArea.withTrimmedTop (controlsHeight).reduced (1, 1).withTrimmedTop (4);

    if (mode == Mode::rta)
    {
        rtaDisplay.setBounds (c.removeFromLeft (120));
        c.removeFromLeft (theme::grid);
        rtaResolution.setBounds (c.removeFromLeft (200));
        c.removeFromLeft (theme::grid);
        rtaAveraging.setBounds (c.removeFromLeft (190));
        c.removeFromLeft (theme::pad);
        clearButton.setBounds (c.removeFromRight (80));
        c.removeFromRight (theme::grid);
        captureButton.setBounds (c.removeFromRight (108));
        c.removeFromRight (theme::grid);
        peakHoldToggle.setBounds (c.removeFromRight (juce::jmin (112, c.getWidth())));
        rtaPlot.setBounds (plots);
    }
    else
    {
        findDelayButton.setBounds (c.removeFromLeft (120));
        c.removeFromLeft (theme::grid);
        if (haveDelay)
        {
            clearDelayButton.setBounds (c.removeFromRight (76));
            c.removeFromRight (theme::grid);
        }
        delayChip.setBounds (c.removeFromLeft (juce::jmin (c.getWidth(), delayChip.getIdealWidth() + 8)).withSizeKeepingCentre (juce::jmin (c.getWidth(), delayChip.getIdealWidth() + 8), 24));
        clearDelayButton.setVisible (mode == Mode::transfer && haveDelay);

        const int coherenceH = 72;
        const int phaseH = juce::jmax (110, plots.getHeight() / 4);
        tfCoherence.setBounds (plots.removeFromBottom (coherenceH));
        tfPhase.setBounds (plots.removeFromBottom (phaseH));
        tfMagnitude.setBounds (plots);
    }
}

void LivePage::setMode (Mode m)
{
    mode = m;
    const bool rta = m == Mode::rta;

    for (auto* c : std::initializer_list<juce::Component*> { &rtaDisplay, &rtaResolution, &rtaAveraging, &peakHoldToggle, &captureButton, &clearButton, &rtaPlot })
        c->setVisible (rta);
    for (auto* c : std::initializer_list<juce::Component*> { &tfMagnitude, &tfPhase, &tfCoherence, &findDelayButton, &delayChip })
        c->setVisible (! rta);
    clearDelayButton.setVisible (! rta && haveDelay);

    applyAnalyzerSettings();
    session.getAnalyzer().reset();
    layoutMode();
    repaint();
}

//==============================================================================
void LivePage::pageShown()
{
    updateAvailability();
    applyAnalyzerSettings();
    generator.setSettings (session.getAudio().getGenerator());
    startTimerHz (25);
}

void LivePage::pageHidden()
{
    stopTimer();
}

void LivePage::sessionChanged()
{
    updateAvailability();
}

void LivePage::updateAvailability()
{
    const bool demo = session.getMode() == Session::Mode::demo;
    juce::String whyNot;
    const bool ready = session.getAudio().isReady (whyNot);
    liveAvailable = ! demo && ready;
    unavailableReason = demo ? "Demo mode" : whyNot;

    banner.setVisible (demo);
    generator.setHardwareAvailable (! demo);

    const auto routing = session.getAudio().getRouting();
    const int mics = (int) routing.micInputs.size();
    {
        const int wanted = juce::jlimit (0, juce::jmax (0, mics - 1), kit::liveAnalyzerMic (session));
        if (wanted != micIndex)
        {
            micIndex = wanted;
            peakHold.clear();
            session.getAnalyzer().reset();
            spl.resetHold();
        }
        juce::String text = demo ? "Virtual mic" : "Mic " + juce::String (micIndex + 1);
        if (! demo && mics > 1)
            text << " of " << mics;
        micChip.set (text + "  -  set on Rig", theme::textDim);
    }

    if (! liveAvailable)
        spl.setIdle (demo ? "no live audio in Demo mode" : whyNot);

    resized();
    repaint();
}

void LivePage::applyAnalyzerSettings()
{
    auto st = session.getAnalyzer().getSettings();
    st.averagingTimeS = averagingSecondsFor (rtaAveraging.getSelected());
    const bool line = rtaDisplay.getSelected() == 1;
    st.smoothingFraction = (mode == Mode::rta && line) ? (double) bandsPerOctaveFor (rtaResolution.getSelected()) : (mode == Mode::rta ? 0.0 : 24.0);
    const auto gen = session.getAudio().getGenerator();
    st.hannWindow = gen.signal != GeneratorSettings::Signal::pinkPeriodic;
    const auto cal = session.getCalibration (micIndex);
    st.splOffsetDb = cal.isValid() ? cal.sensitivityDb : 0.0;
    session.getAnalyzer().setSettings (st);
}

void LivePage::rebuildBands()
{
    const int n = bandsPerOctaveFor (rtaResolution.getSelected());
    if (n == lastBandsPerOctave)
        return;
    lastBandsPerOctave = n;
    bandCentres.clear();
    for (int k = -200; k <= 200; ++k)
    {
        const double fc = 1000.0 * std::pow (2.0, (double) k / n);
        if (fc >= 20.0 && fc <= 20000.0)
            bandCentres.push_back (fc);
    }
    peakHold.clear();
    rtaPlot.removeTrace ("peak");
}

void LivePage::pushGenerator()
{
    if (session.getMode() == Session::Mode::demo)
    {
        if (shell != nullptr)
            shell->toast ("Generator needs Live mode", AppShell::ToastKind::warn);
        return;
    }
    session.getAudio().setGenerator (generator.getSettings());
    applyAnalyzerSettings();
}

//==============================================================================
void LivePage::timerCallback()
{
    if (! liveAvailable)
    {
        generator.setOutputPeak (-90.0f);
        return;
    }

    auto& audio = session.getAudio();
    auto& an = session.getAnalyzer();

    // SPL + meters
    const auto cal = session.getCalibration (micIndex);
    const double lvl = an.computeLevel (micIndex, weightingFor (spl.getWeighting()));
    spl.setLevel (lvl, cal.isValid());
    const auto routing = audio.getRouting();
    if (micIndex < (int) routing.micInputs.size())
        spl.setInputPeak (audio.getInputPeakDb (routing.micInputs[(size_t) micIndex]));
    generator.setOutputPeak (audio.getOutputPeakDb());

    if (mode == Mode::rta) tickRta();
    else                   tickTransfer();
}

void LivePage::tickRta()
{
    auto& an = session.getAnalyzer();
    const auto r = an.computeRTA (micIndex);
    if (r.isEmpty())
        return;

    const bool bars = rtaDisplay.getSelected() == 0;
    std::vector<double> freqs, db;

    if (bars)
    {
        // power-average the fine grid into fractional-octave bands
        const int n = lastBandsPerOctave;
        const double halfBand = std::pow (2.0, 0.5 / n);
        freqs = bandCentres;
        db.assign (freqs.size(), -120.0);
        size_t start = 0;
        for (size_t b = 0; b < freqs.size(); ++b)
        {
            const double lo = freqs[b] / halfBand, hi = freqs[b] * halfBand;
            double p = 0.0; int cnt = 0;
            while (start < r.size() && r.freqs[start] < lo) ++start;
            for (size_t i = start; i < r.size() && r.freqs[i] < hi; ++i)
            {
                p += std::pow (10.0, r.magnitudeDb (i) / 10.0);
                ++cnt;
            }
            if (cnt > 0)
                db[b] = 10.0 * std::log10 (p / cnt);
        }
    }
    else
    {
        freqs.reserve (r.size());
        db.reserve (r.size());
        for (size_t i = 0; i < r.size(); ++i)
            if (r.freqs[i] >= 20.0 && r.freqs[i] <= 20000.0)
            {
                freqs.push_back (r.freqs[i]);
                db.push_back (r.magnitudeDb (i));
            }
    }

    if (freqs.empty())
        return;

    snapshotFreqs = freqs;
    snapshotDb = db;

    auto live = PlotTrace::fromDb ("live", bars ? "RTA" : "RTA (line)", theme::accent, freqs, db);
    live.bars = bars;
    live.fillToBottom = ! bars;
    live.thickness = 2.0f;
    rtaPlot.setTrace (live);

    if (peakHoldToggle.getToggleState())
    {
        if (peakHold.size() != db.size())
            peakHold.assign (db.size(), -200.0);
        for (size_t i = 0; i < db.size(); ++i)
            peakHold[i] = std::max (peakHold[i] - 0.08, db[i]);   // very slow decay (~2 dB/s)
        auto pk = PlotTrace::fromDb ("peak", "Peak hold", theme::roleTweeter, freqs, peakHold);
        pk.thickness = 1.2f;
        rtaPlot.setTrace (pk);
    }

    // adaptive range: keep the loudest band inside the top 10..20 dB of the plot
    double mx = -200.0;
    for (double v : db) mx = std::max (mx, v);
    if (mx > -150.0)
    {
        const double top = std::ceil ((mx + 8.0) / 10.0) * 10.0;
        if (! rtaRangeSet || top > rtaYMax || top < rtaYMax - 20.0)
        {
            rtaYMax = top;
            rtaYMin = top - 90.0;
            rtaRangeSet = true;
            rtaPlot.setRanges (20.0, 20000.0, rtaYMin, rtaYMax);
        }
    }
}

void LivePage::tickTransfer()
{
    auto& an = session.getAnalyzer();
    const auto tf = an.computeTransferFunction (micIndex);
    if (tf.h.isEmpty())
        return;

    auto mag = PlotTrace::fromResponse ("tf", "Mic / reference", theme::accent, tf.h);
    mag.alpha = tf.coherence;
    mag.thickness = 2.0f;
    tfMagnitude.setTrace (mag);

    auto ph = PlotTrace::fromPhase ("tfp", "Phase", theme::roleTweeter, tf.h);
    ph.alpha = tf.coherence;
    ph.thickness = 1.6f;
    tfPhase.setTrace (ph);

    auto coh = PlotTrace::fromDb ("coh", "Coherence", theme::good, tf.h.freqs, tf.coherence);
    coh.fillToBottom = true;
    coh.thickness = 1.4f;
    tfCoherence.setTrace (coh);

    if (! haveDelay && tf.delayMs != 0.0)
    {
        haveDelay = true;
        delayMs = tf.delayMs;
        delayChip.set ("Delay " + theme::formatMs (delayMs) + juce::String (juce::CharPointer_UTF8 (" ms  \xC2\xB7  ")) + juce::String (juce::roundToInt (msToMetres (delayMs) * 100.0)) + " cm", theme::accent);
        layoutMode();
    }
}
} // namespace fa::ui
