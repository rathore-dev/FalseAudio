#include "TeachWizard.h"

namespace fa::ui::dsp
{
//==============================================================================
/** Polls the control under the mouse on a background thread (UI Automation calls can take tens
    of milliseconds) and hands the description to the wizard on the message thread. */
class TeachWizard::HoverPoller : private juce::Thread
{
public:
    HoverPoller (void* window, std::function<void (const juce::String&)> onText)
        : juce::Thread ("DSP teach hover"), dspWindow (window), callback (std::move (onText))
    {
        startThread (juce::Thread::Priority::low);
    }

    ~HoverPoller() override
    {
        stopThread (2000);
    }

private:
    void run() override
    {
        juce::String last;
        while (! threadShouldExit())
        {
            const auto pos = uia::Automation::mousePosition();
            const auto bounds = uia::Automation::windowScreenBounds (dspWindow);
            juce::String text;
            if (bounds.contains (pos))
                text = describeElement (automation.elementAt (pos, dspWindow));
            else
                text = "Move the mouse over the DSP software...";

            if (text != last)
            {
                last = text;
                auto cb = callback;
                juce::MessageManager::callAsync ([cb, text, alive = aliveFlag] { if (*alive && cb) cb (text); });
            }
            wait (120);
        }
        *aliveFlag = false;
    }

    void* dspWindow;
    uia::Automation automation;
    std::function<void (const juce::String&)> callback;
    std::shared_ptr<std::atomic<bool>> aliveFlag = std::make_shared<std::atomic<bool>> (true);
};

//==============================================================================
static juce::String defaultTestValue (const juce::String& key)
{
    if (key.startsWith ("gain") || key == "peq.gain") return "-3";
    if (key == "delay")                              return "1.5";
    if (key == "hp.freq")                            return "80";
    if (key == "lp.freq")                            return "3000";
    if (key == "peq.freq")                           return "1000";
    if (key == "peq.q")                              return "1.41";
    return "0";
}

TeachWizard::TeachWizard (Session& s, DspWindowHost& h) : session (s), host (h)
{
    viewport.setViewedComponent (&body, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    for (auto* b : { &cancelBtn, &backBtn, &skipBtn, &retakeBtn, &nextBtn })
        addAndMakeVisible (b);

    cancelBtn.onClick = [this] { cancel(); };
    backBtn.onClick = [this]
    {
        if (stage == Stage::capture)
        {
            if (stepIndex > 0) enterStep (stepIndex - 1);
            else               setStage (Stage::setup);
        }
        else if (stage == Stage::caps)
        {
            if (steps.empty()) setStage (Stage::setup);
            else { setStage (Stage::capture); enterStep ((int) steps.size() - 1); }
        }
        else if (stage == Stage::finish) setStage (Stage::caps);
    };
    skipBtn.onClick = [this]
    {
        if (auto* st = const_cast<Step*> (currentStep()))
        {
            st->skipped = true;
            profile.bindings.erase (st->key);
            if (stepIndex + 1 < (int) steps.size()) enterStep (stepIndex + 1);
            else                                     setStage (Stage::caps);
        }
    };
    retakeBtn.onClick = [this] { captured.reset(); testResult.clear(); beginCapture(); };
    nextBtn.onClick = [this]
    {
        switch (stage)
        {
            case Stage::setup:   buildSteps(); if (steps.empty()) setStage (Stage::caps); else { setStage (Stage::capture); enterStep (0); } break;
            case Stage::capture: commitStep(); if (stepIndex + 1 < (int) steps.size()) enterStep (stepIndex + 1); else setStage (Stage::caps); break;
            case Stage::caps:    setStage (Stage::finish); break;
            case Stage::finish:  finish(); break;
        }
    };

    // ---- setup widgets
    setupInfo.setText ("Teach once, reuse forever",
                       "For each control FalseAudio asks you to click it inside your DSP software. It records how to reach "
                       "that control and how to operate it. Clicks are only watched inside the embedded DSP window.");
    body.addAndMakeVisible (setupInfo);
    body.addAndMakeVisible (channelLayout);
    body.addAndMakeVisible (bandLayout);
    body.addAndMakeVisible (setupBands);
    body.addAndMakeVisible (essentialOnly);
    channelLayout.onChange = [this] (int) { layoutBody(); body.repaint(); };
    bandLayout.onChange = [this] (int) { layoutBody(); body.repaint(); };
    setupBands.onChange = [this] (double) { body.repaint(); };
    essentialOnly.onClick = [this] { body.repaint(); };

    // ---- capture widgets
    methodBox.addItemList (methodNames(), 1);
    methodBox.onChange = [this]
    {
        if (captured && methodBox.getSelectedId() > 0)
            captured->method = (ControlBinding::Method) (methodBox.getSelectedId() - 1);
    };
    for (auto* u : { "ms", "cm", "m", "in", "samples" })
        unitBox.addItem (u, unitBox.getNumItems() + 1);
    unitBox.setSelectedId (1, juce::dontSendNotification);
    for (int i = 0; i < crossoverRows().size(); ++i)
        testSelect.addItem (crossoverRows()[i], i + 1);
    testSelect.setSelectedId (2, juce::dontSendNotification);

    for (auto* c : std::initializer_list<juce::Component*> { &methodBox, &unitBox, &testSelect, &decimalsField, &scaleField,
                                                             &offsetField, &settleField, &testValue, &invertToggle, &testToggle, &testBtn })
        body.addChildComponent (c);

    testBtn.onClick = [this] { runTest(); };
    testToggle.setToggleState (true, juce::dontSendNotification);

    for (int i = 0; i < crossoverRows().size(); ++i)
    {
        auto* e = mapEditors.add (new juce::TextEditor());
        e->setFont (theme::font (12.5f));
        e->setColour (juce::TextEditor::backgroundColourId, theme::bg1);
        e->setColour (juce::TextEditor::outlineColourId, theme::border);
        e->setColour (juce::TextEditor::focusedOutlineColourId, theme::accentDim);
        e->setColour (juce::TextEditor::textColourId, theme::text);
        e->setTextToShowWhenEmpty ("(not available)", theme::textMuted);
        body.addChildComponent (e);
    }

    // ---- caps / finish
    body.addChildComponent (capsEditor);
    nameEditor.setFont (theme::fontMedium (15.0f));
    nameEditor.setColour (juce::TextEditor::backgroundColourId, theme::bg1);
    nameEditor.setColour (juce::TextEditor::outlineColourId, theme::border);
    nameEditor.setColour (juce::TextEditor::focusedOutlineColourId, theme::accent);
    nameEditor.setColour (juce::TextEditor::textColourId, theme::text);
    nameEditor.setTextToShowWhenEmpty ("Profile name, e.g. Helix DSP PC-Tool 5", theme::textMuted);
    nameEditor.onTextChange = [this] { updateButtons(); };
    body.addChildComponent (nameEditor);
    activateNow.setToggleState (true, juce::dontSendNotification);
    body.addChildComponent (activateNow);

    setStage (Stage::setup);
}

TeachWizard::~TeachWizard()
{
    stop();
}

//==============================================================================
void TeachWizard::start (const DspProfile& existing, const juce::File& existingFile)
{
    stop();
    active = true;
    profile = existing;
    profileFile = existingFile;
    steps.clear();
    stepIndex = -1;
    captured.reset();
    testResult.clear();

    if (profile.caps.peqBandsPerChannel <= 0)
        profile.caps = session.getConfig().caps;
    if (existingFile == juce::File())
        profile.caps = session.getConfig().caps;
    setupBands.setValue (profile.caps.peqBandsPerChannel);

    const auto w = host.getAttached();
    profile.processName = w.processName;
    profile.windowTitleContains = w.title;
    nameEditor.setText (existing.name.isNotEmpty() && existingFile != juce::File() ? existing.name
                        : (w.processName.isNotEmpty() ? w.processName.upToLastOccurrenceOf (".", false, false) + " profile" : juce::String ("New DSP profile")),
                        false);

    // Guess layouts from an existing profile: any "@c" keyed non-select binding => own controls per channel.
    bool ownPerChannel = false, ownPerBand = false;
    for (auto& [k, v] : profile.bindings)
    {
        if (k.contains ("@c") && ! k.startsWith ("channel.select")) ownPerChannel = true;
        if (k.contains ("@b") && ! k.startsWith ("peq.select"))     ownPerBand = true;
    }
    channelLayout.setSelected (ownPerChannel ? 1 : 0);
    bandLayout.setSelected (ownPerBand ? 1 : 0);

    setStage (Stage::setup);
}

void TeachWizard::stop()
{
    poller.reset();
    if (teach != nullptr)
        teach->cancel();
    teach.reset();
    capturing = false;
    active = false;
}

void TeachWizard::windowChanged()
{
    if (! active)
        return;
    if (! host.isAttached())
    {
        if (toast) toast ("The DSP software was detached - teaching cancelled.", AppShell::ToastKind::warn);
        cancel();
        return;
    }
    if (stage == Stage::capture)
    {
        teach.reset();
        poller.reset();
        if (! captured)
            beginCapture();
    }
}

void TeachWizard::cancel()
{
    stop();
    if (onCancelled)
        onCancelled();
}

//==============================================================================
void TeachWizard::setStage (Stage s)
{
    stage = s;
    if (s != Stage::capture)
    {
        if (teach != nullptr) teach->cancel();
        poller.reset();
        capturing = false;
    }
    if (s == Stage::caps)
    {
        auto c = profile.caps;
        c.peqBandsPerChannel = (int) setupBands.getValue();
        capsEditor.setCapabilities (c);
    }
    if (s == Stage::finish)
        profile.caps = capsEditor.getCapabilities();

    const bool setup = s == Stage::setup, cap = s == Stage::capture, caps = s == Stage::caps, fin = s == Stage::finish;
    setupInfo.setVisible (setup); channelLayout.setVisible (setup); bandLayout.setVisible (setup);
    setupBands.setVisible (setup); essentialOnly.setVisible (setup);
    capsEditor.setVisible (caps);
    nameEditor.setVisible (fin); activateNow.setVisible (fin);
    if (! cap)
        for (auto* c : std::initializer_list<juce::Component*> { &methodBox, &unitBox, &testSelect, &decimalsField, &scaleField,
                                                                 &offsetField, &settleField, &testValue, &invertToggle, &testToggle, &testBtn })
            c->setVisible (false);
    if (! cap)
        for (auto* e : mapEditors) e->setVisible (false);

    updateButtons();
    layoutBody();
    viewport.setViewPosition (0, 0);
    repaint();
    if (fin)
        nameEditor.grabKeyboardFocus();
}

juce::String TeachWizard::channelLabel (int dspOutput) const
{
    for (auto& ch : session.getConfig().channels)
        if (ch.dspOutput == dspOutput)
            return ch.label + " (out " + juce::String (dspOutput + 1) + ")";
    return "Output " + juce::String (dspOutput + 1);
}

void TeachWizard::buildSteps()
{
    steps.clear();
    std::vector<int> outputs;
    for (auto& ch : session.getConfig().channels)
        outputs.push_back (ch.dspOutput);
    std::sort (outputs.begin(), outputs.end());
    outputs.erase (std::unique (outputs.begin(), outputs.end()), outputs.end());
    if (outputs.empty())
        outputs = { 0 };

    const bool ownPerChannel = channelLayout.getSelected() == 1;
    const bool ownPerBand = bandLayout.getSelected() == 1;
    const int bands = std::max (1, (int) setupBands.getValue());
    const bool essential = essentialOnly.getToggleState();

    std::vector<int> allBands;
    for (int b = 0; b < bands; ++b)
        allBands.push_back (b);

    for (auto& slot : teachSlots())
    {
        if (essential && ! slot.important && slot.scope == TeachSlot::Scope::global)
            continue;

        std::vector<int> chans = ownPerChannel ? outputs : std::vector<int> { -1 };
        std::vector<int> bandList { -1 };

        if (slot.scope == TeachSlot::Scope::perChannel)
        {
            if (ownPerChannel) continue;           // no selector needed
            chans = outputs;
        }
        else if (slot.scope == TeachSlot::Scope::perBand)
        {
            if (ownPerBand) continue;              // no band selector needed
            bandList = allBands;
        }
        else if (slot.key.startsWith ("peq.") && ownPerBand)
        {
            bandList = allBands;
        }

        for (int c : chans)
            for (int b : bandList)
            {
                Step st;
                st.slot = &slot;
                st.channel = c;
                st.band = b;
                st.key = DspProfile::keyFor (slot.key, c, b);
                st.title = slot.label;
                if (c >= 0) st.title << juce::String (juce::CharPointer_UTF8 ("  —  ")) << channelLabel (c);
                if (b >= 0) st.title << juce::String (juce::CharPointer_UTF8 ("  —  band ")) << (b + 1);
                steps.push_back (st);
            }
    }
}

void TeachWizard::enterStep (int index)
{
    stepIndex = juce::jlimit (0, (int) steps.size() - 1, index);
    auto& st = steps[(size_t) stepIndex];
    testResult.clear();
    captured.reset();

    auto it = profile.bindings.find (st.key);
    if (it != profile.bindings.end())
    {
        captured = it->second;
        capturedInfo = {};
        loadOptionsFrom (*captured);
        capturing = false;
        if (teach != nullptr) teach->cancel();
    }
    else
    {
        ControlBinding defaults;
        if (st.slot->key == "delay") defaults.unit = "ms";
        if (st.slot->key == "peq.q") defaults.decimals = 3;
        if (st.slot->key.endsWith (".freq")) defaults.decimals = 0;
        loadOptionsFrom (defaults);
        beginCapture();
    }

    testValue.setValue (defaultTestValue (st.slot->key).getDoubleValue());
    updateButtons();
    layoutBody();
    viewport.setViewPosition (0, 0);
    repaint();
}

void TeachWizard::beginCapture()
{
    if (! host.isAttached())
        return;
    auto* window = host.getAttached().handle;
    if (teach == nullptr)
        teach = std::make_unique<TeachSession> (window);
    if (poller == nullptr)
        poller = std::make_unique<HoverPoller> (window, [this] (const juce::String& t) { hoverText = t; body.repaint (hoverRect); });

    const auto* st = currentStep();
    if (st == nullptr)
        return;
    capturing = true;
    teach->capture (st->slot->kind, [this] (const ControlBinding& b, const uia::ElementInfo& info) { onCaptured (b, info); });
    updateButtons();
    layoutBody();
    repaint();
}

void TeachWizard::onCaptured (const ControlBinding& b, const uia::ElementInfo& info)
{
    capturing = false;
    captured = b;
    capturedInfo = info;
    testResult.clear();
    methodBox.setSelectedId ((int) b.method + 1, juce::dontSendNotification);
    if (toast) toast ("Captured: " + b.capturedDescription, AppShell::ToastKind::good);
    updateButtons();
    layoutBody();
    repaint();
}

void TeachWizard::loadOptionsFrom (const ControlBinding& b)
{
    methodBox.setSelectedId ((int) b.method + 1, juce::dontSendNotification);
    const int unitId = juce::StringArray { "ms", "cm", "m", "in", "samples" }.indexOf (b.unit) + 1;
    unitBox.setSelectedId (unitId > 0 ? unitId : 1, juce::dontSendNotification);
    decimalsField.setValue (b.decimals);
    scaleField.setValue (b.scale);
    offsetField.setValue (b.offset);
    settleField.setValue (b.settleMs);
    invertToggle.setToggleState (b.invertToggle, juce::dontSendNotification);
    for (int i = 0; i < mapEditors.size(); ++i)
    {
        auto it = b.valueMap.find (crossoverRows()[i]);
        mapEditors[i]->setText (it != b.valueMap.end() ? it->second : juce::String(), false);
    }
}

void TeachWizard::applyOptionsTo (ControlBinding& b) const
{
    const auto* st = currentStep();
    if (st == nullptr)
        return;
    if (methodBox.getSelectedId() > 0)
        b.method = (ControlBinding::Method) (methodBox.getSelectedId() - 1);
    b.unit = st->slot->key == "delay" ? unitBox.getText() : juce::String();
    b.decimals = (int) decimalsField.getValue();
    b.scale = scaleField.getValue();
    b.offset = offsetField.getValue();
    b.settleMs = (int) settleField.getValue();
    b.invertToggle = invertToggle.getToggleState();
    b.valueMap.clear();
    if (st->slot->kind == TeachSlot::Kind::select)
        for (int i = 0; i < mapEditors.size(); ++i)
            if (mapEditors[i]->getText().trim().isNotEmpty())
                b.valueMap[crossoverRows()[i]] = mapEditors[i]->getText().trim();
}

void TeachWizard::commitStep()
{
    auto* st = const_cast<Step*> (currentStep());
    if (st == nullptr)
        return;
    if (captured)
    {
        applyOptionsTo (*captured);
        profile.bindings[st->key] = *captured;
        st->skipped = false;
    }
    else
    {
        st->skipped = true;
        profile.bindings.erase (st->key);
    }
}

int TeachWizard::countTaught() const
{
    int n = 0;
    for (auto& st : steps)
        if (profile.bindings.count (st.key) > 0)
            ++n;
    return n;
}

//==============================================================================
void TeachWizard::runTest()
{
    const auto* st = currentStep();
    if (st == nullptr || ! captured || testing || ! host.isAttached())
        return;

    ControlBinding b = *captured;
    applyOptionsTo (b);
    auto* window = host.getAttached().handle;

    juce::String text;
    double numeric = 0.0;
    bool toggle = true;
    switch (st->slot->kind)
    {
        case TeachSlot::Kind::numeric:
            numeric = testValue.getValue() * b.scale + b.offset;
            text = juce::String (numeric, b.decimals);
            break;
        case TeachSlot::Kind::toggle:
            toggle = testToggle.getToggleState();
            break;
        case TeachSlot::Kind::select:
            text = testSelect.getText();        // perform() maps it through valueMap
            break;
        case TeachSlot::Kind::action:
            break;
    }

    testing = true;
    testResult = "Testing...";
    updateButtons();
    body.repaint (testRect);

    juce::Component::SafePointer<TeachWizard> safe (this);
    juce::Thread::launch ([safe, b, window, text, numeric, toggle]
    {
        juce::String error;
        bool ok = false;
        {
            UIAutomationController controller (DspProfile(), [window] { return window; }, nullptr);
            if (controller.connect (error))
                ok = controller.perform (b, text, numeric, toggle, error);
        }
        juce::MessageManager::callAsync ([safe, ok, error]
        {
            if (safe != nullptr)
                safe->testFinished (ok, error);
        });
    });
}

void TeachWizard::testFinished (bool ok, const juce::String& error)
{
    testing = false;
    testOk = ok;
    testResult = ok ? "Worked - check the value changed in the DSP software." : (error.isNotEmpty() ? error : juce::String ("Failed."));
    updateButtons();
    body.repaint (testRect);
}

void TeachWizard::finish()
{
    profile.name = nameEditor.getText().trim();
    if (profile.name.isEmpty())
        return;
    profile.caps = capsEditor.getCapabilities();
    const auto w = host.getAttached();
    if (w.processName.isNotEmpty())
    {
        profile.processName = w.processName;
        profile.windowTitleContains = w.title;
    }

    juce::File target = profileFile;
    if (target == juce::File() || target.getFileNameWithoutExtension() != juce::File::createLegalFileName (profile.name))
    {
        target = profileFileFor (profile.name);
        if (profileFile != juce::File() && profileFile.existsAsFile() && target != profileFile)
            profileFile.deleteFile();     // renamed
    }

    if (! profile.save (target))
    {
        if (toast) toast ("Could not save " + target.getFullPathName(), AppShell::ToastKind::bad);
        return;
    }

    const bool activate = activateNow.getToggleState();
    const auto saved = profile;
    stop();
    if (toast) toast ("Saved profile \"" + saved.name + "\" (" + juce::String ((int) saved.bindings.size()) + " controls)", AppShell::ToastKind::good);
    if (onFinished)
        onFinished (saved, target, activate);
}

//==============================================================================
void TeachWizard::updateButtons()
{
    const auto* st = currentStep();
    const bool cap = stage == Stage::capture;
    backBtn.setVisible (stage != Stage::setup);
    skipBtn.setVisible (cap && ! captured);
    retakeBtn.setVisible (cap && (captured.has_value() || ! capturing));
    retakeBtn.setButtonText (captured ? "Retake" : "Capture again");
    testBtn.setEnabled (captured.has_value() && ! testing);

    switch (stage)
    {
        case Stage::setup:   nextBtn.setButtonText ("Start teaching"); nextBtn.setIcon (icons::Id::play); nextBtn.setEnabled (host.isAttached()); break;
        case Stage::capture: nextBtn.setButtonText (stepIndex + 1 >= (int) steps.size() ? "Capabilities" : "Next"); nextBtn.setIcon (icons::Id::chevronRight); nextBtn.setEnabled (captured.has_value()); break;
        case Stage::caps:    nextBtn.setButtonText ("Next"); nextBtn.setIcon (icons::Id::chevronRight); nextBtn.setEnabled (true); break;
        case Stage::finish:  nextBtn.setButtonText ("Save profile"); nextBtn.setIcon (icons::Id::save); nextBtn.setEnabled (nameEditor.getText().trim().isNotEmpty()); break;
    }
    juce::ignoreUnused (st);
    resized();      // button-bar layout depends on which buttons are visible
}

void TeachWizard::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (stage == Stage::capture ? 64 : 48);          // header painted by paint()

    auto bar = r.removeFromBottom (theme::controlHeight);
    cancelBtn.setBounds (bar.removeFromLeft (72));
    nextBtn.setBounds (bar.removeFromRight (std::min (150, bar.getWidth() / 2)));
    bar.removeFromRight (6);
    auto mid = bar;
    if (retakeBtn.isVisible()) { retakeBtn.setBounds (mid.removeFromRight (std::min (120, mid.getWidth()))); mid.removeFromRight (6); }
    if (skipBtn.isVisible())   { skipBtn.setBounds (mid.removeFromRight (std::min (64, mid.getWidth()))); mid.removeFromRight (6); }
    if (backBtn.isVisible())     backBtn.setBounds (mid.removeFromRight (std::min (64, mid.getWidth())));
    r.removeFromBottom (theme::grid);

    viewport.setBounds (r);
    layoutBody();
}

void TeachWizard::layoutBody()
{
    const int vw = viewport.getWidth();
    if (vw <= 0)
        return;

    auto layoutAt = [this] (int width) -> int
    {
        const int gap = theme::grid;
        int y = 0;
        switch (stage)
        {
            case Stage::setup:
            {
                setupInfo.setBounds (0, y, width, setupInfo.idealHeight (width)); y += setupInfo.getHeight() + gap;
                y += 28;   // detected app row (painted)
                y += 22;   // "CHANNEL LAYOUT"
                channelLayout.setBounds (0, y, width, 34); y += 34 + 4;
                y += 34;   // explanation line (painted)
                y += 22;   // "EQ BANDS"
                bandLayout.setBounds (0, y, width, 34); y += 34 + gap;
                setupBands.setBounds (0, y, std::min (width, 240), theme::controlHeight); y += theme::controlHeight + gap;
                essentialOnly.setBounds (0, y, width, 28); y += 28 + gap;
                y += 40;   // step estimate (painted)
                break;
            }
            case Stage::capture:
            {
                const auto* st = currentStep();
                const bool numeric = st != nullptr && st->slot->kind == TeachSlot::Kind::numeric;
                const bool toggle  = st != nullptr && st->slot->kind == TeachSlot::Kind::toggle;
                const bool select  = st != nullptr && st->slot->kind == TeachSlot::Kind::select;
                const bool isDelay = st != nullptr && st->slot->key == "delay";

                promptRect = { 0, y, width, 104 }; y += 104 + gap;
                hoverRect = { 0, y, width, 66 }; y += 66 + gap;

                const bool have = captured.has_value();
                capturedRect = { 0, y, width, have ? 96 : 56 };
                methodBox.setVisible (have);
                if (have) methodBox.setBounds (capturedRect.reduced (12).removeFromBottom (theme::controlHeight - 2));
                y += capturedRect.getHeight() + gap;

                // options
                y += 22;
                const int half = (width - gap) / 2;
                int oy = y;
                unitBox.setVisible (isDelay);
                decimalsField.setVisible (numeric);
                scaleField.setVisible (numeric);
                offsetField.setVisible (numeric);
                invertToggle.setVisible (toggle);
                settleField.setVisible (true);
                if (numeric)
                {
                    if (isDelay) { unitBox.setBounds (0, oy, half, theme::controlHeight); decimalsField.setBounds (half + gap, oy, half, theme::controlHeight); }
                    else         { decimalsField.setBounds (0, oy, half, theme::controlHeight); }
                    oy += theme::controlHeight + gap;
                    scaleField.setBounds (0, oy, half, theme::controlHeight);
                    offsetField.setBounds (half + gap, oy, half, theme::controlHeight);
                    oy += theme::controlHeight + gap;
                }
                if (toggle)
                {
                    invertToggle.setBounds (0, oy, width, 28);
                    oy += 28 + gap;
                }
                settleField.setBounds (0, oy, half, theme::controlHeight);
                oy += theme::controlHeight + gap;
                optionsRect = { 0, y, width, oy - y };
                y = oy;

                // value map
                for (auto* e : mapEditors) e->setVisible (select);
                if (select)
                {
                    y += 22;
                    mapRect.setBounds (0, y, width, 0);
                    for (auto* e : mapEditors)
                    {
                        e->setBounds (120, y, width - 120, 26);
                        y += 26 + 4;
                    }
                    mapRect.setHeight (y - mapRect.getY());
                    y += gap;
                }

                // test
                y += 22;
                testValue.setVisible (numeric);
                testToggle.setVisible (toggle);
                testSelect.setVisible (select);
                testBtn.setVisible (true);
                auto row = juce::Rectangle<int> (0, y, width, theme::controlHeight);
                testBtn.setBounds (row.removeFromRight (90));
                row.removeFromRight (gap);
                if (numeric)      testValue.setBounds (row);
                else if (toggle)  testToggle.setBounds (row);
                else if (select)  testSelect.setBounds (row);
                y += theme::controlHeight + 4;
                testRect = { 0, y, width, 36 };
                y += 36;
                break;
            }
            case Stage::caps:
                y += 40;    // intro (painted)
                capsEditor.setBounds (0, y, width, capsEditor.idealHeight()); y += capsEditor.getHeight();
                break;
            case Stage::finish:
                y += 70;    // summary (painted)
                y += 22;
                nameEditor.setBounds (0, y, width, 36); y += 36 + gap;
                activateNow.setBounds (0, y, width, 28); y += 28 + gap;
                y += 60;    // note
                break;
        }
        return y;
    };

    int h = layoutAt (vw);
    int w = vw;
    if (h > viewport.getHeight())
    {
        w = vw - viewport.getScrollBarThickness() - 4;
        h = layoutAt (w);
    }
    body.setSize (w, std::max (h, viewport.getHeight()));
    body.repaint();
}

//==============================================================================
void TeachWizard::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    auto header = r.removeFromTop (stage == Stage::capture ? 64 : 48);

    juce::String title, sub;
    switch (stage)
    {
        case Stage::setup:   title = profileFile != juce::File() ? "Re-teach profile" : "Teach a new profile"; sub = "Step 1 - how your DSP software is laid out"; break;
        case Stage::capture: title = "Teach controls"; sub = "Step " + juce::String (stepIndex + 1) + " of " + juce::String ((int) steps.size()) + juce::String (juce::CharPointer_UTF8 ("  ·  ")) + juce::String (countTaught()) + " taught"; break;
        case Stage::caps:    title = "DSP capabilities"; sub = "What the optimiser may use"; break;
        case Stage::finish:  title = "Save profile"; sub = juce::String (countTaught()) + " controls taught"; break;
    }

    auto titleRow = header.removeFromTop (24);
    g.setColour (theme::text);
    g.setFont (theme::fontBold (16.0f));
    g.drawText (title, titleRow, juce::Justification::centredLeft);
    g.setColour (theme::textDim);
    g.setFont (theme::font (12.0f));
    g.drawText (sub, titleRow, juce::Justification::centredRight);

    if (stage == Stage::capture && ! steps.empty())
    {
        // progress dots: one per slot group
        struct Group { juce::String key; bool allDone = true, anySkipped = false, current = false; };
        std::vector<Group> groups;
        for (size_t i = 0; i < steps.size(); ++i)
        {
            if (groups.empty() || groups.back().key != steps[i].slot->key)
                groups.push_back ({ steps[i].slot->key });
            auto& gr = groups.back();
            gr.allDone = gr.allDone && profile.bindings.count (steps[i].key) > 0;
            gr.anySkipped = gr.anySkipped || steps[i].skipped;
            gr.current = gr.current || (int) i == stepIndex;
        }
        auto dots = header.removeFromBottom (14).withTrimmedTop (2);
        const int n = (int) groups.size();
        const float dotW = std::min (22.0f, (float) dots.getWidth() / (float) std::max (1, n));
        float x = (float) dots.getX();
        for (auto& gr : groups)
        {
            auto c = gr.current ? theme::accent : gr.allDone ? theme::good : gr.anySkipped ? theme::textMuted : theme::borderHi;
            const float h = gr.current ? 6.0f : 4.0f;
            auto d = juce::Rectangle<float> (x + 1.5f, dots.getCentreY() - h * 0.5f, dotW - 3.0f, h);
            if (gr.current)
                theme::drawGlow (g, d, theme::accent, 3.0f, 4.0f);
            g.setColour (c);
            g.fillRoundedRectangle (d, h * 0.5f);
            x += dotW;
        }
    }

    g.setColour (theme::border);
    g.fillRect (header.getX(), header.getBottom() - 1, header.getWidth(), 1);
}

void TeachWizard::paintBody (juce::Graphics& g)
{
    switch (stage)
    {
        case Stage::setup:   paintSetup (g);   break;
        case Stage::capture: paintCapture (g); break;
        case Stage::caps:    paintCaps (g);    break;
        case Stage::finish:  paintFinish (g);  break;
    }
}

void TeachWizard::paintSetup (juce::Graphics& g)
{
    const int width = body.getWidth();
    int y = setupInfo.getBottom() + theme::grid;

    // detected app
    const auto w = host.getAttached();
    g.setColour (host.isAttached() ? theme::good : theme::warn);
    g.fillEllipse ((float) 2, (float) y + 10, 7.0f, 7.0f);
    g.setColour (theme::textDim);
    g.setFont (theme::font (12.5f));
    g.drawText (host.isAttached() ? "Teaching " + w.processName + juce::String (juce::CharPointer_UTF8 ("  ·  ")) + w.title : juce::String ("Attach your DSP software first"),
                14, y, width - 14, 28, juce::Justification::centredLeft, true);
    y += 28;

    drawSectionLabel (g, "Channel layout", { 0, y, width, 18 }); y += 22;
    y += 34 + 4;
    g.setColour (theme::textMuted);
    g.setFont (theme::font (12.0f));
    g.drawFittedText (channelLayout.getSelected() == 0
                          ? "Most DSP tools show one output at a time: you will teach the channel selector for every output, then each control once."
                          : "Every output has its own controls on screen: each control is taught once per output.",
                      { 0, y, width, 34 }, juce::Justification::topLeft, 2);
    y += 34;

    drawSectionLabel (g, "Equaliser bands", { 0, y, width, 18 }); y += 22;
    y += 34 + theme::grid + theme::controlHeight + theme::grid + 28 + theme::grid;

    // estimate
    const int outs = std::max (1, (int) session.getConfig().channels.size());
    const int bands = std::max (1, (int) setupBands.getValue());
    int est = 0;
    for (auto& slot : teachSlots())
    {
        if (essentialOnly.getToggleState() && ! slot.important && slot.scope == TeachSlot::Scope::global) continue;
        const bool ownC = channelLayout.getSelected() == 1, ownB = bandLayout.getSelected() == 1;
        int c = ownC ? outs : 1, b = 1;
        if (slot.scope == TeachSlot::Scope::perChannel) { if (ownC) continue; c = outs; }
        else if (slot.scope == TeachSlot::Scope::perBand) { if (ownB) continue; b = bands; }
        else if (slot.key.startsWith ("peq.") && ownB) b = bands;
        est += c * b;
    }
    g.setColour (est > 60 ? theme::warn : theme::textDim);
    g.setFont (theme::fontMedium (12.5f));
    g.drawFittedText ("About " + juce::String (est) + " clicks to teach" + (est > 60 ? " - consider the selector layouts if your DSP has them." : "."),
                      { 0, y, width, 40 }, juce::Justification::topLeft, 2);
}

void TeachWizard::paintCapture (juce::Graphics& g)
{
    const auto* st = currentStep();
    if (st == nullptr)
        return;
    const int width = body.getWidth();

    // prompt
    {
        auto r = promptRect.toFloat();
        if (capturing)
            theme::drawGlow (g, r, theme::accent, theme::radius, 10.0f);
        theme::fillCard (g, r, theme::bg3, capturing ? theme::accent.withAlpha (0.8f) : captured ? theme::good.withAlpha (0.6f) : theme::borderHi);
        auto inner = promptRect.reduced (14, 10);
        g.setColour (capturing ? theme::accent : theme::textDim);
        g.setFont (theme::fontMedium (11.0f));
        g.drawText (capturing ? "WAITING FOR YOUR CLICK" : captured ? "CAPTURED" : "NOT CAPTURED", inner.removeFromTop (14), juce::Justification::centredLeft);
        g.setColour (theme::text);
        g.setFont (theme::fontBold (16.0f));
        g.drawFittedText (capturing ? "Click the " + st->title + " in your DSP software now" : st->title,
                          inner.removeFromTop (42), juce::Justification::centredLeft, 2);
        g.setColour (theme::textDim);
        g.setFont (theme::font (12.0f));
        g.drawFittedText (st->slot->hint, inner, juce::Justification::topLeft, 2);
    }

    // hover
    {
        theme::fillCard (g, hoverRect.toFloat(), theme::bg1, theme::border, theme::radiusSm);
        auto inner = hoverRect.reduced (12, 8);
        drawSectionLabel (g, "Live: under the mouse", inner.removeFromTop (16));
        g.setColour (theme::accentHi.withAlpha (0.9f));
        g.setFont (theme::mono (11.5f));
        g.drawFittedText (hoverText, inner, juce::Justification::topLeft, 2);
    }

    // captured
    {
        theme::fillCard (g, capturedRect.toFloat(), theme::bg2, captured ? theme::good.withAlpha (0.5f) : theme::border, theme::radiusSm);
        auto inner = capturedRect.reduced (12, 8);
        drawSectionLabel (g, "Captured control", inner.removeFromTop (16));
        g.setColour (captured ? theme::text : theme::textMuted);
        g.setFont (theme::font (12.5f));
        juce::String desc = captured ? captured->capturedDescription : juce::String ("Nothing yet - click the control in the DSP window.");
        if (captured && ! captured->path.isEmpty())
            desc << juce::String (juce::CharPointer_UTF8 ("   ·   ")) << captured->path.describe();
        g.drawFittedText (desc, inner.removeFromTop (captured ? 22 : 30), juce::Justification::centredLeft, captured ? 1 : 2);
    }

    drawSectionLabel (g, "Options", { 0, optionsRect.getY() - 22, width, 18 });

    if (st->slot->kind == TeachSlot::Kind::select)
    {
        drawSectionLabel (g, "Combo text for each crossover type", { 0, mapRect.getY() - 22, width, 18 });
        for (int i = 0; i < mapEditors.size(); ++i)
        {
            auto e = mapEditors[i]->getBounds();
            g.setColour (theme::text);
            g.setFont (theme::fontMedium (12.5f));
            g.drawText (crossoverRows()[i], 0, e.getY(), 44, e.getHeight(), juce::Justification::centredLeft);
            g.setColour (theme::textMuted);
            g.setFont (theme::font (10.5f));
            g.drawText (crossoverRowDescription (crossoverRows()[i]), 44, e.getY(), 76, e.getHeight(), juce::Justification::centredLeft, true);
        }
    }

    drawSectionLabel (g, "Test on the DSP software", { 0, testRect.getY() - theme::controlHeight - 4 - 22, width, 18 });
    if (testResult.isNotEmpty())
    {
        g.setColour (testing ? theme::textDim : testOk ? theme::good : theme::bad);
        g.setFont (theme::font (12.0f));
        g.drawFittedText (testResult, testRect, juce::Justification::topLeft, 2);
    }
}

void TeachWizard::paintCaps (juce::Graphics& g)
{
    g.setColour (theme::textDim);
    g.setFont (theme::font (12.5f));
    g.drawFittedText ("The optimiser only proposes settings your DSP accepts. Check these against your DSP's manual - "
                      "wrong limits mean values your DSP will clip or refuse.",
                      { 0, 0, body.getWidth(), 36 }, juce::Justification::topLeft, 2);
}

void TeachWizard::paintFinish (juce::Graphics& g)
{
    const int width = body.getWidth();
    int skipped = 0;
    for (auto& st : steps)
        if (profile.bindings.count (st.key) == 0)
            ++skipped;

    auto r = juce::Rectangle<int> (0, 0, width, 62);
    theme::fillCard (g, r.toFloat(), theme::bg2, theme::border, theme::radiusSm);
    auto inner = r.reduced (12, 8);
    g.setColour (theme::text);
    g.setFont (theme::fontBold (22.0f));
    g.drawText (juce::String (countTaught()), inner.removeFromLeft (56), juce::Justification::centredLeft);
    g.setColour (theme::textDim);
    g.setFont (theme::font (12.5f));
    g.drawFittedText ("controls automated" + (skipped > 0 ? "\n" + juce::String (skipped) + " skipped - these will appear as a checklist during Auto Tune (hybrid mode)."
                                                           : juce::String ("\nEverything the optimiser needs is automated.")),
                      inner, juce::Justification::centredLeft, 2);

    drawSectionLabel (g, "Profile name", { 0, 70, width, 18 });

    g.setColour (theme::textMuted);
    g.setFont (theme::font (12.0f));
    g.drawFittedText ("Saved to " + DspProfile::profilesFolder().getFullPathName(),
                      { 0, activateNow.getBottom() + theme::grid, width, 50 }, juce::Justification::topLeft, 3);
}
} // namespace fa::ui::dsp
