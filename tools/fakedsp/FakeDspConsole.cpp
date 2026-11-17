// FakeDspConsole - DEV/TEST ONLY. A fake "vendor DSP software" window used to exercise the AI operator
// without real hardware or audio. It never plays or opens any audio device.
//
//   FakeDspConsole.exe [--state=<file>] [--custom] [--muted]
//     --state=<file>  write the full internal state (JSON) here after every change; ONLY the test harness reads it
//     --custom        custom-drawn variant: output tabs, MUTE and the HPF/LPF frequency fields are painted
//                     controls without any UI Automation presence (exercises click_xy / type_text)
//     --muted         factory state: every output muted (default: everything unmuted, like a fresh unit)
//
// Vendor-like quirks: abbreviated labels (HPF / LPF / Freq / Q / Dly / Type), group boxes instead of long names,
// numbers typed into a text box and committed with Enter (focus loss reverts), units shown in the box ("80 Hz"),
// one editor that serves the selected output (select-output-then-edit), one editor for the selected PEQ band.

#include <juce_gui_extra/juce_gui_extra.h>
#include "FakeDspState.h"

namespace
{
using namespace juce;

struct Shared
{
    fakedsp::State state;
    File stateFile;
    bool custom = false;
    std::function<void()> refresh;

    void changed()
    {
        if (stateFile != File())
            state.save (stateFile);
    }
    fakedsp::Output& cur() { return state.out[state.selected]; }
};

String numberText (double v, int decimals, const String& unit)
{
    return String (v, decimals) + (unit.isEmpty() ? String() : " " + unit);
}

/** Text box that commits on Enter only; focus loss / Escape reverts (a typical vendor behaviour). */
class NumField : public TextEditor
{
public:
    NumField (const String& title, const String& unitIn, int decimalsIn, double lo, double hi, std::function<double()> get, std::function<void (double)> set)
        : unit (unitIn), decimals (decimalsIn), minV (lo), maxV (hi), getter (std::move (get)), setter (std::move (set))
    {
        setTitle (title);
        setMultiLine (false);
        setJustification (Justification::centredRight);
        setSelectAllWhenFocused (true);
        refreshText();
    }
    void refreshText() { setText (numberText (getter(), decimals, unit), dontSendNotification); }
    void returnPressed() override
    {
        auto t = getText().trim().replaceCharacter (',', '.');
        auto digits = t.initialSectionContainingOnly ("+-0123456789.");
        if (! digits.containsAnyOf ("0123456789"))
        {
            refreshText();
            return;
        }
        setter (jlimit (minV, maxV, digits.getDoubleValue()));
        refreshText();
    }
    void escapePressed() override { refreshText(); }
    void focusLost (FocusChangeType t) override { TextEditor::focusLost (t); refreshText(); }

    /** Expose the box to UI Automation as an Edit with the Value pattern, like a native or WPF vendor tool does
        (JUCE's default text-editor handler only offers the Text pattern, which hides the content from read_value). */
    std::unique_ptr<AccessibilityHandler> createAccessibilityHandler() override
    {
        struct Value : AccessibilityTextValueInterface
        {
            explicit Value (NumField& f) : field (f) {}
            bool isReadOnly() const override { return false; }
            String getCurrentValueAsString() const override { return field.getText(); }
            void setValueAsString (const String& s) override { field.setText (s, dontSendNotification); }   // typed, not yet committed
            NumField& field;
        };
        return std::make_unique<AccessibilityHandler> (*this, AccessibilityRole::editableText, AccessibilityActions(),
                                                       AccessibilityHandler::Interfaces { std::make_unique<Value> (*this) });
    }

private:
    String unit;
    int decimals;
    double minV, maxV;
    std::function<double()> getter;
    std::function<void (double)> setter;
};

/** Combo whose current entry can be read and set by its text through UI Automation (Value pattern), as a Win32/WPF combo allows.
    (JUCE's own combo handler opens a popup menu in a separate window, which an automation client cannot browse.) */
class TypeCombo : public ComboBox
{
public:
    std::unique_ptr<AccessibilityHandler> createAccessibilityHandler() override
    {
        struct Value : AccessibilityTextValueInterface
        {
            explicit Value (TypeCombo& c) : combo (c) {}
            bool isReadOnly() const override { return false; }
            String getCurrentValueAsString() const override { return combo.getText(); }
            void setValueAsString (const String& s) override
            {
                for (int i = 0; i < combo.getNumItems(); ++i)
                    if (combo.getItemText (i).equalsIgnoreCase (s.trim()))
                        combo.setSelectedItemIndex (i, sendNotificationSync);
            }
            TypeCombo& combo;
        };
        return std::make_unique<AccessibilityHandler> (*this, AccessibilityRole::comboBox, AccessibilityActions(),
                                                       AccessibilityHandler::Interfaces { std::make_unique<Value> (*this) });
    }
};

/** Painted numeric field without UI Automation: double-click, type, Enter. */
class CustomField : public Component
{
public:
    CustomField (const String& unitIn, double lo, double hi, std::function<double()> get, std::function<void (double)> set)
        : unit (unitIn), minV (lo), maxV (hi), getter (std::move (get)), setter (std::move (set))
    {
        setAccessible (false);
        editor.setAccessible (false);
        editor.setJustification (Justification::centredRight);
        editor.onReturnKey = [this] { commit(); };
        editor.onEscapeKey = [this] { editor.setVisible (false); repaint(); };
        editor.onFocusLost = [this] { editor.setVisible (false); repaint(); };
        addChildComponent (editor);
    }
    void paint (Graphics& g) override
    {
        g.setColour (Colour (0xff202830));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 3.0f);
        g.setColour (Colours::lightgreen);
        g.setFont (15.0f);
        g.drawText (String ((int) std::round (getter())) + " " + unit, getLocalBounds().reduced (6, 0), Justification::centredRight);
    }
    void resized() override { editor.setBounds (getLocalBounds()); }
    void mouseDoubleClick (const MouseEvent&) override
    {
        editor.setText (String ((int) std::round (getter())), dontSendNotification);
        editor.setVisible (true);
        editor.grabKeyboardFocus();
        editor.selectAll();
    }
    void refreshText() { repaint(); }

private:
    void commit()
    {
        auto digits = editor.getText().trim().replaceCharacter (',', '.').initialSectionContainingOnly ("+-0123456789.");
        if (digits.containsAnyOf ("0123456789"))
            setter (jlimit (minV, maxV, digits.getDoubleValue()));
        editor.setVisible (false);
        repaint();
    }
    TextEditor editor;
    String unit;
    double minV, maxV;
    std::function<double()> getter;
    std::function<void (double)> setter;
};

/** Painted toggle / tab without UI Automation. */
class CustomButton : public Component
{
public:
    CustomButton (const String& text, Colour on, std::function<bool()> isOn, std::function<void()> click)
        : label (text), onColour (on), state (std::move (isOn)), onClick (std::move (click))
    {
        setAccessible (false);
    }
    void paint (Graphics& g) override
    {
        g.setColour (state() ? onColour : Colour (0xff3a4048));
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f);
        g.setColour (Colours::white);
        g.setFont (15.0f);
        g.drawText (label, getLocalBounds(), Justification::centred);
    }
    void mouseDown (const MouseEvent&) override { onClick(); repaint(); }

private:
    String label;
    Colour onColour;
    std::function<bool()> state;
    std::function<void()> onClick;
};

class MainComponent : public Component
{
public:
    explicit MainComponent (Shared& sh) : shared (sh)
    {
        shared.refresh = [this] { refreshAll(); };

        addAndMakeVisible (heading);
        heading.setText ("DSP outputs", dontSendNotification);
        heading.setFont (FontOptions (18.0f, Font::bold));

        // ---- output selector (OUT 1..8) ----
        for (int i = 0; i < fakedsp::numOutputs; ++i)
        {
            if (shared.custom)
            {
                auto* c = new CustomButton ("OUT " + String (i + 1), Colour (0xff2e7d32), [this, i] { return shared.state.selected == i; },
                                            [this, i] { select (i); });
                customs.add (c);
                addAndMakeVisible (c);
            }
            else
            {
                auto* b = new TextButton ("OUT " + String (i + 1));
                b->setTitle ("OUT " + String (i + 1));
                b->setClickingTogglesState (true);
                b->setRadioGroupId (101);
                b->setColour (TextButton::buttonOnColourId, Colour (0xff2e7d32));
                b->onClick = [this, i, b] { if (b->getToggleState()) select (i); };
                tabs.add (b);
                addAndMakeVisible (b);
            }
        }

        addAndMakeVisible (editing);
        editing.setFont (FontOptions (15.0f, Font::bold));

        // ---- channel group ----
        addAndMakeVisible (chGroup);
        chGroup.setText ("Channel");
        chGroup.setTitle ("Channel");
        label (gainL, "Gain");
        addAndMakeVisible (gain);
        gain.setTitle ("Gain");
        gain.setSliderStyle (Slider::LinearHorizontal);
        gain.setTextBoxStyle (Slider::NoTextBox, true, 0, 0);
        gain.setRange (-40.0, 12.0, 0.1);
        gain.onValueChange = [this] { if (! refreshing) { shared.cur().gain = gain.getValue(); shared.changed(); updateReadouts(); } };
        addAndMakeVisible (gainReadout);
        label (delayL, "Dly");
        delay = std::make_unique<NumField> ("Dly", "ms", 2, 0.0, 20.0, [this] { return shared.cur().delayMs; },
                                            [this] (double v) { shared.cur().delayMs = v; shared.changed(); });
        addAndMakeVisible (*delay);
        addAndMakeVisible (phase);
        phase.setButtonText ("Phase 180");
        phase.setTitle ("Phase 180");
        phase.onClick = [this] { if (! refreshing) { shared.cur().inverted = phase.getToggleState(); shared.changed(); } };
        if (shared.custom)
        {
            auto* m = new CustomButton ("MUTE", Colour (0xffc62828), [this] { return shared.cur().muted; },
                                        [this] { shared.cur().muted = ! shared.cur().muted; shared.changed(); });
            customs.add (m);
            customMute = m;
            addAndMakeVisible (m);
        }
        else
        {
            addAndMakeVisible (mute);
            mute.setButtonText ("MUTE");
            mute.setTitle ("MUTE");
            mute.setClickingTogglesState (true);
            mute.setColour (TextButton::buttonOnColourId, Colour (0xffc62828));
            mute.onClick = [this] { if (! refreshing) { shared.cur().muted = mute.getToggleState(); shared.changed(); } };
        }

        // ---- HPF / LPF ----
        setupFilter (hpGroup, "HPF", hpType, hpFreq, hpFreqC, true);
        setupFilter (lpGroup, "LPF", lpType, lpFreq, lpFreqC, false);

        // ---- PEQ ----
        addAndMakeVisible (peqGroup);
        peqGroup.setText ("PEQ");
        peqGroup.setTitle ("PEQ");
        for (int b = 0; b < fakedsp::numPeqBands; ++b)
        {
            auto* t = new TextButton ("B" + String (b + 1));
            t->setTitle ("Band " + String (b + 1));
            t->setClickingTogglesState (true);
            t->setRadioGroupId (202);
            t->onClick = [this, b, t] { if (t->getToggleState()) { band = b; refreshAll(); } };
            bandButtons.add (t);
            addAndMakeVisible (t);
        }
        label (peqFL, "Freq"); label (peqGL, "Gain"); label (peqQL, "Q");
        peqFreq = std::make_unique<NumField> ("Freq", "Hz", 0, 20.0, 20000.0, [this] { return shared.cur().peq[band].freq; },
                                              [this] (double v) { shared.cur().peq[band].freq = v; shared.changed(); });
        peqGain = std::make_unique<NumField> ("Gain", "dB", 1, -15.0, 12.0, [this] { return shared.cur().peq[band].gain; },
                                              [this] (double v) { shared.cur().peq[band].gain = v; shared.changed(); });
        peqQ = std::make_unique<NumField> ("Q", "", 2, 0.4, 15.0, [this] { return shared.cur().peq[band].q; },
                                           [this] (double v) { shared.cur().peq[band].q = v; shared.changed(); });
        addAndMakeVisible (*peqFreq); addAndMakeVisible (*peqGain); addAndMakeVisible (*peqQ);

        addAndMakeVisible (store);
        store.setButtonText ("Store");
        store.setTitle ("Store");
        store.onClick = [this] { ++shared.state.storeCount; shared.changed(); status.setText ("Stored to DSP memory (" + String (shared.state.storeCount) + ")", dontSendNotification); };
        addAndMakeVisible (status);
        status.setText ("Ready - no audio device in use", dontSendNotification);

        refreshAll();
        setSize (860, 580);
    }

    void paint (Graphics& g) override { g.fillAll (Colour (0xff1b1f24)); }

    void resized() override
    {
        heading.setBounds (20, 8, 400, 26);
        const int tw = 100;
        for (int i = 0; i < fakedsp::numOutputs; ++i)
        {
            Rectangle<int> r (20 + i * (tw + 4), 40, tw, 34);
            if (shared.custom) customs[i]->setBounds (r); else tabs[i]->setBounds (r);
        }
        editing.setBounds (20, 80, 400, 22);

        chGroup.setBounds (20, 106, 820, 100);
        gainL.setBounds (36, 136, 40, 24);
        gain.setBounds (80, 136, 240, 24);
        gainReadout.setBounds (326, 136, 80, 24);
        delayL.setBounds (430, 136, 36, 24);
        delay->setBounds (470, 136, 90, 24);
        phase.setBounds (590, 136, 110, 24);
        if (shared.custom) customMute->setBounds (720, 130, 100, 34); else mute.setBounds (720, 130, 100, 34);

        hpGroup.setBounds (20, 216, 400, 100);
        lpGroup.setBounds (440, 216, 400, 100);
        layoutFilter (36, hpTypeL, hpType, hpFreqL, hpFreq, hpFreqC);
        layoutFilter (456, lpTypeL, lpType, lpFreqL, lpFreq, lpFreqC);

        peqGroup.setBounds (20, 326, 820, 150);
        for (int b = 0; b < fakedsp::numPeqBands; ++b)
            bandButtons[b]->setBounds (36 + b * 64, 354, 60, 28);
        peqFL.setBounds (36, 400, 40, 24);   peqFreq->setBounds (80, 400, 100, 24);
        peqGL.setBounds (210, 400, 40, 24);  peqGain->setBounds (254, 400, 100, 24);
        peqQL.setBounds (384, 400, 24, 24);  peqQ->setBounds (412, 400, 80, 24);

        store.setBounds (740, 490, 100, 32);
        status.setBounds (20, 540, 600, 24);
    }

private:
    Shared& shared;
    bool refreshing = false;
    int band = 0;

    Label heading, editing, gainL, delayL, gainReadout, status;
    GroupComponent chGroup, hpGroup, lpGroup, peqGroup;
    OwnedArray<TextButton> tabs, bandButtons;
    OwnedArray<CustomButton> customs;
    CustomButton* customMute = nullptr;
    Slider gain;
    std::unique_ptr<NumField> delay, hpFreq, lpFreq, peqFreq, peqGain, peqQ;
    std::unique_ptr<CustomField> hpFreqC, lpFreqC;
    ToggleButton phase;
    TextButton mute, store;
    TypeCombo hpType, lpType;
    Label hpTypeL, hpFreqL, lpTypeL, lpFreqL, peqFL, peqGL, peqQL;

    void label (Label& l, const String& t)
    {
        l.setText (t, dontSendNotification);
        addAndMakeVisible (l);
    }

    fakedsp::Filter& filterOf (bool hp) { return hp ? shared.cur().hp : shared.cur().lp; }

    void setupFilter (GroupComponent& grp, const String& name, TypeCombo& type, std::unique_ptr<NumField>& freq,
                      std::unique_ptr<CustomField>& freqC, bool hp)
    {
        addAndMakeVisible (grp);
        grp.setText (name);
        grp.setTitle (name);
        auto& typeL = hp ? hpTypeL : lpTypeL;
        auto& freqL = hp ? hpFreqL : lpFreqL;
        label (typeL, "Type");
        label (freqL, "Freq");
        addAndMakeVisible (type);
        type.setTitle ("Type");
        const auto choices = fakedsp::filterChoices();
        for (int i = 0; i < choices.size(); ++i)
            type.addItem (choices[i], i + 1);
        type.onChange = [this, &type, hp] { if (! refreshing) { filterOf (hp).choice = type.getSelectedId() - 1; shared.changed(); } };
        if (shared.custom)
        {
            freqC = std::make_unique<CustomField> ("Hz", 10.0, 24000.0, [this, hp] { return filterOf (hp).freq; },
                                                   [this, hp] (double v) { filterOf (hp).freq = v; shared.changed(); });
            addAndMakeVisible (*freqC);
        }
        else
        {
            freq = std::make_unique<NumField> ("Freq", "Hz", 0, 10.0, 24000.0, [this, hp] { return filterOf (hp).freq; },
                                               [this, hp] (double v) { filterOf (hp).freq = v; shared.changed(); });
            addAndMakeVisible (*freq);
        }
    }

    void layoutFilter (int x, Label& typeL, TypeCombo& type, Label& freqL, std::unique_ptr<NumField>& freq, std::unique_ptr<CustomField>& freqC)
    {
        typeL.setBounds (x, 246, 40, 24);
        type.setBounds (x + 44, 246, 200, 24);
        freqL.setBounds (x, 280, 40, 24);
        const Rectangle<int> r (x + 44, 280, 110, 24);
        if (shared.custom) freqC->setBounds (r); else freq->setBounds (r);
    }

    void select (int i)
    {
        shared.state.selected = i;
        shared.changed();
        refreshAll();
    }

    void updateReadouts() { gainReadout.setText (numberText (shared.cur().gain, 1, "dB"), dontSendNotification); }

    void refreshAll()
    {
        const juce::ScopedValueSetter<bool> guard (refreshing, true);
        auto& o = shared.cur();
        editing.setText ("Editing: OUT " + String (shared.state.selected + 1), dontSendNotification);
        if (! shared.custom)
            for (int i = 0; i < tabs.size(); ++i)
                tabs[i]->setToggleState (i == shared.state.selected, dontSendNotification);
        for (auto* c : customs) c->repaint();
        gain.setValue (o.gain, dontSendNotification);
        updateReadouts();
        delay->refreshText();
        phase.setToggleState (o.inverted, dontSendNotification);
        if (! shared.custom) mute.setToggleState (o.muted, dontSendNotification);
        hpType.setSelectedId (o.hp.choice + 1, dontSendNotification);
        lpType.setSelectedId (o.lp.choice + 1, dontSendNotification);
        if (shared.custom) { hpFreqC->refreshText(); lpFreqC->refreshText(); } else { hpFreq->refreshText(); lpFreq->refreshText(); }
        for (int b = 0; b < bandButtons.size(); ++b)
            bandButtons[b]->setToggleState (b == band, dontSendNotification);
        peqFreq->refreshText(); peqGain->refreshText(); peqQ->refreshText();
    }
};

class FakeDspApp : public JUCEApplication
{
public:
    const String getApplicationName() override { return "FakeDspConsole"; }
    const String getApplicationVersion() override { return "1.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise (const String& commandLine) override
    {
        const auto args = StringArray::fromTokens (commandLine, true);
        shared.custom = args.contains ("--custom");
        const bool muted = args.contains ("--muted");
        for (auto& a : args)
            if (a.startsWith ("--state="))
                shared.stateFile = File (a.fromFirstOccurrenceOf ("=", false, false).unquoted());
        for (int i = 0; i < fakedsp::numOutputs; ++i)
        {
            auto& o = shared.state.out[i];
            o.muted = muted;
            o.hp.freq = 80.0; o.lp.freq = 5000.0;
            for (int b = 0; b < fakedsp::numPeqBands; ++b)
                o.peq[b].freq = 100.0 * std::pow (2.5, b);   // factory band centres, flat
        }
        shared.changed();
        window = std::make_unique<Window> (shared);
    }
    void shutdown() override { window.reset(); }
    void systemRequestedQuit() override { quit(); }

private:
    struct Window : DocumentWindow
    {
        explicit Window (Shared& sh)
            : DocumentWindow (fakedsp::windowTitle, Colour (0xff1b1f24), DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent (sh), true);
            setResizable (false, false);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }
        void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

    Shared shared;
    std::unique_ptr<Window> window;
};
} // namespace

START_JUCE_APPLICATION (FakeDspApp)
