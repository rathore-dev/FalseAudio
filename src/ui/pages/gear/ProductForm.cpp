#include "ProductForm.h"

namespace fa::ui
{
using namespace gearkit;

//==============================================================================
// Completeness checks
//==============================================================================
std::vector<CalcCheck> calcChecks (const gear::Product& p)
{
    using C = gear::Category;
    std::vector<CalcCheck> out;
    auto check = [&out] (const juce::String& name, bool ok, const juce::String& missing)
    {
        out.push_back ({ name, ok, ok ? juce::String() : missing });
    };
    auto has = [] (double v) { return v > 0; };

    switch (p.category)
    {
        case C::headUnit:
        {
            const gear::SourceSpecs s = p.source.value_or (gear::SourceSpecs{});
            check ("Source level to amps / DSP", has (s.preoutVoltageV), "needs pre-out voltage");
            check ("Max clean volume step", has (s.preoutVoltageV) && has (s.maxVolumeStep), "needs volume steps");
            check ("Exact clip step", has (s.cleanVolumeStep), "optional: clean volume step");
            break;
        }
        case C::dsp: case C::dac: case C::dspAmp:
        {
            const gear::SourceSpecs s = p.source.value_or (gear::SourceSpecs{});
            check ("Input clip check", has (s.maxInputVoltageV), "needs max input voltage");
            if (p.category != C::dspAmp)
                check ("Output level to amps", has (s.preoutVoltageV), "needs output voltage");
            check ("Channel count vs layout", has (s.outputs), "needs outputs");
            check ("PEQ / delay limits for the tuner", has (s.peqBandsPerChannel) && has (s.maxDelayMs), "needs PEQ bands and max delay");
            if (p.category == C::dsp || p.category == C::dspAmp)
                check ("Automatic DSP control", s.software.isNotEmpty(), "optional: software name");
            if (p.category != C::dspAmp)
                break;
            [[fallthrough]];
        }
        case C::ampMulti: case C::ampMono:
        {
            const gear::AmpSpecs a = p.amp.value_or (gear::AmpSpecs{});
            bool anyPower = false;
            for (auto& [k, w] : a.rmsPerChannelW) anyPower = anyPower || w > 0;
            check ("Gain setting (V)", anyPower && has (a.inputSensitivityMaxV), "needs RMS power and input sensitivity");
            check ("Wire gauge and fuse", anyPower, "needs RMS power");
            check ("Load safety (min ohms)", has (a.minStableOhms), "needs min stable load");
            check ("Current draw accuracy", has (a.efficiencyPercent) || a.amplifierClass.isNotEmpty(), "optional: efficiency or class");
            break;
        }
        case C::subwoofer:
        {
            const gear::DriverSpecs d = p.driver.value_or (gear::DriverSpecs{});
            const bool imp = d.voiceCoils > 1 ? has (d.coilOhms) : (has (d.nominalOhms) || has (d.coilOhms));
            check ("Wiring and load", imp, "needs voice coils and ohms per coil");
            check ("Power match / gain target", imp && has (d.powerRmsW), "needs RMS power");
            check ("Safe crossover", has (d.fsHz), "needs Fs");
            check ("Excursion-limited high-pass", has (d.xmaxMm) && has (d.sdCm2) && has (d.fsHz), "needs Xmax, Sd and Fs");
            check ("Box / alignment hints", has (d.qts) && has (d.vasLitres), "optional: Qts and Vas");
            break;
        }
        case C::driverTweeter: case C::driverMidrange: case C::driverMidbass: case C::driverCoax: case C::componentSet:
        {
            const gear::DriverSpecs d = p.driver.value_or (gear::DriverSpecs{});
            check ("Power match / gain target", has (d.nominalOhms) && has (d.powerRmsW), "needs impedance and RMS power");
            check ("Safe crossover", has (d.fsHz), "needs Fs");
            check ("Excursion-limited high-pass", has (d.xmaxMm) && has (d.sdCm2) && has (d.fsHz), "needs Xmax and Sd");
            check ("Level matching", has (d.sensitivityDb), "optional: sensitivity");
            check ("Size-based defaults", has (d.sizeInches), "needs size");
            break;
        }
        case C::powerWire: case C::speakerWire:
        {
            const gear::WireSpecs w = p.wire.value_or (gear::WireSpecs{});
            check ("Voltage drop", (p.wire.has_value() && (w.awg != 0 || w.mm2 > 0 || w.awg == 0)) && w.material.isNotEmpty(), "needs gauge and material");
            check ("Ampacity / fuse", has (w.ampacityA) || w.material.isNotEmpty(), "needs material or ampacity");
            break;
        }
        case C::rca: case C::fuseHolder:
            check ("Listed in the plan", p.brand.isNotEmpty() && p.model.isNotEmpty(), "needs brand and model");
            break;
        case C::battery: case C::alternator:
            check ("Electrical budget", p.brand.isNotEmpty() && p.model.isNotEmpty(), "needs brand and model");
            break;
        case C::audioInterface:
        {
            const gear::InterfaceSpecs i = p.audioInterface.value_or (gear::InterfaceSpecs{});
            check ("Mic setup guide", has (i.inputs) && has (i.outputs), "needs inputs and outputs");
            check ("Phantom power check", i.phantomPower || has (i.micPreamps), "optional: phantom / preamps");
            check ("Loopback timing reference", true, "");
            break;
        }
        case C::measurementMic:
        {
            const gear::MicSpecs m = p.mic.value_or (gear::MicSpecs{});
            check ("Connection guide", m.type.isNotEmpty(), "needs type (USB / XLR)");
            check ("Calibration reminder", m.calFileAvailable, "optional: calibration file");
            break;
        }
    }
    return out;
}

void CompletenessMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().reduced (0, 4);
    for (auto& c : checks)
    {
        auto row = r.removeFromTop (rowH);
        const auto col = c.ok ? theme::good : theme::warn;
        auto disc = row.removeFromLeft (18).withSizeKeepingCentre (16, 16).toFloat();
        g.setColour (col.withAlpha (0.16f));
        g.fillEllipse (disc);
        icons::draw (g, c.ok ? icons::Id::check : icons::Id::warning, disc.reduced (3.5f), col);
        row.removeFromLeft (8);
        g.setColour (theme::text);
        g.setFont (theme::fontMedium (12.5f));
        const int nameW = (int) std::ceil (theme::textWidth (theme::fontMedium (12.5f), c.name));
        g.drawText (c.name, row.removeFromLeft (juce::jmin (nameW, row.getWidth())), juce::Justification::centredLeft, false);
        if (c.missing.isNotEmpty())
        {
            row.removeFromLeft (8);
            g.setColour (theme::textMuted);
            g.setFont (theme::font (11.5f));
            g.drawText (c.missing, row, juce::Justification::centredLeft, true);
        }
    }
}

//==============================================================================
// One spec field
//==============================================================================
class ProductForm::Field : public juce::Component
{
public:
    Field (const gear::SpecField& def, std::function<void()> changed) : def (def), onChanged (std::move (changed))
    {
        using K = gear::SpecField::Kind;
        if (def.kind == K::boolean)
        {
            toggle = std::make_unique<ToggleSwitch> (def.label);
            toggle->onClick = [this] { if (onChanged) onChanged(); };
            addAndMakeVisible (*toggle);
        }
        else if (def.kind == K::text && ! def.options.isEmpty())
        {
            combo = std::make_unique<juce::ComboBox>();
            styleCombo (*combo);
            combo->setEditableText (true);
            int i = 1;
            for (auto& o : def.options) combo->addItem (o, i++);
            combo->setTextWhenNothingSelected (def.hint.isNotEmpty() ? def.hint : "Choose or type");
            combo->onChange = [this] { if (onChanged) onChanged(); };
            addAndMakeVisible (*combo);
        }
        else
        {
            editor = std::make_unique<juce::TextEditor>();
            styleEditor (*editor, def.hint.isNotEmpty() ? def.hint : (def.kind == K::number ? "Number" : "Text"));
            editor->setJustification (juce::Justification::centredLeft);
            if (def.kind == K::number)
                editor->setInputRestrictions (12, "0123456789.-");
            editor->onTextChange = [this] { if (onChanged) onChanged(); };
            addAndMakeVisible (*editor);
        }
    }

    static constexpr int height = 72;
    const gear::SpecField def;

    void setValue (const juce::var& v)
    {
        if (toggle)       toggle->setToggleState ((bool) v, juce::dontSendNotification);
        else if (combo)   combo->setText (v.isVoid() ? juce::String() : v.toString(), juce::dontSendNotification);
        else if (editor)
        {
            juce::String s;
            if (! v.isVoid())
                s = v.isDouble() || v.isInt() || v.isInt64() ? num ((double) v, 3) : v.toString();
            editor->setText (s, false);
        }
    }

    juce::var getValue() const
    {
        if (toggle) return toggle->getToggleState();
        if (combo)  return combo->getText().trim().isEmpty() ? juce::var() : juce::var (combo->getText().trim());
        if (editor)
        {
            const auto s = editor->getText().trim();
            if (s.isEmpty()) return {};
            if (def.kind == gear::SpecField::Kind::number) return s.getDoubleValue();
            return s;
        }
        return {};
    }

    bool isEmpty() const { return getValue().isVoid(); }

    bool outOfRange() const
    {
        if (def.kind != gear::SpecField::Kind::number || isEmpty()) return false;
        if (def.minV == 0 && def.maxV == 0) return false;
        const double v = (double) getValue();
        return v < def.minV || v > def.maxV;
    }

    void setMissing (bool m) { if (missing != m) { missing = m; repaint(); } }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        auto labelRow = r.removeFromTop (18);
        if (toggle == nullptr)
        {
            g.setColour (theme::textDim);
            g.setFont (theme::fontMedium (12.0f));
            g.drawText (def.label, labelRow, juce::Justification::centredLeft, true);
            if (def.required)
            {
                const int w = (int) std::ceil (theme::textWidth (theme::fontMedium (12.0f), def.label));
                g.setColour (missing ? theme::warn : theme::accent);
                g.fillEllipse ((float) (labelRow.getX() + w + 6), (float) labelRow.getCentreY() - 2.5f, 5.0f, 5.0f);
            }
            if (def.unit.isNotEmpty())
            {
                g.setColour (theme::textMuted);
                g.setFont (theme::mono (11.0f));
                g.drawText (def.unit, labelRow, juce::Justification::centredRight, false);
            }
        }
        r.removeFromTop (btnH + 4);
        g.setFont (theme::font (10.5f));
        if (outOfRange())
        {
            g.setColour (theme::bad);
            g.drawText ("Expected " + num (def.minV, 2) + " to " + num (def.maxV, 2) + " " + def.unit, r, juce::Justification::topLeft, true);
        }
        else
        {
            g.setColour (theme::textMuted);
            g.drawText (def.usedFor.isNotEmpty() ? "Used for: " + def.usedFor : juce::String(), r, juce::Justification::topLeft, true);
        }
        // outline state on the editor
        if (editor)
            editor->setColour (juce::TextEditor::outlineColourId, outOfRange() ? theme::bad : (missing ? theme::warn.withAlpha (0.7f) : theme::border));
    }

    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromTop (18);
        auto ctl = r.removeFromTop (btnH);
        if (toggle)       toggle->setBounds (ctl.withY (ctl.getY() - 6).withHeight (btnH + 4));
        else if (combo)   combo->setBounds (ctl);
        else if (editor)  editor->setBounds (ctl);
    }

private:
    std::function<void()> onChanged;
    std::unique_ptr<juce::TextEditor> editor;
    std::unique_ptr<juce::ComboBox> combo;
    std::unique_ptr<ToggleSwitch> toggle;
    bool missing = false;
};

//==============================================================================
ProductForm::ProductForm (std::vector<gear::Category> allowed, std::optional<gear::Product> existing)
    : categories (std::move (allowed))
{
    if (categories.empty())
        categories.push_back (gear::Category::headUnit);

    if (existing)
    {
        draft = *existing;
        editing = true;
        categories = { draft.category };
    }
    else
        draft.category = categories.front();

    addAndMakeVisible (body);
    body.layout = [this] (int w) { return layoutForm (w); };
    auto& c = body.content();

    styleCombo (categoryBox);
    int i = 1;
    for (auto cat : categories)
        categoryBox.addItem (categoryLabel (cat), i++);
    categoryBox.setSelectedId (1, juce::dontSendNotification);
    categoryBox.setEnabled (! editing && categories.size() > 1);
    categoryBox.onChange = [this]
    {
        const int idx = categoryBox.getSelectedId() - 1;
        if (idx >= 0 && idx < (int) categories.size())
            setCategory (categories[(size_t) idx]);
    };

    styleEditor (brandEd, "e.g. JL Audio");
    styleEditor (modelEd, "e.g. XD600/6v2");
    styleEditor (seriesEd, "optional");
    styleEditor (notesEd, "Where the specs came from, anything unusual");
    notesEd.setMultiLine (true, true);
    notesEd.setReturnKeyStartsNewLine (true);
    brandEd.setText (draft.brand, false);
    modelEd.setText (draft.model, false);
    seriesEd.setText (draft.series, false);
    notesEd.setText (draft.notes, false);
    brandEd.setEnabled (! editing);     // keep the id stable when overriding a built-in
    modelEd.setEnabled (! editing);
    for (auto* e : { &brandEd, &modelEd, &seriesEd, &notesEd })
        e->onTextChange = [this] { refresh(); };

    for (auto* w : std::initializer_list<juce::Component*> { &categoryBox, &brandEd, &modelEd, &seriesEd, &notesEd,
                                                             &identityCap, &requiredCap, &optionalCap })
        c.addAndMakeVisible (w);

    addAndMakeVisible (meterCap);
    addAndMakeVisible (previewCap);
    addAndMakeVisible (meter);
    addAndMakeVisible (saveBtn);
    addAndMakeVisible (cancelBtn);
    saveBtn.onClick = [this] { save(); };
    cancelBtn.onClick = [this] { if (onCancel) onCancel(); };

    rebuildFields();
    setSize (idealWidth, idealHeight);
}

ProductForm::~ProductForm() = default;

void ProductForm::setCategory (gear::Category cat)
{
    if (draft.category == cat)
        return;
    gear::Product fresh;
    fresh.category = cat;
    fresh.brand = draft.brand; fresh.model = draft.model; fresh.series = draft.series; fresh.notes = draft.notes;
    draft = fresh;
    rebuildFields();
}

void ProductForm::rebuildFields()
{
    fields.clear();
    auto& c = body.content();
    for (auto& def : gear::specFields (draft.category))
    {
        auto f = std::make_unique<Field> (def, [this] { refresh(); });
        f->setValue (gear::getSpecValue (draft, def.key));
        c.addAndMakeVisible (*f);
        fields.push_back (std::move (f));
    }
    refresh();
    body.relayout();
}

void ProductForm::refresh()
{
    draft.brand  = brandEd.getText().trim();
    draft.model  = modelEd.getText().trim();
    draft.series = seriesEd.getText().trim();
    draft.notes  = notesEd.getText().trim();
    for (auto& f : fields)
        gear::setSpecValue (draft, f->def.key, f->getValue());

    const auto missingKeys = gear::missingRequiredKeys (draft);
    missing = gear::missingRequired (draft);
    bool rangeError = false;
    for (auto& f : fields)
    {
        f->setMissing (missingKeys.contains (f->def.key));
        rangeError = rangeError || f->outOfRange();
    }

    meter.setProduct (draft);
    previewChips = specChips (draft);
    errorText = rangeError ? "Some values are outside their sensible range."
              : missing.isEmpty() ? juce::String() : "Missing: " + missing.joinIntoString (", ");
    saveBtn.setEnabled (missing.isEmpty() && ! rangeError);
    repaint();
}

void ProductForm::save()
{
    refresh();
    if (! saveBtn.isEnabled())
        return;

    gear::Product p = draft;
    p.verified = false;
    p.userDefined = true;
    if (editing && p.notes.isEmpty())
        p.notes = "Specs edited by user";

    juce::String error;
    if (! gear::GearDatabase::instance().addUserProduct (p, error))
    {
        errorText = error.isNotEmpty() ? error : "Could not save the product.";
        repaint();
        return;
    }
    if (p.id.isEmpty())
    {
        // the database may have generated the id; look it up by brand + model
        for (auto* q : gear::GearDatabase::instance().search (p.brand + " " + p.model, p.category, 20))
            if (q->brand == p.brand && q->model == p.model) { p.id = q->id; break; }
    }
    if (onSaved)
        onSaved (p);
}

void ProductForm::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    auto head = r.removeFromTop (64);
    g.setColour (theme::text);
    g.setFont (theme::fontBold (19.0f));
    g.drawText (editing ? "Edit specs" : "Add your own product", head.withTrimmedTop (8).removeFromTop (28), juce::Justification::centredLeft, false);
    g.setColour (theme::textDim);
    g.setFont (theme::font (12.5f));
    g.drawText (editing ? "Your values are saved as a user copy of " + draft.displayName() + "; the built-in database stays untouched."
                        : "Enter the specs from the spec sheet or manual. Required fields unlock the gain, wire and crossover calculations.",
                head.withTrimmedTop (36), juce::Justification::centredLeft, true);

    // side panel
    auto side = r.removeFromRight (300).reduced (0, 0);
    theme::fillCard (g, side.toFloat(), theme::bg1, theme::border, theme::radius);

    auto inner = side.reduced (gap2);
    inner.removeFromTop (16 + gap);
    inner.removeFromTop (meter.idealHeight() + gap2);
    inner.removeFromTop (16 + gap);
    auto chips = inner.removeFromTop (22);
    if (previewChips.isEmpty())
    {
        g.setColour (theme::textMuted);
        g.setFont (theme::font (12.0f));
        g.drawText ("Specs appear here as you type", chips, juce::Justification::centredLeft, false);
    }
    else
    {
        // wrap chips over two rows
        const auto f = theme::fontMedium (11.0f);
        juce::StringArray row1, row2;
        int w = 0;
        for (auto& c : previewChips)
        {
            const int cw = (int) std::ceil (theme::textWidth (f, c)) + 20;
            if (w + cw <= chips.getWidth() && row2.isEmpty()) { row1.add (c); w += cw; }
            else row2.add (c);
        }
        drawChips (g, row1, chips, theme::accent);
        inner.removeFromTop (6);
        drawChips (g, row2, inner.removeFromTop (22), theme::accent);
    }

    if (errorText.isNotEmpty())
    {
        auto err = getLocalBounds().removeFromBottom (btnH + gap2 * 2).reduced (gap2).withRight (side.getX() - gap2);
        g.setColour (theme::warn);
        g.setFont (theme::font (12.0f));
        g.drawText (errorText, err, juce::Justification::centredLeft, true);
    }
}

void ProductForm::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (64);
    auto side = r.removeFromRight (300);
    r.removeFromRight (gap2);

    auto inner = side.reduced (gap2);
    auto buttons = inner.removeFromBottom (btnH);
    saveBtn.setBounds (buttons.removeFromRight (150));
    buttons.removeFromRight (gap);
    cancelBtn.setBounds (buttons.removeFromRight (90));

    meterCap.setBounds (inner.removeFromTop (16));
    inner.removeFromTop (gap);
    meter.setBounds (inner.removeFromTop (meter.idealHeight()));
    inner.removeFromTop (gap2);
    previewCap.setBounds (inner.removeFromTop (16));

    r.removeFromBottom (btnH + gap2);     // room for the error line
    body.setBounds (r);
}

int ProductForm::layoutForm (int width)
{
    const int colW = (width - gap2) / 2;
    int y = 0;

    identityCap.setBounds (0, y, width, 16); y += 16 + gap;
    categoryBox.setBounds (0, y, colW, btnH);
    seriesEd.setBounds (colW + gap2, y, colW, btnH); y += btnH + gap;
    brandEd.setBounds (0, y, colW, btnH);
    modelEd.setBounds (colW + gap2, y, colW, btnH); y += btnH + gap2;

    auto placeGroup = [&] (bool required)
    {
        int col = 0;
        bool any = false;
        for (auto& f : fields)
        {
            if (f->def.required != required) continue;
            any = true;
            f->setBounds (col == 0 ? 0 : colW + gap2, y, colW, Field::height);
            if (++col == 2) { col = 0; y += Field::height + gap; }
        }
        if (col != 0) y += Field::height + gap;
        return any;
    };

    requiredCap.setBounds (0, y, width, 16);
    const bool hasReq = fields.end() != std::find_if (fields.begin(), fields.end(), [] (auto& f) { return f->def.required; });
    requiredCap.setVisible (hasReq);
    if (hasReq) { y += 16 + gap; placeGroup (true); y += gap; }

    optionalCap.setBounds (0, y, width, 16);
    const bool hasOpt = fields.end() != std::find_if (fields.begin(), fields.end(), [] (auto& f) { return ! f->def.required; });
    optionalCap.setVisible (hasOpt);
    if (hasOpt) { y += 16 + gap; placeGroup (false); y += gap; }

    notesEd.setBounds (0, y, width, 64); y += 64 + gap2;
    return y;
}
} // namespace fa::ui
