#pragma once

#include "GearKit.h"

namespace fa::ui
{
/** One calculation the Install & Gains page can run, and whether this product unlocks it. */
struct CalcCheck
{
    juce::String name;          // "Gain setting"
    bool ok = false;
    juce::String missing;       // "needs Xmax" when ! ok
};

/** Which calculations the product's specs unlock (category-specific). */
std::vector<CalcCheck> calcChecks (const gear::Product& p);

/** Live completeness meter: one row per calculation with a tick / warning. */
class CompletenessMeter : public juce::Component
{
public:
    void setProduct (const gear::Product& p) { checks = calcChecks (p); repaint(); }
    const std::vector<CalcCheck>& getChecks() const noexcept { return checks; }
    int idealHeight() const { return 8 + (int) checks.size() * rowH; }
    void paint (juce::Graphics&) override;
    static constexpr int rowH = 26;
private:
    std::vector<CalcCheck> checks;
};

/** Form to add a product that isn't in the database, or to override the specs of one that is.
    Category-specific fields (gear::specFields) grouped REQUIRED / OPTIONAL, each with unit, range
    validation, a "where to find it" hint and a "used for" line, plus a live completeness meter.
    Saves via GearDatabase::addUserProduct (same id when editing: the user copy wins, the built-in
    file stays untouched). */
class ProductForm : public juce::Component
{
public:
    /** `existing` pre-fills the form (edit mode). `allowedCategories` populates the category
        selector when creating (first = default). */
    ProductForm (std::vector<gear::Category> allowedCategories,
                 std::optional<gear::Product> existing = std::nullopt);
    ~ProductForm() override;

    std::function<void (const gear::Product&)> onSaved;
    std::function<void()> onCancel;

    static constexpr int idealWidth = 1000;
    static constexpr int idealHeight = 680;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Field;
    void rebuildFields();
    void refresh();                 // draft -> meter, validation, preview, save enabled
    int layoutForm (int width);
    void save();
    void setCategory (gear::Category c);

    std::vector<gear::Category> categories;
    gear::Product draft;
    bool editing = false;

    juce::ComboBox categoryBox;
    juce::TextEditor brandEd, modelEd, seriesEd, notesEd;
    gearkit::Caption identityCap { "Identity" };
    gearkit::Caption requiredCap { "Required - the calculations need these", theme::accent };
    gearkit::Caption optionalCap { "Optional - improves accuracy" };
    gearkit::Caption meterCap { "What this product unlocks" }, previewCap { "Preview" };
    gearkit::ScrollBody body;
    std::vector<std::unique_ptr<Field>> fields;
    CompletenessMeter meter;
    FaButton saveBtn { "Save product", FaButton::Style::primary, icons::Id::save };
    FaButton cancelBtn { "Cancel", FaButton::Style::ghost };
    juce::StringArray missing;
    juce::String errorText;
    juce::StringArray previewChips;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProductForm)
};
} // namespace fa::ui
