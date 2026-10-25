#pragma once

#include <JuceHeader.h>
#include "Theme.h"
#include "Icons.h"

namespace fa::ui
{
/** Restyles every stock JUCE widget from the theme tokens so anything a page drops in
    (ComboBox, Slider, TextEditor, ListBox, TabbedComponent, AlertWindow, FileChooser...) looks
    native to FalseAudio. Installed as the default LookAndFeel by the application. */
class FaLookAndFeel : public juce::LookAndFeel_V4
{
public:
    FaLookAndFeel();
    ~FaLookAndFeel() override = default;

    // ---- Buttons ----------------------------------------------------------
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    int getTextButtonWidthToFitText (juce::TextButton&, int buttonHeight) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawTickBox (juce::Graphics&, juce::Component&, float x, float y, float w, float h,
                      bool ticked, bool isEnabled, bool highlighted, bool down) override;
    void drawDrawableButton (juce::Graphics&, juce::DrawableButton&, bool highlighted, bool down) override;
    juce::Path getTickShape (float height) override;
    juce::Path getCrossShape (float height) override;

    // ---- ComboBox & menus ---------------------------------------------------
    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::PopupMenu::Options getOptionsForComboBoxPopupMenu (juce::ComboBox&, juce::Label&) override;

    void drawPopupMenuBackgroundWithOptions (juce::Graphics&, int width, int height, const juce::PopupMenu::Options&) override;
    void drawPopupMenuItemWithOptions (juce::Graphics&, const juce::Rectangle<int>& area, bool isHighlighted,
                                       const juce::PopupMenu::Item&, const juce::PopupMenu::Options&) override;
    void drawPopupMenuSectionHeaderWithOptions (juce::Graphics&, const juce::Rectangle<int>& area,
                                                const juce::String& sectionName, const juce::PopupMenu::Options&) override;
    void getIdealPopupMenuItemSizeWithOptions (const juce::String& text, bool isSeparator, int standardMenuItemHeight,
                                               int& idealWidth, int& idealHeight, const juce::PopupMenu::Options&) override;
    juce::Font getPopupMenuFont() override;
    int getPopupMenuBorderSizeWithOptions (const juce::PopupMenu::Options&) override;
    void drawPopupMenuUpDownArrowWithOptions (juce::Graphics&, int width, int height, bool isScrollUpArrow,
                                              const juce::PopupMenu::Options&) override;
    void drawMenuBarBackground (juce::Graphics&, int width, int height, bool isMouseOverBar, juce::MenuBarComponent&) override;
    void drawMenuBarItem (juce::Graphics&, int width, int height, int itemIndex, const juce::String& itemText,
                          bool isMouseOverItem, bool isMenuOpen, bool isMouseOverBar, juce::MenuBarComponent&) override;

    // ---- Sliders ------------------------------------------------------------
    int getSliderThumbRadius (juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPosProportional,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    juce::Font getSliderPopupFont (juce::Slider&) override;
    juce::Slider::SliderLayout getSliderLayout (juce::Slider&) override;

    // ---- Text ---------------------------------------------------------------
    void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawLabel (juce::Graphics&, juce::Label&) override;
    juce::Font getLabelFont (juce::Label&) override;

    // ---- Scrollbars, lists, tables, trees ---------------------------------
    bool areScrollbarButtonsVisible() override { return false; }
    int getDefaultScrollbarWidth() override { return 10; }
    int getMinimumScrollbarThumbSize (juce::ScrollBar&) override { return 28; }
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height, bool isVertical,
                        int thumbStartPosition, int thumbSize, bool isMouseOver, bool isMouseDown) override;
    void drawTableHeaderBackground (juce::Graphics&, juce::TableHeaderComponent&) override;
    void drawTableHeaderColumn (juce::Graphics&, juce::TableHeaderComponent&, const juce::String& columnName, int columnId,
                                int width, int height, bool isMouseOver, bool isMouseDown, int columnFlags) override;
    void drawTreeviewPlusMinusBox (juce::Graphics&, const juce::Rectangle<float>& area, juce::Colour backgroundColour,
                                   bool isOpen, bool isMouseOver) override;
    bool areLinesDrawnForTreeView (juce::TreeView&) override { return false; }

    // ---- Tabs ---------------------------------------------------------------
    int getTabButtonBestWidth (juce::TabBarButton&, int tabDepth) override;
    int getTabButtonOverlap (int) override { return 0; }
    void drawTabButton (juce::TabBarButton&, juce::Graphics&, bool isMouseOver, bool isMouseDown) override;
    void drawTabbedButtonBarBackground (juce::TabbedButtonBar&, juce::Graphics&) override;
    void drawTabAreaBehindFrontButton (juce::TabbedButtonBar&, juce::Graphics&, int w, int h) override;
    juce::Font getTabButtonFont (juce::TabBarButton&, float height) override;

    // ---- Windows, tooltips, alerts, file browser --------------------------
    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                           juce::Rectangle<int> parentArea) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;

    void drawAlertBox (juce::Graphics&, juce::AlertWindow&, const juce::Rectangle<int>& textArea, juce::TextLayout&) override;
    int getAlertWindowButtonHeight() override { return 32; }
    juce::Font getAlertWindowTitleFont() override   { return theme::fontBold (17.0f); }
    juce::Font getAlertWindowMessageFont() override { return theme::font (14.0f); }
    juce::Font getAlertWindowFont() override        { return theme::font (13.0f); }

    void drawFileBrowserRow (juce::Graphics&, int width, int height, const juce::File&, const juce::String& filename,
                             juce::Image* icon, const juce::String& fileSizeDescription,
                             const juce::String& fileTimeDescription, bool isDirectory, bool isItemSelected,
                             int itemIndex, juce::DirectoryContentsDisplayComponent&) override;

    void drawProgressBar (juce::Graphics&, juce::ProgressBar&, int width, int height, double progress,
                          const juce::String& textToShow) override;
    void drawSpinningWaitAnimation (juce::Graphics&, const juce::Colour&, int x, int y, int w, int h) override;

    void drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics&, const juce::Path&, juce::Image&) override;
    int getCallOutBoxBorderSize (const juce::CallOutBox&) override { return 24; }
    float getCallOutBoxCornerSize (const juce::CallOutBox&) override { return theme::radius; }
    void drawBubble (juce::Graphics&, juce::BubbleComponent&, const juce::Point<float>& tip,
                     const juce::Rectangle<float>& body) override;

    void drawGroupComponentOutline (juce::Graphics&, int w, int h, const juce::String& text,
                                    const juce::Justification&, juce::GroupComponent&) override;
    void drawCornerResizer (juce::Graphics&, int w, int h, bool isMouseOver, bool isMouseDragging) override;
    void drawResizableFrame (juce::Graphics&, int w, int h, const juce::BorderSize<int>&) override;
    void drawStretchableLayoutResizerBar (juce::Graphics&, int w, int h, bool isVertical,
                                          bool isMouseOver, bool isMouseDragging) override;
    void drawLevelMeter (juce::Graphics&, int width, int height, float level) override;

    juce::Button* createDocumentWindowButton (int buttonType) override;
    void positionDocumentWindowButtons (juce::DocumentWindow&, int titleBarX, int titleBarY, int titleBarW, int titleBarH,
                                        juce::Button* minimiseButton, juce::Button* maximiseButton,
                                        juce::Button* closeButton, bool positionTitleBarButtonsOnLeft) override;
    void drawDocumentWindowTitleBar (juce::DocumentWindow&, juce::Graphics&, int w, int h, int titleSpaceX,
                                     int titleSpaceW, const juce::Image* icon, bool drawTitleTextOnLeft) override;

private:
    void applyColours();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FaLookAndFeel)
};
} // namespace fa::ui
