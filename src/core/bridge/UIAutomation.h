#pragma once

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>

namespace fa::uia
{
/** One step of the path from a top-level window down to a control. */
struct PathStep
{
    juce::String controlType;   // e.g. "Edit", "Button", "ComboBox"
    juce::String automationId;
    juce::String name;
    juce::String className;
    int index = 0;              // position among siblings with the same type + automationId

    juce::var toVar() const;
    static PathStep fromVar (const juce::var& v);
};

/** Identifies a control inside another application's window. */
struct ElementPath
{
    std::vector<PathStep> steps;
    bool isEmpty() const noexcept { return steps.empty(); }
    juce::String describe() const;

    juce::var toVar() const;
    static ElementPath fromVar (const juce::var& v);
};

/** What we know about a control under the mouse. */
struct ElementInfo
{
    bool valid = false;
    ElementPath path;
    juce::String controlType, name, automationId, className, value;
    juce::Rectangle<int> screenBounds;
    bool supportsValue = false, supportsRange = false, supportsToggle = false,
         supportsInvoke = false, supportsSelection = false, supportsExpand = false;
    double rangeMin = 0.0, rangeMax = 0.0;
};

/** One element of a window snapshot (see Automation::snapshot). */
struct TreeNode
{
    int parent = -1;     // index into the snapshot vector, -1 for the root's direct children
    int depth = 0;
    ElementInfo info;    // info.path is relative to the snapshot's root window
};

/** Thin wrapper over Windows UI Automation + SendInput. Every function is safe to call from
    any thread (COM is initialised per call site). On non-Windows platforms all calls fail. */
class Automation
{
public:
    Automation();
    ~Automation();

    bool isAvailable() const noexcept;

    /** Top-level window handle that owns the given screen point (nullptr if none). */
    void* topLevelWindowAt (juce::Point<int> screenPos) const;

    /** Describes the control at a screen position, with its path relative to `rootWindow`. */
    ElementInfo elementAt (juce::Point<int> screenPos, void* rootWindow) const;

    /** Resolves a path inside rootWindow. Returns false if the control can't be found. */
    bool find (void* rootWindow, const ElementPath& path, ElementInfo& out) const;

    // Actions on a resolved path (all return false with an error message on failure)
    bool setText   (void* rootWindow, const ElementPath& path, const juce::String& text, juce::String& error) const;
    bool setRange  (void* rootWindow, const ElementPath& path, double value, juce::String& error) const;
    bool setToggle (void* rootWindow, const ElementPath& path, bool on, juce::String& error) const;
    bool invoke    (void* rootWindow, const ElementPath& path, juce::String& error) const;
    bool selectItem (void* rootWindow, const ElementPath& path, const juce::String& itemText, juce::String& error) const;
    juce::String readValue (void* rootWindow, const ElementPath& path) const;

    // ---- additive API used by the AI operator ------------------------------------------------
    /** Depth-first snapshot of the control view under rootWindow (paths are root-relative, bounds
        are screen coordinates, pattern flags and values are filled in). Stops after maxNodes. */
    std::vector<TreeNode> snapshot (void* rootWindow, int maxNodes, int maxDepth) const;
    /** Reads a toggle/checkbox state. False if the element is missing or not a toggle. */
    bool readToggle (void* rootWindow, const ElementPath& path, bool& state) const;
    /** Captures the window (PrintWindow, falls back to a screen copy) as an opaque RGB image. */
    static juce::Image captureWindow (void* window);
    static void bringToFront (void* window);

    // Raw input fallback (for custom-drawn controls without UI Automation support)
    static void clickAt (juce::Point<int> screenPos, bool doubleClick = false);
    static void typeText (const juce::String& text, bool pressEnter);
    static void pressKeyCombo (int virtualKey, bool ctrl = false, bool shift = false);
    static juce::Rectangle<int> windowScreenBounds (void* window);
    static juce::Point<int> mousePosition();
    static bool isMouseButtonDown();

    /** Client area (no title bar, borders or system buttons) in screen coordinates. Empty if unknown. */
    static juce::Rectangle<int> windowClientBounds (void* window);
    /** True when `window` (or a window it owns, e.g. a popup / modal dialog) is the foreground window. */
    static bool isForeground (void* window);
    /** True when the top-level window under the screen point is `window` (or owned by it): nothing covers the point. */
    static bool isWindowAt (void* window, juce::Point<int> screenPos);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace fa::uia
