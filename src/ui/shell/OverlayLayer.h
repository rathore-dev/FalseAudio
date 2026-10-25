#pragma once

#include "../AppShell.h"
#include "../theme/Animation.h"

namespace fa::ui
{
/** Transparent layer above the pages that hosts modal sheets (dimmed backdrop + rounded panel
    that fades/slides in) and the toast stack (bottom-right, auto-dismissing). */
class OverlayLayer : public juce::Component
{
public:
    OverlayLayer();
    ~OverlayLayer() override;

    void showSheet (std::unique_ptr<juce::Component> content, int width, int height);
    void closeSheet();
    bool hasSheet() const noexcept { return sheet != nullptr && ! closing; }

    void toast (const juce::String& message, AppShell::ToastKind kind);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    class SheetHolder;
    class Toast;

    void layoutSheet();
    void layoutToasts();
    void dismissToast (Toast* t);
    void updateInterception();

    std::unique_ptr<SheetHolder> sheet;
    int sheetW = 0, sheetH = 0;
    bool closing = false;
    anim::Value dimAnim { *this, 0.0f, 14.0f };

    std::vector<std::unique_ptr<Toast>> toasts;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OverlayLayer)
};
} // namespace fa::ui
