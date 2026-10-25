#pragma once

#include "../Page.h"
#include "../AppShell.h"
#include "../components/Widgets.h"
#include "../components/Plots.h"
#include "gear/GearKit.h"
#include "gear/InstallationStore.h"
#include "gear/SlotCard.h"

namespace fa::ui
{
/** Equipment page: the "what's in the car" builder. Source, processing, amplifiers (with channel
    assignment and power wiring), per-channel speakers (with sub wiring), measurement rig and
    electrical. Every slot opens a product picker backed by the gear database, with a first-class
    "add your own / edit specs" flow. The result is persisted in InstallationStore. */
class GearPage : public Page,
                 private juce::Timer
{
public:
    explicit GearPage (Session& s);
    ~GearPage() override;

    juce::String getTitle() const override    { return "Equipment"; }
    juce::String getSubtitle() const override { return "Head unit, DSP, amps, speakers and wiring from the database"; }
    icons::Id getIcon() const override        { return icons::Id::speaker; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void pageShown() override;
    void pageHidden() override;
    void sessionChanged() override;

private:
    class AmpCard;
    class DriverRow;

    void timerCallback() override;
    int layoutContent (int width);
    void rebuildRows();                 // config channels / amps changed -> rebuild cards
    void refreshFromStore();            // installation changed -> update every slot
    void updateSummary();
    void openPicker (const juce::String& slotTitle, std::vector<gear::Category> cats, const juce::String& currentId,
                     std::function<void (const juce::String& productId)> onPicked);
    void openEditor (const juce::String& productId);
    void editInstallation (const std::function<void (install::Installation&)>& fn);
    void applyToTuning();
    void resetInstallation();
    void toast (const juce::String& msg, AppShell::ToastKind kind = AppShell::ToastKind::info);
    void setLoadingState (bool loading);

    /** Page already derives (privately) from ChangeListener for the Session, so the store gets its own. */
    struct StoreListener : public juce::ChangeListener
    {
        explicit StoreListener (GearPage& o) : owner (o) {}
        void changeListenerCallback (juce::ChangeBroadcaster*) override { owner.refreshFromStore(); }
        GearPage& owner;
    } storeListener { *this };

    gearkit::ScrollBody body;
    std::unique_ptr<juce::Component> localSheet;    // fallback when no shell is attached
    bool dbLoading = false;
    float shimmer = 0.0f;
    juce::String highlightedChannel;

    // ---- summary strip
    Card summaryCard;
    gearkit::Note summaryText;
    ProgressBarFa completeness;
    FaButton applyBtn { "Apply driver specs to tuning", FaButton::Style::primary, icons::Id::check };
    FaButton installBtn { "Install & gains", FaButton::Style::secondary, icons::Id::arrowRight };
    FaButton resetBtn { "Reset", FaButton::Style::ghost, icons::Id::trash };

    // ---- source & processing
    gearkit::SectionHeader sourceHeader { "Step 1", "Source & processing", "The head unit feeding the system and the processor that tunes it." };
    SlotCard headUnitSlot { "Head unit", { gear::Category::headUnit } };
    SlotCard dspSlot { "DSP / processor", { gear::Category::dsp, gear::Category::dspAmp } };
    SlotCard dacSlot { "DAC (optional)", { gear::Category::dac } };

    // ---- amplifiers
    gearkit::SectionHeader ampHeader { "Step 2", "Amplifiers", "Add each amp, then tap the channels it powers. Bridged pairs count as one channel." };
    std::vector<std::unique_ptr<AmpCard>> ampCards;
    FaButton addAmpBtn { "Add amplifier", FaButton::Style::secondary, icons::Id::plus };
    gearkit::Note ampEmpty { "No amplifiers yet. If a DSP amplifier powers the speakers, add it here too so gains and wiring can be calculated." };

    // ---- speakers
    gearkit::SectionHeader speakerHeader { "Step 3", "Speakers", "One driver per DSP channel from your layout. Click a speaker on the car to choose it." };
    Card carCard { "Your layout", "From the System page" };
    CarView car;
    std::vector<std::unique_ptr<DriverRow>> driverRows;

    // ---- measurement rig
    gearkit::SectionHeader rigHeader { "Step 4", "Measurement rig", "The interface and microphones used to measure. Two mics enable the dual-seat workflow." };
    SlotCard interfaceSlot { "Audio interface", { gear::Category::audioInterface } };
    SlotCard mic1Slot { "Microphone 1", { gear::Category::measurementMic } };
    SlotCard mic2Slot { "Microphone 2 (optional)", { gear::Category::measurementMic } };

    // ---- electrical
    gearkit::SectionHeader elecHeader { "Step 5", "Electrical", "Used for current draw, wire gauge and fuse recommendations." };
    SlotCard alternatorSlot { "Alternator (optional)", { gear::Category::alternator } };
    SlotCard batterySlot { "Battery (optional)", { gear::Category::battery } };
    Card elecCard { "Supply", "" };
    ValueField altVoltage { "Voltage, engine on", "V", 11.0, 16.0, 0.1, 1 };
    ValueField altAmps { "Alternator", "A", 0, 600, 5, 0 };
    ValueField battAh { "Battery", "Ah", 0, 400, 5, 0 };
    ValueField headroom { "Amp / driver headroom", "x", 1.0, 1.25, 0.05, 2 };
    gearkit::Note elecHint { "Gains are always set so a driver never receives more than its RMS rating; the headroom ratio only decides when the plan warns that an amp is oversized." };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GearPage)
};
} // namespace fa::ui
