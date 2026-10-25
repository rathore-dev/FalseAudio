#pragma once

#include "ProductForm.h"

namespace fa::ui
{
/** Scrolling spec sheet (label / value rows, grouped). */
class SpecSheetView : public juce::Component
{
public:
    SpecSheetView();
    void setProduct (const gear::Product* p);
    void resized() override;
private:
    class Body;
    juce::Viewport viewport;
    std::unique_ptr<Body> body;
};

/** Modal product picker shown in an AppShell sheet: instant search, brand browsing, result rows
    with spec chips and badges, a spec-sheet panel, "+ Add your own" and "Edit specs" (both open a
    ProductForm inside the same sheet). */
class ProductPicker : public juce::Component,
                      private juce::ListBoxModel,
                      private juce::Timer
{
public:
    /** `categories`: the categories this slot accepts (first = default filter). */
    ProductPicker (const juce::String& slotTitle, std::vector<gear::Category> categories,
                   const juce::String& currentProductId = {});
    ~ProductPicker() override;

    std::function<void (const gear::Product&)> onPicked;
    std::function<void()> onCancel;

    static constexpr int idealWidth = 1040;
    static constexpr int idealHeight = 680;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    class BrandList;
    class ResultRow;

    // ListBoxModel (results)
    int getNumRows() override { return (int) results.size(); }
    void paintListBoxItem (int, juce::Graphics&, int, int, bool) override {}
    juce::Component* refreshComponentForRow (int row, bool selected, juce::Component* existing) override;
    void selectedRowsChanged (int lastRow) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void returnKeyPressed (int row) override;

    void timerCallback() override;        // shimmer while loading
    void onDatabaseReady();
    void runSearch();
    void select (const gear::Product* p);
    void pick();
    void openForm (std::optional<gear::Product> existing);
    void closeForm (const gear::Product* saved);
    std::optional<gear::Category> activeCategory() const;

    juce::String slotTitle;
    std::vector<gear::Category> categories;
    juce::String currentId;
    bool loading = false;
    float shimmerPhase = 0.0f;

    juce::TextEditor search;
    SegmentedControl categorySeg;
    FaButton addBtn { "Add your own", FaButton::Style::secondary, icons::Id::plus };
    FaButton closeBtn { "", FaButton::Style::ghost, icons::Id::cross };
    std::unique_ptr<BrandList> brands;
    juce::ListBox list;
    std::vector<const gear::Product*> results;
    const gear::Product* selected = nullptr;

    // detail panel
    SpecSheetView sheet;
    FaButton useBtn { "Use this product", FaButton::Style::primary, icons::Id::check };
    FaButton editBtn { "Edit specs", FaButton::Style::ghost, icons::Id::sliders };
    FaButton removeBtn { "Remove user copy", FaButton::Style::ghost, icons::Id::trash };
    juce::String statusText;

    std::unique_ptr<ProductForm> form;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProductPicker)
};
} // namespace fa::ui
