#pragma once

#include "Page.h"

namespace fa::ui
{
/** Identifiers of the seven top-level pages, in navigation-rail order.
    Car hosts the sub-tabs "layout" and "equipment"; Expert hosts "measure", "live",
    "alignment" and "crossover" (see showPage (id, tab)). */
enum class PageId { autoTune, results, car, install, rig, dsp, expert };

/** The main window content: header (title, Demo/Live pill), navigation rail, page area, and
    an overlay layer for modal sheets and toasts.
    CONTRACT used by pages (implemented by the shell worker): */
class AppShell : public juce::Component
{
public:
    ~AppShell() override = default;

    /** Shows a page; `tab` optionally opens one of its sub-tabs, e.g. (PageId::expert, "alignment").
        With an empty tab the page reopens on the sub-tab it last showed. */
    virtual void showPage (PageId id, const juce::String& tab = {}) = 0;
    /** Re-reads the current page's title / subtitle (container pages call this on a tab change). */
    virtual void refreshHeader() = 0;
    virtual Page* getPage (PageId id) = 0;

    /** Shows `content` centred in a dimmed modal sheet (owned by the shell until closed).
        The sheet closes when closeSheet() is called. */
    virtual void showSheet (std::unique_ptr<juce::Component> content, int width, int height) = 0;
    virtual void closeSheet() = 0;

    enum class ToastKind { info, good, warn, bad };
    virtual void toast (const juce::String& message, ToastKind kind = ToastKind::info) = 0;
};
} // namespace fa::ui
