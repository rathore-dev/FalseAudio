#include "GearPage.h"
#include "gear/ProductPicker.h"

namespace fa::ui
{
using namespace gearkit;

namespace
{
    const std::vector<gear::Category> kAmpCategories { gear::Category::ampMulti, gear::Category::ampMono, gear::Category::dspAmp };

    struct AwgOption { const char* label; double awg; };
    const AwgOption kPowerAwg[]   { { "1/0 AWG", 0 }, { "2 AWG", 2 }, { "4 AWG", 4 }, { "8 AWG", 8 }, { "10 AWG", 10 } };
    const AwgOption kSpeakerAwg[] { { "10 AWG", 10 }, { "12 AWG", 12 }, { "14 AWG", 14 }, { "16 AWG", 16 }, { "18 AWG", 18 } };
    const char* kMaterials[] { "OFC", "CCA", "Tinned OFC" };
    const char* kWiring[] { "single", "series", "parallel", "dvc-series", "dvc-parallel" };
    const char* kWiringLabels[] { "Single", "Series", "Parallel", "DVC - coils in series", "DVC - coils in parallel" };

    template <size_t N>
    void fillAwgBox (juce::ComboBox& box, const AwgOption (&opts)[N])
    {
        box.clear (juce::dontSendNotification);
        for (size_t i = 0; i < N; ++i)
            box.addItem (opts[i].label, (int) i + 1);
    }
    template <size_t N>
    void selectAwg (juce::ComboBox& box, const AwgOption (&opts)[N], double awg)
    {
        int best = 1;
        double bestDiff = 1e9;
        for (size_t i = 0; i < N; ++i)
        {
            const double d = std::abs (opts[i].awg - awg);
            if (d < bestDiff) { bestDiff = d; best = (int) i + 1; }
        }
        box.setSelectedId (best, juce::dontSendNotification);
    }
    template <size_t N>
    double awgOf (const juce::ComboBox& box, const AwgOption (&opts)[N])
    {
        const int id = box.getSelectedId();
        return id >= 1 && id <= (int) N ? opts[(size_t) id - 1].awg : opts[0].awg;
    }

    juce::String shortChannelName (const OutputChannel& ch)
    {
        juce::String s = ch.side == Side::left ? "L " : ch.side == Side::right ? "R " : "";
        return s + ch.label.replace ("Front ", "").replace ("Rear ", "").replace ("Left ", "").replace ("Right ", "");
    }

    install::Installation::DriverUse* findDriver (install::Installation& inst, const juce::String& channelId)
    {
        for (auto& d : inst.drivers)
            if (d.channelId == channelId)
                return &d;
        return nullptr;
    }
    const install::Installation::DriverUse* findDriver (const install::Installation& inst, const juce::String& channelId)
    {
        for (auto& d : inst.drivers)
            if (d.channelId == channelId)
                return &d;
        return nullptr;
    }
    install::Installation::DriverUse& ensureDriver (install::Installation& inst, const juce::String& channelId, bool isSub)
    {
        if (auto* d = findDriver (inst, channelId))
            return *d;
        install::Installation::DriverUse d;
        d.channelId = channelId;
        d.wiring = "single";
        d.speakerWireAwg = isSub ? 12.0 : 16.0;
        inst.drivers.push_back (d);
        return inst.drivers.back();
    }
}

//==============================================================================
// AmpCard
//==============================================================================
class GearPage::AmpCard : public Card
{
public:
    AmpCard (GearPage& o, int idx) : Card ("Amplifier " + juce::String (idx + 1)), owner (o), index (idx)
    {
        addAndMakeVisible (slot);
        slot.setHint ("Multi-channel, mono or DSP amplifier");
        slot.onChoose = [this]
        {
            owner.openPicker ("Amplifier " + juce::String (index + 1), kAmpCategories, slot.getProductId(),
                              [this] (const juce::String& id) { owner.editInstallation ([this, id] (install::Installation& i) { use (i).ampId = id; }); });
        };
        slot.onEdit  = [this] { owner.openEditor (slot.getProductId()); };
        slot.onClear = [this] { owner.editInstallation ([this] (install::Installation& i) { use (i).ampId.clear(); }); };

        addAndMakeVisible (chCap);
        addAndMakeVisible (bridged);
        bridged.onClick = [this] { owner.editInstallation ([this] (install::Installation& i) { use (i).bridged = bridged.getToggleState(); }); };

        addAndMakeVisible (wireCap);
        addAndMakeVisible (wireSlot);
        wireSlot.setHint ("Or set gauge and material below");
        wireSlot.onChoose = [this]
        {
            owner.openPicker ("Power wire for amplifier " + juce::String (index + 1), { gear::Category::powerWire }, wireSlot.getProductId(),
                              [this] (const juce::String& id)
                              {
                                  owner.editInstallation ([this, id] (install::Installation& i)
                                  {
                                      auto& a = use (i);
                                      a.powerWireId = id;
                                      if (auto* p = gear::GearDatabase::instance().find (id); p != nullptr && p->wire)
                                      {
                                          a.powerWireAwg = p->wire->mm2 > 0 && p->wire->awg == 0 ? install::calc::awgFromMm2 (p->wire->mm2) : p->wire->awg;
                                          if (p->wire->material.isNotEmpty()) a.powerWireMaterial = p->wire->material;
                                      }
                                  });
                              });
        };
        wireSlot.onEdit  = [this] { owner.openEditor (wireSlot.getProductId()); };
        wireSlot.onClear = [this] { owner.editInstallation ([this] (install::Installation& i) { use (i).powerWireId.clear(); }); };

        styleCombo (awgBox);
        fillAwgBox (awgBox, kPowerAwg);
        awgBox.onChange = [this] { owner.editInstallation ([this] (install::Installation& i) { use (i).powerWireAwg = awgOf (awgBox, kPowerAwg); }); };
        styleCombo (materialBox);
        int mid = 1;
        for (auto* m : kMaterials) materialBox.addItem (m, mid++);
        materialBox.onChange = [this] { owner.editInstallation ([this] (install::Installation& i) { use (i).powerWireMaterial = materialBox.getText(); }); };
        addAndMakeVisible (awgBox);
        addAndMakeVisible (materialBox);
        addAndMakeVisible (awgCap);
        addAndMakeVisible (materialCap);
        addAndMakeVisible (wireLen);
        addAndMakeVisible (groundLen);
        wireLen.onChange   = [this] (double v) { owner.editInstallation ([this, v] (install::Installation& i) { use (i).powerWireLengthM = v; }); };
        groundLen.onChange = [this] (double v) { owner.editInstallation ([this, v] (install::Installation& i) { use (i).groundWireLengthM = v; }); };

        addAndMakeVisible (removeBtn);
        removeBtn.onClick = [this]
        {
            owner.editInstallation ([this] (install::Installation& i)
            {
                if (index < (int) i.amps.size())
                    i.amps.erase (i.amps.begin() + index);
            });
        };
    }

    install::Installation::AmpUse& use (install::Installation& i)
    {
        while ((int) i.amps.size() <= index)
            i.amps.push_back ({});
        return i.amps[(size_t) index];
    }

    void update (const install::Installation& inst, const SystemConfig& cfg, bool loading, float phase)
    {
        if (index >= (int) inst.amps.size())
            return;
        const auto& a = inst.amps[(size_t) index];
        slot.setProductId (a.ampId);
        slot.setLoading (loading && a.ampId.isNotEmpty(), phase);
        wireSlot.setProductId (a.powerWireId);
        wireSlot.setLoading (loading && a.powerWireId.isNotEmpty(), phase);
        bridged.setToggleState (a.bridged, juce::dontSendNotification);
        selectAwg (awgBox, kPowerAwg, a.powerWireAwg);
        materialBox.setText (a.powerWireMaterial, juce::dontSendNotification);
        wireLen.setValue (a.powerWireLengthM);
        groundLen.setValue (a.groundWireLengthM);

        const auto* product = a.ampId.isNotEmpty() && GearLoader::ready() ? gear::GearDatabase::instance().find (a.ampId) : nullptr;
        if (product != nullptr)
            setTitle ("Amplifier " + juce::String (index + 1), product->displayName());
        else
            setTitle ("Amplifier " + juce::String (index + 1), "");

        // channel chips
        chips.clear();
        for (auto& ch : cfg.channels)
        {
            const bool mine = std::find (a.channelIds.begin(), a.channelIds.end(), ch.id) != a.channelIds.end();
            int otherAmp = -1;
            for (size_t k = 0; k < inst.amps.size(); ++k)
                if ((int) k != index && std::find (inst.amps[k].channelIds.begin(), inst.amps[k].channelIds.end(), ch.id) != inst.amps[k].channelIds.end())
                    otherAmp = (int) k;

            auto label = shortChannelName (ch);
            if (otherAmp >= 0) label << "  |  Amp " << (otherAmp + 1);
            auto chip = std::make_unique<Chip> (label, mine ? theme::roleColour (ch.role) : theme::textMuted);
            const auto id = ch.id;
            chip->onClick = [this, id]
            {
                owner.editInstallation ([this, id] (install::Installation& i)
                {
                    for (auto& other : i.amps)
                        if (&other != &use (i))
                            other.channelIds.erase (std::remove (other.channelIds.begin(), other.channelIds.end(), id), other.channelIds.end());
                    auto& mineIds = use (i).channelIds;
                    const auto it = std::find (mineIds.begin(), mineIds.end(), id);
                    if (it != mineIds.end()) mineIds.erase (it); else mineIds.push_back (id);
                });
            };
            addAndMakeVisible (*chip);
            chips.push_back (std::move (chip));
        }

        // load note
        const int ampCh = product != nullptr && product->amp ? product->amp->channels : 0;
        const int used = (int) a.channelIds.size() * (a.bridged ? 2 : 1);
        if (ampCh > 0)
        {
            loadText = juce::String (used) + " of " + juce::String (ampCh) + " amp channels used";
            loadColour = used > ampCh ? theme::bad : theme::textMuted;
            if (used > ampCh) loadText << "  -  too many for this amp";
        }
        else
            loadText = a.channelIds.empty() ? "Tap the channels this amp powers" : juce::String (a.channelIds.size()) + " channels assigned";
        if (! a.channelIds.empty() && ampCh > 0 && used <= ampCh)
            loadColour = theme::good;
        resized();      // chips were rebuilt
        repaint();
    }

    void setLoading (bool loading, float phase)
    {
        slot.setLoading (loading && slot.getProductId().isNotEmpty(), phase);
        wireSlot.setLoading (loading && wireSlot.getProductId().isNotEmpty(), phase);
    }

    int heightFor (int width)
    {
        const int inner = width - theme::pad * 2;
        int h = cardChrome + SlotCard::height + 12 + 16 + 6 + chipRowsHeight (inner) + 6 + 18 + 8 + 28 + 16
                + 16 + 6 + SlotCard::height + 10 + fieldH + theme::pad;
        return h;
    }

    void paint (juce::Graphics& g) override
    {
        Card::paint (g);
        g.setColour (loadColour);
        g.setFont (theme::font (11.5f));
        g.drawText (loadText, loadArea, juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        auto r = getContentArea();
        removeBtn.setBounds (getHeaderActionArea().removeFromRight (100));
        slot.setBounds (r.removeFromTop (SlotCard::height));
        r.removeFromTop (12);
        chCap.setBounds (r.removeFromTop (16));
        r.removeFromTop (6);
        auto chipArea = r.removeFromTop (chipRowsHeight (r.getWidth()));
        int x = chipArea.getX(), y = chipArea.getY();
        for (auto& c : chips)
        {
            const int w = c->getIdealWidth();
            if (x + w > chipArea.getRight() && x > chipArea.getX()) { x = chipArea.getX(); y += 26 + 6; }
            c->setBounds (x, y, w, 26);
            x += w + 6;
        }
        r.removeFromTop (6);
        loadArea = r.removeFromTop (18);
        r.removeFromTop (8);
        bridged.setBounds (r.removeFromTop (28).withWidth (260));
        r.removeFromTop (16);
        wireCap.setBounds (r.removeFromTop (16));
        r.removeFromTop (6);
        wireSlot.setBounds (r.removeFromTop (SlotCard::height));
        r.removeFromTop (10);
        auto fields = r.removeFromTop (fieldH);
        const int fw = (fields.getWidth() - gap * 3) / 4;
        auto a = fields.removeFromLeft (fw); fields.removeFromLeft (gap);
        awgCap.setBounds (a.removeFromTop (16)); a.removeFromTop (4); awgBox.setBounds (a.removeFromTop (btnH));
        auto m = fields.removeFromLeft (fw); fields.removeFromLeft (gap);
        materialCap.setBounds (m.removeFromTop (16)); m.removeFromTop (4); materialBox.setBounds (m.removeFromTop (btnH));
        wireLen.setBounds (fields.removeFromLeft (fw)); fields.removeFromLeft (gap);
        groundLen.setBounds (fields);
    }

private:
    int chipRowsHeight (int width) const
    {
        int x = 0, rows = chips.empty() ? 0 : 1;
        for (auto& c : chips)
        {
            const int w = c->getIdealWidth();
            if (x + w > width && x > 0) { x = 0; ++rows; }
            x += w + 6;
        }
        return rows * 26 + juce::jmax (0, rows - 1) * 6;
    }

    GearPage& owner;
    int index;
    SlotCard slot { "Amplifier", kAmpCategories };
    Caption chCap { "Powers these channels" }, wireCap { "Power wire" }, awgCap { "Gauge" }, materialCap { "Material" };
    std::vector<std::unique_ptr<Chip>> chips;
    ToggleSwitch bridged { "Bridged (two amp channels per speaker)" };
    SlotCard wireSlot { "Power wire product", { gear::Category::powerWire } };
    juce::ComboBox awgBox, materialBox;
    ValueField wireLen { "Power wire length", "m", 0.5, 15.0, 0.5, 1 };
    ValueField groundLen { "Ground wire length", "m", 0.1, 5.0, 0.1, 1 };
    FaButton removeBtn { "Remove", FaButton::Style::ghost, icons::Id::trash };
    juce::Rectangle<int> loadArea;
    juce::String loadText;
    juce::Colour loadColour = theme::textMuted;
};

//==============================================================================
// DriverRow
//==============================================================================
class GearPage::DriverRow : public juce::Component
{
public:
    DriverRow (GearPage& o, const OutputChannel& ch)
        : owner (o), channelId (ch.id), label (ch.label), role (ch.role), isSub (ch.role == DriverRole::subwoofer),
          colour (theme::sideTint (theme::roleColour (ch.role), ch.side)),
          slot (toString (ch.role), categoriesForRole (ch.role), colour)
    {
        addAndMakeVisible (slot);
        slot.setHint (isSub ? "Subwoofer driver (count and wiring below)" : "Pick the driver on this channel");
        slot.onChoose = [this] { choose(); };
        slot.onEdit   = [this] { owner.openEditor (slot.getProductId()); };
        slot.onClear  = [this] { owner.editInstallation ([this] (install::Installation& i) { ensureDriver (i, channelId, isSub).productId.clear(); }); };

        styleCombo (wiringBox);
        for (int i = 0; i < 5; ++i) wiringBox.addItem (kWiringLabels[i], i + 1);
        wiringBox.onChange = [this]
        {
            const int id = wiringBox.getSelectedId();
            owner.editInstallation ([this, id] (install::Installation& i) { ensureDriver (i, channelId, isSub).wiring = kWiring[juce::jlimit (0, 4, id - 1)]; });
        };
        count.onChange = [this] (double v) { owner.editInstallation ([this, v] (install::Installation& i) { ensureDriver (i, channelId, isSub).count = (int) v; }); };
        styleCombo (awgBox);
        fillAwgBox (awgBox, kSpeakerAwg);
        awgBox.onChange = [this] { owner.editInstallation ([this] (install::Installation& i) { ensureDriver (i, channelId, isSub).speakerWireAwg = awgOf (awgBox, kSpeakerAwg); }); };
        wireLen.onChange = [this] (double v) { owner.editInstallation ([this, v] (install::Installation& i) { ensureDriver (i, channelId, isSub).speakerWireLengthM = v; }); };

        addAndMakeVisible (awgCap);
        addAndMakeVisible (awgBox);
        addAndMakeVisible (wireLen);
        if (isSub)
        {
            addAndMakeVisible (count);
            addAndMakeVisible (wiringCap);
            addAndMakeVisible (wiringBox);
        }
    }

    void choose()
    {
        owner.openPicker (label, categoriesForRole (role), slot.getProductId(),
                          [this] (const juce::String& id) { owner.editInstallation ([this, id] (install::Installation& i) { ensureDriver (i, channelId, isSub).productId = id; }); });
    }

    void update (const install::Installation& inst, bool loading, float phase)
    {
        const auto* d = findDriver (inst, channelId);
        const juce::String pid = d != nullptr ? d->productId : juce::String();
        slot.setProductId (pid);
        slot.setLoading (loading && pid.isNotEmpty(), phase);
        count.setValue (d != nullptr ? d->count : 1);
        int w = 1;
        for (int i = 0; i < 5; ++i) if (d != nullptr && d->wiring == kWiring[i]) w = i + 1;
        wiringBox.setSelectedId (w, juce::dontSendNotification);
        selectAwg (awgBox, kSpeakerAwg, d != nullptr ? d->speakerWireAwg : (isSub ? 12.0 : 16.0));
        wireLen.setValue (d != nullptr ? d->speakerWireLengthM : 4.0);

        loadText.clear();
        if (const auto* p = pid.isNotEmpty() && GearLoader::ready() ? gear::GearDatabase::instance().find (pid) : nullptr; p != nullptr && p->driver)
        {
            const auto& ds = *p->driver;
            const double coil = ds.voiceCoils > 1 ? ds.coilOhms : (ds.nominalOhms > 0 ? ds.nominalOhms : ds.coilOhms);
            if (coil > 0)
            {
                const double load = install::calc::combineLoad (coil, juce::jmax (1, ds.voiceCoils), d != nullptr ? juce::jmax (1, d->count) : 1,
                                                                d != nullptr ? d->wiring : "single");
                if (load > 0)
                    loadText = "Amp sees " + num (load, 2) + juce::String (juce::CharPointer_UTF8 (" Ω"));
                if (ds.powerRmsW > 0 && d != nullptr)
                    loadText << "  |  " << num (ds.powerRmsW * juce::jmax (1, d->count), 0) << " W RMS total";
            }
        }
        repaint();
    }

    void setLoading (bool loading, float phase) { slot.setLoading (loading && slot.getProductId().isNotEmpty(), phase); }
    void setHighlighted (bool b) { if (highlighted != b) { highlighted = b; repaint(); } }
    const juce::String& getChannelId() const noexcept { return channelId; }

    int heightFor() const { return 16 + 22 + 8 + SlotCard::height + 8 + 18 + 8 + fieldH + (isSub ? fieldH + gap : 0) + 16; }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        theme::fillCard (g, r, theme::bg2, highlighted ? colour.withAlpha (0.8f) : theme::border, theme::radius);
        if (highlighted)
            theme::drawGlow (g, r, colour, theme::radius, 8.0f);
        g.setColour (colour);
        g.fillRoundedRectangle (r.getX() + 1.0f, r.getY() + 16.0f, 3.0f, 22.0f, 1.5f);

        auto inner = getLocalBounds().reduced (theme::pad);
        auto title = inner.removeFromTop (22);
        g.setColour (theme::text);
        g.setFont (theme::fontBold (14.0f));
        const int tw = (int) std::ceil (theme::textWidth (theme::fontBold (14.0f), label)) + 2;
        g.drawText (label, title.removeFromLeft (juce::jmin (tw, title.getWidth() - 80)), juce::Justification::centredLeft, true);
        title.removeFromLeft (8);
        drawChips (g, { toString (role) }, title.withSizeKeepingCentre (title.getWidth(), 18), colour, 10.0f);
        inner.removeFromTop (8 + SlotCard::height + 8);
        g.setColour (theme::textDim);
        g.setFont (theme::font (11.5f));
        g.drawText (loadText, inner.removeFromTop (18), juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (theme::pad);
        r.removeFromTop (22 + 8);
        slot.setBounds (r.removeFromTop (SlotCard::height));
        r.removeFromTop (8 + 18 + 8);
        if (isSub)
        {
            auto row = r.removeFromTop (fieldH);
            const int w = (row.getWidth() - gap) / 2;
            count.setBounds (row.removeFromLeft (w));
            row.removeFromLeft (gap);
            wiringCap.setBounds (row.removeFromTop (16));
            row.removeFromTop (4);
            wiringBox.setBounds (row.removeFromTop (btnH));
            r.removeFromTop (gap);
        }
        auto row = r.removeFromTop (fieldH);
        const int w = (row.getWidth() - gap) / 2;
        auto a = row.removeFromLeft (w);
        awgCap.setBounds (a.removeFromTop (16));
        a.removeFromTop (4);
        awgBox.setBounds (a.removeFromTop (btnH));
        row.removeFromLeft (gap);
        wireLen.setBounds (row);
    }

private:
    GearPage& owner;
    juce::String channelId, label;
    DriverRole role;
    bool isSub, highlighted = false;
    juce::Colour colour;
    SlotCard slot;
    ValueField count { "Drivers on this channel", "", 1, 4, 1, 0 };
    Caption wiringCap { "Wiring" }, awgCap { "Speaker wire gauge" };
    juce::ComboBox wiringBox, awgBox;
    ValueField wireLen { "Speaker wire length", "m", 0.5, 15.0, 0.5, 1 };
    juce::String loadText;
};

//==============================================================================
GearPage::GearPage (Session& s) : Page (s)
{
    addAndMakeVisible (body);
    body.layout = [this] (int w) { return layoutContent (w); };
    auto& c = body.content();

    // summary
    summaryCard.setTitle ("Your installation", "Everything the gain, wiring and crossover calculations are based on");
    c.addAndMakeVisible (summaryCard);
    summaryCard.addAndMakeVisible (summaryText);
    summaryCard.addAndMakeVisible (completeness);
    summaryCard.addAndMakeVisible (applyBtn);
    summaryCard.addAndMakeVisible (installBtn);
    summaryCard.addAndMakeVisible (resetBtn);
    summaryText.setTextColour (theme::text);
    applyBtn.onClick   = [this] { applyToTuning(); };
    installBtn.onClick = [this] { if (shell != nullptr) shell->showPage (PageId::install); };
    resetBtn.onClick   = [this] { resetInstallation(); };

    // sections
    for (auto* w : std::initializer_list<juce::Component*> { &sourceHeader, &headUnitSlot, &dspSlot, &dacSlot,
                                                             &ampHeader, &addAmpBtn, &ampEmpty,
                                                             &speakerHeader, &carCard,
                                                             &rigHeader, &rigCard,
                                                             &elecHeader, &alternatorSlot, &batterySlot, &elecCard })
        c.addAndMakeVisible (w);

    auto bindSlot = [this] (SlotCard& slot, const juce::String& title, std::function<juce::String& (install::Installation&)> field)
    {
        slot.onChoose = [this, &slot, title, field]
        {
            openPicker (title, slot.getCategories(), slot.getProductId(),
                        [this, field] (const juce::String& id) { editInstallation ([&] (install::Installation& i) { field (i) = id; }); });
        };
        slot.onEdit  = [this, &slot] { openEditor (slot.getProductId()); };
        slot.onClear = [this, field] { editInstallation ([&] (install::Installation& i) { field (i).clear(); }); };
    };
    bindSlot (headUnitSlot, "Head unit", [] (install::Installation& i) -> juce::String& { return i.headUnitId; });
    bindSlot (dspSlot, "DSP / processor", [] (install::Installation& i) -> juce::String& { return i.dspId; });
    bindSlot (dacSlot, "DAC", [] (install::Installation& i) -> juce::String& { return i.dacId; });
    bindSlot (alternatorSlot, "Alternator", [] (install::Installation& i) -> juce::String& { return i.alternatorId; });
    bindSlot (batterySlot, "Battery", [] (install::Installation& i) -> juce::String& { return i.batteryId; });
    alternatorSlot.setHint ("Rated output sets the current budget when no amps value is typed");
    batterySlot.setHint ("Capacity (Ah) is taken from the product when not typed below");
    for (auto* chip : { &rigInterfaceChip, &rigMic1Chip, &rigMic2Chip })
    {
        rigCard.addAndMakeVisible (chip);
        chip->setTooltip ("Chosen on the Rig page");
        chip->onClick = [this] { if (shell != nullptr) shell->showPage (PageId::rig); };
    }
    rigCard.addAndMakeVisible (rigBtn);
    rigBtn.onClick = [this] { if (shell != nullptr) shell->showPage (PageId::rig); };
    headUnitSlot.setHint ("Pre-out voltage sets the clean volume and amp gains");
    dspSlot.setHint ("Stand-alone DSP or DSP amplifier");
    dacSlot.setHint ("Only if a separate DAC sits between source and DSP");

    addAmpBtn.onClick = [this] { editInstallation ([] (install::Installation& i) { i.amps.push_back ({}); }); };

    carCard.addAndMakeVisible (car);
    car.onChannelClicked = [this] (const juce::String& id)
    {
        highlightedChannel = id;
        car.setHighlight (id);
        for (auto& row : driverRows)
        {
            row->setHighlighted (row->getChannelId() == id);
            if (row->getChannelId() == id)
            {
                auto& vp = body.getViewport();
                vp.setViewPosition (0, juce::jmax (0, row->getY() - 80));
                row->choose();
            }
        }
    };

    elecCard.addAndMakeVisible (altVoltage);
    elecCard.addAndMakeVisible (altAmps);
    elecCard.addAndMakeVisible (battAh);
    elecCard.addAndMakeVisible (headroom);
    elecCard.addAndMakeVisible (elecHint);
    altVoltage.onChange = [this] (double v) { editInstallation ([v] (install::Installation& i) { i.batteryVoltageEngineOn = v; }); };
    altAmps.onChange    = [this] (double v) { editInstallation ([v] (install::Installation& i) { i.alternatorA = v; }); };
    battAh.onChange     = [this] (double v) { editInstallation ([v] (install::Installation& i) { i.batteryAh = v; }); };
    headroom.onChange   = [this] (double v) { editInstallation ([v] (install::Installation& i) { i.ampToDriverPowerHeadroom = v; }); };

    InstallationStore::instance().addChangeListener (&storeListener);
    rebuildRows();
}

GearPage::~GearPage()
{
    InstallationStore::instance().removeChangeListener (&storeListener);
}

//==============================================================================
void GearPage::pageShown()
{
    auto& loader = GearLoader::instance();
    if (! loader.isLoaded())
        setLoadingState (true);
    loader.ensureLoaded ([safe = juce::Component::SafePointer<GearPage> (this)]
    {
        if (safe != nullptr)
        {
            safe->setLoadingState (false);
            safe->refreshFromStore();
        }
    });
    car.setConfig (session.getConfig());
    car.setSeat (session.getConfig().listeningSeat);
    refreshFromStore();
}

void GearPage::pageHidden()
{
    InstallationStore::instance().flush();
}

void GearPage::sessionChanged()
{
    setEnabled (! session.isConfigLocked());   // no edits while Auto Tune runs
    car.setConfig (session.getConfig());
    car.setSeat (session.getConfig().listeningSeat);
    rebuildRows();
}

void GearPage::setLoadingState (bool loading)
{
    dbLoading = loading;
    if (loading) startTimerHz (30); else stopTimer();
    refreshFromStore();
}

void GearPage::timerCallback()
{
    shimmer = std::fmod (shimmer + 0.02f, 1.0f);
    for (auto* s : { &headUnitSlot, &dspSlot, &dacSlot, &alternatorSlot, &batterySlot })
        s->setLoading (s->getProductId().isNotEmpty(), shimmer);
    for (auto& a : ampCards) a->setLoading (true, shimmer);
    for (auto& d : driverRows) d->setLoading (true, shimmer);
}

void GearPage::rebuildRows()
{
    const auto& inst = InstallationStore::instance().get();
    const auto& cfg = session.getConfig();
    auto& c = body.content();

    if (ampCards.size() != inst.amps.size())
    {
        ampCards.clear();
        for (size_t i = 0; i < inst.amps.size(); ++i)
        {
            auto card = std::make_unique<AmpCard> (*this, (int) i);
            c.addAndMakeVisible (*card);
            ampCards.push_back (std::move (card));
        }
    }

    bool sameChannels = driverRows.size() == cfg.channels.size();
    for (size_t i = 0; sameChannels && i < cfg.channels.size(); ++i)
        sameChannels = driverRows[i]->getChannelId() == cfg.channels[i].id;
    if (! sameChannels)
    {
        driverRows.clear();
        for (auto& ch : cfg.channels)
        {
            auto row = std::make_unique<DriverRow> (*this, ch);
            c.addAndMakeVisible (*row);
            driverRows.push_back (std::move (row));
        }
    }
    refreshFromStore();
    body.relayout();
}

void GearPage::refreshFromStore()
{
    const auto& inst = InstallationStore::instance().get();
    if (ampCards.size() != inst.amps.size())
    {
        rebuildRows();      // re-enters with matching sizes
        return;
    }
    const auto& cfg = session.getConfig();
    headUnitSlot.setProductId (inst.headUnitId);
    dspSlot.setProductId (inst.dspId);
    dacSlot.setProductId (inst.dacId);
    {
        auto chipFor = [this] (Chip& chip, const juce::String& what, const juce::String& id)
        {
            const auto* p = id.isNotEmpty() && GearLoader::ready() ? gear::GearDatabase::instance().find (id) : nullptr;
            const auto name = p != nullptr ? p->displayName() : (id.isNotEmpty() ? id : juce::String ("not chosen"));
            chip.set (what + ": " + name, id.isNotEmpty() ? theme::accent : theme::textDim);
        };
        chipFor (rigInterfaceChip, "Interface", inst.audioInterfaceId);
        chipFor (rigMic1Chip, "Mic 1", inst.micIds.size() > 0 ? inst.micIds[0] : juce::String());
        chipFor (rigMic2Chip, "Mic 2", inst.micIds.size() > 1 ? inst.micIds[1] : juce::String());
    }
    alternatorSlot.setProductId (inst.alternatorId);
    batterySlot.setProductId (inst.batteryId);
    for (auto* s : { &headUnitSlot, &dspSlot, &dacSlot, &alternatorSlot, &batterySlot })
        s->setLoading (dbLoading && s->getProductId().isNotEmpty(), shimmer);
    for (auto& a : ampCards) a->update (inst, cfg, dbLoading, shimmer);
    for (auto& d : driverRows) d->update (inst, dbLoading, shimmer);

    altVoltage.setValue (inst.batteryVoltageEngineOn);
    altAmps.setValue (inst.alternatorA);
    battAh.setValue (inst.batteryAh);
    headroom.setValue (inst.ampToDriverPowerHeadroom);
    ampEmpty.setVisible (inst.amps.empty());
    updateSummary();
    body.relayout();
}

void GearPage::updateSummary()
{
    const auto& inst = InstallationStore::instance().get();
    const auto& cfg = session.getConfig();
    int chosenDrivers = 0;
    for (auto& ch : cfg.channels)
        if (auto* d = findDriver (inst, ch.id); d != nullptr && d->productId.isNotEmpty())
            ++chosenDrivers;
    int ampsWithProduct = 0;
    for (auto& a : inst.amps) if (a.ampId.isNotEmpty()) ++ampsWithProduct;
    const bool hasMic = ! inst.micIds.empty() && inst.micIds[0].isNotEmpty();

    const int total = 4 + (int) cfg.channels.size();
    int done = (inst.headUnitId.isNotEmpty() ? 1 : 0) + (inst.dspId.isNotEmpty() ? 1 : 0) + (ampsWithProduct > 0 ? 1 : 0)
             + ((inst.audioInterfaceId.isNotEmpty() || hasMic) ? 1 : 0) + chosenDrivers;
    completeness.setProgress (total > 0 ? (double) done / total : 0.0);

    juce::StringArray parts;
    parts.add (inst.headUnitId.isNotEmpty() ? "Head unit chosen" : "No head unit");
    parts.add (inst.dspId.isNotEmpty() ? "DSP chosen" : "No DSP");
    parts.add (ampsWithProduct > 0 ? juce::String (ampsWithProduct) + (ampsWithProduct == 1 ? " amplifier" : " amplifiers") : "No amplifiers");
    parts.add (juce::String (chosenDrivers) + " of " + juce::String (cfg.channels.size()) + " speakers");
    parts.add (hasMic ? "Mic ready" : "No measurement mic");
    if (dbLoading)
        parts.add (juce::String (juce::CharPointer_UTF8 ("Loading database…")));
    else
        parts.add (juce::String (gear::GearDatabase::instance().size()) + " products in the database");
    summaryText.setText (parts.joinIntoString ("   |   "));
    applyBtn.setEnabled (chosenDrivers > 0 && ! dbLoading);
}

//==============================================================================
void GearPage::editInstallation (const std::function<void (install::Installation&)>& fn)
{
    InstallationStore::instance().edit (fn);     // broadcasts -> refreshFromStore()
}

void GearPage::openPicker (const juce::String& slotTitle, std::vector<gear::Category> cats, const juce::String& currentId,
                           std::function<void (const juce::String&)> onPicked)
{
    auto picker = std::make_unique<ProductPicker> (slotTitle, std::move (cats), currentId);
    auto* raw = picker.get();
    picker->onPicked = [this, onPicked] (const gear::Product& p)
    {
        const auto id = p.id;
        if (shell != nullptr) shell->closeSheet();
        else juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<GearPage> (this)] { if (safe != nullptr) safe->localSheet.reset(); });
        onPicked (id);
        toast (p.displayName() + " selected", AppShell::ToastKind::good);
    };
    picker->onCancel = [this]
    {
        if (shell != nullptr) shell->closeSheet();
        else juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<GearPage> (this)] { if (safe != nullptr) safe->localSheet.reset(); });
    };

    if (shell != nullptr)
    {
        shell->showSheet (std::move (picker), ProductPicker::idealWidth, ProductPicker::idealHeight);
    }
    else
    {
        localSheet = std::move (picker);
        addAndMakeVisible (*localSheet);
        resized();
        raw->toFront (true);
    }
}

void GearPage::openEditor (const juce::String& productId)
{
    const auto* p = GearLoader::ready() ? gear::GearDatabase::instance().find (productId) : nullptr;
    if (p == nullptr)
    {
        toast (dbLoading ? "The database is still loading" : "This product isn't in the database yet", AppShell::ToastKind::warn);
        return;
    }
    auto form = std::make_unique<ProductForm> (std::vector<gear::Category> { p->category }, *p);
    form->onSaved = [this] (const gear::Product& saved)
    {
        if (shell != nullptr) shell->closeSheet();
        else juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<GearPage> (this)] { if (safe != nullptr) safe->localSheet.reset(); });
        toast (saved.displayName() + " specs saved to your library", AppShell::ToastKind::good);
        // the installation itself didn't change, but every card showing this product should refresh
        InstallationStore::instance().set (InstallationStore::instance().get());
    };
    form->onCancel = [this]
    {
        if (shell != nullptr) shell->closeSheet();
        else juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<GearPage> (this)] { if (safe != nullptr) safe->localSheet.reset(); });
    };
    if (shell != nullptr)
        shell->showSheet (std::move (form), ProductForm::idealWidth, ProductForm::idealHeight);
    else
    {
        localSheet = std::move (form);
        addAndMakeVisible (*localSheet);
        resized();
    }
}

void GearPage::applyToTuning()
{
    const auto& inst = InstallationStore::instance().get();
    auto& db = gear::GearDatabase::instance();
    auto cfg = session.getConfig();
    const auto plan = install::buildPlan (inst, cfg, db);
    install::applyToConfig (plan, inst, db, cfg);
    if (! session.setConfig (cfg))
    {
        toast (Session::configLockedMessage(), AppShell::ToastKind::warn);
        return;
    }

    juce::String msg = "Driver specs applied to the tuning config";
    if (! plan.warnings.isEmpty())
        msg << "  |  " << plan.warnings.size() << (plan.warnings.size() == 1 ? " warning" : " warnings") << " - see Install & gains";
    toast (msg, plan.warnings.isEmpty() ? AppShell::ToastKind::good : AppShell::ToastKind::warn);
}

void GearPage::resetInstallation()
{
    if (shell == nullptr)
    {
        InstallationStore::instance().set ({});
        return;
    }
    auto sheet = std::make_unique<MessageSheet> ("Reset installation?",
                                                 "Every chosen product, amp assignment and wire setting is cleared. Your own products stay in your library.",
                                                 juce::StringArray(), "Reset", "Keep", icons::Id::warning);
    sheet->onResult = [this] (bool ok)
    {
        if (ok)
        {
            InstallationStore::instance().set ({});
            toast ("Installation reset");
        }
    };
    shell->showSheet (std::move (sheet), 460, MessageSheet::idealHeight (0));
}

void GearPage::toast (const juce::String& msg, AppShell::ToastKind kind)
{
    if (shell != nullptr)
        shell->toast (msg, kind);
}

//==============================================================================
void GearPage::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg1);
    if (localSheet != nullptr)
    {
        g.setColour (theme::overlay);
        g.fillRect (getLocalBounds());
    }
}

void GearPage::resized()
{
    body.setBounds (getLocalBounds());
    if (localSheet != nullptr)
    {
        const int w = juce::jmin (ProductPicker::idealWidth, getWidth() - 32);
        const int h = juce::jmin (ProductPicker::idealHeight, getHeight() - 32);
        localSheet->setBounds (getLocalBounds().withSizeKeepingCentre (w, h));
    }
}

int GearPage::layoutContent (int width)
{
    const int x0 = margin;
    const int w = width - margin * 2;
    int y = margin;
    const bool wide = w >= 1000;

    // ---- summary strip
    {
        const int h = cardChrome + 18 + 10 + 6 + 10 + btnH + theme::pad - 8;
        summaryCard.setBounds (x0, y, w, h);
        auto r = summaryCard.getContentArea();
        summaryText.setBounds (r.removeFromTop (18));
        r.removeFromTop (10);
        completeness.setBounds (r.removeFromTop (6));
        r.removeFromTop (10);
        auto buttons = r.removeFromTop (btnH);
        applyBtn.setBounds (buttons.removeFromLeft (250));
        buttons.removeFromLeft (gap);
        installBtn.setBounds (buttons.removeFromLeft (150));
        resetBtn.setBounds (buttons.removeFromRight (90));
        y += h + margin;
    }

    auto section = [&] (SectionHeader& h)
    {
        h.setBounds (x0, y, w, SectionHeader::height);
        y += SectionHeader::height + gap;
    };

    // ---- source
    section (sourceHeader);
    {
        std::vector<juce::Component*> slots { &headUnitSlot, &dspSlot, &dacSlot };
        const int cols = w >= 900 ? 3 : 1;
        const int cw = (w - gap2 * (cols - 1)) / cols;
        int x = x0, col = 0;
        for (auto* s : slots)
        {
            s->setBounds (x, y, cw, SlotCard::height);
            if (++col == cols) { col = 0; x = x0; y += SlotCard::height + gap2; } else x += cw + gap2;
        }
        if (col != 0) y += SlotCard::height + gap2;
        y += margin - gap2;
    }

    // ---- amplifiers
    section (ampHeader);
    {
        const int cols = wide && ampCards.size() > 1 ? 2 : 1;
        const int cw = (w - gap2 * (cols - 1)) / cols;
        int col = 0, rowH = 0, x = x0;
        for (auto& a : ampCards)
        {
            const int h = a->heightFor (cw);
            a->setBounds (x, y, cw, h);
            rowH = juce::jmax (rowH, h);
            if (++col == cols) { col = 0; x = x0; y += rowH + gap2; rowH = 0; } else x += cw + gap2;
        }
        if (col != 0) y += rowH + gap2;
        if (ampCards.empty())
        {
            ampEmpty.setBounds (x0, y, juce::jmin (w, 720), ampEmpty.heightFor (juce::jmin (w, 720)));
            y += ampEmpty.getHeight() + gap;
        }
        addAmpBtn.setBounds (x0, y, 170, btnH);
        y += btnH + margin;
    }

    // ---- speakers
    section (speakerHeader);
    {
        const int carW = wide ? 320 : w;
        const int carH = wide ? 420 : 300;
        carCard.setBounds (x0, y, carW, carH);
        car.setBounds (carCard.getContentArea());

        int rx = wide ? x0 + carW + gap2 : x0;
        int rw = wide ? w - carW - gap2 : w;
        int ry = wide ? y : y + carH + gap2;
        const int cols = rw >= 700 ? 2 : 1;
        const int cw = (rw - gap2 * (cols - 1)) / cols;
        int col = 0, rowH = 0, x = rx;
        for (auto& d : driverRows)
        {
            const int h = d->heightFor();
            d->setBounds (x, ry, cw, h);
            rowH = juce::jmax (rowH, h);
            if (++col == cols) { col = 0; x = rx; ry += rowH + gap2; rowH = 0; } else x += cw + gap2;
        }
        if (col != 0) ry += rowH + gap2;
        y = juce::jmax (ry, wide ? y + carH + gap2 : ry) + margin - gap2;
    }

    // ---- rig
    section (rigHeader);
    {
        // read-only: the rig is chosen on the Rig page
        const int h = btnH + gap2 * 2;
        rigCard.setBounds (x0, y, w, h);
        auto r = rigCard.getLocalBounds().reduced (gap2);
        rigBtn.setBounds (r.removeFromRight (120));
        r.removeFromRight (gap2);
        for (auto* chip : { &rigInterfaceChip, &rigMic1Chip, &rigMic2Chip })
        {
            const int cw = juce::jmin (chip->getIdealWidth(), r.getWidth());
            chip->setBounds (r.removeFromLeft (cw).withSizeKeepingCentre (cw, 24));
            r.removeFromLeft (gap);
        }
        y += h + margin;
    }

    // ---- electrical
    section (elecHeader);
    {
        const int cols = w >= 900 ? 2 : 1;
        const int cw = (w - gap2 * (cols - 1)) / cols;
        int x = x0, col = 0;
        for (auto* s : { &alternatorSlot, &batterySlot })
        {
            s->setBounds (x, y, cw, SlotCard::height);
            if (++col == cols) { col = 0; x = x0; y += SlotCard::height + gap2; } else x += cw + gap2;
        }
        if (col != 0) y += SlotCard::height + gap2;
    }
    {
        const int hintW = w - theme::pad * 2;
        const int hintH = elecHint.heightFor (hintW);
        const int h = cardChrome + fieldH + gap2 + hintH + theme::pad - 8;
        elecCard.setBounds (x0, y, w, h);
        auto r = elecCard.getContentArea();
        auto fields = r.removeFromTop (fieldH);
        const int cols = w >= 760 ? 4 : 2;
        const int fw = (fields.getWidth() - gap * (cols - 1)) / cols;
        altVoltage.setBounds (fields.removeFromLeft (fw)); fields.removeFromLeft (gap);
        altAmps.setBounds (fields.removeFromLeft (fw)); fields.removeFromLeft (gap);
        battAh.setBounds (fields.removeFromLeft (fw)); fields.removeFromLeft (gap);
        headroom.setBounds (fields.removeFromLeft (fw));
        r.removeFromTop (gap2);
        elecHint.setBounds (r.removeFromTop (hintH));
        y += h + margin;
    }

    return y;
}
} // namespace fa::ui
