#pragma once

#include "Page.h"

namespace fa::ui
{
/** Identifiers of every page, in navigation-rail order. */
enum class PageId { autoTune, system, hardware, measure, live, alignment, crossoverEq, dsp, report };

/** The main window content: header (title, Demo/Live pill), navigation rail, page area, and
    an overlay layer for modal sheets and toasts.
    CONTRACT used by pages (implemented by the shell worker): */
class AppShell : public juce::Component
{
public:
    ~AppShell() override = default;

    virtual void showPage (PageId id) = 0;
    virtual Page* getPage (PageId id) = 0;

    /** Shows `content` centred in a dimmed modal sheet (owned by the shell until closed).
        The sheet closes when closeSheet() is called. */
    virtual void showSheet (std::unique_ptr<juce::Component> content, int width, int height) = 0;
    virtual void closeSheet() = 0;

    enum class ToastKind { info, good, warn, bad };
    virtual void toast (const juce::String& message, ToastKind kind = ToastKind::info) = 0;
};
} // namespace fa::ui
