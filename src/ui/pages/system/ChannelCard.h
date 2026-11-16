#pragma once

#include "PageKit.h"

namespace fa::ui
{
/** One output channel on the System page: label, role chip, DSP output, driver size, Fs, model.
    Edits are pushed through onEdit (the page forwards to Session::updateChannel). */
class ChannelCard : public juce::Component
{
public:
    static constexpr int height = 196;

    ChannelCard (const OutputChannel& ch,
                 std::function<void (const OutputChannel&)> onEditFn,
                 std::function<void (const juce::String&)> onSelectFn)
        : channel (ch), onEdit (std::move (onEditFn)), onSelect (std::move (onSelectFn)),
          roleChip (toString (ch.role), theme::roleColour (ch.role)),
          outField ("DSP out", "", 1, 32, 1, 0),
          sizeField ("Size", "in", 0, 24, 0.5, 1),
          fsField ("Fs", "Hz", 0, 5000, 1, 0),
          xoverField ("Xover", "Hz", 0, 8000, 50, 0)
    {
        addAndMakeVisible (kindBox);
        addAndMakeVisible (xoverField);
        kit::styleCombo (kindBox);
        kindBox.addItem ("Active driver (own output)", 1);
        kindBox.addItem ("Passive set (woofer + tweeter, 1 output)", 2);
        kindBox.addItem ("Full range driver", 3);
        kindBox.addItem ("Subwoofer", 4);
        kindBox.setTooltip ("How this output is wired: one driver per output, a component set or coax behind a passive "
                            "crossover, a single full-range driver, or a sub.");
        kindBox.onChange = [this]
        {
            if (updatingUi) return;
            const auto k = (ChannelKind) (kindBox.getSelectedId() - 1);
            if (k == channel.kind) return;
            if (k == ChannelKind::activeDriver && (channel.role == DriverRole::fullrange || channel.role == DriverRole::subwoofer))
                channel.role = DriverRole::midbass;
            channel.kind = k;
            channel.normaliseKind();
            push();
        };
        xoverField.onChange = [this] (double v) { channel.passive.crossoverHz = v; push(); };
        xoverField.setTooltip ("Passive crossover point of the set (0 = unknown). The DSP cannot change it; it is only used "
                               "to look for the crossover notch.");

        addAndMakeVisible (roleChip);
        addAndMakeVisible (outField);
        addAndMakeVisible (sizeField);
        addAndMakeVisible (fsField);
        addAndMakeVisible (modelEditor);
        kit::styleEditor (modelEditor, "Driver model (optional)");

        outField.onChange  = [this] (double v) { channel.dspOutput = (int) v - 1; push(); };
        sizeField.onChange = [this] (double v) { channel.driver.sizeInches = v; push(); };
        fsField.onChange   = [this] (double v) { channel.driver.fsHz = v; push(); };
        modelEditor.onFocusLost = [this] { commitModel(); };
        modelEditor.onReturnKey = [this] { commitModel(); };
        modelEditor.onTextChange = [this] { select(); };
        update (ch);
    }

    void update (const OutputChannel& ch)
    {
        channel = ch;
        roleChip.set (toString (ch.role), theme::roleColour (ch.role));
        outField.setValue (ch.dspOutput + 1);
        sizeField.setValue (ch.driver.sizeInches);
        fsField.setValue (ch.driver.fsHz);
        {
            const juce::ScopedValueSetter<bool> svs (updatingUi, true);
            kindBox.setSelectedId ((int) ch.kind + 1, juce::dontSendNotification);
        }
        xoverField.setValue (ch.passive.crossoverHz);
        const bool passiveSet = ch.isPassiveSet();
        if (xoverField.isVisible() != passiveSet)
        {
            xoverField.setVisible (passiveSet);
            resized();
        }
        if (! modelEditor.hasKeyboardFocus (true))
            modelEditor.setText (ch.driver.model, juce::dontSendNotification);
        repaint();
    }

    void setSelected (bool b)
    {
        if (selected != b) { selected = b; repaint(); }
    }

    const juce::String& getChannelId() const noexcept { return channel.id; }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        const auto role = theme::roleColour (channel.role);

        if (selected)
            theme::drawGlow (g, r, role.withAlpha (0.35f), theme::radius, 10.0f);

        theme::fillCard (g, r, hover || selected ? theme::bg3 : theme::bg2,
                         selected ? role.withAlpha (0.9f) : (hover ? theme::borderHi : theme::border));

        // role stripe
        g.setColour (role.withAlpha (selected ? 1.0f : 0.7f));
        g.fillRoundedRectangle (r.getX() + 1.0f, r.getY() + 14.0f, 3.0f, r.getHeight() - 28.0f, 1.5f);

        auto top = getLocalBounds().reduced (16, 12).removeFromTop (22);
        g.setColour (theme::text);
        g.setFont (theme::fontMedium (14.0f));
        g.drawText (channel.label, top.removeFromLeft (top.getWidth() - roleChip.getWidth() - 8),
                    juce::Justification::centredLeft, true);

        auto side = getLocalBounds().reduced (16, 12);
        g.setColour (theme::textMuted);
        g.setFont (theme::mono (10.5f));
        g.drawText (channel.id, side.removeFromTop (34).removeFromBottom (12), juce::Justification::centredLeft, false);
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (16, 12);
        auto top = b.removeFromTop (22);
        roleChip.setBounds (top.removeFromRight (roleChip.getIdealWidth()).withSizeKeepingCentre (roleChip.getIdealWidth(), 20));

        b.removeFromTop (14);
        kindBox.setBounds (b.removeFromTop (kit::btnH - 4));
        b.removeFromTop (kit::gap);
        auto row = b.removeFromTop (kit::fieldH);
        const int cols = channel.isPassiveSet() ? 4 : 3;
        const int fw = (row.getWidth() - (cols - 1) * kit::gap) / cols;
        outField.setBounds (row.removeFromLeft (fw));  row.removeFromLeft (kit::gap);
        sizeField.setBounds (row.removeFromLeft (fw)); row.removeFromLeft (kit::gap);
        if (cols == 4)
        {
            fsField.setBounds (row.removeFromLeft (fw)); row.removeFromLeft (kit::gap);
            xoverField.setBounds (row);
        }
        else
            fsField.setBounds (row);
        b.removeFromTop (kit::gap);
        modelEditor.setBounds (b.removeFromTop (kit::btnH - 2));
    }

    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit  (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseDown  (const juce::MouseEvent&) override { select(); }

private:
    void select()
    {
        if (onSelect)
            onSelect (channel.id);
    }

    void push()
    {
        select();
        if (onEdit)
            onEdit (channel);
    }

    void commitModel()
    {
        if (modelEditor.getText() != channel.driver.model)
        {
            channel.driver.model = modelEditor.getText();
            push();
        }
    }

    OutputChannel channel;
    std::function<void (const OutputChannel&)> onEdit;
    std::function<void (const juce::String&)> onSelect;
    Chip roleChip;
    ValueField outField, sizeField, fsField, xoverField;
    juce::ComboBox kindBox;
    bool updatingUi = false;
    juce::TextEditor modelEditor;
    bool selected = false, hover = false;
};
} // namespace fa::ui
