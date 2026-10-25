#include "HardwarePage.h"
#include "hardware/ModeBanner.h"
#include "hardware/MicCalPanel.h"

namespace fa::ui
{
namespace
{
    constexpr int kMaxMics = 4;
    constexpr int kTimerHz = 30;
    constexpr int kNoiseTicks  = 24;   // 0.8 s of silence to find the noise floor
    constexpr int kSettleTicks = 6;    // ignore the first 0.2 s after the generator starts
    constexpr int kSignalTicks = 75;   // 2.5 s of pink noise

    int cardChrome (Card& c)
    {
        const auto saved = c.getBounds();
        c.setSize (400, 300);
        const int chrome = 300 - c.getContentArea().getHeight();
        c.setBounds (saved);
        return chrome;
    }

    void fillCombo (juce::ComboBox& box, const juce::StringArray& names, const juce::String& noneText = {})
    {
        box.clear (juce::dontSendNotification);
        int id = 1;
        if (noneText.isNotEmpty())
            box.addItem (noneText, id++);
        for (auto& n : names)
            box.addItem (n, id++);
        box.setEnabled (names.size() > 0);
        if (names.isEmpty() && noneText.isEmpty())
        {
            box.addItem ("No channels", 1);
            box.setSelectedId (1, juce::dontSendNotification);
        }
    }

    juce::String bufferText (int samples, double rate)
    {
        return juce::String (samples) + " samples  (" + juce::String (samples * 1000.0 / juce::jmax (1.0, rate), 1) + " ms)";
    }
}

//==============================================================================
HardwarePage::HardwarePage (Session& s) : Page (s)
{
    addAndMakeVisible (body);
    body.layout = [this] (int w) { return layoutContent (w); };
    auto& c = body.content();

    banner = std::make_unique<ModeBanner>();
    c.addAndMakeVisible (*banner);
    banner->onSwitch = [this] (bool wantLive)
    {
        session.setMode (wantLive ? Session::Mode::live : Session::Mode::demo);
        toast (wantLive ? "Live mode: sweeps now use your interface" : "Demo mode: virtual car and microphones",
               AppShell::ToastKind::good);
    };

    for (auto* card : { &deviceCard, &routingCard, &calCard, &levelsCard })
        c.addAndMakeVisible (card);

    // ---- device
    for (auto* w : std::initializer_list<juce::Component*> { &typeCaption, &typeBox, &outCaption, &outBox, &inCaption, &inBox,
                                                             &rateCaption, &rateBox, &bufferCaption, &bufferBox, &deviceStatus, &rescanBtn })
        deviceCard.addAndMakeVisible (w);
    for (auto* b : { &typeBox, &outBox, &inBox, &rateBox, &bufferBox, &leftOutBox, &rightOutBox, &loopOutBox, &loopInBox })
    {
        kit::styleCombo (*b);
        b->setJustificationType (juce::Justification::centredLeft);
    }
    typeBox.onChange = [this]
    {
        if (updating) return;
        session.getAudio().getDeviceManager().setCurrentAudioDeviceType (typeBox.getText(), true);
        refreshDevices();
    };
    outBox.onChange = [this] { applyDeviceSetup(); };
    inBox.onChange = [this] { applyDeviceSetup(); };
    rateBox.onChange = [this] { applyDeviceSetup(); };
    bufferBox.onChange = [this] { applyDeviceSetup(); };
    rescanBtn.onClick = [this]
    {
        for (auto* t : session.getAudio().getDeviceManager().getAvailableDeviceTypes())
            t->scanForDevices();
        refreshDevices();
        toast ("Devices rescanned");
    };
    deviceListener.onChange = [this] { refreshDevices(); refreshRouting(); refreshCalibration(); refreshMeters(); };
    session.getAudio().getDeviceManager().addChangeListener (&deviceListener);

    // ---- routing
    for (auto* w : std::initializer_list<juce::Component*> { &leftCaption, &leftOutBox, &rightCaption, &rightOutBox, &micsCaption,
                                                             &addMicBtn, &loopCaption, &loopToggle, &loopOutCaption, &loopOutBox,
                                                             &loopInCaption, &loopInBox, &routingHint })
        routingCard.addAndMakeVisible (w);
    leftOutBox.onChange  = [this] { applyRouting(); };
    rightOutBox.onChange = [this] { applyRouting(); };
    loopOutBox.onChange  = [this] { applyRouting(); };
    loopInBox.onChange   = [this] { applyRouting(); };
    loopToggle.onClick   = [this]
    {
        if (updating) return;
        auto r = session.getAudio().getRouting();
        if (loopToggle.getToggleState())
        {
            const int outs = session.getAudio().getOutputChannelNames().size();
            const int ins  = session.getAudio().getInputChannelNames().size();
            r.loopbackOutput = juce::jmax (0, juce::jmin (outs - 1, 2));
            r.loopbackInput  = juce::jmax (0, juce::jmin (ins - 1, (int) r.micInputs.size()));
        }
        else
        {
            r.loopbackOutput = r.loopbackInput = -1;
        }
        session.setRouting (r);
    };
    addMicBtn.onClick = [this]
    {
        auto r = session.getAudio().getRouting();
        if ((int) r.micInputs.size() >= kMaxMics) return;
        const int ins = session.getAudio().getInputChannelNames().size();
        int next = 0;
        for (auto m : r.micInputs) next = juce::jmax (next, m + 1);
        r.micInputs.push_back (ins > 0 ? juce::jmin (next, ins - 1) : next);
        session.setRouting (r);
    };

    // ---- calibration
    calCard.addAndMakeVisible (calHint);

    // ---- levels
    for (auto* w : std::initializer_list<juce::Component*> { &outMeter.name, &outMeter.value, &outMeter.meter, &outMeter.clip,
                                                             &levelCheckBtn, &levelAdvice })
        levelsCard.addAndMakeVisible (w);
    kit::styleLabel (outMeter.name, 12.5f, theme::text);
    kit::styleLabel (outMeter.value, 12.0f, theme::textDim, juce::Justification::centredRight);
    outMeter.value.setFont (theme::mono (12.0f));
    outMeter.name.setText ("Output", juce::dontSendNotification);
    outMeter.clip.setVisible (false);
    levelCheckBtn.onClick = [this] { startLevelCheck(); };

    refreshBanner();
    refreshDevices();
    refreshRouting();
    refreshCalibration();
    refreshMeters();
}

HardwarePage::~HardwarePage()
{
    stopTimer();
    session.getAudio().getDeviceManager().removeChangeListener (&deviceListener);
    if (phase != CheckPhase::idle)
    {
        GeneratorSettings g;
        g.signal = GeneratorSettings::Signal::off;
        session.getAudio().setGenerator (g);
    }
}

//==============================================================================
void HardwarePage::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg1);
}

void HardwarePage::resized()
{
    body.setBounds (getLocalBounds());
}

void HardwarePage::pageShown()
{
    refreshDevices();
    refreshRouting();
    refreshCalibration();
    refreshMeters();
    startTimerHz (kTimerHz);
}

void HardwarePage::pageHidden()
{
    stopTimer();
    if (phase != CheckPhase::idle)
        finishLevelCheck();
}

void HardwarePage::sessionChanged()
{
    refreshBanner();
    refreshRouting();
    refreshCalibration();
    refreshMeters();
}

void HardwarePage::toast (const juce::String& msg, AppShell::ToastKind kind)
{
    if (shell != nullptr)
        shell->toast (msg, kind);
}

//==============================================================================
void HardwarePage::refreshBanner()
{
    banner->setDemo (session.getMode() == Session::Mode::demo);
}

void HardwarePage::refreshDevices()
{
    const juce::ScopedValueSetter<bool> svs (updating, true);
    auto& dm = session.getAudio().getDeviceManager();

    juce::StringArray typeNames;
    for (auto* t : dm.getAvailableDeviceTypes())
        typeNames.add (t->getTypeName());
    fillCombo (typeBox, typeNames);
    typeBox.setText (dm.getCurrentAudioDeviceType(), juce::dontSendNotification);

    auto setup = dm.getAudioDeviceSetup();
    juce::StringArray outs, ins;
    if (auto* type = dm.getCurrentDeviceTypeObject())
    {
        outs = type->getDeviceNames (false);
        ins  = type->getDeviceNames (true);
    }
    fillCombo (outBox, outs, "None");
    fillCombo (inBox, ins, "None");
    outBox.setText (setup.outputDeviceName.isEmpty() ? "None" : setup.outputDeviceName, juce::dontSendNotification);
    inBox.setText (setup.inputDeviceName.isEmpty() ? "None" : setup.inputDeviceName, juce::dontSendNotification);

    rateBox.clear (juce::dontSendNotification);
    bufferBox.clear (juce::dontSendNotification);
    auto* device = dm.getCurrentAudioDevice();
    if (device != nullptr)
    {
        int id = 1;
        for (auto r : device->getAvailableSampleRates())
            rateBox.addItem (juce::String (r / 1000.0, r == std::floor (r / 1000.0) * 1000.0 ? 0 : 1) + " kHz", id++);
        rateBox.setText (juce::String (device->getCurrentSampleRate() / 1000.0, 1).replace (".0", "") + " kHz", juce::dontSendNotification);

        id = 1;
        for (auto b : device->getAvailableBufferSizes())
            bufferBox.addItem (bufferText (b, device->getCurrentSampleRate()), id++);
        bufferBox.setText (bufferText (device->getCurrentBufferSizeSamples(), device->getCurrentSampleRate()), juce::dontSendNotification);

        const int inCh  = device->getActiveInputChannels().countNumberOfSetBits();
        const int outCh = device->getActiveOutputChannels().countNumberOfSetBits();
        deviceStatus.setTextColour (theme::good);
        deviceStatus.setText ("Open" + kit::sep() + device->getTypeName() + kit::sep() + juce::String (inCh) + " in / " + juce::String (outCh)
                              + " out" + kit::sep() + "latency " + juce::String ((device->getInputLatencyInSamples() + device->getOutputLatencyInSamples())
                                                                     * 1000.0 / device->getCurrentSampleRate(), 1) + " ms round trip");
    }
    else
    {
        rateBox.addItem ("-", 1);
        bufferBox.addItem ("-", 1);
        deviceStatus.setTextColour (theme::warn);
        deviceStatus.setText ("No audio device is open. Pick an output and input device above - in Demo mode nothing is needed.");
    }
    rateBox.setEnabled (device != nullptr);
    bufferBox.setEnabled (device != nullptr);
}

void HardwarePage::applyDeviceSetup()
{
    if (updating)
        return;
    auto& dm = session.getAudio().getDeviceManager();
    auto setup = dm.getAudioDeviceSetup();
    setup.outputDeviceName = outBox.getSelectedId() == 1 ? juce::String() : outBox.getText();
    setup.inputDeviceName  = inBox.getSelectedId()  == 1 ? juce::String() : inBox.getText();
    setup.useDefaultInputChannels = true;
    setup.useDefaultOutputChannels = true;

    if (auto* device = dm.getCurrentAudioDevice())
    {
        const auto rates = device->getAvailableSampleRates();
        const int ri = rateBox.getSelectedId() - 1;
        if (ri >= 0 && ri < rates.size())
            setup.sampleRate = rates[ri];
        const auto buffers = device->getAvailableBufferSizes();
        const int bi = bufferBox.getSelectedId() - 1;
        if (bi >= 0 && bi < buffers.size())
            setup.bufferSize = buffers[bi];
    }

    const auto err = dm.setAudioDeviceSetup (setup, true);
    if (err.isNotEmpty())
        toast (err, AppShell::ToastKind::bad);
    refreshDevices();
}

//==============================================================================
void HardwarePage::refreshRouting()
{
    const juce::ScopedValueSetter<bool> svs (updating, true);
    auto& audio = session.getAudio();
    const auto r = audio.getRouting();
    const auto outs = audio.getOutputChannelNames();
    const auto ins  = audio.getInputChannelNames();

    auto selectIndex = [] (juce::ComboBox& box, int index, bool hasNone)
    {
        const int id = hasNone ? index + 2 : index + 1;
        if (box.getNumItems() > 0 && index >= 0 && id <= box.getNumItems())
            box.setSelectedId (id, juce::dontSendNotification);
        else if (hasNone)
            box.setSelectedId (1, juce::dontSendNotification);
        else
            box.setText ((hasNone ? "" : "Channel ") + juce::String (index + 1) + (box.isEnabled() ? "" : " (device closed)"), juce::dontSendNotification);
    };

    fillCombo (leftOutBox, outs);
    fillCombo (rightOutBox, outs);
    selectIndex (leftOutBox, r.leftOutput, false);
    selectIndex (rightOutBox, r.rightOutput, false);

    if (micRows.size() != r.micInputs.size())
    {
        micRows.clear();
        for (size_t i = 0; i < r.micInputs.size(); ++i)
        {
            auto row = std::make_unique<MicRow>();
            row->caption.setText ("Mic " + juce::String (i + 1));
            kit::styleCombo (row->input);
            row->input.onChange = [this] { applyRouting(); };
            row->remove.onClick = [this, i]
            {
                auto rr = session.getAudio().getRouting();
                if (rr.micInputs.size() > 1 && i < rr.micInputs.size())
                {
                    rr.micInputs.erase (rr.micInputs.begin() + (long) i);
                    session.setRouting (rr);
                }
            };
            row->remove.setTooltip ("Remove this microphone");
            routingCard.addAndMakeVisible (row->caption);
            routingCard.addAndMakeVisible (row->input);
            routingCard.addAndMakeVisible (row->remove);
            micRows.push_back (std::move (row));
        }
        body.relayout();
    }
    for (size_t i = 0; i < micRows.size(); ++i)
    {
        fillCombo (micRows[i]->input, ins);
        selectIndex (micRows[i]->input, r.micInputs[i], false);
        micRows[i]->remove.setEnabled (micRows.size() > 1);
    }
    addMicBtn.setEnabled ((int) r.micInputs.size() < kMaxMics);
    addMicBtn.setButtonText ((int) r.micInputs.size() < kMaxMics ? "Add microphone" : "4 microphones max");

    const bool loop = r.loopbackOutput >= 0 && r.loopbackInput >= 0;
    loopToggle.setToggleState (loop, juce::dontSendNotification);
    fillCombo (loopOutBox, outs);
    fillCombo (loopInBox, ins);
    selectIndex (loopOutBox, r.loopbackOutput, false);
    selectIndex (loopInBox, r.loopbackInput, false);
    for (auto* w : std::initializer_list<juce::Component*> { &loopOutCaption, &loopOutBox, &loopInCaption, &loopInBox })
        w->setVisible (loop);
    body.relayout();
}

void HardwarePage::applyRouting()
{
    if (updating)
        return;
    auto r = session.getAudio().getRouting();
    auto indexOf = [] (const juce::ComboBox& box, int fallback) { return box.getSelectedId() > 0 ? box.getSelectedId() - 1 : fallback; };
    r.leftOutput  = indexOf (leftOutBox, r.leftOutput);
    r.rightOutput = indexOf (rightOutBox, r.rightOutput);
    for (size_t i = 0; i < micRows.size() && i < r.micInputs.size(); ++i)
        r.micInputs[i] = indexOf (micRows[i]->input, r.micInputs[i]);
    if (loopToggle.getToggleState())
    {
        r.loopbackOutput = indexOf (loopOutBox, r.loopbackOutput);
        r.loopbackInput  = indexOf (loopInBox, r.loopbackInput);
    }
    session.setRouting (r);
}

//==============================================================================
void HardwarePage::refreshCalibration()
{
    const auto r = session.getAudio().getRouting();
    const auto ins = session.getAudio().getInputChannelNames();

    if (calPanels.size() != r.micInputs.size())
    {
        calPanels.clear();
        for (size_t i = 0; i < r.micInputs.size(); ++i)
        {
            auto p = std::make_unique<MicCalPanel> ((int) i);
            p->onLoad = [this] (int idx) { loadCalibrationFor (idx); };
            calCard.addAndMakeVisible (*p);
            calPanels.push_back (std::move (p));
        }
        body.relayout();
    }
    for (size_t i = 0; i < calPanels.size(); ++i)
    {
        const int in = r.micInputs[i];
        const juce::String inName = in >= 0 && in < ins.size() ? ins[in] : "Input " + juce::String (in + 1);
        calPanels[i]->set (session.getCalibration ((int) i), inName);
    }
}

void HardwarePage::loadCalibrationFor (int micIndex)
{
    chooser = std::make_unique<juce::FileChooser> ("Load microphone calibration for mic " + juce::String (micIndex + 1),
                                                   juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
                                                   "*.txt;*.cal;*.frd;*.csv;*.mic");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, micIndex] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (! file.existsAsFile())
            return;
        juce::String error;
        if (session.loadCalibration (micIndex, file, error))
        {
            const auto cal = session.getCalibration (micIndex);
            toast ("Mic " + juce::String (micIndex + 1) + ": " + (cal.name.isNotEmpty() ? cal.name : file.getFileName())
                   + (cal.serial.isNotEmpty() ? "  S/N " + cal.serial : ""), AppShell::ToastKind::good);
        }
        else
            toast (error, AppShell::ToastKind::bad);
        refreshCalibration();
    });
}

//==============================================================================
void HardwarePage::refreshMeters()
{
    const auto r = session.getAudio().getRouting();
    const auto ins = session.getAudio().getInputChannelNames();

    if (micMeters.size() != r.micInputs.size())
    {
        micMeters.clear();
        for (size_t i = 0; i < r.micInputs.size(); ++i)
        {
            auto m = std::make_unique<MeterRow>();
            kit::styleLabel (m->name, 12.5f, theme::text);
            kit::styleLabel (m->value, 12.0f, theme::textDim, juce::Justification::centredRight);
            m->value.setFont (theme::mono (12.0f));
            m->clip.setVisible (false);
            for (auto* w : std::initializer_list<juce::Component*> { &m->name, &m->value, &m->meter, &m->clip })
                levelsCard.addAndMakeVisible (w);
            micMeters.push_back (std::move (m));
        }
        body.relayout();
    }
    for (size_t i = 0; i < micMeters.size(); ++i)
    {
        const int in = r.micInputs[i];
        const juce::String inName = in >= 0 && in < ins.size() ? ins[in] : "Input " + juce::String (in + 1);
        micMeters[i]->name.setText ("Mic " + juce::String (i + 1) + kit::sep() + inName, juce::dontSendNotification);
    }
}

void HardwarePage::timerCallback()
{
    auto& audio = session.getAudio();
    const auto r = audio.getRouting();

    int maxIn = 0;
    for (auto m : r.micInputs) maxIn = juce::jmax (maxIn, m + 1);
    if (r.loopbackInput >= 0) maxIn = juce::jmax (maxIn, r.loopbackInput + 1);
    lastPeaks.assign ((size_t) maxIn, -120.0f);
    for (int i = 0; i < maxIn; ++i)
        lastPeaks[(size_t) i] = audio.getInputPeakDb (i);
    lastOutPeak = audio.getOutputPeakDb();

    auto show = [] (MeterRow& row, float db)
    {
        row.meter.setLevel (db);
        row.value.setText (db <= -99.0f ? "-inf" : theme::formatDb (db, 1, false), juce::dontSendNotification);
        const auto now = juce::Time::getMillisecondCounter();
        if (db > -0.3f)
        {
            row.clipAt = now;
            row.clip.setVisible (true);
        }
        else if (row.clip.isVisible() && now - row.clipAt > 2000)
            row.clip.setVisible (false);
    };

    for (size_t i = 0; i < micMeters.size() && i < r.micInputs.size(); ++i)
    {
        const int in = r.micInputs[i];
        show (*micMeters[i], in >= 0 && in < (int) lastPeaks.size() ? lastPeaks[(size_t) in] : -120.0f);
    }
    show (outMeter, lastOutPeak);

    // ---- level check state machine
    if (phase == CheckPhase::idle)
        return;

    ++phaseTicks;
    const size_t n = r.micInputs.size();
    noisePeak.resize (n, -120.0f);
    signalPeak.resize (n, -120.0f);

    if (phase == CheckPhase::noise)
    {
        for (size_t i = 0; i < n; ++i)
        {
            const int in = r.micInputs[i];
            if (in >= 0 && in < (int) lastPeaks.size())
                noisePeak[i] = juce::jmax (noisePeak[i], lastPeaks[(size_t) in]);
        }
        if (phaseTicks >= kNoiseTicks)
        {
            GeneratorSettings g;
            g.signal = GeneratorSettings::Signal::pinkRandom;
            g.levelDbFS = -20.0;
            g.feed = Feed::both;
            audio.setGenerator (g);
            phase = CheckPhase::signal;
            phaseTicks = 0;
            levelAdvice.setText ("Playing pink noise at -20 dBFS through both DSP inputs...");
        }
    }
    else if (phase == CheckPhase::signal)
    {
        if (phaseTicks > kSettleTicks)
            for (size_t i = 0; i < n; ++i)
            {
                const int in = r.micInputs[i];
                if (in >= 0 && in < (int) lastPeaks.size())
                    signalPeak[i] = juce::jmax (signalPeak[i], lastPeaks[(size_t) in]);
            }
        if (phaseTicks >= kSettleTicks + kSignalTicks)
            finishLevelCheck();
    }
}

void HardwarePage::startLevelCheck()
{
    if (phase != CheckPhase::idle)
    {
        finishLevelCheck();
        return;
    }
    juce::String why;
    if (! session.getAudio().isReady (why))
    {
        levelAdvice.setTextColour (theme::warn);
        levelAdvice.setText (why + (session.getMode() == Session::Mode::demo
                                    ? "  The level check needs a real device; Demo mode measures a virtual car instead."
                                    : ""));
        body.relayout();
        return;
    }
    noisePeak.clear();
    signalPeak.clear();
    phase = CheckPhase::noise;
    phaseTicks = 0;
    levelCheckBtn.setButtonText ("Stop");
    levelCheckBtn.setIcon (icons::Id::stop);
    levelCheckBtn.setStyle (FaButton::Style::danger);
    levelAdvice.setTextColour (theme::textDim);
    levelAdvice.setText ("Listening for the noise floor - keep the car quiet...");
    if (! isTimerRunning())
        startTimerHz (kTimerHz);
}

void HardwarePage::finishLevelCheck()
{
    GeneratorSettings g;
    g.signal = GeneratorSettings::Signal::off;
    session.getAudio().setGenerator (g);

    const bool completed = phase == CheckPhase::signal && phaseTicks >= kSettleTicks + kSignalTicks;
    phase = CheckPhase::idle;
    levelCheckBtn.setButtonText ("Level check");
    levelCheckBtn.setIcon (icons::Id::play);
    levelCheckBtn.setStyle (FaButton::Style::primary);

    if (! completed)
    {
        levelAdvice.setTextColour (theme::textDim);
        levelAdvice.setText ("Level check stopped.");
        body.relayout();
        return;
    }

    juce::String text;
    juce::Colour worst = theme::good;
    for (size_t i = 0; i < signalPeak.size(); ++i)
    {
        const float sig = signalPeak[i], noise = noisePeak[i];
        const float snr = sig - noise;
        juce::String advice;
        juce::Colour col = theme::good;
        if (sig < -60.0f)       { advice = "no signal - check the mic input, phantom power and that the DSP is on"; col = theme::bad; }
        else if (sig < -40.0f)  { advice = "very low - raise the interface's mic gain or the amplifier level"; col = theme::warn; }
        else if (sig > -3.0f)   { advice = "too hot - lower the mic gain to avoid clipping during sweeps"; col = theme::bad; }
        else if (snr < 20.0f)   { advice = "noisy - close doors, switch off HVAC/engine or raise the sweep level"; col = theme::warn; }
        else if (sig > -12.0f)  { advice = "a little hot - fine, but leave headroom for the sweeps"; col = theme::warn; }
        else                    { advice = "good"; }

        if (col == theme::bad || (col == theme::warn && worst == theme::good))
            worst = col;

        text << "Mic " << juce::String (i + 1) << ": peak " << theme::formatDb (sig, 1, false) << " dBFS, floor "
             << theme::formatDb (noise, 1, false) << " dBFS, SNR " << juce::String (juce::roundToInt (snr)) << " dB - " << advice << "\n";
    }
    text << "Output peaked at " << theme::formatDb (lastOutPeak, 1, false) << " dBFS. Aim for mic peaks between -30 and -12 dBFS with SNR above 30 dB.";
    levelAdvice.setTextColour (worst == theme::good ? theme::textDim : worst);
    levelAdvice.setText (text.trimEnd());
    toast (worst == theme::good ? "Levels look good" : "Level check finished - see advice", worst == theme::good ? AppShell::ToastKind::good : AppShell::ToastKind::warn);
    body.relayout();
}

//==============================================================================
int HardwarePage::layoutContent (int w)
{
    using namespace kit;
    const int innerW = w - 2 * margin;
    const bool twoCols = innerW >= 900;
    const int colW = twoCols ? (innerW - gap2) / 2 : innerW;
    const int chrome = cardChrome (deviceCard);
    const int cellH = 16 + 6 + btnH;   // caption + combo
    int y = margin;

    banner->setBounds (margin, y, innerW, ModeBanner::height);
    y += ModeBanner::height + gap2;

    // ---- device card
    deviceCard.setBounds (margin, y, colW, 300);
    int deviceH = 0;
    {
        auto a = deviceCard.getContentArea();
        rescanBtn.setBounds (deviceCard.getHeaderActionArea().removeFromRight (96).withSizeKeepingCentre (96, 26));
        auto place = [&] (juce::Rectangle<int> cell, kit::Caption& cap, juce::ComboBox& box)
        {
            cap.setBounds (cell.removeFromTop (16));
            cell.removeFromTop (6);
            box.setBounds (cell.removeFromTop (btnH));
        };
        int yy = a.getY();
        place ({ a.getX(), yy, a.getWidth(), cellH }, typeCaption, typeBox);
        yy += cellH + gap;
        const int half = (a.getWidth() - gap) / 2;
        place ({ a.getX(), yy, half, cellH }, outCaption, outBox);
        place ({ a.getX() + half + gap, yy, half, cellH }, inCaption, inBox);
        yy += cellH + gap;
        place ({ a.getX(), yy, half, cellH }, rateCaption, rateBox);
        place ({ a.getX() + half + gap, yy, half, cellH }, bufferCaption, bufferBox);
        yy += cellH + gap2;
        deviceStatus.setBounds (a.getX(), yy, a.getWidth(), deviceStatus.heightFor (a.getWidth()));
        yy += deviceStatus.getHeight();
        deviceH = chrome + (yy - a.getY());
    }

    // ---- routing card
    const int routingX = twoCols ? margin + colW + gap2 : margin;
    const int routingY = twoCols ? y : y + deviceH + gap2;
    routingCard.setBounds (routingX, routingY, colW, 300);
    int routingH = 0;
    {
        auto a = routingCard.getContentArea();
        int yy = a.getY();
        const int half = (a.getWidth() - gap) / 2;
        leftCaption.setBounds (a.getX(), yy, half, 16);
        rightCaption.setBounds (a.getX() + half + gap, yy, half, 16);
        yy += 22;
        leftOutBox.setBounds (a.getX(), yy, half, btnH);
        rightOutBox.setBounds (a.getX() + half + gap, yy, half, btnH);
        yy += btnH + gap2;

        micsCaption.setBounds (a.getX(), yy, a.getWidth(), 16);
        yy += 22;
        for (auto& row : micRows)
        {
            auto line = juce::Rectangle<int> (a.getX(), yy, a.getWidth(), btnH);
            row->caption.setBounds (line.removeFromLeft (52));
            row->remove.setBounds (line.removeFromRight (btnH));
            line.removeFromRight (gap);
            row->input.setBounds (line);
            yy += btnH + gap;
        }
        addMicBtn.setBounds (a.getX(), yy, 170, 28);
        yy += 28 + gap2;

        loopCaption.setBounds (a.getX(), yy, a.getWidth(), 16);
        yy += 22;
        loopToggle.setBounds (a.getX(), yy, a.getWidth(), 24);
        yy += 24 + gap;
        if (loopOutBox.isVisible())
        {
            loopOutCaption.setBounds (a.getX(), yy, half, 16);
            loopInCaption.setBounds (a.getX() + half + gap, yy, half, 16);
            yy += 22;
            loopOutBox.setBounds (a.getX(), yy, half, btnH);
            loopInBox.setBounds (a.getX() + half + gap, yy, half, btnH);
            yy += btnH + gap;
        }
        yy += gap;
        routingHint.setBounds (a.getX(), yy, a.getWidth(), routingHint.heightFor (a.getWidth()));
        yy += routingHint.getHeight();
        routingH = chrome + (yy - a.getY());
    }

    if (twoCols)
    {
        const int h = juce::jmax (deviceH, routingH);
        deviceCard.setSize (colW, h);
        routingCard.setSize (colW, h);
        y += h + gap2;
    }
    else
    {
        deviceCard.setSize (colW, deviceH);
        routingCard.setSize (colW, routingH);
        y = routingY + routingH + gap2;
    }

    // ---- calibration card
    calCard.setBounds (margin, y, innerW, 300);
    {
        auto a = calCard.getContentArea();
        std::vector<juce::Component*> items;
        for (auto& p : calPanels) items.push_back (p.get());
        int yy = a.getY() + gridLayout (a, items, 300, MicCalPanel::height, gap2, gap2) + gap2;
        calHint.setBounds (a.getX(), yy, a.getWidth(), calHint.heightFor (a.getWidth()));
        yy += calHint.getHeight();
        calCard.setSize (innerW, chrome + (yy - a.getY()));
        y += calCard.getHeight() + gap2;
    }

    // ---- levels card
    levelsCard.setBounds (margin, y, innerW, 300);
    {
        auto a = levelsCard.getContentArea();
        int yy = a.getY();
        auto placeMeter = [&] (MeterRow& row)
        {
            auto line = juce::Rectangle<int> (a.getX(), yy, a.getWidth(), 22);
            row.name.setBounds (line.removeFromLeft (juce::jmin (260, a.getWidth() / 3)));
            row.value.setBounds (line.removeFromRight (64));
            line.removeFromRight (gap);
            row.clip.setBounds (line.removeFromRight (44).withSizeKeepingCentre (44, 18));
            line.removeFromRight (gap);
            row.meter.setBounds (line.reduced (0, 4));
            yy += 22 + gap;
        };
        for (auto& m : micMeters)
            placeMeter (*m);
        yy += gap;
        placeMeter (outMeter);
        yy += gap;
        levelCheckBtn.setBounds (a.getX(), yy, 150, btnH);
        const int adviceW = a.getWidth() - 150 - gap2;
        levelAdvice.setBounds (a.getX() + 150 + gap2, yy, adviceW, juce::jmax (btnH, levelAdvice.heightFor (adviceW)));
        yy += juce::jmax (btnH, levelAdvice.getHeight());
        levelsCard.setSize (innerW, chrome + (yy - a.getY()));
        y += levelsCard.getHeight();
    }

    return y + margin;
}
} // namespace fa::ui
