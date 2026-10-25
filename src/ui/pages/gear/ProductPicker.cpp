#include "ProductPicker.h"

namespace fa::ui
{
using namespace gearkit;

//==============================================================================
// SpecSheetView
//==============================================================================
class SpecSheetView::Body : public juce::Component
{
public:
    void set (const gear::Product* p)
    {
        rows = p != nullptr ? specSheet (*p) : std::vector<SpecRow>();
        name = p != nullptr ? p->displayName() : juce::String();
        product = p;
        repaint();
    }

    int heightFor (int width) const
    {
        int h = 0;
        for (auto& r : rows)
            h += r.isGroup ? groupH : rowHeight (r, width);
        return h + 8;
    }

    void paint (juce::Graphics& g) override
    {
        const int w = getWidth();
        int y = 0;
        for (auto& r : rows)
        {
            if (r.isGroup)
            {
                theme::drawEyebrow (g, r.label, { 0, y + 10, w, 14 }, theme::accent);
                g.setColour (theme::border);
                g.fillRect (0, y + groupH - 1, w, 1);
                y += groupH;
                continue;
            }
            const int h = rowHeight (r, w);
            if (r.label.isNotEmpty())
            {
                g.setColour (theme::textDim);
                g.setFont (theme::font (12.0f));
                g.drawText (r.label, 0, y, labelW - 8, 24, juce::Justification::centredLeft, true);
            }
            g.setColour (theme::text);
            g.setFont (r.label.isEmpty() ? theme::font (12.0f) : theme::mono (12.0f));
            const int x = r.label.isEmpty() ? 0 : labelW;
            g.drawFittedText (r.value, x, y + 4, w - x, h - 8, juce::Justification::topLeft, 6, 1.0f);
            y += h;
        }
    }

private:
    static constexpr int labelW = 120, groupH = 32;
    static int rowHeight (const SpecRow& r, int width)
    {
        if (r.label.isNotEmpty() && r.value.length() < 28) return 24;
        juce::GlyphArrangement ga;
        ga.addJustifiedText (theme::font (12.0f), r.value, 0.0f, 0.0f, (float) (width - (r.label.isEmpty() ? 0 : labelW)), juce::Justification::topLeft);
        return juce::jmax (24, (int) std::ceil (ga.getBoundingBox (0, -1, true).getBottom()) + 10);
    }
    std::vector<SpecRow> rows;
    juce::String name;
    const gear::Product* product = nullptr;
};

SpecSheetView::SpecSheetView() : body (std::make_unique<Body>())
{
    viewport.setViewedComponent (body.get(), false);
    viewport.setScrollBarsShown (true, false, true, false);
    viewport.setScrollBarThickness (6);
    addAndMakeVisible (viewport);
}

void SpecSheetView::setProduct (const gear::Product* p)
{
    body->set (p);
    resized();
}

void SpecSheetView::resized()
{
    viewport.setBounds (getLocalBounds());
    const int w = juce::jmax (100, getWidth() - viewport.getScrollBarThickness());
    body->setSize (w, juce::jmax (getHeight(), body->heightFor (w)));
}

//==============================================================================
// Brand list (left column)
//==============================================================================
class ProductPicker::BrandList : public juce::Component,
                                 private juce::ListBoxModel
{
public:
    BrandList()
    {
        list.setModel (this);
        list.setRowHeight (30);
        list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        list.getViewport()->setScrollBarThickness (6);
        addAndMakeVisible (list);
    }

    std::function<void (const juce::String& brand)> onBrand;   // empty = all

    void setBrands (juce::StringArray b)
    {
        brands = std::move (b);
        list.updateContent();
        list.selectRow (0, juce::dontSendNotification);
        list.repaint();
    }
    juce::String getSelectedBrand() const
    {
        const int r = list.getSelectedRow();
        return r <= 0 || r > brands.size() ? juce::String() : brands[r - 1];
    }
    void clearSelection() { list.selectRow (0, juce::dontSendNotification); }
    void resized() override { list.setBounds (getLocalBounds()); }

private:
    int getNumRows() override { return brands.size() + 1; }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool sel) override
    {
        auto r = juce::Rectangle<int> (0, 0, w, h).reduced (2, 1).toFloat();
        if (sel)
        {
            g.setColour (theme::accent.withAlpha (0.14f));
            g.fillRoundedRectangle (r, theme::radiusSm);
            g.setColour (theme::accent);
            g.fillRoundedRectangle (r.getX(), r.getY() + 7.0f, 2.5f, r.getHeight() - 14.0f, 1.0f);
        }
        g.setColour (sel ? theme::text : theme::textDim);
        g.setFont (sel ? theme::fontMedium (12.5f) : theme::font (12.5f));
        g.drawText (row == 0 ? "All brands" : brands[row - 1], r.toNearestInt().reduced (12, 0), juce::Justification::centredLeft, true);
    }
    void selectedRowsChanged (int) override { if (onBrand) onBrand (getSelectedBrand()); }

    juce::ListBox list;
    juce::StringArray brands;
};

//==============================================================================
// Result row
//==============================================================================
class ProductPicker::ResultRow : public juce::Component
{
public:
    void set (const gear::Product* p, bool sel, bool isCurrent)
    {
        product = p; selected = sel; current = isCurrent;
        chips = p != nullptr ? specChips (*p) : juce::StringArray();
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        if (product == nullptr) return;
        auto r = getLocalBounds().reduced (4, 2).toFloat();
        if (selected || hover)
        {
            g.setColour (selected ? theme::accent.withAlpha (0.12f) : theme::bg3);
            g.fillRoundedRectangle (r, theme::radiusSm);
        }
        if (selected)
        {
            g.setColour (theme::accent.withAlpha (0.5f));
            g.drawRoundedRectangle (r.reduced (0.5f), theme::radiusSm, 1.0f);
        }
        auto inner = getLocalBounds().reduced (14, 7);
        auto top = inner.removeFromTop (18);
        g.setColour (theme::textDim);
        g.setFont (theme::fontMedium (11.0f));
        const int bw = (int) std::ceil (theme::textWidth (theme::fontMedium (11.0f), product->brand)) + 2;
        g.drawText (product->brand, top.removeFromLeft (bw), juce::Justification::centredLeft, false);
        top.removeFromLeft (8);
        g.setColour (theme::text);
        g.setFont (theme::fontBold (13.5f));
        const int mw = juce::jmin (top.getWidth() - 60, (int) std::ceil (theme::textWidth (theme::fontBold (13.5f), product->model)) + 2);
        g.drawText (product->model, top.removeFromLeft (mw), juce::Justification::centredLeft, true);
        top.removeFromLeft (8);
        if (product->verified)
        {
            drawVerified (g, top.removeFromLeft (15).withSizeKeepingCentre (15, 15).toFloat());
            top.removeFromLeft (6);
        }
        juce::StringArray badges;
        if (product->userDefined) badges.add ("Your specs");
        if (current) badges.add ("Current");
        drawChips (g, badges, top.withSizeKeepingCentre (top.getWidth(), 17), product->userDefined ? theme::warn : theme::accent, 9.5f);

        inner.removeFromTop (3);
        drawChips (g, chips, inner.removeFromTop (18), theme::textDim, 10.5f);
    }
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit  (const juce::MouseEvent&) override { hover = false; repaint(); }

private:
    const gear::Product* product = nullptr;
    juce::StringArray chips;
    bool selected = false, current = false, hover = false;
};

//==============================================================================
ProductPicker::ProductPicker (const juce::String& title, std::vector<gear::Category> cats, const juce::String& currentProductId)
    : slotTitle (title), categories (std::move (cats)), currentId (currentProductId)
{
    if (categories.empty())
        categories.push_back (gear::Category::headUnit);

    styleEditor (search, juce::String (juce::CharPointer_UTF8 ("Search brand, model or series…")));
    search.setFont (theme::font (14.0f));
    search.setIndents (36, 8);
    search.onTextChange = [this] { brands->clearSelection(); runSearch(); };
    search.onEscapeKey = [this] { if (onCancel) onCancel(); };
    search.onReturnKey = [this] { if (selected != nullptr) pick(); };
    addAndMakeVisible (search);

    juce::StringArray segNames;
    if (categories.size() > 1)
        segNames.add ("All");
    for (auto c : categories)
        segNames.add (categoryLabel (c));
    categorySeg.setOptions (segNames);
    categorySeg.setSelected (0);
    categorySeg.onChange = [this] (int)
    {
        brands->setBrands (activeCategory() ? gear::GearDatabase::instance().brands (*activeCategory()) : juce::StringArray());
        runSearch();
    };
    addAndMakeVisible (categorySeg);
    categorySeg.setVisible (categories.size() > 1);

    brands = std::make_unique<BrandList>();
    brands->onBrand = [this] (const juce::String&) { runSearch(); };
    addAndMakeVisible (*brands);

    list.setModel (this);
    list.setRowHeight (58);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    list.getViewport()->setScrollBarThickness (6);
    addAndMakeVisible (list);

    addAndMakeVisible (sheet);
    addAndMakeVisible (useBtn);
    addAndMakeVisible (editBtn);
    addChildComponent (removeBtn);
    addAndMakeVisible (addBtn);
    addAndMakeVisible (closeBtn);
    useBtn.onClick   = [this] { pick(); };
    editBtn.onClick  = [this] { if (selected != nullptr) openForm (*selected); };
    addBtn.onClick   = [this] { openForm (std::nullopt); };
    closeBtn.onClick = [this] { if (onCancel) onCancel(); };
    removeBtn.onClick = [this]
    {
        if (selected == nullptr || ! selected->userDefined) return;
        const auto id = selected->id;
        selected = nullptr;
        gear::GearDatabase::instance().removeUserProduct (id);
        onDatabaseReady();
        if (auto* p = gear::GearDatabase::instance().find (id))
            select (p);
    };
    useBtn.setEnabled (false);
    editBtn.setEnabled (false);

    setWantsKeyboardFocus (true);
    setSize (idealWidth, idealHeight);

    auto& loader = GearLoader::instance();
    if (! loader.isLoaded())
    {
        loading = true;
        startTimerHz (30);
    }
    loader.ensureLoaded ([safe = juce::Component::SafePointer<ProductPicker> (this)]
    {
        if (safe != nullptr)
            safe->onDatabaseReady();
    });
}

ProductPicker::~ProductPicker() = default;

std::optional<gear::Category> ProductPicker::activeCategory() const
{
    if (categories.size() == 1)
        return categories.front();
    const int s = categorySeg.getSelected();
    if (s <= 0 || s > (int) categories.size())
        return std::nullopt;
    return categories[(size_t) s - 1];
}

void ProductPicker::onDatabaseReady()
{
    loading = false;
    stopTimer();
    auto& db = gear::GearDatabase::instance();
    juce::StringArray b;
    if (auto c = activeCategory())
        b = db.brands (*c);
    else
        for (auto c : categories)
            b.addArray (db.brands (c));
    b.removeDuplicates (true);
    b.sortNatural();
    brands->setBrands (b);
    runSearch();
    if (currentId.isNotEmpty())
        if (auto* p = db.find (currentId))
            select (p);
    search.grabKeyboardFocus();
}

void ProductPicker::timerCallback()
{
    shimmerPhase = std::fmod (shimmerPhase + 0.022f, 1.0f);
    repaint (list.getBounds());
}

void ProductPicker::runSearch()
{
    results.clear();
    if (loading)
    {
        list.updateContent();
        return;
    }
    auto& db = gear::GearDatabase::instance();
    const auto text  = search.getText().trim();
    const auto brand = brands->getSelectedBrand();
    std::vector<gear::Category> cats;
    if (auto active = activeCategory()) cats = { *active }; else cats = categories;

    for (auto cat : cats)
    {
        if (text.isNotEmpty())
        {
            for (auto* p : db.search (text, cat, 300))
                if (brand.isEmpty() || p->brand == brand)
                    results.push_back (p);
        }
        else if (brand.isNotEmpty())
        {
            for (auto* p : db.byBrand (cat, brand))
                results.push_back (p);
        }
        else
        {
            for (auto& b : db.brands (cat))
                for (auto* p : db.byBrand (cat, b))
                    results.push_back (p);
        }
    }
    if (text.isEmpty())
        std::stable_sort (results.begin(), results.end(), [] (auto* a, auto* b)
        {
            const int c = a->brand.compareNatural (b->brand);
            return c != 0 ? c < 0 : a->model.compareNatural (b->model) < 0;
        });

    statusText = results.empty() ? juce::String() : juce::String (results.size()) + (results.size() == 1 ? " product" : " products");
    list.updateContent();
    if (selected != nullptr)
    {
        const auto it = std::find (results.begin(), results.end(), selected);
        if (it != results.end())
            list.selectRow ((int) (it - results.begin()), juce::dontSendNotification);
        else
            list.deselectAllRows();
    }
    repaint();
}

juce::Component* ProductPicker::refreshComponentForRow (int row, bool sel, juce::Component* existing)
{
    auto* r = dynamic_cast<ResultRow*> (existing);
    if (r == nullptr)
    {
        delete existing;
        r = new ResultRow();
    }
    const auto* p = row >= 0 && row < (int) results.size() ? results[(size_t) row] : nullptr;
    r->set (p, sel, p != nullptr && p->id == currentId);
    return r;
}

void ProductPicker::selectedRowsChanged (int lastRow)
{
    if (lastRow >= 0 && lastRow < (int) results.size())
        select (results[(size_t) lastRow]);
}

void ProductPicker::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    if (row >= 0 && row < (int) results.size()) { select (results[(size_t) row]); pick(); }
}

void ProductPicker::returnKeyPressed (int) { pick(); }

void ProductPicker::select (const gear::Product* p)
{
    selected = p;
    sheet.setProduct (p);
    useBtn.setEnabled (p != nullptr);
    editBtn.setEnabled (p != nullptr);
    removeBtn.setVisible (p != nullptr && p->userDefined);
    repaint();
}

void ProductPicker::pick()
{
    if (selected != nullptr && onPicked)
        onPicked (*selected);
}

void ProductPicker::openForm (std::optional<gear::Product> existing)
{
    std::vector<gear::Category> cats;
    if (auto c = activeCategory()) { cats = { *c }; for (auto o : categories) if (o != *c) cats.push_back (o); }
    else cats = categories;

    form = std::make_unique<ProductForm> (cats, std::move (existing));
    form->onSaved  = [this] (const gear::Product& p) { closeForm (&p); };
    form->onCancel = [this] { closeForm (nullptr); };
    addAndMakeVisible (*form);
    resized();
    form->toFront (true);
}

void ProductPicker::closeForm (const gear::Product* saved)
{
    juce::String savedId = saved != nullptr ? saved->id : juce::String();
    const bool pickIt = saved != nullptr && (selected == nullptr || selected->id != savedId);
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<ProductPicker> (this), savedId, pickIt]
    {
        if (safe == nullptr) return;
        safe->form.reset();
        safe->selected = nullptr;
        safe->onDatabaseReady();
        if (savedId.isNotEmpty())
        {
            if (auto* p = gear::GearDatabase::instance().find (savedId))
            {
                safe->search.setText (p->displayName(), false);
                safe->brands->clearSelection();
                safe->runSearch();
                safe->select (p);
                if (pickIt)
                    safe->pick();        // a freshly added product is what the user wants in the slot
            }
        }
        safe->resized();
    });
}

bool ProductPicker::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey) { if (onCancel) onCancel(); return true; }
    return false;
}

void ProductPicker::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    auto head = r.removeFromTop (56);
    theme::drawEyebrow (g, "Equipment picker", head.removeFromTop (14), theme::accent);
    g.setColour (theme::text);
    g.setFont (theme::fontBold (20.0f));
    g.drawText (slotTitle, head.removeFromTop (28), juce::Justification::centredLeft, false);

    if (form != nullptr)
        return;

    // search icon
    icons::draw (g, icons::Id::search, search.getBounds().removeFromLeft (34).withSizeKeepingCentre (16, 16).toFloat(), theme::textMuted);

    // columns
    auto cols = list.getBounds().getUnion (brands->getBounds()).getUnion (sheet.getBounds());
    g.setColour (theme::border);
    g.fillRect (list.getX() - gap / 2, cols.getY(), 1, cols.getHeight());
    g.fillRect (sheet.getX() - gap, cols.getY(), 1, cols.getHeight());

    auto listArea = list.getBounds();
    if (loading)
    {
        auto a = listArea.reduced (8, 4);
        for (int i = 0; i < 6; ++i)
        {
            auto row = a.removeFromTop (58).reduced (6, 9);
            drawShimmer (g, row.removeFromTop (14).withWidth (170).toFloat(), shimmerPhase);
            row.removeFromTop (8);
            drawShimmer (g, row.removeFromTop (16).withWidth (300).toFloat(), shimmerPhase);
        }
        g.setColour (theme::textMuted);
        g.setFont (theme::font (12.0f));
        g.drawText (juce::String (juce::CharPointer_UTF8 ("Loading the equipment database…")), listArea.removeFromBottom (28), juce::Justification::centred, false);
    }
    else if (results.empty())
    {
        auto a = listArea.withSizeKeepingCentre (juce::jmin (360, listArea.getWidth() - 32), 120);
        icons::draw (g, icons::Id::search, a.removeFromTop (36).withSizeKeepingCentre (28, 28).toFloat(), theme::textMuted);
        a.removeFromTop (8);
        g.setColour (theme::text);
        g.setFont (theme::fontMedium (14.0f));
        g.drawText (search.getText().isNotEmpty() ? "Nothing matches \"" + search.getText().trim() + "\"" : "No products in this category yet",
                    a.removeFromTop (22), juce::Justification::centred, true);
        g.setColour (theme::textDim);
        g.setFont (theme::font (12.0f));
        g.drawText ("Use \"Add your own\" to enter the specs from the spec sheet - the calculations run on them just the same.",
                    a, juce::Justification::centredTop, true);
    }

    // status + detail header
    g.setColour (theme::textMuted);
    g.setFont (theme::font (11.5f));
    g.drawText (statusText, list.getBounds().withHeight (16).translated (0, -20).withTrimmedLeft (8), juce::Justification::centredLeft, false);

    auto detail = juce::Rectangle<int> (sheet.getX(), categorySeg.getY(), sheet.getWidth(), sheet.getY() - categorySeg.getY());
    if (selected != nullptr)
    {
        g.setColour (theme::textDim);
        g.setFont (theme::fontMedium (11.0f));
        g.drawText (selected->brand.toUpperCase(), detail.removeFromTop (16), juce::Justification::centredLeft, true);
        auto nameRow = detail.removeFromTop (26);
        g.setColour (theme::text);
        g.setFont (theme::fontBold (17.0f));
        const int nw = juce::jmin (nameRow.getWidth() - 24, (int) std::ceil (theme::textWidth (theme::fontBold (17.0f), selected->model)) + 2);
        g.drawText (selected->model, nameRow.removeFromLeft (nw), juce::Justification::centredLeft, true);
        if (selected->verified)
            drawVerified (g, nameRow.withTrimmedLeft (8).removeFromLeft (16).withSizeKeepingCentre (16, 16).toFloat());
        g.setColour (theme::textMuted);
        g.setFont (theme::font (11.0f));
        g.drawText (selected->verified ? "Verified against the manufacturer's data"
                    : selected->userDefined ? "Your own specs (saved in your user library)" : "Specs from a retailer or community source",
                    detail.removeFromTop (16), juce::Justification::centredLeft, true);
    }
    else
    {
        theme::drawEyebrow (g, "Spec sheet", detail.removeFromTop (14));
        g.setColour (theme::textMuted);
        g.setFont (theme::font (12.0f));
        g.drawText ("Select a product to see every published spec.", detail, juce::Justification::centredLeft, true);
    }
}

void ProductPicker::resized()
{
    auto r = getLocalBounds();
    auto head = r.removeFromTop (56);
    closeBtn.setBounds (head.removeFromRight (32).withSizeKeepingCentre (32, 32));

    if (form != nullptr)
    {
        form->setBounds (r);
        for (auto* c : std::initializer_list<juce::Component*> { &search, &categorySeg, brands.get(), &list, &sheet, &useBtn, &editBtn, &removeBtn, &addBtn })
            c->setVisible (false);
        return;
    }
    for (auto* c : std::initializer_list<juce::Component*> { &search, brands.get(), &list, &sheet, &useBtn, &editBtn, &addBtn })
        c->setVisible (true);
    categorySeg.setVisible (categories.size() > 1);
    removeBtn.setVisible (selected != nullptr && selected->userDefined);

    r.removeFromTop (gap);
    auto top = r.removeFromTop (36);
    addBtn.setBounds (top.removeFromRight (150));
    top.removeFromRight (gap2);
    search.setBounds (top);
    r.removeFromTop (gap2);

    auto detail = r.removeFromRight (320);
    r.removeFromRight (gap2);
    auto detailButtons = detail.removeFromBottom (btnH);
    useBtn.setBounds (detailButtons.removeFromLeft (170));
    detailButtons.removeFromLeft (gap);
    editBtn.setBounds (detailButtons.removeFromLeft (110));
    detail.removeFromBottom (gap);
    removeBtn.setBounds (detail.removeFromBottom (btnH).removeFromLeft (170));
    detail.removeFromBottom (gap);

    auto seg = r.removeFromTop (btnH);
    categorySeg.setBounds (seg.removeFromLeft (juce::jmin (seg.getWidth(), 120 * (int) categories.size() + 70)));
    r.removeFromTop (gap2 + 4);
    detail.removeFromTop (seg.getHeight() + gap2 + 4 + 44);
    sheet.setBounds (detail);

    brands->setBounds (r.removeFromLeft (190));
    r.removeFromLeft (gap);
    list.setBounds (r);
}
} // namespace fa::ui
