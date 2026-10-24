#pragma once

#include "../system/PageKit.h"

namespace fa::ui
{
/** Calibration status for one measurement microphone: name / serial / sensitivity, a tiny plot of
    the correction curve and a Load button. */
class MicCalPanel : public juce::Component
{
public:
    static constexpr int height = 216;

    explicit MicCalPanel (int micIndex) : index (micIndex)
    {
        addAndMakeVisible (loadBtn);
        addAndMakeVisible (plot);
        loadBtn.onClick = [this] { if (onLoad) onLoad (index); };
        plot.setRanges (20.0, 20000.0, -12.0, 12.0);
        plot.setShowLegend (false);
        plot.setInterceptsMouseClicks (false, false);
    }

    void set (const MicCalibration& c, const juce::String& inputLabel)
    {
        cal = c;
        input = inputLabel;
        plot.clearTraces();
        if (cal.isValid())
        {
            auto t = PlotTrace::fromDb ("cal", "Correction", theme::accent, cal.freqs, cal.gainDb);
            t.thickness = 1.6f;
            t.fillToBottom = true;
            plot.setTrace (t);
        }
        loadBtn.setButtonText (cal.isValid() ? "Replace file" : "Load file");
        repaint();
    }

    std::function<void (int micIndex)> onLoad;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        theme::fillCard (g, r, theme::bg1.withAlpha (0.6f), cal.isValid() ? theme::accent.withAlpha (0.35f) : theme::border, theme::radiusSm);

        auto b = getLocalBounds().reduced (14, 12);
        auto head = b.removeFromTop (22);
        head.removeFromRight (loadBtn.getWidth() + 8);

        g.setColour (theme::text);
        g.setFont (theme::fontMedium (13.5f));
        g.drawText ("Mic " + juce::String (index + 1), head.removeFromLeft (46), juce::Justification::centredLeft, false);
        g.setColour (theme::textMuted);
        g.setFont (theme::font (12.0f));
        g.drawText (input, head, juce::Justification::centredLeft, true);

        b.removeFromTop (8);
        auto info = b.removeFromTop (38);
        if (cal.isValid())
        {
            g.setColour (theme::textDim);
            g.setFont (theme::font (12.0f));
            juce::String line1 = cal.name.isNotEmpty() ? cal.name : "Calibration loaded";
            juce::String line2;
            if (cal.serial.isNotEmpty())
                line2 << "S/N " << cal.serial << "    ";
            if (std::abs (cal.sensitivityDb) > 1e-6)
                line2 << "Sens " << theme::formatDb (cal.sensitivityDb, 2) << "    ";
            line2 << juce::String ((int) cal.freqs.size()) << " points" << (cal.hasPhase ? kit::sep() + "with phase" : juce::String());
            g.setColour (theme::text.withAlpha (0.9f));
            g.drawText (line1, info.removeFromTop (18), juce::Justification::centredLeft, true);
            g.setColour (theme::textDim);
            g.setFont (theme::mono (11.0f));
            g.drawText (line2, info, juce::Justification::centredLeft, true);
        }
        else
        {
            g.setColour (theme::warn.withAlpha (0.9f));
            g.setFont (theme::font (12.0f));
            g.drawFittedText ("No calibration - response assumed flat, SPL relative only. Fine for tuning; load the file for absolute levels.",
                              info, juce::Justification::topLeft, 2);
        }
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (14, 12);
        loadBtn.setBounds (b.removeFromTop (22).removeFromRight (108).withSizeKeepingCentre (108, 26));
        b.removeFromTop (8 + 38 + 8);
        plot.setBounds (b);
    }

private:
    int index;
    MicCalibration cal;
    juce::String input;
    FaButton loadBtn { "Load file", FaButton::Style::secondary, icons::Id::folder };
    FrequencyPlot plot;
};
} // namespace fa::ui
