#pragma once

#include "../system/PageKit.h"
#include "../../components/Widgets.h"
#include "../../../core/Session.h"

namespace fa::ui
{
/** Rig > "Mic check": records 4 s from the active input (input-only, nothing is played) and shows the calibrated noise
    floor (dBA, dB SPL Z), peak, clipping, mains hum and a verdict. If the Windows capture level is not at the recommended
    value it explains why and offers "Set to recommended" - which only acts when the user clicks it. */
class MicCheckCard : public Card
{
public:
    explicit MicCheckCard (Session& s)
        : Card ("Mic check", "Input only - nothing is played"), session (s)
    {
        for (auto* w : std::initializer_list<juce::Component*> { &runBtn, &status, &results, &volumeNote, &setBtn })
            addAndMakeVisible (w);
        runBtn.setComponentID ("rig.micCheck");
        setBtn.setComponentID ("rig.micCheck.setRecommended");
        runBtn.onClick = [this] { running ? cancel() : start(); };
        setBtn.onClick = [this] { setRecommended(); };
        setBtn.setVisible (false);
        volumeNote.setTextColour (theme::warn);
    }

    ~MicCheckCard() override
    {
        cancelFlag = true;
        alive->store (false);
        if (worker.joinable())
            worker.join();
    }

    std::function<void (const juce::String&, bool good)> onMessage;
    std::function<void()> onHeightChanged;

    /** Lays the children out for `width`; returns the card height. */
    int layoutFor (int width, int btnH, int chrome)
    {
        setSize (width, 300);
        auto a = getContentArea();
        int y = a.getY();
        runBtn.setBounds (a.getX(), y, 150, btnH);
        const int sw = a.getWidth() - 150 - 12;
        status.setBounds (a.getX() + 150 + 12, y, sw, juce::jmax (btnH, status.heightFor (sw)));
        y += juce::jmax (btnH, status.getHeight()) + 8;
        if (results.getText().isNotEmpty())
        {
            results.setBounds (a.getX(), y, a.getWidth(), results.heightFor (a.getWidth()));
            y += results.getHeight() + 6;
        }
        else
            results.setBounds ({});
        const bool showVol = volumeNote.getText().isNotEmpty();
        volumeNote.setVisible (showVol);
        setBtn.setVisible (showVol && canSet);
        if (showVol)
        {
            volumeNote.setBounds (a.getX(), y, a.getWidth(), volumeNote.heightFor (a.getWidth()));
            y += volumeNote.getHeight() + 6;
            if (canSet)
            {
                setBtn.setBounds (a.getX(), y, 190, btnH);
                y += btnH;
            }
        }
        const int h = chrome + (y - a.getY());
        setSize (width, h);
        return h;
    }

private:
    void start()
    {
        if (running) return;
        if (worker.joinable())
            worker.join();
        running = true;
        cancelFlag = false;
        runBtn.setButtonText ("Cancel");
        runBtn.setStyle (FaButton::Style::danger);
        status.setTextColour (theme::textDim);
        status.setText ("Recording 4 s from the input - keep the car quiet, nothing is played...");
        results.setText ({});
        volumeNote.setText ({});
        if (auto* dev = session.getAudio().getDeviceManager().getCurrentAudioDevice())
            deviceName = dev->getName();
        else
            deviceName = {};
        relayout();

        auto aliveFlag = alive;
        juce::Component::SafePointer<MicCheckCard> self (this);
        worker = std::thread ([this, aliveFlag, self]
        {
            auto report = session.runMicSelfTest (4.0, cancelFlag);
            juce::MessageManager::callAsync ([aliveFlag, self, report]
            {
                if (aliveFlag->load() && self != nullptr)
                    self->finish (report);
            });
        });
    }

    void cancel() { cancelFlag = true; }

    void finish (const MicSelfTestReport& r)
    {
        running = false;
        runBtn.setButtonText ("Run mic check");
        runBtn.setStyle (FaButton::Style::primary);
        const bool noData = r.verdict == MicSelfTestReport::Verdict::noData;
        const bool good = r.verdict == MicSelfTestReport::Verdict::ok;
        const auto colour = good ? theme::good : (noData ? theme::textDim : theme::warn);
        status.setTextColour (colour);
        status.setText (MicSelfTestReport::verdictName (r.verdict) + " - " + r.message);

        if (noData)
            results.setText ({});
        else
        {
            juce::String t;
            t << "Noise floor  " << juce::String (r.levelADbSpl, 1) << " dBA  |  " << juce::String (r.levelZDbSpl, 1) << " dB SPL Z"
              << (r.absolute ? "" : "  (uncalibrated - load the mic file)") << "\n";
            t << "Peak  " << juce::String (r.peakDbFS, 1) << " dBFS  |  Clipping  "
              << (r.clipping ? juce::String (r.clippedSamples) + " samples" : juce::String ("none"))
              << "  |  Hum  " << (r.humDetected ? juce::String ((int) r.humFundamentalHz) + " Hz, " + r.humDetail : juce::String ("none")) << "\n";
            t << "Windows capture level  " << (r.endpointKnown ? juce::String (r.endpointDb, 1) + " dB" : juce::String ("unknown"));
            results.setText (t);
        }

        canSet = r.volumeWarning && r.endpointKnown && deviceName.isNotEmpty();
        volumeNote.setText (r.volumeWarning ? r.volumeMessage : juce::String());
        relayout();
        if (onMessage && ! noData)
            onMessage ("Mic check: " + MicSelfTestReport::verdictName (r.verdict), good);
    }

    void setRecommended()
    {
        juce::String err;
        const bool ok = setCaptureEndpointVolumeDb (deviceName, kRecommendedEndpointDb, err);
        if (onMessage)
            onMessage (ok ? "Windows capture level set to " + juce::String (kRecommendedEndpointDb, 0) + " dB - run the mic check again"
                          : "Could not change the Windows level: " + err, ok);
        if (ok)
        {
            canSet = false;
            volumeNote.setText ("Windows capture level changed. Run the mic check again to refresh the numbers.");
            relayout();
        }
    }

    void relayout()
    {
        if (onHeightChanged) onHeightChanged();
        repaint();
    }

    Session& session;
    FaButton runBtn { "Run mic check", FaButton::Style::primary, icons::Id::mic };
    kit::Note status { "Records 4 s from the selected input and reports the noise floor, peak, clipping and hum. Nothing is played." };
    kit::Note results { {}, 12.5f, theme::text };
    kit::Note volumeNote;
    FaButton setBtn { "Set to recommended", FaButton::Style::secondary };

    std::thread worker;
    std::atomic<bool> cancelFlag { false };
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    bool running = false, canSet = false;
    juce::String deviceName;
};
} // namespace fa::ui
