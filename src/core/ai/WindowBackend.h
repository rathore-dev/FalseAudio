#pragma once

#include "../bridge/UIAutomation.h"
#include <functional>
#include <memory>

namespace fa
{
/** A downscaled picture of the attached window, ready for an image block. */
struct WindowScreenshot
{
    juce::String mediaType = "image/png";
    juce::String base64;
    int width = 0, height = 0;          // size of the returned image
    double scale = 1.0;                 // returned px per real window px (<= 1)
};

/** Everything the AI operator needs from the vendor window. All coordinates passed to or returned
    from click/screenshot calls are WINDOW-RELATIVE pixels (0,0 = window top-left). The real
    implementation wraps uia::Automation; tests supply a fake window. Must be callable from any thread. */
class WindowBackend
{
public:
    virtual ~WindowBackend() = default;

    virtual bool isAttached() const = 0;
    virtual juce::Rectangle<int> windowBounds() const = 0;           // screen coordinates
    virtual juce::String windowTitle() const { return {}; }          // used to find a saved map

    virtual std::vector<uia::TreeNode> snapshot (int maxNodes, int maxDepth) = 0;
    virtual bool find (const uia::ElementPath& path, uia::ElementInfo& out) = 0;

    virtual bool setText   (const uia::ElementPath& p, const juce::String& text, juce::String& error) = 0;
    virtual bool setRange  (const uia::ElementPath& p, double value, juce::String& error) = 0;
    virtual bool setToggle (const uia::ElementPath& p, bool on, juce::String& error) = 0;
    virtual bool invoke    (const uia::ElementPath& p, juce::String& error) = 0;
    virtual bool selectItem (const uia::ElementPath& p, const juce::String& itemText, juce::String& error) = 0;
    virtual juce::String readValue (const uia::ElementPath& p) = 0;
    virtual bool readToggle (const uia::ElementPath& p, bool& state) = 0;

    virtual void clickAt (juce::Point<int> windowRel, bool doubleClick) = 0;
    virtual void typeText (const juce::String& text, bool pressEnter) = 0;
    virtual bool keyCombo (const juce::String& combo, juce::String& error) = 0;

    /** Captures the window, downscaled so the long edge is <= maxEdge. */
    virtual bool screenshot (int maxEdge, WindowScreenshot& out) = 0;
    virtual void bringToFront() {}
};

/** Real backend: Windows UI Automation + SendInput on the window returned by `window()`. */
std::shared_ptr<WindowBackend> makeUiaWindowBackend (std::function<void*()> window);

/** "ctrl+shift+a", "Return", "F5" ... -> virtual key + modifiers. False if unsupported (e.g. alt). */
bool parseKeyCombo (const juce::String& combo, int& virtualKey, bool& ctrl, bool& shift);
} // namespace fa
