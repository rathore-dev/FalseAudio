#pragma once

#include "../system/PageKit.h"

namespace fa::ui
{
/** One row of the alignment table: role dot + label, measured arrival, delay (ms + cm),
    polarity, gain and mute. Edits go through onEdit (page -> Session::updateChannel). */
class AlignmentRow : public juce::Component
{
public:
    static constexpr int height = 64;

    // column widths used by the page's header row as well
    static constexpr int nameW = 164, arrivalW = 76, delayW = 104, cmW = 70, polW = 100, gainW = 100, muteW = 72;

    AlignmentRow (const OutputChannel& ch, const DspCapabilities& caps,
                  std::function<void (const OutputChannel&)> onEditFn,
                  std::function<void (const juce::String&)> onSelectFn)
        : channel (ch), onEdit (std::move (onEditFn)), onSelect (std::move (onSelectFn)),
          delayField ("Delay", "ms", 0.0, caps.maxDelayMs, caps.delayStepMs, 2),
          gainField ("Gain", "dB", caps.minChannelGainDb, caps.maxChannelGainDb, caps.gainStepDb, 1),
          polarity ("+ Normal", "- Inverted", theme::warn),
          mute ("Mute")
    {
        addAndMakeVisible (delayField);
        addAndMakeVisible (gainField);
        addAndMakeVisible (polarity);
        addAndMakeVisible (mute);

        delayField.onChange = [this] (double v) { channel.settings.delayMs = v; push(); };
        gainField.onChange  = [this] (double v) { channel.settings.gainDb = v; push(); };
        polarity.onChange   = [this] (bool on) { channel.settings.inverted = on; push(); };
        mute.onClick        = [this] { channel.settings.muted = mute.getToggleState(); push(); };
        update (ch, caps);
    }

    void update (const OutputChannel& ch, const DspCapabilities& caps)
    {
        channel = ch;
        delayField.setRange (0.0, caps.maxDelayMs, caps.delayStepMs);
        gainField.setRange (caps.minChannelGainDb, caps.maxChannelGainDb, caps.gainStepDb);
        delayField.setValue (ch.settings.delayMs);
        gainField.setValue (ch.settings.gainDb);
        polarity.setOn (ch.settings.inverted);
        mute.setToggleState (ch.settings.muted, juce::dontSendNotification);
        repaint();
    }

    void setArrival (std::optional<double> ms) { arrivalMs = ms; repaint(); }
    void setSelected (bool b) { if (selected != b) { selected = b; repaint(); } }
    const juce::String& getChannelId() const noexcept { return channel.id; }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        const auto role = theme::roleColour (channel.role);
        if (selected || hover)
        {
            g.setColour (selected ? role.withAlpha (0.08f) : theme::bg3.withAlpha (0.6f));
            g.fillRoundedRectangle (r, theme::radiusSm);
        }
        if (selected)
        {
            g.setColour (role.withAlpha (0.9f));
            g.fillRoundedRectangle (r.getX(), r.getY() + 12.0f, 3.0f, r.getHeight() - 24.0f, 1.5f);
        }
        g.setColour (theme::border);
        g.fillRect (r.getX() + 12.0f, r.getBottom() - 1.0f, r.getWidth() - 24.0f, 1.0f);

        auto b = getLocalBounds().reduced (12, 0);
        auto name = b.removeFromLeft (nameW);
        auto dot = name.removeFromLeft (18).toFloat().withSizeKeepingCentre (10.0f, 10.0f);
        g.setColour (role.withAlpha (0.25f));
        g.fillEllipse (dot.expanded (3.0f));
        g.setColour (channel.settings.muted ? role.withAlpha (0.4f) : role);
        g.fillEllipse (dot);
        name.removeFromLeft (8);
        g.setColour (channel.settings.muted ? theme::textMuted : theme::text);
        g.setFont (theme::fontMedium (13.5f));
        g.drawText (channel.label, name.removeFromTop (height / 2).withTrimmedTop (12), juce::Justification::centredLeft, true);
        g.setColour (theme::textMuted);
        g.setFont (theme::mono (10.5f));
        g.drawText ("OUT " + juce::String (channel.dspOutput + 1) + kit::sep() + toString (channel.role).toUpperCase(),
                    name.withTrimmedBottom (12), juce::Justification::centredLeft, true);

        auto arrival = b.removeFromLeft (arrivalW);
        g.setFont (theme::mono (12.5f));
        g.setColour (arrivalMs ? theme::textDim : theme::textMuted);
        g.drawText (arrivalMs ? theme::formatMs (*arrivalMs, 2) : juce::String ("-"), arrival, juce::Justification::centred, false);

        b.removeFromLeft (delayW + kit::gap);
        auto cm = b.removeFromLeft (cmW);
        g.setColour (theme::textDim);
        g.setFont (theme::mono (12.0f));
        g.drawText ("= " + juce::String (msToMetres (channel.settings.delayMs) * 100.0, 1) + " cm", cm, juce::Justification::centredLeft, false);
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (12, 0);
        b.removeFromLeft (nameW + arrivalW);
        delayField.setBounds (b.removeFromLeft (delayW).withSizeKeepingCentre (delayW, kit::fieldH));
        b.removeFromLeft (kit::gap + cmW);
        polarity.setBounds (b.removeFromLeft (polW).withSizeKeepingCentre (polW - kit::gap, 26));
        gainField.setBounds (b.removeFromLeft (gainW).withSizeKeepingCentre (gainW - kit::gap, kit::fieldH));
        mute.setBounds (b.removeFromLeft (muteW).withSizeKeepingCentre (muteW, 24));
    }

    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit  (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseDown  (const juce::MouseEvent&) override { if (onSelect) onSelect (channel.id); }

private:
    void push()
    {
        if (onSelect) onSelect (channel.id);
        if (onEdit) onEdit (channel);
    }

    OutputChannel channel;
    std::function<void (const OutputChannel&)> onEdit;
    std::function<void (const juce::String&)> onSelect;
    ValueField delayField, gainField;
    kit::PillToggle polarity;
    ToggleSwitch mute;
    std::optional<double> arrivalMs;
    bool selected = false, hover = false;
};
} // namespace fa::ui
