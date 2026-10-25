#include "LookAndFeel.h"

namespace fa::ui
{
using namespace juce;

namespace
{
    Colour alphaFor (bool enabled, Colour c) { return enabled ? c : c.withMultipliedAlpha (0.4f); }

    void drawRoundedBorder (Graphics& g, Rectangle<float> r, Colour c, float radius, float thickness = 1.0f)
    {
        g.setColour (c);
        g.drawRoundedRectangle (r.reduced (thickness * 0.5f), radius, thickness);
    }

    /** Window-title-bar button drawn with our vector icons. */
    class TitleBarButton : public Button
    {
    public:
        TitleBarButton (const String& name, icons::Id iconToUse, bool isClose)
            : Button (name), icon (iconToUse), closeButton (isClose) {}

        void paintButton (Graphics& g, bool highlighted, bool down) override
        {
            auto r = getLocalBounds().toFloat();
            if (highlighted || down)
            {
                g.setColour (closeButton ? theme::bad.withAlpha (down ? 0.9f : 0.75f)
                                         : Colours::white.withAlpha (down ? 0.12f : 0.07f));
                g.fillRect (r);
            }
            const Colour c = (closeButton && (highlighted || down)) ? Colours::white : theme::textDim;
            icons::draw (g, icon, r.withSizeKeepingCentre (16.0f, 16.0f), c);
        }

    private:
        icons::Id icon;
        bool closeButton;
    };
}

FaLookAndFeel::FaLookAndFeel()
    : LookAndFeel_V4 (ColourScheme (theme::bg0,      // windowBackground
                                    theme::bg2,      // widgetBackground
                                    theme::bg3,      // menuBackground
                                    theme::border,   // outline
                                    theme::text,     // defaultText
                                    theme::bg4,      // defaultFill
                                    theme::text,     // highlightedText
                                    theme::accent,   // highlightedFill
                                    theme::text))    // menuText
{
    setDefaultSansSerifTypefaceName ("Segoe UI");
    applyColours();
}

void FaLookAndFeel::applyColours()
{
    const auto transparent = Colours::transparentBlack;

    setColour (ResizableWindow::backgroundColourId, theme::bg0);
    setColour (DocumentWindow::textColourId, theme::text);

    setColour (TextButton::buttonColourId, theme::bg3);
    setColour (TextButton::buttonOnColourId, theme::accent);
    setColour (TextButton::textColourOffId, theme::text);
    setColour (TextButton::textColourOnId, theme::onAccent);
    setColour (ToggleButton::textColourId, theme::text);
    setColour (ToggleButton::tickColourId, theme::accent);
    setColour (ToggleButton::tickDisabledColourId, theme::textMuted);
    setColour (DrawableButton::backgroundColourId, transparent);
    setColour (DrawableButton::backgroundOnColourId, theme::accent.withAlpha (0.18f));
    setColour (DrawableButton::textColourId, theme::textDim);
    setColour (DrawableButton::textColourOnId, theme::text);
    setColour (HyperlinkButton::textColourId, theme::accent);

    setColour (ComboBox::backgroundColourId, theme::bg2);
    setColour (ComboBox::textColourId, theme::text);
    setColour (ComboBox::outlineColourId, theme::border);
    setColour (ComboBox::buttonColourId, theme::bg3);
    setColour (ComboBox::arrowColourId, theme::textDim);
    setColour (ComboBox::focusedOutlineColourId, theme::accent);

    // A not-quite-opaque menu background makes JUCE create a transparent window so the corners can be rounded.
    setColour (PopupMenu::backgroundColourId, theme::bg3.withAlpha (0.995f));
    setColour (PopupMenu::textColourId, theme::text);
    setColour (PopupMenu::headerTextColourId, theme::textMuted);
    setColour (PopupMenu::highlightedBackgroundColourId, theme::accent.withAlpha (0.14f));
    setColour (PopupMenu::highlightedTextColourId, theme::text);

    setColour (TextEditor::backgroundColourId, theme::bg1);
    setColour (TextEditor::textColourId, theme::text);
    setColour (TextEditor::highlightColourId, theme::accent.withAlpha (0.28f));
    setColour (TextEditor::highlightedTextColourId, theme::text);
    setColour (TextEditor::outlineColourId, theme::border);
    setColour (TextEditor::focusedOutlineColourId, theme::accent);
    setColour (TextEditor::shadowColourId, transparent);
    setColour (CaretComponent::caretColourId, theme::accent);

    setColour (Label::textColourId, theme::text);
    setColour (Label::backgroundColourId, transparent);
    setColour (Label::outlineColourId, transparent);
    setColour (Label::textWhenEditingColourId, theme::text);
    setColour (Label::backgroundWhenEditingColourId, theme::bg1);
    setColour (Label::outlineWhenEditingColourId, theme::accent);

    setColour (Slider::backgroundColourId, theme::bg4);
    setColour (Slider::trackColourId, theme::accent);
    setColour (Slider::thumbColourId, theme::text);
    setColour (Slider::rotarySliderFillColourId, theme::accent);
    setColour (Slider::rotarySliderOutlineColourId, theme::bg4);
    setColour (Slider::textBoxTextColourId, theme::text);
    setColour (Slider::textBoxBackgroundColourId, theme::bg1);
    setColour (Slider::textBoxHighlightColourId, theme::accent.withAlpha (0.28f));
    setColour (Slider::textBoxOutlineColourId, theme::border);

    setColour (ScrollBar::backgroundColourId, transparent);
    setColour (ScrollBar::thumbColourId, theme::borderHi);
    setColour (ScrollBar::trackColourId, transparent);

    setColour (ListBox::backgroundColourId, theme::bg2);
    setColour (ListBox::outlineColourId, theme::border);
    setColour (ListBox::textColourId, theme::text);
    setColour (TableHeaderComponent::textColourId, theme::textDim);
    setColour (TableHeaderComponent::backgroundColourId, theme::bg3);
    setColour (TableHeaderComponent::outlineColourId, theme::border);
    setColour (TableHeaderComponent::highlightColourId, theme::bg4);
    setColour (TableListBox::backgroundColourId, theme::bg2);
    setColour (TreeView::backgroundColourId, transparent);
    setColour (TreeView::linesColourId, theme::border);
    setColour (TreeView::selectedItemBackgroundColourId, theme::accent.withAlpha (0.16f));
    setColour (TreeView::evenItemsColourId, transparent);
    setColour (TreeView::oddItemsColourId, transparent);

    setColour (TooltipWindow::backgroundColourId, theme::bg4);
    setColour (TooltipWindow::textColourId, theme::text);
    setColour (TooltipWindow::outlineColourId, theme::borderHi);

    setColour (AlertWindow::backgroundColourId, theme::bg2);
    setColour (AlertWindow::textColourId, theme::text);
    setColour (AlertWindow::outlineColourId, theme::border);

    setColour (TabbedComponent::backgroundColourId, theme::bg1);
    setColour (TabbedComponent::outlineColourId, theme::border);
    setColour (TabbedButtonBar::tabOutlineColourId, theme::border);
    setColour (TabbedButtonBar::tabTextColourId, theme::textDim);
    setColour (TabbedButtonBar::frontOutlineColourId, theme::accent);
    setColour (TabbedButtonBar::frontTextColourId, theme::text);

    setColour (ProgressBar::backgroundColourId, theme::bg4);
    setColour (ProgressBar::foregroundColourId, theme::accent);

    setColour (GroupComponent::outlineColourId, theme::border);
    setColour (GroupComponent::textColourId, theme::textDim);

    setColour (BubbleComponent::backgroundColourId, theme::bg4);
    setColour (BubbleComponent::outlineColourId, theme::borderHi);

    setColour (PropertyComponent::backgroundColourId, theme::bg2);
    setColour (PropertyComponent::labelTextColourId, theme::textDim);
    setColour (TextPropertyComponent::backgroundColourId, theme::bg1);
    setColour (TextPropertyComponent::textColourId, theme::text);
    setColour (TextPropertyComponent::outlineColourId, theme::border);
    setColour (BooleanPropertyComponent::backgroundColourId, theme::bg1);
    setColour (BooleanPropertyComponent::outlineColourId, theme::border);

    setColour (FileBrowserComponent::currentPathBoxBackgroundColourId, theme::bg1);
    setColour (FileBrowserComponent::currentPathBoxTextColourId, theme::text);
    setColour (FileBrowserComponent::currentPathBoxArrowColourId, theme::textDim);
    setColour (FileBrowserComponent::filenameBoxBackgroundColourId, theme::bg1);
    setColour (FileBrowserComponent::filenameBoxTextColourId, theme::text);
    setColour (FileChooserDialogBox::titleTextColourId, theme::text);
    setColour (DirectoryContentsDisplayComponent::highlightColourId, theme::accent.withAlpha (0.16f));
    setColour (DirectoryContentsDisplayComponent::textColourId, theme::text);
    setColour (DirectoryContentsDisplayComponent::highlightedTextColourId, theme::text);
    setColour (FileSearchPathListComponent::backgroundColourId, theme::bg2);

    setColour (SidePanel::backgroundColour, theme::bg2);
    setColour (SidePanel::titleTextColour, theme::text);
    setColour (SidePanel::dismissButtonNormalColour, theme::textDim);
    setColour (SidePanel::dismissButtonOverColour, theme::text);
    setColour (SidePanel::dismissButtonDownColour, theme::accent);

    setColour (ColourSelector::backgroundColourId, theme::bg2);
    setColour (ColourSelector::labelTextColourId, theme::textDim);
    setColour (KeyMappingEditorComponent::backgroundColourId, theme::bg2);
    setColour (KeyMappingEditorComponent::textColourId, theme::text);
    setColour (CodeEditorComponent::backgroundColourId, theme::bg1);
    setColour (CodeEditorComponent::defaultTextColourId, theme::text);
    setColour (CodeEditorComponent::lineNumberBackgroundId, theme::bg2);
    setColour (CodeEditorComponent::lineNumberTextId, theme::textMuted);
    setColour (CodeEditorComponent::highlightColourId, theme::accent.withAlpha (0.25f));
    setColour (Toolbar::backgroundColourId, theme::bg2);
    setColour (Toolbar::separatorColourId, theme::border);
    setColour (Toolbar::buttonMouseOverBackgroundColourId, theme::bg3);
    setColour (Toolbar::buttonMouseDownBackgroundColourId, theme::bg4);
    setColour (Toolbar::labelTextColourId, theme::textDim);
    setColour (Toolbar::editingModeOutlineColourId, theme::accent);
    setColour (MidiKeyboardComponent::whiteNoteColourId, theme::text);
    setColour (MidiKeyboardComponent::blackNoteColourId, theme::bg0);
    setColour (TooltipWindow::backgroundColourId, theme::bg4);
    setColour (0x1000440 /* LassoComponent::lassoFillColourId */, theme::accent.withAlpha (0.1f));
    setColour (0x1000441 /* LassoComponent::lassoOutlineColourId */, theme::accent);
}

// =============================================================================
// Buttons
// =============================================================================
void FaLookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour& backgroundColour,
                                          bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    const float rad = jmin (theme::radiusSm, r.getHeight() * 0.5f);

    Colour fill = on ? b.findColour (TextButton::buttonOnColourId) : backgroundColour;
    if (down)             fill = on ? fill.darker (0.15f) : fill.brighter (0.18f);
    else if (highlighted) fill = on ? fill.brighter (0.08f) : fill.brighter (0.1f);

    if (on && b.isEnabled())
        theme::drawGlow (g, r, fill, rad, 6.0f);

    g.setColour (alphaFor (b.isEnabled(), fill));
    g.fillRoundedRectangle (r, rad);

    // depth: hairline highlight on top, border all round
    g.setColour (Colours::white.withAlpha (on ? 0.12f : 0.04f));
    g.drawRoundedRectangle (r.reduced (1.0f), rad - 1.0f, 1.0f);
    drawRoundedBorder (g, r, alphaFor (b.isEnabled(), on ? fill.brighter (0.25f) : (highlighted ? theme::borderHi : theme::border)), rad);

    if (b.hasKeyboardFocus (false) && b.isEnabled())
        theme::drawFocusRing (g, r, rad);
}

Font FaLookAndFeel::getTextButtonFont (TextButton&, int buttonHeight)
{
    return theme::fontMedium (jlimit (11.0f, 14.0f, (float) buttonHeight * 0.42f));
}

int FaLookAndFeel::getTextButtonWidthToFitText (TextButton& b, int buttonHeight)
{
    return (int) std::ceil (theme::textWidth (getTextButtonFont (b, buttonHeight), b.getButtonText())) + 28;
}

void FaLookAndFeel::drawButtonText (Graphics& g, TextButton& b, bool highlighted, bool)
{
    const auto font = getTextButtonFont (b, b.getHeight());
    g.setFont (font);
    Colour c = b.findColour (b.getToggleState() ? TextButton::textColourOnId : TextButton::textColourOffId);
    if (highlighted && ! b.getToggleState())
        c = c.brighter (0.1f);
    g.setColour (alphaFor (b.isEnabled(), c));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (8, 2), Justification::centred, 2, 0.9f);
}

void FaLookAndFeel::drawToggleButton (Graphics& g, ToggleButton& b, bool highlighted, bool down)
{
    const float boxSize = 18.0f;
    auto r = b.getLocalBounds().toFloat();
    const float y = r.getCentreY() - boxSize * 0.5f;
    drawTickBox (g, b, 2.0f, y, boxSize, boxSize, b.getToggleState(), b.isEnabled(), highlighted, down);

    g.setColour (alphaFor (b.isEnabled(), b.findColour (ToggleButton::textColourId)));
    g.setFont (theme::font (13.0f));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().withTrimmedLeft ((int) boxSize + 10).withTrimmedRight (2),
                      Justification::centredLeft, 2);

    if (b.hasKeyboardFocus (false) && b.isEnabled())
        theme::drawFocusRing (g, Rectangle<float> (2.0f, y, boxSize, boxSize), 5.0f);
}

void FaLookAndFeel::drawTickBox (Graphics& g, Component& c, float x, float y, float w, float h,
                                 bool ticked, bool isEnabled, bool highlighted, bool down)
{
    ignoreUnused (down);
    Rectangle<float> r (x, y, w, h);
    const float rad = 5.0f;
    const Colour accent = c.findColour (ToggleButton::tickColourId);

    if (ticked)
    {
        if (isEnabled)
            theme::drawGlow (g, r, accent, rad, 4.0f);
        g.setColour (alphaFor (isEnabled, accent));
        g.fillRoundedRectangle (r, rad);
        icons::draw (g, icons::Id::check, r.reduced (3.0f), alphaFor (isEnabled, theme::onAccent));
    }
    else
    {
        g.setColour (alphaFor (isEnabled, highlighted ? theme::bg3 : theme::bg1));
        g.fillRoundedRectangle (r, rad);
        drawRoundedBorder (g, r, alphaFor (isEnabled, highlighted ? theme::borderHi : theme::border), rad);
    }
}

void FaLookAndFeel::drawDrawableButton (Graphics& g, DrawableButton& b, bool highlighted, bool down)
{
    const bool on = b.getToggleState();
    auto r = b.getLocalBounds().toFloat();
    const float rad = theme::radiusSm;

    if (on || highlighted || down)
    {
        g.setColour (on ? b.findColour (DrawableButton::backgroundOnColourId)
                        : Colours::white.withAlpha (down ? 0.1f : 0.06f));
        g.fillRoundedRectangle (r, rad);
    }

    if (b.getStyle() != DrawableButton::ImageFitted && b.getStyle() != DrawableButton::ImageRaw
        && b.getStyle() != DrawableButton::ImageStretched && b.getStyle() != DrawableButton::ImageOnButtonBackground
        && b.getStyle() != DrawableButton::ImageOnButtonBackgroundOriginalSize)
    {
        g.setColour (alphaFor (b.isEnabled(), b.findColour (on ? DrawableButton::textColourOnId : DrawableButton::textColourId)));
        g.setFont (theme::fontMedium (11.0f));
        const int textH = jmin (16, b.proportionOfHeight (0.25f));
        g.drawFittedText (b.getButtonText(), 2, b.getHeight() - textH - 1, b.getWidth() - 4, textH, Justification::centred, 1);
    }
}

Path FaLookAndFeel::getTickShape (float height)
{
    auto p = icons::get (icons::Id::check);
    p.scaleToFit (0.0f, 0.0f, height, height, true);
    return p;
}

Path FaLookAndFeel::getCrossShape (float height)
{
    auto p = icons::get (icons::Id::cross);
    p.scaleToFit (0.0f, 0.0f, height, height, true);
    return p;
}

// =============================================================================
// ComboBox & menus
// =============================================================================
void FaLookAndFeel::drawComboBox (Graphics& g, int width, int height, bool isButtonDown,
                                  int, int, int, int, ComboBox& box)
{
    Rectangle<float> r (0.0f, 0.0f, (float) width, (float) height);
    r.reduce (0.5f, 0.5f);
    const float rad = jmin (theme::radiusSm, r.getHeight() * 0.5f);
    const bool hover = box.isMouseOver (true) || isButtonDown;
    const bool focused = box.hasKeyboardFocus (true) || isButtonDown;

    g.setColour (alphaFor (box.isEnabled(), hover ? theme::bg3 : box.findColour (ComboBox::backgroundColourId)));
    g.fillRoundedRectangle (r, rad);
    g.setColour (Colours::white.withAlpha (0.03f));
    g.drawRoundedRectangle (r.reduced (1.0f), rad - 1.0f, 1.0f);
    drawRoundedBorder (g, r, alphaFor (box.isEnabled(), focused ? box.findColour (ComboBox::focusedOutlineColourId)
                                                                : (hover ? theme::borderHi : box.findColour (ComboBox::outlineColourId))), rad);

    const float iconSize = 14.0f;
    Rectangle<float> arrow (r.getRight() - iconSize - 9.0f, r.getCentreY() - iconSize * 0.5f, iconSize, iconSize);
    icons::draw (g, icons::Id::chevronDown, arrow, alphaFor (box.isEnabled(), focused ? theme::accent : box.findColour (ComboBox::arrowColourId)));
}

Font FaLookAndFeel::getComboBoxFont (ComboBox& box)
{
    return theme::font (jlimit (11.0f, 13.5f, (float) box.getHeight() * 0.42f));
}

void FaLookAndFeel::positionComboBoxText (ComboBox& box, Label& label)
{
    label.setBounds (8, 1, box.getWidth() - 36, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

PopupMenu::Options FaLookAndFeel::getOptionsForComboBoxPopupMenu (ComboBox& box, Label& label)
{
    return PopupMenu::Options().withTargetComponent (&box)
                               .withItemThatMustBeVisible (box.getSelectedId())
                               .withInitiallySelectedItem (box.getSelectedId())
                               .withMinimumWidth (box.getWidth())
                               .withMaximumNumColumns (1)
                               .withStandardItemHeight (28)
                               .withParentComponent (nullptr)
                               .withPreferredPopupDirection (PopupMenu::Options::PopupDirection::downwards)
                               .withMousePosition()
                               .withTargetScreenArea (box.getScreenBounds())
                               .withDeletionCheck (label);
}

void FaLookAndFeel::drawPopupMenuBackgroundWithOptions (Graphics& g, int width, int height, const PopupMenu::Options&)
{
    Rectangle<float> r (0.0f, 0.0f, (float) width, (float) height);
    const float rad = 8.0f;
    g.setColour (theme::bg3);
    g.fillRoundedRectangle (r, rad);
    g.setColour (Colours::white.withAlpha (0.035f));
    g.drawRoundedRectangle (r.reduced (1.5f), rad - 1.0f, 1.0f);
    drawRoundedBorder (g, r, theme::borderHi, rad);
}

void FaLookAndFeel::drawPopupMenuItemWithOptions (Graphics& g, const Rectangle<int>& area, bool isHighlighted,
                                                  const PopupMenu::Item& item, const PopupMenu::Options& options)
{
    if (item.isSeparator)
    {
        auto r = area.reduced (10, 0);
        g.setColour (theme::border);
        g.fillRect (r.removeFromTop (area.getHeight() / 2 + 1).removeFromBottom (1));
        return;
    }

    if (item.isSectionHeader)
    {
        drawPopupMenuSectionHeaderWithOptions (g, area, item.text, options);
        return;
    }

    const bool enabled = item.isEnabled;
    auto r = area.reduced (4, 0);
    const Colour textColour = item.colour.isTransparent() ? findColour (PopupMenu::textColourId) : item.colour;

    if (isHighlighted && enabled)
    {
        g.setColour (findColour (PopupMenu::highlightedBackgroundColourId));
        g.fillRoundedRectangle (r.toFloat().reduced (0.0f, 1.0f), 5.0f);
    }

    auto content = r.reduced (8, 0);
    const int tickW = 18;
    auto tickArea = content.removeFromLeft (tickW);
    content.removeFromLeft (6);

    if (item.isTicked)
        icons::draw (g, icons::Id::check, tickArea.toFloat().withSizeKeepingCentre (14.0f, 14.0f), theme::accent);
    else if (item.image != nullptr)
        item.image->drawWithin (g, tickArea.toFloat().withSizeKeepingCentre (16.0f, 16.0f),
                                RectanglePlacement::centred, enabled ? 1.0f : 0.4f);

    if (item.subMenu != nullptr)
    {
        auto arrowArea = content.removeFromRight (16);
        icons::draw (g, icons::Id::chevronRight, arrowArea.toFloat().withSizeKeepingCentre (14.0f, 14.0f),
                     alphaFor (enabled, theme::textDim));
    }

    g.setFont (getPopupMenuFont());
    if (item.shortcutKeyDescription.isNotEmpty())
    {
        const auto f = theme::mono (11.0f);
        const int w = (int) std::ceil (theme::textWidth (f, item.shortcutKeyDescription)) + 8;
        auto scArea = content.removeFromRight (w);
        g.setFont (f);
        g.setColour (alphaFor (enabled, theme::textMuted));
        g.drawText (item.shortcutKeyDescription, scArea, Justification::centredRight, true);
        g.setFont (getPopupMenuFont());
    }

    g.setColour (alphaFor (enabled, isHighlighted ? findColour (PopupMenu::highlightedTextColourId) : textColour));
    g.drawFittedText (item.text, content, Justification::centredLeft, 1);
}

void FaLookAndFeel::drawPopupMenuSectionHeaderWithOptions (Graphics& g, const Rectangle<int>& area,
                                                           const String& sectionName, const PopupMenu::Options&)
{
    theme::drawEyebrow (g, sectionName, area.reduced (12, 0).withTrimmedTop (4), theme::textMuted);
}

void FaLookAndFeel::getIdealPopupMenuItemSizeWithOptions (const String& text, bool isSeparator, int standardMenuItemHeight,
                                                          int& idealWidth, int& idealHeight, const PopupMenu::Options&)
{
    if (isSeparator)
    {
        idealWidth = 40;
        idealHeight = 9;
        return;
    }

    idealHeight = standardMenuItemHeight > 0 ? standardMenuItemHeight : 28;
    idealWidth = (int) std::ceil (theme::textWidth (getPopupMenuFont(), text)) + 64;
}

Font FaLookAndFeel::getPopupMenuFont()                                           { return theme::font (13.0f); }
int  FaLookAndFeel::getPopupMenuBorderSizeWithOptions (const PopupMenu::Options&) { return 6; }

void FaLookAndFeel::drawPopupMenuUpDownArrowWithOptions (Graphics& g, int width, int height, bool isScrollUpArrow,
                                                         const PopupMenu::Options&)
{
    Rectangle<float> r (0.0f, 0.0f, (float) width, (float) height);
    icons::draw (g, isScrollUpArrow ? icons::Id::chevronDown : icons::Id::chevronDown,
                 r.withSizeKeepingCentre (14.0f, 14.0f), theme::textDim);
    if (isScrollUpArrow)
    {
        // flip for "up"
        g.setColour (theme::bg3);
        g.fillRect (r);
        auto p = icons::get (icons::Id::chevronDown);
        p.applyTransform (AffineTransform::verticalFlip (24.0f));
        g.setColour (theme::textDim);
        g.fillPath (p, AffineTransform::scale (14.0f / 24.0f).translated (r.getCentreX() - 7.0f, r.getCentreY() - 7.0f));
    }
}

void FaLookAndFeel::drawMenuBarBackground (Graphics& g, int width, int height, bool, MenuBarComponent&)
{
    g.fillAll (theme::bg0);
    g.setColour (theme::border);
    g.fillRect (0, height - 1, width, 1);
}

void FaLookAndFeel::drawMenuBarItem (Graphics& g, int width, int height, int, const String& itemText,
                                     bool isMouseOverItem, bool isMenuOpen, bool, MenuBarComponent& bar)
{
    if (isMenuOpen || isMouseOverItem)
    {
        g.setColour (isMenuOpen ? theme::bg4 : theme::bg3);
        g.fillRoundedRectangle (Rectangle<float> (2.0f, 3.0f, (float) width - 4.0f, (float) height - 6.0f), theme::radiusSm);
    }
    g.setColour (alphaFor (bar.isEnabled(), theme::text));
    g.setFont (theme::font (13.0f));
    g.drawFittedText (itemText, 0, 0, width, height, Justification::centred, 1);
}

// =============================================================================
// Sliders
// =============================================================================
int FaLookAndFeel::getSliderThumbRadius (Slider& s)
{
    return jmin (7, s.isHorizontal() ? s.getHeight() / 2 : s.getWidth() / 2);
}

void FaLookAndFeel::drawLinearSlider (Graphics& g, int x, int y, int width, int height, float sliderPos,
                                      float minSliderPos, float maxSliderPos, Slider::SliderStyle style, Slider& s)
{
    const bool enabled = s.isEnabled();
    const Colour trackC = alphaFor (enabled, s.findColour (Slider::trackColourId));
    const Colour bgC    = alphaFor (enabled, s.findColour (Slider::backgroundColourId));
    const bool active   = enabled && (s.isMouseOverOrDragging() || s.hasKeyboardFocus (false));

    if (s.isBar())
    {
        Rectangle<float> r ((float) x, (float) y, (float) width, (float) height);
        const float rad = jmin (theme::radiusSm, r.getHeight() * 0.5f);
        g.setColour (bgC);
        g.fillRoundedRectangle (r, rad);

        Rectangle<float> fill = style == Slider::LinearBar
            ? Rectangle<float> ((float) x, (float) y, sliderPos - (float) x, (float) height)
            : Rectangle<float> ((float) x, sliderPos, (float) width, (float) y + (float) height - sliderPos);
        g.saveState();
        g.reduceClipRegion (fill.toNearestInt());
        g.setColour (trackC.withAlpha (0.85f));
        g.fillRoundedRectangle (r, rad);
        g.restoreState();
        drawRoundedBorder (g, r, active ? theme::borderHi : theme::border, rad);
        return;
    }

    const bool horizontal = s.isHorizontal();
    const float trackThickness = 4.0f;
    const float radius = (float) getSliderThumbRadius (s);

    Point<float> start, end;
    if (horizontal)
    {
        start = { (float) x, (float) y + (float) height * 0.5f };
        end   = { (float) x + (float) width, start.y };
    }
    else
    {
        start = { (float) x + (float) width * 0.5f, (float) y + (float) height };
        end   = { start.x, (float) y };
    }

    // track
    {
        Path p;
        p.startNewSubPath (start);
        p.lineTo (end);
        g.setColour (bgC);
        g.strokePath (p, PathStrokeType (trackThickness, PathStrokeType::curved, PathStrokeType::rounded));
    }

    // filled range
    {
        Point<float> a = start, b;
        if (s.isTwoValue() || s.isThreeValue())
        {
            a = horizontal ? Point<float> (minSliderPos, start.y) : Point<float> (start.x, minSliderPos);
            b = horizontal ? Point<float> (maxSliderPos, start.y) : Point<float> (start.x, maxSliderPos);
        }
        else
        {
            // fill from zero when the range crosses zero (gain sliders), else from the minimum
            const double minV = s.getMinimum(), maxV = s.getMaximum();
            if (minV < 0.0 && maxV > 0.0)
            {
                const float zeroPos = (float) s.valueToProportionOfLength (0.0);
                a = horizontal ? Point<float> ((float) x + zeroPos * (float) width, start.y)
                               : Point<float> (start.x, (float) y + (float) height - zeroPos * (float) height);
            }
            b = horizontal ? Point<float> (sliderPos, start.y) : Point<float> (start.x, sliderPos);
        }
        Path p;
        p.startNewSubPath (a);
        p.lineTo (b);
        if (active)
        {
            g.setColour (trackC.withAlpha (0.25f));
            g.strokePath (p, PathStrokeType (trackThickness + 6.0f, PathStrokeType::curved, PathStrokeType::rounded));
        }
        g.setColour (trackC);
        g.strokePath (p, PathStrokeType (trackThickness, PathStrokeType::curved, PathStrokeType::rounded));
    }

    auto drawThumb = [&] (Point<float> c)
    {
        const float r = radius;
        if (active)
            theme::drawGlow (g, Rectangle<float> (c.x - r, c.y - r, r * 2, r * 2), trackC, r, 5.0f);
        g.setColour (alphaFor (enabled, s.findColour (Slider::thumbColourId)));
        g.fillEllipse (c.x - r, c.y - r, r * 2, r * 2);
        g.setColour (trackC.withAlpha (enabled ? 1.0f : 0.4f));
        g.drawEllipse (c.x - r + 1.0f, c.y - r + 1.0f, r * 2 - 2.0f, r * 2 - 2.0f, 2.0f);
    };

    if (s.isTwoValue() || s.isThreeValue())
    {
        drawThumb (horizontal ? Point<float> (minSliderPos, start.y) : Point<float> (start.x, minSliderPos));
        drawThumb (horizontal ? Point<float> (maxSliderPos, start.y) : Point<float> (start.x, maxSliderPos));
        if (s.isThreeValue())
            drawThumb (horizontal ? Point<float> (sliderPos, start.y) : Point<float> (start.x, sliderPos));
    }
    else
    {
        drawThumb (horizontal ? Point<float> (sliderPos, start.y) : Point<float> (start.x, sliderPos));
    }
}

void FaLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int width, int height, float pos,
                                      float startAngle, float endAngle, Slider& s)
{
    const bool enabled = s.isEnabled();
    const bool active  = enabled && (s.isMouseOverOrDragging() || s.hasKeyboardFocus (false));
    auto bounds = Rectangle<int> (x, y, width, height).toFloat().reduced (6.0f);
    const float radius = jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float arcW = jlimit (3.0f, 5.0f, radius * 0.12f);
    const float arcR = radius - arcW * 0.5f;
    const float angle = startAngle + pos * (endAngle - startAngle);

    const Colour fillC = alphaFor (enabled, s.findColour (Slider::rotarySliderFillColourId));
    const Colour outlineC = alphaFor (enabled, s.findColour (Slider::rotarySliderOutlineColourId));

    Path track;
    track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (outlineC);
    g.strokePath (track, PathStrokeType (arcW, PathStrokeType::curved, PathStrokeType::rounded));

    // fill from zero for bipolar ranges
    float fromAngle = startAngle;
    if (s.getMinimum() < 0.0 && s.getMaximum() > 0.0)
        fromAngle = startAngle + (float) s.valueToProportionOfLength (0.0) * (endAngle - startAngle);

    Path fill;
    fill.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, jmin (fromAngle, angle), jmax (fromAngle, angle), true);
    if (active)
    {
        g.setColour (fillC.withAlpha (0.25f));
        g.strokePath (fill, PathStrokeType (arcW + 6.0f, PathStrokeType::curved, PathStrokeType::rounded));
    }
    g.setColour (fillC);
    g.strokePath (fill, PathStrokeType (arcW, PathStrokeType::curved, PathStrokeType::rounded));

    // knob body
    const float knobR = radius - arcW - 5.0f;
    if (knobR > 4.0f)
    {
        Rectangle<float> knob (centre.x - knobR, centre.y - knobR, knobR * 2, knobR * 2);
        g.setColour (alphaFor (enabled, active ? theme::bg4 : theme::bg3));
        g.fillEllipse (knob);
        g.setColour (alphaFor (enabled, theme::borderHi));
        g.drawEllipse (knob.reduced (0.5f), 1.0f);

        // pointer
        const float len = knobR * 0.55f;
        Point<float> tip (centre.x + (knobR - 4.0f) * std::sin (angle), centre.y - (knobR - 4.0f) * std::cos (angle));
        Point<float> base (centre.x + (knobR - 4.0f - len) * std::sin (angle), centre.y - (knobR - 4.0f - len) * std::cos (angle));
        Path pointer;
        pointer.startNewSubPath (base);
        pointer.lineTo (tip);
        g.setColour (alphaFor (enabled, theme::text));
        g.strokePath (pointer, PathStrokeType (2.5f, PathStrokeType::curved, PathStrokeType::rounded));
    }

    // tip dot on the arc
    Point<float> dot (centre.x + arcR * std::sin (angle), centre.y - arcR * std::cos (angle));
    g.setColour (alphaFor (enabled, theme::text));
    g.fillEllipse (dot.x - arcW * 0.6f, dot.y - arcW * 0.6f, arcW * 1.2f, arcW * 1.2f);
}

Label* FaLookAndFeel::createSliderTextBox (Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (theme::mono (12.0f));
    l->setJustificationType (Justification::centred);
    l->setColour (Label::textColourId, theme::text);
    l->setColour (Label::backgroundColourId, theme::bg1);
    l->setColour (Label::outlineColourId, theme::border);
    l->setColour (TextEditor::textColourId, theme::text);
    l->setColour (TextEditor::backgroundColourId, theme::bg1);
    l->setColour (TextEditor::highlightColourId, theme::accent.withAlpha (0.28f));
    l->setColour (TextEditor::focusedOutlineColourId, theme::accent);
    return l;
}

Font FaLookAndFeel::getSliderPopupFont (Slider&) { return theme::mono (12.0f); }

Slider::SliderLayout FaLookAndFeel::getSliderLayout (Slider& s)
{
    auto layout = LookAndFeel_V4::getSliderLayout (s);
    if (! layout.textBoxBounds.isEmpty() && s.isHorizontal())
        layout.textBoxBounds = layout.textBoxBounds.withSizeKeepingCentre (layout.textBoxBounds.getWidth(),
                                                                          jmin (24, layout.textBoxBounds.getHeight()));
    return layout;
}

// =============================================================================
// Text
// =============================================================================
void FaLookAndFeel::fillTextEditorBackground (Graphics& g, int width, int height, TextEditor& e)
{
    Rectangle<float> r (0.0f, 0.0f, (float) width, (float) height);
    const float rad = jmin (theme::radiusSm, r.getHeight() * 0.5f);
    g.setColour (alphaFor (e.isEnabled(), e.findColour (TextEditor::backgroundColourId)));
    g.fillRoundedRectangle (r, rad);
}

void FaLookAndFeel::drawTextEditorOutline (Graphics& g, int width, int height, TextEditor& e)
{
    if (dynamic_cast<AlertWindow*> (e.getParentComponent()) != nullptr)
        return;

    Rectangle<float> r (0.0f, 0.0f, (float) width, (float) height);
    const float rad = jmin (theme::radiusSm, r.getHeight() * 0.5f);
    const bool focused = e.hasKeyboardFocus (true) && ! e.isReadOnly();
    drawRoundedBorder (g, r, alphaFor (e.isEnabled(), focused ? e.findColour (TextEditor::focusedOutlineColourId)
                                                              : e.findColour (TextEditor::outlineColourId)), rad);
    if (focused)
        theme::drawFocusRing (g, r.reduced (1.0f), rad);
}

void FaLookAndFeel::drawLabel (Graphics& g, Label& label)
{
    g.fillAll (label.findColour (Label::backgroundColourId));

    if (! label.isBeingEdited())
    {
        const float alpha = label.isEnabled() ? 1.0f : 0.5f;
        const Font font (getLabelFont (label));
        g.setColour (label.findColour (Label::textColourId).withMultipliedAlpha (alpha));
        g.setFont (font);
        auto textArea = getLabelBorderSize (label).subtractedFrom (label.getLocalBounds());
        g.drawFittedText (label.getText(), textArea, label.getJustificationType(),
                          jmax (1, (int) ((float) textArea.getHeight() / font.getHeight())),
                          label.getMinimumHorizontalScale());

        const auto outline = label.findColour (Label::outlineColourId);
        if (! outline.isTransparent())
        {
            g.setColour (outline.withMultipliedAlpha (alpha));
            g.drawRoundedRectangle (label.getLocalBounds().toFloat().reduced (0.5f), theme::radiusSm, 1.0f);
        }
    }
    else if (label.isEnabled())
    {
        g.setColour (label.findColour (Label::outlineColourId));
        g.drawRoundedRectangle (label.getLocalBounds().toFloat().reduced (0.5f), theme::radiusSm, 1.0f);
    }
}

Font FaLookAndFeel::getLabelFont (Label& label)
{
    return label.getFont();
}

// =============================================================================
// Scrollbars, tables, trees
// =============================================================================
void FaLookAndFeel::drawScrollbar (Graphics& g, ScrollBar& bar, int x, int y, int width, int height, bool isVertical,
                                   int thumbStartPosition, int thumbSize, bool isMouseOver, bool isMouseDown)
{
    ignoreUnused (bar);
    Rectangle<float> thumb = isVertical ? Rectangle<float> ((float) x, (float) thumbStartPosition, (float) width, (float) thumbSize)
                                        : Rectangle<float> ((float) thumbStartPosition, (float) y, (float) thumbSize, (float) height);
    if (thumbSize <= 0)
        return;

    const float inset = (isMouseOver || isMouseDown) ? 2.0f : 3.0f;
    thumb = isVertical ? thumb.reduced (inset, 1.0f) : thumb.reduced (1.0f, inset);
    g.setColour (isMouseDown ? theme::accentDim : (isMouseOver ? theme::textMuted : bar.findColour (ScrollBar::thumbColourId)));
    g.fillRoundedRectangle (thumb, jmin (thumb.getWidth(), thumb.getHeight()) * 0.5f);
}

void FaLookAndFeel::drawTableHeaderBackground (Graphics& g, TableHeaderComponent& header)
{
    auto r = header.getLocalBounds();
    g.setColour (header.findColour (TableHeaderComponent::backgroundColourId));
    g.fillRect (r);
    g.setColour (header.findColour (TableHeaderComponent::outlineColourId));
    g.fillRect (r.removeFromBottom (1));
}

void FaLookAndFeel::drawTableHeaderColumn (Graphics& g, TableHeaderComponent& header, const String& columnName, int,
                                           int width, int height, bool isMouseOver, bool isMouseDown, int columnFlags)
{
    if (isMouseDown || isMouseOver)
    {
        g.setColour (header.findColour (TableHeaderComponent::highlightColourId).withAlpha (isMouseDown ? 1.0f : 0.6f));
        g.fillRect (0, 0, width - 1, height);
    }

    Rectangle<int> area (width, height);
    area.reduce (8, 0);

    if ((columnFlags & (TableHeaderComponent::sortedForwards | TableHeaderComponent::sortedBackwards)) != 0)
    {
        auto arrow = area.removeFromRight (12).toFloat().withSizeKeepingCentre (10.0f, 10.0f);
        auto p = icons::get (icons::Id::chevronDown);
        if ((columnFlags & TableHeaderComponent::sortedBackwards) != 0)
            p.applyTransform (AffineTransform::verticalFlip (24.0f));
        g.setColour (theme::accent);
        g.fillPath (p, p.getTransformToScaleToFit (arrow, true));
        area.removeFromRight (4);
    }

    g.setColour (header.findColour (TableHeaderComponent::textColourId));
    g.setFont (theme::fontBold (10.5f).withExtraKerningFactor (0.08f));
    g.drawFittedText (columnName.toUpperCase(), area, Justification::centredLeft, 1);

    g.setColour (header.findColour (TableHeaderComponent::outlineColourId));
    g.fillRect (width - 1, height / 4, 1, height / 2);
}

void FaLookAndFeel::drawTreeviewPlusMinusBox (Graphics& g, const Rectangle<float>& area, Colour, bool isOpen, bool isMouseOver)
{
    icons::draw (g, isOpen ? icons::Id::chevronDown : icons::Id::chevronRight,
                 area.withSizeKeepingCentre (14.0f, 14.0f), isMouseOver ? theme::text : theme::textDim);
}

// =============================================================================
// Tabs
// =============================================================================
Font FaLookAndFeel::getTabButtonFont (TabBarButton&, float height)
{
    return theme::fontMedium (jlimit (11.0f, 13.0f, height * 0.4f));
}

int FaLookAndFeel::getTabButtonBestWidth (TabBarButton& b, int tabDepth)
{
    int w = (int) std::ceil (theme::textWidth (getTabButtonFont (b, (float) tabDepth), b.getButtonText())) + 32;
    if (auto* extra = b.getExtraComponent())
        w += extra->getWidth() + 8;
    return w;
}

void FaLookAndFeel::drawTabButton (TabBarButton& b, Graphics& g, bool isMouseOver, bool isMouseDown)
{
    const auto area = b.getActiveArea();
    const bool front = b.isFrontTab();
    const auto orientation = b.getTabbedButtonBar().getOrientation();
    const bool horizontal = orientation == TabbedButtonBar::TabsAtTop || orientation == TabbedButtonBar::TabsAtBottom;
    const Colour tabColour = b.getTabBackgroundColour();

    if (isMouseOver || isMouseDown)
    {
        g.setColour (Colours::white.withAlpha (isMouseDown ? 0.06f : 0.035f));
        g.fillRoundedRectangle (area.toFloat().reduced (3.0f), theme::radiusSm);
    }

    // active indicator
    if (front)
    {
        const Colour ind = tabColour.isTransparent() || tabColour == theme::bg2 ? theme::accent : tabColour;
        Rectangle<float> line;
        switch (orientation)
        {
            case TabbedButtonBar::TabsAtTop:    line = area.toFloat().removeFromBottom (2.0f).reduced (10.0f, 0.0f); break;
            case TabbedButtonBar::TabsAtBottom: line = area.toFloat().removeFromTop (2.0f).reduced (10.0f, 0.0f); break;
            case TabbedButtonBar::TabsAtLeft:   line = area.toFloat().removeFromRight (2.0f).reduced (0.0f, 10.0f); break;
            case TabbedButtonBar::TabsAtRight:  line = area.toFloat().removeFromLeft (2.0f).reduced (0.0f, 10.0f); break;
            default: break;
        }
        theme::drawGlow (g, line, ind, 1.0f, 4.0f);
        g.setColour (ind);
        g.fillRoundedRectangle (line, 1.0f);
    }

    const Font font = getTabButtonFont (b, (float) (horizontal ? area.getHeight() : area.getWidth()));
    Colour textColour = front ? b.findColour (TabbedButtonBar::frontTextColourId)
                              : b.findColour (TabbedButtonBar::tabTextColourId);
    if (isMouseOver && ! front)
        textColour = textColour.brighter (0.3f);
    if (! b.isEnabled())
        textColour = textColour.withMultipliedAlpha (0.4f);

    Rectangle<int> textArea = area;
    const Rectangle<int> extraArea = b.getExtraComponent() != nullptr ? b.getExtraComponent()->getBounds() : Rectangle<int>();
    if (! extraArea.isEmpty())
    {
        if (b.getExtraComponentPlacement() == TabBarButton::beforeText)
            textArea = textArea.withTrimmedLeft (extraArea.getRight() - textArea.getX() + 4);
        else
            textArea = textArea.withTrimmedRight (textArea.getRight() - extraArea.getX() + 4);
    }

    g.setColour (textColour);
    g.setFont (font);
    if (horizontal)
    {
        g.drawFittedText (b.getButtonText(), textArea.reduced (8, 0), Justification::centred, 1);
    }
    else
    {
        g.saveState();
        const auto rotation = orientation == TabbedButtonBar::TabsAtLeft ? -MathConstants<float>::halfPi : MathConstants<float>::halfPi;
        g.addTransform (AffineTransform::rotation (rotation, (float) textArea.getCentreX(), (float) textArea.getCentreY()));
        Rectangle<int> rotated (textArea.getCentreX() - textArea.getHeight() / 2, textArea.getCentreY() - textArea.getWidth() / 2,
                                textArea.getHeight(), textArea.getWidth());
        g.drawFittedText (b.getButtonText(), rotated.reduced (8, 0), Justification::centred, 1);
        g.restoreState();
    }
}

void FaLookAndFeel::drawTabbedButtonBarBackground (TabbedButtonBar&, Graphics&) {}

void FaLookAndFeel::drawTabAreaBehindFrontButton (TabbedButtonBar& bar, Graphics& g, int w, int h)
{
    g.setColour (bar.findColour (TabbedButtonBar::tabOutlineColourId));
    switch (bar.getOrientation())
    {
        case TabbedButtonBar::TabsAtTop:    g.fillRect (0, h - 1, w, 1); break;
        case TabbedButtonBar::TabsAtBottom: g.fillRect (0, 0, w, 1); break;
        case TabbedButtonBar::TabsAtLeft:   g.fillRect (w - 1, 0, 1, h); break;
        case TabbedButtonBar::TabsAtRight:  g.fillRect (0, 0, 1, h); break;
        default: break;
    }
}

// =============================================================================
// Tooltips, alerts, file browser, misc
// =============================================================================
Rectangle<int> FaLookAndFeel::getTooltipBounds (const String& tipText, Point<int> screenPos, Rectangle<int> parentArea)
{
    const auto font = theme::font (12.5f);
    const int maxW = 320;
    TextLayout tl;
    AttributedString s;
    s.setJustification (Justification::centredLeft);
    s.append (tipText, font, theme::text);
    tl.createLayout (s, (float) maxW);
    const int w = (int) std::ceil (tl.getWidth()) + 24;
    const int h = (int) std::ceil (tl.getHeight()) + 14;

    return Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 20,
                           screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 22,
                           w, h).constrainedWithin (parentArea);
}

void FaLookAndFeel::drawTooltip (Graphics& g, const String& text, int width, int height)
{
    Rectangle<float> r (0.0f, 0.0f, (float) width, (float) height);
    // tooltip windows are opaque; keep the corners square-safe with a plain fill then a soft frame
    g.fillAll (findColour (TooltipWindow::backgroundColourId));
    g.setColour (findColour (TooltipWindow::outlineColourId));
    g.drawRect (r, 1.0f);
    g.setColour (theme::accent.withAlpha (0.9f));
    g.fillRect (r.removeFromLeft (2.0f));

    AttributedString s;
    s.setJustification (Justification::centredLeft);
    s.append (text, theme::font (12.5f), findColour (TooltipWindow::textColourId));
    TextLayout tl;
    tl.createLayout (s, (float) width - 24.0f);
    tl.draw (g, Rectangle<float> (12.0f, 0.0f, (float) width - 24.0f, (float) height));
}

void FaLookAndFeel::drawAlertBox (Graphics& g, AlertWindow& w, const Rectangle<int>& textArea, TextLayout& layout)
{
    auto bounds = w.getLocalBounds().toFloat();
    g.fillAll (theme::bg0);
    theme::fillCard (g, bounds.reduced (0.5f), w.findColour (AlertWindow::backgroundColourId), theme::borderHi, theme::radiusSm);

    const int iconSize = 40;
    Rectangle<int> iconRect (20, 22, iconSize, iconSize);
    std::optional<icons::Id> icon;
    Colour iconColour = theme::accent;
    switch (w.getAlertType())
    {
        case MessageBoxIconType::WarningIcon:  icon = icons::Id::warning; iconColour = theme::warn; break;
        case MessageBoxIconType::InfoIcon:     icon = icons::Id::info;    iconColour = theme::accent; break;
        case MessageBoxIconType::QuestionIcon: icon = icons::Id::help;    iconColour = theme::accent; break;
        case MessageBoxIconType::NoIcon:
        default: break;
    }

    if (icon.has_value())
    {
        g.setColour (iconColour.withAlpha (0.14f));
        g.fillRoundedRectangle (iconRect.toFloat(), theme::radius);
        icons::draw (g, *icon, iconRect.toFloat().reduced (9.0f), iconColour);
    }

    g.setColour (w.findColour (AlertWindow::textColourId));
    auto area = textArea;
    if (icon.has_value())
        area = area.withTrimmedLeft (jmax (0, iconRect.getRight() + 16 - textArea.getX()));
    layout.draw (g, area.toFloat());
}

void FaLookAndFeel::drawFileBrowserRow (Graphics& g, int width, int height, const File&, const String& filename,
                                        Image* icon, const String& fileSizeDescription, const String& fileTimeDescription,
                                        bool isDirectory, bool isItemSelected, int, DirectoryContentsDisplayComponent& dcc)
{
    auto* fileListComp = dynamic_cast<Component*> (&dcc);
    Rectangle<float> row (0.0f, 0.0f, (float) width, (float) height);

    if (isItemSelected)
    {
        g.setColour (fileListComp != nullptr ? fileListComp->findColour (DirectoryContentsDisplayComponent::highlightColourId)
                                             : theme::accent.withAlpha (0.16f));
        g.fillRoundedRectangle (row.reduced (4.0f, 1.0f), theme::radiusSm);
    }

    const int iconW = 24;
    Rectangle<float> iconArea (8.0f, 0.0f, (float) iconW, (float) height);
    if (icon != nullptr && icon->isValid())
        g.drawImageWithin (*icon, (int) iconArea.getX(), 2, iconW, height - 4, RectanglePlacement::centred | RectanglePlacement::onlyReduceInSize);
    else
        icons::draw (g, isDirectory ? icons::Id::folder : icons::Id::document,
                     iconArea.withSizeKeepingCentre (16.0f, 16.0f), isDirectory ? theme::warn : theme::textDim);

    const Colour textColour = fileListComp != nullptr
        ? fileListComp->findColour (isItemSelected ? DirectoryContentsDisplayComponent::highlightedTextColourId
                                                   : DirectoryContentsDisplayComponent::textColourId)
        : theme::text;

    g.setColour (textColour);
    g.setFont (theme::font (13.0f));
    const int x = 8 + iconW + 10;

    if (width > 450 && ! isDirectory)
    {
        const int sizeX = roundToInt ((float) width * 0.7f);
        const int dateX = roundToInt ((float) width * 0.8f);
        g.drawFittedText (filename, x, 0, sizeX - x, height, Justification::centredLeft, 1);
        g.setColour (theme::textDim);
        g.setFont (theme::mono (11.5f));
        g.drawFittedText (fileSizeDescription, sizeX, 0, dateX - sizeX - 8, height, Justification::centredRight, 1);
        g.drawFittedText (fileTimeDescription, dateX, 0, width - 8 - dateX, height, Justification::centredRight, 1);
    }
    else
    {
        g.drawFittedText (filename, x, 0, width - x, height, Justification::centredLeft, 1);
    }
}

void FaLookAndFeel::drawProgressBar (Graphics& g, ProgressBar& bar, int width, int height, double progress, const String& textToShow)
{
    Rectangle<float> r (0.0f, 0.0f, (float) width, (float) height);
    const bool showText = textToShow.isNotEmpty() && height >= 16;
    Rectangle<float> track = showText ? r.withSizeKeepingCentre (r.getWidth(), 6.0f).translated (0.0f, (float) height * 0.25f) : r;
    const float rad = track.getHeight() * 0.5f;
    const Colour fg = bar.findColour (ProgressBar::foregroundColourId);

    g.setColour (bar.findColour (ProgressBar::backgroundColourId));
    g.fillRoundedRectangle (track, rad);

    if (progress >= 0.0 && progress <= 1.0)
    {
        auto fill = track.withWidth (jmax (track.getHeight(), (float) progress * track.getWidth()));
        theme::drawGlow (g, fill, fg, rad, 3.0f);
        g.setColour (fg);
        g.fillRoundedRectangle (fill, rad);
    }
    else
    {
        // indeterminate: shimmer band
        const double t = std::fmod (Time::getMillisecondCounterHiRes() * 0.0006, 1.0);
        const float bandW = track.getWidth() * 0.3f;
        const float xPos = track.getX() - bandW + (float) t * (track.getWidth() + bandW);
        g.saveState();
        g.reduceClipRegion (track.toNearestInt());
        ColourGradient band (fg.withAlpha (0.0f), xPos, 0.0f, fg.withAlpha (0.0f), xPos + bandW, 0.0f, false);
        band.addColour (0.5, fg);
        g.setGradientFill (band);
        g.fillRoundedRectangle (Rectangle<float> (xPos, track.getY(), bandW, track.getHeight()), rad);
        g.restoreState();
    }

    if (showText)
    {
        g.setColour (theme::textDim);
        g.setFont (theme::mono (11.0f));
        g.drawText (textToShow, r.withTrimmedBottom ((float) height * 0.5f).toNearestInt(), Justification::centred, false);
    }
}

void FaLookAndFeel::drawSpinningWaitAnimation (Graphics& g, const Colour& colour, int x, int y, int w, int h)
{
    const float radius = (float) jmin (w, h) * 0.4f;
    const auto centre = Rectangle<int> (x, y, w, h).toFloat().getCentre();
    const float t = (float) std::fmod (Time::getMillisecondCounterHiRes() * 0.004, MathConstants<double>::twoPi);
    const float thickness = jmax (2.0f, radius * 0.18f);

    Path track;
    track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, 0.0f, MathConstants<float>::twoPi, true);
    g.setColour (colour.withAlpha (0.15f));
    g.strokePath (track, PathStrokeType (thickness));

    Path arc;
    arc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, t, t + MathConstants<float>::pi * 0.6f, true);
    g.setColour (colour);
    g.strokePath (arc, PathStrokeType (thickness, PathStrokeType::curved, PathStrokeType::rounded));
}

void FaLookAndFeel::drawCallOutBoxBackground (CallOutBox& box, Graphics& g, const Path& path, Image& cachedImage)
{
    if (cachedImage.isNull())
    {
        cachedImage = { Image::ARGB, box.getWidth(), box.getHeight(), true };
        Graphics g2 (cachedImage);
        DropShadow (Colours::black.withAlpha (0.6f), 14, { 0, 6 }).drawForPath (g2, path);
    }

    g.setColour (Colours::black);
    g.drawImageAt (cachedImage, 0, 0);
    g.setColour (theme::bg3);
    g.fillPath (path);
    g.setColour (theme::borderHi);
    g.strokePath (path, PathStrokeType (1.0f));
}

void FaLookAndFeel::drawBubble (Graphics& g, BubbleComponent& b, const Point<float>& tip, const Rectangle<float>& body)
{
    Path p;
    p.addBubble (body.reduced (0.5f), body.getUnion (Rectangle<float> (tip.x, tip.y, 1.0f, 1.0f)),
                 tip, theme::radiusSm, jmin (15.0f, body.getWidth() * 0.2f, body.getHeight() * 0.2f));
    g.setColour (b.findColour (BubbleComponent::backgroundColourId));
    g.fillPath (p);
    g.setColour (b.findColour (BubbleComponent::outlineColourId));
    g.strokePath (p, PathStrokeType (1.0f));
}

void FaLookAndFeel::drawGroupComponentOutline (Graphics& g, int width, int height, const String& text,
                                               const Justification& position, GroupComponent& group)
{
    const float textH = 14.0f;
    const float indent = 10.0f;
    const auto font = theme::fontBold (10.5f).withExtraKerningFactor (0.1f);
    const float textW = text.isEmpty() ? 0.0f : theme::textWidth (font, text.toUpperCase()) + 10.0f;

    Rectangle<float> r (0.5f, textH * 0.5f, (float) width - 1.0f, (float) height - textH * 0.5f - 0.5f);
    Path outline;
    outline.addRoundedRectangle (r, theme::radius);

    float textX = indent + 4.0f;
    if (position.testFlags (Justification::horizontallyCentred)) textX = ((float) width - textW) * 0.5f;
    else if (position.testFlags (Justification::right))          textX = (float) width - indent - 4.0f - textW;

    g.setColour (group.findColour (GroupComponent::outlineColourId));
    if (textW > 0.0f)
    {
        g.saveState();
        g.excludeClipRegion (Rectangle<float> (textX - 2.0f, 0.0f, textW + 4.0f, textH).toNearestInt());
        g.strokePath (outline, PathStrokeType (1.0f));
        g.restoreState();
        g.setColour (group.findColour (GroupComponent::textColourId));
        g.setFont (font);
        g.drawText (text.toUpperCase(), roundToInt (textX), 0, roundToInt (textW), roundToInt (textH), Justification::centred, false);
    }
    else
    {
        g.strokePath (outline, PathStrokeType (1.0f));
    }
}

void FaLookAndFeel::drawCornerResizer (Graphics& g, int w, int h, bool isMouseOver, bool)
{
    g.setColour (isMouseOver ? theme::textDim : theme::border);
    for (int i = 0; i < 3; ++i)
    {
        const float o = (float) i * 4.0f;
        g.drawLine ((float) w - 2.0f - o, (float) h - 2.0f, (float) w - 2.0f, (float) h - 2.0f - o, 1.0f);
    }
}

void FaLookAndFeel::drawResizableFrame (Graphics&, int, int, const BorderSize<int>&) {}

void FaLookAndFeel::drawStretchableLayoutResizerBar (Graphics& g, int w, int h, bool isVertical, bool isMouseOver, bool isMouseDragging)
{
    const Colour c = isMouseDragging ? theme::accent : (isMouseOver ? theme::borderHi : theme::border);
    g.setColour (c);
    if (isVertical)
        g.fillRoundedRectangle (Rectangle<float> ((float) w * 0.5f - 1.0f, (float) h * 0.5f - 16.0f, 2.0f, 32.0f), 1.0f);
    else
        g.fillRoundedRectangle (Rectangle<float> ((float) w * 0.5f - 16.0f, (float) h * 0.5f - 1.0f, 32.0f, 2.0f), 1.0f);
}

void FaLookAndFeel::drawLevelMeter (Graphics& g, int width, int height, float level)
{
    Rectangle<float> r (0.0f, 0.0f, (float) width, (float) height);
    const float rad = jmin (3.0f, r.getHeight() * 0.5f);
    g.setColour (theme::bg1);
    g.fillRoundedRectangle (r, rad);
    auto fill = r.withWidth (r.getWidth() * jlimit (0.0f, 1.0f, level));
    ColourGradient grad (theme::good, r.getX(), 0.0f, theme::bad, r.getRight(), 0.0f, false);
    grad.addColour (0.72, theme::warn);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (fill, rad);
    drawRoundedBorder (g, r, theme::border, rad);
}

// =============================================================================
// Document window (used only when a custom title bar is enabled)
// =============================================================================
Button* FaLookAndFeel::createDocumentWindowButton (int buttonType)
{
    if (buttonType == DocumentWindow::closeButton)    return new TitleBarButton ("close", icons::Id::cross, true);
    if (buttonType == DocumentWindow::minimiseButton) return new TitleBarButton ("minimise", icons::Id::minimise, false);
    return new TitleBarButton ("maximise", icons::Id::maximise, false);
}

void FaLookAndFeel::positionDocumentWindowButtons (DocumentWindow&, int titleBarX, int titleBarY, int titleBarW, int titleBarH,
                                                   Button* minimiseButton, Button* maximiseButton, Button* closeButton,
                                                   bool positionTitleBarButtonsOnLeft)
{
    const int buttonW = 46;
    int x = positionTitleBarButtonsOnLeft ? titleBarX : titleBarX + titleBarW - buttonW;
    const int step = positionTitleBarButtonsOnLeft ? buttonW : -buttonW;

    if (closeButton != nullptr)    { closeButton->setBounds (x, titleBarY, buttonW, titleBarH); x += step; }
    if (positionTitleBarButtonsOnLeft) std::swap (minimiseButton, maximiseButton);
    if (maximiseButton != nullptr) { maximiseButton->setBounds (x, titleBarY, buttonW, titleBarH); x += step; }
    if (minimiseButton != nullptr) { minimiseButton->setBounds (x, titleBarY, buttonW, titleBarH); }
}

void FaLookAndFeel::drawDocumentWindowTitleBar (DocumentWindow& w, Graphics& g, int width, int height, int titleSpaceX,
                                                int titleSpaceW, const Image*, bool drawTitleTextOnLeft)
{
    g.fillAll (theme::bg0);
    g.setColour (theme::border);
    g.fillRect (0, height - 1, width, 1);

    const Rectangle<float> logo ((float) titleSpaceX + 4.0f, (float) height * 0.5f - 9.0f, 18.0f, 18.0f);
    g.setColour (theme::accent);
    g.fillRoundedRectangle (logo, 5.0f);
    icons::draw (g, icons::Id::bolt, logo.reduced (3.0f), theme::onAccent);

    g.setColour (w.isActiveWindow() ? theme::text : theme::textMuted);
    g.setFont (theme::fontMedium (13.0f));
    g.drawFittedText (w.getName(), titleSpaceX + 30, 0, titleSpaceW - 30, height,
                      drawTitleTextOnLeft ? Justification::centredLeft : Justification::centred, 1);
}
} // namespace fa::ui
