#pragma once

#include "GearKit.h"

namespace fa::ui
{
/** One equipment slot: empty ("Choose head unit") or showing the selected product with brand,
    model, spec chips and badges. Click opens the picker; small actions edit specs / clear. */
class SlotCard : public juce::Component
{
public:
    SlotCard (const juce::String& title, std::vector<gear::Category> categories, juce::Colour accent = theme::accent)
        : titleText (title), cats (std::move (categories)), accentCol (accent)
    {
        editBtn.setTooltip ("Edit specs");
        clearBtn.setTooltip ("Remove");
        editBtn.onClick  = [this] { if (onEdit) onEdit(); };
        clearBtn.onClick = [this] { if (onClear) onClear(); };
        addChildComponent (editBtn);
        addChildComponent (clearBtn);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    static constexpr int height = 84;

    std::function<void()> onChoose, onEdit, onClear;

    const std::vector<gear::Category>& getCategories() const noexcept { return cats; }
    const juce::String& getProductId() const noexcept { return productId; }

    void setTitle (const juce::String& t) { titleText = t; repaint(); }
    void setHint (const juce::String& h) { hintText = h; repaint(); }

    /** Looks the product up in the database (safe before it is loaded: shows the id). */
    void setProductId (const juce::String& id)
    {
        productId = id;
        product = id.isEmpty() || ! gearkit::GearLoader::ready() ? nullptr : gear::GearDatabase::instance().find (id);
        chips = product != nullptr ? gearkit::specChips (*product) : juce::StringArray();
        editBtn.setVisible (product != nullptr);
        clearBtn.setVisible (productId.isNotEmpty());
        repaint();
    }

    void setLoading (bool b, float phase = 0.0f) { loading = b; shimmer = phase; repaint(); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        const bool filled = productId.isNotEmpty();
        const auto bg = hover ? theme::bg3 : theme::bg2;

        if (filled)
        {
            theme::fillCard (g, r, bg, hover ? accentCol.withAlpha (0.6f) : theme::border, theme::radius);
            g.setColour (accentCol.withAlpha (0.9f));
            g.fillRoundedRectangle (r.getX() + 1.0f, r.getY() + 14.0f, 3.0f, r.getHeight() - 28.0f, 1.5f);
        }
        else
        {
            g.setColour (bg.withAlpha (0.6f));
            g.fillRoundedRectangle (r, theme::radius);
            juce::Path p;
            p.addRoundedRectangle (r.reduced (0.75f), theme::radius);
            g.setColour (hover ? accentCol.withAlpha (0.7f) : theme::borderHi);
            theme::drawDashedPath (g, p, 1.0f, 6.0f, 5.0f);
        }

        auto inner = getLocalBounds().reduced (gearkit::gap2, 12);
        auto iconArea = inner.removeFromLeft (44).withSizeKeepingCentre (40, 40).toFloat();
        g.setColour (filled ? accentCol.withAlpha (0.16f) : theme::bg3);
        g.fillEllipse (iconArea);
        icons::draw (g, gearkit::categoryIcon (cats.empty() ? gear::Category::headUnit : cats.front()),
                     iconArea.reduced (11.0f), filled ? accentCol : theme::textDim);
        inner.removeFromLeft (gearkit::gap2);

        if (loading)
        {
            gearkit::drawShimmer (g, inner.removeFromTop (14).withWidth (140).toFloat(), shimmer);
            inner.removeFromTop (8);
            gearkit::drawShimmer (g, inner.removeFromTop (20).withWidth (260).toFloat(), shimmer);
            return;
        }

        if (! filled)
        {
            g.setColour (theme::text);
            g.setFont (theme::fontMedium (14.0f));
            // lower-case the first letter only, so acronyms (DAC, DSP) stay upper-case
            auto chooseWhat = titleText;
            if (chooseWhat.length() > 1 && ! juce::CharacterFunctions::isUpperCase (chooseWhat[1]))
                chooseWhat = chooseWhat.substring (0, 1).toLowerCase() + chooseWhat.substring (1);
            g.drawText ("Choose " + chooseWhat, inner.removeFromTop (22), juce::Justification::centredLeft, true);
            g.setColour (theme::textMuted);
            g.setFont (theme::font (12.0f));
            g.drawText (hintText.isNotEmpty() ? hintText : "Search the database or add your own", inner.removeFromTop (18),
                        juce::Justification::centredLeft, true);
            icons::draw (g, icons::Id::plus, getLocalBounds().removeFromRight (44).withSizeKeepingCentre (18, 18).toFloat(),
                         hover ? accentCol : theme::textMuted);
            return;
        }

        auto text = inner.withTrimmedRight (72);
        auto top = text.removeFromTop (16);
        g.setColour (theme::textDim);
        g.setFont (theme::fontMedium (11.0f));
        g.drawText (titleText.toUpperCase() + (product != nullptr ? "  |  " + product->brand.toUpperCase() : juce::String()),
                    top, juce::Justification::centredLeft, true);

        auto nameRow = text.removeFromTop (22);
        const auto name = product != nullptr ? product->model : productId;
        g.setColour (theme::text);
        g.setFont (theme::fontBold (15.0f));
        const int nameW = juce::jmin (nameRow.getWidth(), (int) std::ceil (theme::textWidth (theme::fontBold (15.0f), name)) + 2);
        g.drawText (name, nameRow.removeFromLeft (nameW), juce::Justification::centredLeft, true);
        nameRow.removeFromLeft (8);
        if (product != nullptr && product->verified)
        {
            gearkit::drawVerified (g, nameRow.removeFromLeft (16).withSizeKeepingCentre (16, 16).toFloat());
            nameRow.removeFromLeft (6);
        }
        if (product != nullptr && product->userDefined)
            gearkit::drawChips (g, { "Your specs" }, nameRow.withSizeKeepingCentre (nameRow.getWidth(), 18), theme::warn, 10.0f);
        else if (product == nullptr && gearkit::GearLoader::ready())
            gearkit::drawChips (g, { "Not in database" }, nameRow.withSizeKeepingCentre (nameRow.getWidth(), 18), theme::warn, 10.0f);

        text.removeFromTop (4);
        gearkit::drawChips (g, chips, text.removeFromTop (18), theme::textDim, 10.5f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10);
        auto col = r.removeFromRight (30);
        editBtn.setBounds (col.removeFromTop (28));
        clearBtn.setBounds (col.removeFromBottom (28));
    }

    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit  (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (getLocalBounds().contains (e.getPosition()) && ! e.mouseWasDraggedSinceMouseDown() && onChoose)
            onChoose();
    }

private:
    juce::String titleText, hintText, productId;
    std::vector<gear::Category> cats;
    juce::Colour accentCol;
    const gear::Product* product = nullptr;
    juce::StringArray chips;
    bool hover = false, loading = false;
    float shimmer = 0.0f;
    FaButton editBtn { "", FaButton::Style::ghost, icons::Id::sliders };
    FaButton clearBtn { "", FaButton::Style::ghost, icons::Id::cross };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SlotCard)
};
} // namespace fa::ui
