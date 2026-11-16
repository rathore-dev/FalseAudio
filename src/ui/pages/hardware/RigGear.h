#pragma once

#include "../gear/GearKit.h"
#include "../gear/InstallationStore.h"
#include "../gear/SlotCard.h"
#include "../gear/ProductPicker.h"
#include "../gear/ProductForm.h"
#include "../system/PageKit.h"

namespace fa::ui
{
/** "Measurement gear" block of the Rig page: the audio interface and microphone products
    (from the gear database, or your own). This is the one place they are chosen; Car >
    Equipment and Install & Gains only read them from the InstallationStore. */
class RigGearPanel : public juce::Component,
                     private juce::ChangeListener
{
public:
    RigGearPanel (Session& s, std::function<AppShell*()> shellGetter) : session (s), getShell (std::move (shellGetter))
    {
        for (auto* c : std::initializer_list<juce::Component*> { &header, &interfaceSlot, &mic1Slot, &mic2Slot })
            addAndMakeVisible (c);
        addChildComponent (analyzerCaption);
        addChildComponent (analyzerMic);
        analyzerMic.setTooltip ("The microphone the Live RTA and transfer function listen to (Expert > Live RTA)");
        analyzerMic.onChange = [this] (int i) { kit::setLiveAnalyzerMic (session, i); };

        bind (interfaceSlot, "Audio interface", [] (install::Installation& i) -> juce::String& { return i.audioInterfaceId; });
        bind (mic1Slot, "Microphone 1", [] (install::Installation& i) -> juce::String& { if (i.micIds.empty()) i.micIds.push_back ({}); return i.micIds[0]; });
        bind (mic2Slot, "Microphone 2", [] (install::Installation& i) -> juce::String& { while (i.micIds.size() < 2) i.micIds.push_back ({}); return i.micIds[1]; });
        interfaceSlot.setHint ("USB interface driving the sweeps and mic inputs");
        mic1Slot.setHint ("Measurement microphone at the listening seat");
        mic2Slot.setHint ("Second mic for the two-seat workflow");

        InstallationStore::instance().addChangeListener (this);
    }

    ~RigGearPanel() override
    {
        InstallationStore::instance().removeChangeListener (this);
    }

    /** Loads the gear database if needed and refreshes the slots (call from pageShown). */
    void activate()
    {
        gearkit::GearLoader::instance().ensureLoaded ([safe = juce::Component::SafePointer<RigGearPanel> (this)]
        {
            if (safe != nullptr)
                safe->refresh();
        });
        refresh();
    }

    void refresh()
    {
        const auto& inst = InstallationStore::instance().get();
        interfaceSlot.setProductId (inst.audioInterfaceId);
        mic1Slot.setProductId (inst.micIds.size() > 0 ? inst.micIds[0] : juce::String());
        mic2Slot.setProductId (inst.micIds.size() > 1 ? inst.micIds[1] : juce::String());
    }

    /** Number of microphones routed on the interface; with two or more, a selector for the
        Live analyzer's microphone appears. */
    void setRoutedMics (int n)
    {
        routedMics = n;
        juce::StringArray names;
        for (int i = 0; i < n; ++i)
            names.add ("Mic " + juce::String (i + 1));
        analyzerMic.setOptions (names);
        analyzerMic.setSelected (juce::jlimit (0, juce::jmax (0, n - 1), kit::liveAnalyzerMic (session)));
        analyzerMic.setVisible (n > 1);
        analyzerCaption.setVisible (n > 1);
        resized();
    }

    int heightFor (int width) const
    {
        const int rows = width >= 900 ? 1 : 3;
        return gearkit::SectionHeader::height + gearkit::gap + rows * SlotCard::height + (rows - 1) * gearkit::gap2
               + (routedMics > 1 ? gearkit::gap2 + gearkit::btnH : 0);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        header.setBounds (r.removeFromTop (gearkit::SectionHeader::height));
        r.removeFromTop (gearkit::gap);
        SlotCard* slots[] = { &interfaceSlot, &mic1Slot, &mic2Slot };
        if (getWidth() >= 900)
        {
            const int cw = (r.getWidth() - 2 * gearkit::gap2) / 3;
            int x = r.getX();
            for (auto* s : slots)
            {
                s->setBounds (x, r.getY(), cw, SlotCard::height);
                x += cw + gearkit::gap2;
            }
        }
        else
        {
            for (auto* s : slots)
            {
                s->setBounds (r.removeFromTop (SlotCard::height));
                r.removeFromTop (gearkit::gap2);
            }
        }

        if (routedMics > 1)
        {
            auto row = getLocalBounds().removeFromBottom (gearkit::btnH);
            analyzerCaption.setBounds (row.removeFromLeft (190));
            analyzerMic.setBounds (row.removeFromLeft (juce::jmin (row.getWidth(), 76 * routedMics)));
        }
    }

private:
    using Field = std::function<juce::String& (install::Installation&)>;

    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }

    void toast (const juce::String& msg, AppShell::ToastKind kind)
    {
        if (auto* shell = getShell())
            shell->toast (msg, kind);
    }

    void bind (SlotCard& slot, const juce::String& title, Field field)
    {
        slot.onChoose = [this, &slot, title, field] { openPicker (slot, title, field); };
        slot.onEdit   = [this, &slot] { openEditor (slot.getProductId()); };
        slot.onClear  = [field] { InstallationStore::instance().edit ([&] (install::Installation& i) { field (i).clear(); }); };
    }

    void openPicker (SlotCard& slot, const juce::String& title, Field field)
    {
        auto* shell = getShell();
        if (shell == nullptr)
            return;
        auto picker = std::make_unique<ProductPicker> (title, slot.getCategories(), slot.getProductId());
        picker->onPicked = [this, shell, field] (const gear::Product& p)
        {
            const auto id = p.id;
            const auto name = p.displayName();
            shell->closeSheet();
            InstallationStore::instance().edit ([&] (install::Installation& i) { field (i) = id; });
            toast (name + " selected", AppShell::ToastKind::good);
        };
        picker->onCancel = [shell] { shell->closeSheet(); };
        shell->showSheet (std::move (picker), ProductPicker::idealWidth, ProductPicker::idealHeight);
    }

    void openEditor (const juce::String& productId)
    {
        auto* shell = getShell();
        if (shell == nullptr)
            return;
        const auto* p = gearkit::GearLoader::ready() ? gear::GearDatabase::instance().find (productId) : nullptr;
        if (p == nullptr)
        {
            toast ("This product isn't in the database yet", AppShell::ToastKind::warn);
            return;
        }
        auto form = std::make_unique<ProductForm> (std::vector<gear::Category> { p->category }, *p);
        form->onSaved = [this, shell] (const gear::Product& saved)
        {
            const auto name = saved.displayName();
            shell->closeSheet();
            toast (name + " specs saved to your library", AppShell::ToastKind::good);
            InstallationStore::instance().set (InstallationStore::instance().get());   // refresh every card showing it
        };
        form->onCancel = [shell] { shell->closeSheet(); };
        shell->showSheet (std::move (form), ProductForm::idealWidth, ProductForm::idealHeight);
    }

    Session& session;
    std::function<AppShell*()> getShell;
    gearkit::Caption analyzerCaption { "Live analyzer listens to" };
    SegmentedControl analyzerMic;
    int routedMics = 0;
    gearkit::SectionHeader header { "Your rig", "Measurement gear",
                                    "The interface and microphones you measure with. Two mics enable the dual-seat workflow." };
    SlotCard interfaceSlot { "Audio interface", { gear::Category::audioInterface } };
    SlotCard mic1Slot { "Microphone 1", { gear::Category::measurementMic } };
    SlotCard mic2Slot { "Microphone 2 (optional)", { gear::Category::measurementMic } };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RigGearPanel)
};
} // namespace fa::ui
