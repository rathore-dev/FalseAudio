#include "AiPanel.h"

namespace fa::ui::dsp
{
using F = AiOperatorFacade;

namespace
{
    constexpr int kLogH = 188;
    constexpr int kRowGap = 8;

    juce::Colour kindColour (const juce::String& k)
    {
        if (k == "click" || k == "type") return theme::accent;
        if (k == "think")  return theme::roleSub;
        if (k == "safety") return theme::warn;
        if (k == "error")  return theme::bad;
        return theme::textDim;
    }

    juce::String ellipsize (const juce::String& s, float maxW, const juce::Font& f)
    {
        if (theme::textWidth (f, s) <= maxW) return s;
        auto t = s;
        while (t.length() > 4 && theme::textWidth (f, t + "...") > maxW) t = t.dropLastCharacters (1);
        return t + "...";
    }

    void styleEditor (juce::TextEditor& e)
    {
        e.setColour (juce::TextEditor::backgroundColourId, theme::bg1);
        e.setColour (juce::TextEditor::outlineColourId, theme::border);
        e.setColour (juce::TextEditor::focusedOutlineColourId, theme::accentDim);
        e.setColour (juce::TextEditor::textColourId, theme::text);
        e.setColour (juce::TextEditor::highlightColourId, theme::accent.withAlpha (0.3f));
        e.setFont (theme::mono (12.5f));
        e.setIndents (10, 0);
    }
}

//==============================================================================
/** Scrolling action log: newest at the bottom, follows the newest entry until the user scrolls. */
class LogView : public juce::Component
{
public:
    void add (const F::LogEntry& e)
    {
        entries.push_back (e);
        if (entries.size() > 300) entries.erase (entries.begin());
        if (follow) scroll = 0; else scroll = std::min (scroll + 1, (int) entries.size() - 1);
        repaint();
    }
    void clear() { entries.clear(); scroll = 0; follow = true; repaint(); }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (theme::bg1);
        g.fillRoundedRectangle (b, theme::radiusSm);
        g.setColour (theme::border);
        g.drawRoundedRectangle (b.reduced (0.5f), theme::radiusSm, 1.0f);

        if (entries.empty())
        {
            g.setColour (theme::textMuted);
            g.setFont (theme::font (12.5f));
            g.drawFittedText ("What the AI clicks, types and reads appears here, with the reason for each action.",
                              getLocalBounds().reduced (16, 10), juce::Justification::centred, 3);
            return;
        }

        g.saveState();
        g.reduceClipRegion (getLocalBounds().reduced (1));
        const int rowH = 38;
        int y = getHeight() - 6;
        for (int i = (int) entries.size() - 1 - scroll; i >= 0 && y > -rowH; --i)
        {
            const auto& e = entries[(size_t) i];
            auto row = juce::Rectangle<int> (10, y - rowH, getWidth() - 20, rowH);
            const auto c = kindColour (e.kind);
            g.setColour (c);
            g.fillRoundedRectangle ((float) row.getX(), (float) row.getY() + 4.0f, 3.0f, (float) rowH - 12.0f, 1.5f);
            auto text = row.withTrimmedLeft (12);
            g.setFont (theme::mono (10.5f));
            g.setColour (theme::textMuted);
            g.drawText (e.time, text.removeFromTop (14).removeFromLeft (60), juce::Justification::centredLeft);
            g.setFont (theme::fontBold (10.0f));
            g.setColour (c);
            g.drawText (e.kind.toUpperCase(), text.getX() + 62, row.getY(), 52, 14, juce::Justification::centredLeft);
            g.setFont (theme::fontMedium (12.5f));
            g.setColour (theme::text);
            g.drawText (ellipsize (e.what, (float) text.getWidth() - 120, theme::fontMedium (12.5f)),
                        text.getX() + 112, row.getY() - 1, text.getWidth() - 112, 16, juce::Justification::centredLeft);
            g.setFont (theme::font (11.5f));
            g.setColour (theme::textDim);
            if (e.why.isNotEmpty())
            g.drawText (ellipsize ("Why: " + e.why, (float) text.getWidth(), theme::font (11.5f)),
                        text.getX(), row.getY() + 17, text.getWidth(), 16, juce::Justification::centredLeft);
            y -= rowH;
        }
        g.restoreState();

        if (scroll > 0)
        {
            g.setColour (theme::bg3);
            g.fillRoundedRectangle ((float) getWidth() - 96.0f, 6.0f, 88.0f, 20.0f, 10.0f);
            g.setColour (theme::accent);
            g.setFont (theme::fontMedium (11.0f));
            g.drawText ("Jump to latest", getWidth() - 96, 6, 88, 20, juce::Justification::centred);
        }
    }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        const int n = (int) entries.size();
        const int d = w.deltaY > 0 ? 1 : -1;
        scroll = juce::jlimit (0, std::max (0, n - 1), scroll + d);
        follow = scroll == 0;
        repaint();
    }
    void mouseDown (const juce::MouseEvent&) override { scroll = 0; follow = true; repaint(); }

private:
    std::vector<F::LogEntry> entries;
    int scroll = 0;
    bool follow = true;
};

//==============================================================================
class AiPanel::Body : public juce::Component, private juce::Timer
{
public:
    Body (AiPanel& o) : owner (o)
    {
        keyEditor.setPasswordCharacter ((juce::juce_wchar) 0x2022);
        keyEditor.setTextToShowWhenEmpty ("sk-ant-...", theme::textMuted);
        keyEditor.setComponentID ("ai.key");
        keyEditor.setSelectAllWhenFocused (true);
        keyEditor.setMultiLine (false);
        styleEditor (keyEditor);
        keyEditor.onReturnKey = [this] { saveKey(); };
        addAndMakeVisible (keyEditor);

        saveBtn.setComponentID ("ai.save");     testBtn.setComponentID ("ai.test");     removeBtn.setComponentID ("ai.remove");
        consent.setComponentID ("ai.consent");  runBtn.setComponentID ("ai.run");       stopBtn.setComponentID ("ai.stop");
        relearnBtn.setComponentID ("ai.relearn"); deleteBtn.setComponentID ("ai.delete"); getSwBtn.setComponentID ("ai.getsoftware");
        for (auto* c : std::initializer_list<juce::Component*> { &saveBtn, &testBtn, &removeBtn, &consent, &runBtn, &stopBtn,
                                                                  &relearnBtn, &deleteBtn, &getSwBtn, &modelBox, &budget, &log })
            addAndMakeVisible (c);
        addAndMakeVisible (keyLink);
        for (auto* c : std::initializer_list<juce::Component*> { &providerBox, &ollamaUrlEd, &ollamaModelEd, &testOllamaBtn })
            addAndMakeVisible (c);
        providerBox.setComponentID ("ai.provider");   ollamaUrlEd.setComponentID ("ai.ollamaurl");
        ollamaModelEd.setComponentID ("ai.ollamamodel"); testOllamaBtn.setComponentID ("ai.testollama");
        providerBox.addItem ("Claude (Anthropic API)", 1);
        providerBox.addItem ("Ollama local (free)", 2);
        for (auto* e : { &ollamaUrlEd, &ollamaModelEd })
        {
            styleEditor (*e);
            e->setMultiLine (false);
            e->onFocusLost = [this] { commitOllama(); };
            e->onReturnKey = [this] { commitOllama(); };
        }
        ollamaUrlEd.setTextToShowWhenEmpty ("http://localhost:11434", theme::textMuted);
        ollamaModelEd.setTextToShowWhenEmpty ("qwen3-vl:4b", theme::textMuted);
        providerBox.onChange = [this]
        {
            if (f == nullptr) return;
            f->setProvider (providerBox.getSelectedId() == 2 ? "ollama" : "claude");
            refreshAll();
        };
        testOllamaBtn.onClick = [this]
        {
            if (f == nullptr) return;
            commitOllama();
            ollamaState = "Testing Ollama...";  ollamaStateColour = theme::textDim;  repaint();
            juce::Component::SafePointer<Body> sp (this);
            f->testOllama ([sp] (bool ok, juce::String m)
            {
                if (sp == nullptr) return;
                sp->ollamaState = m;  sp->ollamaStateColour = ok ? theme::good : theme::bad;  sp->repaint();
            });
        };

        keyLink.setButtonText ("Get an API key at console.anthropic.com");
        keyLink.setURL (juce::URL ("https://console.anthropic.com/settings/keys"));
        keyLink.setFont (theme::font (12.0f), false, juce::Justification::centredLeft);
        keyLink.setColour (juce::HyperlinkButton::textColourId, theme::accent);

        saveBtn.onClick = [this] { saveKey(); };
        testBtn.onClick = [this]
        {
            if (f == nullptr) return;
            keyState = "Testing the key...";  keyStateColour = theme::textDim;  repaint();
            juce::Component::SafePointer<Body> sp (this);
            f->testKey ([sp] (bool ok, juce::String msg)
            {
                if (sp == nullptr) return;
                sp->keyState = msg;  sp->keyStateColour = ok ? theme::good : theme::bad;  sp->repaint();
            });
        };
        removeBtn.onClick = [this]
        {
            if (f == nullptr) return;
            f->removeKey();
            keyEditor.clear();
            keyState = {};
            refreshAll();
        };

        modelBox.onChange = [this]
        {
            if (f == nullptr) return;
            const auto ms = f->models();
            const int i = modelBox.getSelectedId() - 1;
            if (i >= 0 && i < (int) ms.size()) f->setModel (ms[(size_t) i].id);
            repaint();
        };
        budget.onChange = [this] (double v) { if (f != nullptr) f->setBudgetUsd (v); };

        consent.onClick = [this]
        {
            if (f != nullptr) f->setConsent (consent.getToggleState());
            refreshAll();
        };

        runBtn.onClick = [this] { startRun (false); };
        relearnBtn.onClick = [this] { startRun (true); };
        stopBtn.onClick = [this] { owner.stopNow(); };
        deleteBtn.onClick = [this]
        {
            if (f == nullptr) return;
            f->deleteMap();
            if (owner.toast) owner.toast ("Learned map deleted. The next run learns the layout again.", AppShell::ToastKind::info);
        };
        getSwBtn.onClick = [this]
        {
            if (f == nullptr) return;
            const auto list = f->vendorList();
            if (list.empty())
            {
                const auto url = f->softwareInfo().downloadUrl;
                if (url.isNotEmpty()) juce::URL (url).launchInDefaultBrowser();
                return;
            }
            juce::PopupMenu m;
            for (size_t i = 0; i < list.size(); ++i)
                m.addItem ((int) i + 1, list[i].vendor + " " + list[i].productName + (list[i].installed ? "  (installed)" : ""));
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&getSwBtn).withMinimumWidth (getSwBtn.getWidth()),
                             [list] (int r)
                             {
                                 if (r > 0 && (size_t) r <= list.size() && list[(size_t) r - 1].downloadUrl.isNotEmpty())
                                     juce::URL (list[(size_t) r - 1].downloadUrl).launchInDefaultBrowser();
                             });
        };

        runBtn.setHero (true);
        stopBtn.setHero (true);
    }

    void bind (F* facade)
    {
        f = facade;
        if (f == nullptr) { stopTimer(); return; }
        f->onStep = [this] (F::Step s, F::StepState st) { steps[(size_t) s] = st; repaint(); };
        f->onLog = [this] (const F::LogEntry& e) { log.add (e); };
        f->onCost = [this] (const F::Cost& c) { cost = c; repaint (costArea.expanded (4)); };
        f->onState = [this] (F::State s, const juce::String& m)
        {
            statusMsg = m;
            if (s == F::State::running)
            {
                steps.fill (F::StepState::pending);
                log.clear();
            }
            if (s == F::State::stopped || s == F::State::error)
                for (auto& st : steps) if (st == F::StepState::active) st = F::StepState::failed;
            if (s == F::State::error && owner.toast) owner.toast (m, AppShell::ToastKind::bad);
            if (s == F::State::done && owner.toast) owner.toast (m, AppShell::ToastKind::good);
            refreshAll();
        };
        f->onThumbnail = [this] (const juce::Image& im) { thumb = im; repaint (thumbArea); };
        f->onMapChanged = [this] { refreshMap(); };

        const auto ms = f->models();
        modelBox.clear (juce::dontSendNotification);
        int sel = 1;
        for (size_t i = 0; i < ms.size(); ++i)
        {
            modelBox.addItem (ms[i].label, (int) i + 1);
            if (ms[i].id == f->getModel()) sel = (int) i + 1;
        }
        modelBox.setSelectedId (sel, juce::dontSendNotification);
        budget.setRange (0.25, 50.0, 0.25);
        budget.setValue (f->getBudgetUsd());
        cost.budgetUsd = f->getBudgetUsd();
        consent.setToggleState (f->hasConsent(), juce::dontSendNotification);
        providerBox.setSelectedId (f->getProvider() == "ollama" ? 2 : 1, juce::dontSendNotification);
        ollamaUrlEd.setText (f->getOllamaUrl(), juce::dontSendNotification);
        ollamaModelEd.setText (f->getOllamaModel(), juce::dontSendNotification);
        refreshMap();
        refreshAll();
    }

    void setTarget (const juce::String& proc, const juce::String& title)
    {
        procName = proc;  winTitle = title;
        if (f != nullptr)
        {
            if (proc.isEmpty()) { f->setConsent (false); consent.setToggleState (false, juce::dontSendNotification); }
            f->setTarget (proc, title);
        }
        refreshAll();
    }

    bool isRunning() const { return f != nullptr && f->getState() == F::State::running; }

    //==========================================================================
    int layoutFor (int w)
    {
        width = w;
        int y = 0;
        auto row = [&] (int h) { auto r = juce::Rectangle<int> (0, y, w, h); y += h; return r; };
        auto gap = [&] (int h = kRowGap) { y += h; };

        const bool ol = isOllama();
        providerLabel = row (18);
        providerBox.setBounds (row (theme::controlHeight));
        gap (14);
        for (auto* c : std::initializer_list<juce::Component*> { &keyEditor, &saveBtn, &testBtn, &removeBtn, &keyLink, &modelBox, &budget })
            c->setVisible (! ol);
        for (auto* c : std::initializer_list<juce::Component*> { &ollamaUrlEd, &ollamaModelEd, &testOllamaBtn })
            c->setVisible (ol);
        keyLabel = keyStatusArea = modelNoteArea = ollamaLabel = ollamaStatusArea = ollamaNoteArea = {};
        if (ol)
        {
            ollamaLabel = row (18);
            {
                auto r = row (theme::controlHeight);
                testOllamaBtn.setBounds (r.removeFromRight (120)); r.removeFromRight (8);
                ollamaUrlEd.setBounds (r);
            }
            gap (6);
            ollamaModelEd.setBounds (row (theme::controlHeight));
            gap (6);
            ollamaStatusArea = row (18);
            gap (2);
            ollamaNoteArea = row (48);
        }
        else
        {
        keyLabel = row (18);
        {
            auto r = row (theme::controlHeight);
            removeBtn.setBounds (r.removeFromRight (82)); r.removeFromRight (6);
            testBtn.setBounds (r.removeFromRight (62));   r.removeFromRight (6);
            saveBtn.setBounds (r.removeFromRight (62));   r.removeFromRight (8);
            keyEditor.setBounds (r);
        }
        gap (6);
        keyStatusArea = row (18);
        keyLink.setBounds (row (18));
        gap (10);
        {
            auto r = row (theme::controlHeight);
            budget.setBounds (r.removeFromRight (130));   r.removeFromRight (8);
            modelBox.setBounds (r);
        }
        gap (4);
        modelNoteArea = row (16);
        }

        gap (14);
        consentLabel = row (18);
        targetArea = row (22);
        getSwBtn.setBounds (juce::Rectangle<int> (0, y, std::min (w, 250), theme::controlHeight));
        if (getSwBtn.isVisible()) y += theme::controlHeight + 6;
        consent.setBounds (row (30));
        gap (4);
        explainArea = row (juce::roundToInt (explainHeight (w)));
        gap (8);
        readyArea = row (20);
        gap (8);
        {
            auto r = row (46);
            stopBtn.setBounds (r.removeFromRight (std::min (120, w / 3)));  r.removeFromRight (8);
            runBtn.setBounds (r);
        }
        gap (4);
        statusArea = row (20);

        gap (14);
        activityLabel = row (18);
        {
            auto r = row (122);
            stepsArea = r.removeFromLeft (w * 46 / 100);
            r.removeFromLeft (12);
            thumbArea = r;
        }
        gap (8);
        costArea = row (46);
        gap (8);
        log.setBounds (row (kLogH));

        gap (14);
        mapLabel = row (18);
        {
            auto r = row (theme::controlHeight);
            deleteBtn.setBounds (r.removeFromRight (104)); r.removeFromRight (6);
            relearnBtn.setBounds (r.removeFromRight (104));
        }
        gap (6);
        mapArea = row (map.exists ? 24 + (int) map.outputs.size() * 26 : 44);
        gap (72);       // room so the global toast never covers the last row
        return y;
    }

    void paint (juce::Graphics& g) override
    {
        drawSectionLabel (g, "AI PROVIDER", providerLabel);
        drawSectionLabel (g, "ANTHROPIC API KEY", keyLabel);
        drawSectionLabel (g, "OLLAMA (RUNS ON THIS PC)", ollamaLabel);
        if (! ollamaStatusArea.isEmpty())
        {
            g.setFont (theme::font (12.5f));
            g.setColour (ollamaState.isNotEmpty() ? ollamaStateColour : theme::textDim);
            g.drawText (ellipsize (ollamaState.isNotEmpty() ? ollamaState : "Press Test Ollama to check the server and the model.",
                                   (float) ollamaStatusArea.getWidth(), theme::font (12.5f)), ollamaStatusArea, juce::Justification::centredLeft);
            juce::AttributedString a;
            a.setJustification (juce::Justification::topLeft);
            a.append ("Free and private: screenshots never leave your PC, no API key. Local runs are slow, about 1 minute per step on this PC, "
                      "so a first learn can take a while. Later runs reuse the saved map.", theme::font (11.5f), theme::textMuted);
            a.draw (g, ollamaNoteArea.toFloat());
        }
        drawSectionLabel (g, "SOFTWARE & CONSENT", consentLabel);
        drawSectionLabel (g, "LIVE ACTIVITY", activityLabel);
        drawSectionLabel (g, "LEARNED MAP", mapLabel);

        // key status
        {
            const bool has = f != nullptr && f->hasKey();
            auto r = keyStatusArea;
            icons::draw (g, has ? icons::Id::check : icons::Id::info, r.removeFromLeft (16).toFloat().reduced (1),
                         has ? theme::good : theme::textMuted);
            r.removeFromLeft (6);
            g.setFont (theme::font (12.5f));
            g.setColour (keyState.isNotEmpty() ? keyStateColour : has ? theme::good : theme::textDim);
            juce::String s = keyState.isNotEmpty() ? keyState
                           : has ? "Key saved (encrypted on this PC)  " + f->keyHint()
                                 : "No key saved. Paste your key above and press Save.";
            g.drawText (ellipsize (s, (float) r.getWidth(), theme::font (12.5f)), r, juce::Justification::centredLeft);
        }
        // model note
        if (f != nullptr)
        {
            const auto ms = f->models();
            const int i = modelBox.getSelectedId() - 1;
            g.setFont (theme::font (11.5f));
            g.setColour (theme::textMuted);
            g.drawText (i >= 0 && i < (int) ms.size() ? ms[(size_t) i].note + ".  The run stops by itself at the budget cap." : juce::String(),
                        modelNoteArea, juce::Justification::centredLeft);
        }

        // target
        {
            auto r = targetArea;
            const bool has = procName.isNotEmpty();
            icons::draw (g, has ? icons::Id::link : icons::Id::unlink, r.removeFromLeft (18).toFloat().reduced (1), has ? theme::good : theme::textMuted);
            r.removeFromLeft (6);
            g.setFont (theme::fontMedium (13.0f));
            g.setColour (has ? theme::text : theme::textDim);
            g.drawText (ellipsize (has ? procName + juce::String (juce::CharPointer_UTF8 ("   ·   ")) + winTitle
                                       : juce::String ("No DSP software attached - use Attach or Launch on the left"),
                                   (float) r.getWidth(), theme::fontMedium (13.0f)), r, juce::Justification::centredLeft);
        }
        paintExplain (g);

        // readiness
        {
            auto r = readyArea;
            const bool k = isOllama() || (f != nullptr && f->hasKey()), w = procName.isNotEmpty(), c = consent.getToggleState();
            const std::pair<const char*, bool> items[] = { { isOllama() ? "Ollama" : "API key", k }, { "Window", w }, { "Consent", c } };
            for (auto& it : items)
            {
                auto cell = r.removeFromLeft (100);
                auto ic = cell.removeFromLeft (14).toFloat().reduced (1);
                if (it.second) icons::draw (g, icons::Id::check, ic, theme::good);
                else { g.setColour (theme::borderHi); g.drawEllipse (ic.reduced (1.5f), 1.3f); }
                g.setFont (theme::font (12.0f));
                g.setColour (it.second ? theme::text : theme::textMuted);
                g.drawText (it.first, cell.withTrimmedLeft (4), juce::Justification::centredLeft);
            }
        }
        // status message
        {
            g.setFont (theme::fontMedium (12.5f));
            const auto st = f != nullptr ? f->getState() : F::State::idle;
            g.setColour (st == F::State::error ? theme::bad : st == F::State::stopped ? theme::warn
                       : st == F::State::done ? theme::good : st == F::State::running ? theme::accent : theme::textDim);
            g.drawText (ellipsize (statusMsg.isNotEmpty() ? statusMsg : "Idle. Nothing is written until you start.",
                                   (float) statusArea.getWidth(), theme::fontMedium (12.5f)), statusArea, juce::Justification::centredLeft);
        }

        paintSteps (g);
        paintThumb (g);
        paintCost (g);
        paintMap (g);
    }

    void refreshAll()
    {
        const bool run = isRunning();
        const bool has = f != nullptr && f->hasKey();
        const bool attached = procName.isNotEmpty();

        providerBox.setEnabled (! run);  ollamaUrlEd.setEnabled (! run);  ollamaModelEd.setEnabled (! run);  testOllamaBtn.setEnabled (! run);
        keyEditor.setEnabled (! run);  saveBtn.setEnabled (! run);  testBtn.setEnabled (has && ! run);  removeBtn.setEnabled (has && ! run);
        modelBox.setEnabled (! run);   budget.setEnabled (! run);
        consent.setEnabled (attached && ! run);
        const auto t = ellipsize (winTitle.isNotEmpty() ? winTitle : procName, 200.0f, theme::font (13.0f));
        consent.setButtonText (attached ? "Allow AI to control " + t : juce::String ("Allow AI to control the DSP window"));
        runBtn.setEnabled (! run);
        runBtn.setButtonText (map.exists ? "Apply tune with AI" : "Learn software and apply tune");
        stopBtn.setEnabled (run);
        relearnBtn.setEnabled (! run && map.exists);
        deleteBtn.setEnabled (! run && map.exists);

        const auto sw = f != nullptr ? f->softwareInfo() : F::SoftwareInfo();
        const bool hasList = f != nullptr && f->hasVendorList();
        const bool showGet = f != nullptr && ! attached && (hasList || (sw.vendor.isNotEmpty() && ! sw.installed));
        getSwBtn.setButtonText (hasList ? juce::String ("Get DSP software...") : "Get " + sw.vendor + " software");
        getSwBtn.setVisible (showGet);

        if (run && ! isTimerRunning()) startTimer (50);
        if (! run) stopTimer();

        owner.resized();
        repaint();
    }

    int bodyH() { return layoutFor (width); }

    AiPanel& owner;
    F* f = nullptr;
    int width = 400;

private:
    void commitOllama()
    {
        if (f != nullptr) f->setOllama (ollamaUrlEd.getText(), ollamaModelEd.getText());
    }

    void startRun (bool relearn)
    {
        if (f == nullptr) return;
        juce::String err;
        if (! f->start (relearn, err) && owner.toast)
            owner.toast (err.isNotEmpty() ? err : "Could not start the AI operator.", AppShell::ToastKind::warn);
        refreshAll();
    }

    void saveKey()
    {
        if (f == nullptr) return;
        juce::String err;
        if (f->saveKey (keyEditor.getText().trim(), err))
        {
            keyEditor.clear();
            keyState = {};
            if (owner.toast) owner.toast ("API key saved (encrypted on this PC).", AppShell::ToastKind::good);
        }
        else
        {
            keyState = err.isNotEmpty() ? err : "Could not save the key.";
            keyStateColour = theme::bad;
        }
        refreshAll();
    }

    void refreshMap() { if (f != nullptr) map = f->getMap(); refreshAll(); }

    float explainHeight (int w) const
    {
        juce::TextLayout tl;
        tl.createLayout (explainText(), (float) w);
        return std::ceil (tl.getHeight()) + 2.0f;
    }
    juce::AttributedString explainText() const
    {
        juce::AttributedString a;
        a.setJustification (juce::Justification::topLeft);
        a.append (juce::String (isOllama() ? "The local model sees screenshots of this one window (they stay on this PC) and may click and type only inside it. "
                                           : "Claude sees screenshots of this one window (sent to Anthropic with your key) and may click and type only inside it. ") +
                  "Gain, delay and crossover limits are enforced in code, and every write is read back and verified. STOP halts it instantly.",
                  theme::font (12.0f), theme::textDim);
        return a;
    }
    void paintExplain (juce::Graphics& g)
    {
        explainText().draw (g, explainArea.toFloat());
    }

    void paintSteps (juce::Graphics& g)
    {
        static const char* names[F::numSteps] = { "Learning layout", "Mapping outputs", "Verifying with test tone",
                                                  "Applying tune", "Read-back check" };
        const float pulse = 0.55f + 0.45f * std::sin (juce::Time::getMillisecondCounter() * 0.008f);
        const int rowH = stepsArea.getHeight() / F::numSteps;
        for (int i = 0; i < F::numSteps; ++i)
        {
            auto r = juce::Rectangle<int> (stepsArea.getX(), stepsArea.getY() + i * rowH, stepsArea.getWidth(), rowH);
            auto dot = r.removeFromLeft (22).toFloat().withSizeKeepingCentre (18, 18);
            const auto st = steps[(size_t) i];
            if (st == F::StepState::done)
            {
                g.setColour (theme::good.withAlpha (0.18f)); g.fillEllipse (dot);
                icons::draw (g, icons::Id::check, dot.reduced (3), theme::good);
            }
            else if (st == F::StepState::failed)
            {
                g.setColour (theme::bad.withAlpha (0.18f)); g.fillEllipse (dot);
                icons::draw (g, icons::Id::cross, dot.reduced (4), theme::bad);
            }
            else if (st == F::StepState::active)
            {
                g.setColour (theme::accent.withAlpha (0.2f * pulse + 0.1f)); g.fillEllipse (dot.expanded (2));
                g.setColour (theme::accent); g.drawEllipse (dot, 1.6f);
                g.fillEllipse (dot.reduced (6.0f));
            }
            else
            {
                g.setColour (theme::borderHi); g.drawEllipse (dot.reduced (1), 1.2f);
            }
            g.setFont (st == F::StepState::active ? theme::fontMedium (12.5f) : theme::font (12.5f));
            g.setColour (st == F::StepState::pending ? theme::textMuted : st == F::StepState::active ? theme::text : theme::textDim);
            g.drawText (names[i], r.withTrimmedLeft (8), juce::Justification::centredLeft);
        }
    }

    void paintThumb (juce::Graphics& g)
    {
        auto r = thumbArea.toFloat();
        g.setColour (theme::bg1);
        g.fillRoundedRectangle (r, theme::radiusSm);
        if (thumb.isValid())
        {
            g.saveState();
            juce::Path clip; clip.addRoundedRectangle (r, theme::radiusSm);
            g.reduceClipRegion (clip);
            g.drawImage (thumb, r.reduced (1), juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize);
            g.restoreState();
        }
        else
        {
            icons::draw (g, icons::Id::eye, r.withSizeKeepingCentre (22, 22).translated (0, -8), theme::textMuted);
            g.setColour (theme::textMuted);
            g.setFont (theme::font (11.5f));
            g.drawText ("What the AI sees", thumbArea.withTrimmedTop (thumbArea.getHeight() / 2 + 6), juce::Justification::centredTop);
        }
        g.setColour (theme::border);
        g.drawRoundedRectangle (r.reduced (0.5f), theme::radiusSm, 1.0f);
        if (thumb.isValid())
        {
            const auto tag = juce::Rectangle<float> (r.getX() + 6, r.getY() + 6, 68, 16);
            theme::fillPill (g, tag, theme::bg0.withAlpha (0.8f));
            g.setColour (theme::textDim); g.setFont (theme::fontMedium (10.0f));
            g.drawText ("AI VIEW", tag, juce::Justification::centred);
        }
    }

    void paintCost (juce::Graphics& g)
    {
        auto r = costArea;
        auto left = r.removeFromLeft (110);
        g.setColour (theme::text);
        g.setFont (theme::display (22.0f));
        g.drawText ("$" + juce::String (isOllama() ? 0.0 : cost.usd, 2), left.removeFromTop (28), juce::Justification::centredLeft);
        g.setColour (theme::textMuted);
        g.setFont (theme::font (11.0f));
        g.drawText (isOllama() ? "local model, free" : "spent this session", left, juce::Justification::centredLeft);

        r.removeFromLeft (8);
        const double cap = std::max (0.01, cost.budgetUsd > 0 ? cost.budgetUsd : (f != nullptr ? f->getBudgetUsd() : 1.0));
        const float frac = (float) juce::jlimit (0.0, 1.0, cost.usd / cap);
        auto bar = r.removeFromTop (8).toFloat().withTrimmedTop (2);
        g.setColour (theme::bg4); g.fillRoundedRectangle (bar, 3.0f);
        g.setColour (frac < 0.7f ? theme::accent : frac < 0.9f ? theme::warn : theme::bad);
        g.fillRoundedRectangle (bar.withWidth (std::max (6.0f, bar.getWidth() * frac)), 3.0f);
        r.removeFromTop (4);
        g.setColour (theme::textDim);
        g.setFont (theme::font (11.5f));
        auto k = [] (int n) { return n >= 1000 ? juce::String (n / 1000.0, 1) + "k" : juce::String (n); };
        g.drawText ((isOllama() ? juce::String ("no cost  ") : "of $" + juce::String (cap, 2) + " cap  ") + juce::String (juce::CharPointer_UTF8 ("·")) + "  " + k (cost.tokensIn)
                        + " in / " + k (cost.tokensOut) + " out  " + juce::String (juce::CharPointer_UTF8 ("·")) + "  " + juce::String (cost.calls) + " calls",
                    r, juce::Justification::centredLeft);
    }

    void paintMap (juce::Graphics& g)
    {
        auto r = mapArea;
        if (! map.exists)
        {
            g.setColour (theme::textMuted);
            g.setFont (theme::font (12.0f));
            g.drawFittedText ("No map yet. The first run learns this software's layout with a few AI calls; later runs reuse the saved map and cost nothing.",
                              r, juce::Justification::topLeft, 3);
            return;
        }
        g.setColour (theme::textDim);
        g.setFont (theme::font (12.0f));
        g.drawText (ellipsize (map.software, (float) r.getWidth() - 110, theme::font (12.0f)) + "", r.removeFromTop (20), juce::Justification::centredLeft);
        // date right-aligned on the same line
        g.setColour (theme::textMuted);
        g.drawText ("learned " + map.learnedOn, mapArea.withHeight (20), juce::Justification::centredRight);
        r.removeFromTop (4);

        for (size_t i = 0; i < map.outputs.size(); ++i)
        {
            const auto& o = map.outputs[i];
            auto row = r.removeFromTop (26);
            if (i % 2 == 0) { g.setColour (theme::bg1); g.fillRoundedRectangle (row.toFloat(), 4.0f); }
            auto cell = row.reduced (8, 0);
            g.setFont (theme::mono (11.5f));
            g.setColour (theme::textDim);
            g.drawText (o.output, cell.removeFromLeft (48), juce::Justification::centredLeft);
            const bool unassigned = o.driver.startsWith ("(");
            const int total = o.found.size() + o.missing.size();
            auto right = cell.removeFromRight (std::min (150, cell.getWidth() / 2 + 10));
            g.setFont (theme::fontMedium (12.0f));
            g.setColour (unassigned ? theme::textMuted : theme::text);
            g.drawText (ellipsize (o.driver, (float) cell.getWidth() - 4, theme::fontMedium (12.0f)), cell, juce::Justification::centredLeft);

            const bool all = o.missing.isEmpty();
            icons::draw (g, all ? icons::Id::check : icons::Id::cross, right.removeFromLeft (14).toFloat().reduced (1),
                         all ? theme::good : theme::warn);
            g.setFont (theme::font (11.5f));
            g.setColour (all ? theme::good : theme::warn);
            g.drawText (all ? juce::String (o.found.size()) + "/" + juce::String (total) + " controls"
                            : "no " + o.missing.joinIntoString (", "),
                        right.withTrimmedLeft (4), juce::Justification::centredLeft);
        }
    }

    void timerCallback() override { repaint (stepsArea); }

    juce::TextEditor keyEditor;
    FaButton saveBtn { "Save", FaButton::Style::primary }, testBtn { "Test", FaButton::Style::secondary },
             removeBtn { "Remove", FaButton::Style::ghost, icons::Id::trash };
    juce::HyperlinkButton keyLink;
    juce::ComboBox providerBox;
    juce::TextEditor ollamaUrlEd, ollamaModelEd;
    FaButton testOllamaBtn { "Test Ollama", FaButton::Style::secondary, icons::Id::refresh };
    juce::Rectangle<int> providerLabel, ollamaLabel, ollamaStatusArea, ollamaNoteArea;
    juce::String ollamaState;
    juce::Colour ollamaStateColour = theme::textDim;
    bool isOllama() const { return f != nullptr && f->getProvider() == "ollama"; }
    juce::ComboBox modelBox;
    ValueField budget { "Cap $", "", 0.25, 50.0, 0.25, 2 };
    ToggleSwitch consent { "Allow AI to control the DSP window" };
    FaButton getSwBtn { "Get software", FaButton::Style::secondary, icons::Id::download };
    FaButton runBtn { "Learn software and apply tune", FaButton::Style::primary, icons::Id::bolt };
    FaButton stopBtn { "STOP", FaButton::Style::danger, icons::Id::stop };
    FaButton relearnBtn { "Re-learn", FaButton::Style::secondary, icons::Id::refresh };
    FaButton deleteBtn { "Delete map", FaButton::Style::ghost, icons::Id::trash };
    LogView log;

    juce::Rectangle<int> keyLabel, keyStatusArea, modelNoteArea, consentLabel, targetArea, explainArea, readyArea,
                         statusArea, activityLabel, stepsArea, thumbArea, costArea, mapLabel, mapArea;
    std::array<F::StepState, F::numSteps> steps = [] { std::array<F::StepState, F::numSteps> a; a.fill (F::StepState::pending); return a; }();
    F::Cost cost;
    F::MapSummary map;
    juce::Image thumb;
    juce::String keyState, statusMsg, procName, winTitle;
    juce::Colour keyStateColour = theme::textDim;
};

//==============================================================================
AiPanel::AiPanel()
{
    body = std::make_unique<Body> (*this);
    viewport.setViewedComponent (body.get(), false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);
}

AiPanel::~AiPanel()
{
    if (facade != nullptr)
    {
        facade->stop();
        facade->onStep = nullptr; facade->onLog = nullptr; facade->onCost = nullptr; facade->onState = nullptr;
        facade->onThumbnail = nullptr; facade->onMapChanged = nullptr;
    }
}

void AiPanel::setFacade (AiOperatorFacade* f)
{
    if (facade != nullptr)
    {
        facade->stop();
        facade->onStep = nullptr; facade->onLog = nullptr; facade->onCost = nullptr; facade->onState = nullptr;
        facade->onThumbnail = nullptr; facade->onMapChanged = nullptr;
    }
    facade = f;
    body->bind (f);
}

void AiPanel::setTarget (const juce::String& p, const juce::String& t) { body->setTarget (p, t); }
void AiPanel::stopNow()   { if (facade != nullptr) facade->stop(); body->refreshAll(); }
bool AiPanel::isRunning() const { return body->isRunning(); }
void AiPanel::visibilityChanged() {}

void AiPanel::resized()
{
    viewport.setBounds (getLocalBounds());
    const int w = std::max (200, viewport.getMaximumVisibleWidth() - 2);
    body->width = w;
    const int h = body->bodyH();
    if (body->getWidth() != w || body->getHeight() != h)
        body->setSize (w, h);
}
} // namespace fa::ui::dsp
