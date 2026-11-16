#include "WindowBackend.h"

#if JUCE_WINDOWS
 #define NOMINMAX
 #include <windows.h>
#endif

namespace fa
{
bool parseKeyCombo (const juce::String& combo, int& vk, bool& ctrl, bool& shift)
{
#if JUCE_WINDOWS
    ctrl = shift = false;
    vk = 0;
    const auto parts = juce::StringArray::fromTokens (combo.toLowerCase().removeCharacters (" "), "+", "");
    for (auto& p : parts)
    {
        if (p == "ctrl" || p == "control") { ctrl = true; continue; }
        if (p == "shift") { shift = true; continue; }
        if (p == "alt" || p == "win" || p == "super" || p == "meta") return false;
        if (vk != 0) return false;

        if (p.length() == 1 && juce::CharacterFunctions::isLetterOrDigit (p[0]))
            vk = (int) juce::CharacterFunctions::toUpperCase (p[0]);
        else if (p == "return" || p == "enter") vk = VK_RETURN;
        else if (p == "tab")       vk = VK_TAB;
        else if (p == "escape" || p == "esc") vk = VK_ESCAPE;
        else if (p == "space")     vk = VK_SPACE;
        else if (p == "backspace") vk = VK_BACK;
        else if (p == "delete" || p == "del") vk = VK_DELETE;
        else if (p == "up")        vk = VK_UP;
        else if (p == "down")      vk = VK_DOWN;
        else if (p == "left")      vk = VK_LEFT;
        else if (p == "right")     vk = VK_RIGHT;
        else if (p == "home")      vk = VK_HOME;
        else if (p == "end")       vk = VK_END;
        else if (p == "pageup")    vk = VK_PRIOR;
        else if (p == "pagedown")  vk = VK_NEXT;
        else if (p.startsWith ("f") && p.substring (1).containsOnly ("0123456789") && p.length() <= 3)
        {
            const int n = p.substring (1).getIntValue();
            if (n < 1 || n > 12) return false;
            vk = VK_F1 + n - 1;
        }
        else return false;
    }
    return vk != 0;
#else
    juce::ignoreUnused (combo, vk, ctrl, shift);
    return false;
#endif
}

namespace
{
    class UiaWindowBackend : public WindowBackend
    {
    public:
        explicit UiaWindowBackend (std::function<void*()> w) : window (std::move (w)) {}

        void* hwnd() const { return window != nullptr ? window() : nullptr; }
        bool isAttached() const override { return hwnd() != nullptr; }
        juce::Rectangle<int> windowBounds() const override { return uia::Automation::windowScreenBounds (hwnd()); }

        juce::String windowTitle() const override
        {
#if JUCE_WINDOWS
            wchar_t buf[256] {};
            if (hwnd() != nullptr)
                GetWindowTextW ((HWND) hwnd(), buf, 255);
            return juce::String (buf);
#else
            return {};
#endif
        }

        std::vector<uia::TreeNode> snapshot (int maxNodes, int maxDepth) override { return a.snapshot (hwnd(), maxNodes, maxDepth); }
        bool find (const uia::ElementPath& p, uia::ElementInfo& out) override { return a.find (hwnd(), p, out); }
        bool setText (const uia::ElementPath& p, const juce::String& t, juce::String& e) override { return a.setText (hwnd(), p, t, e); }
        bool setRange (const uia::ElementPath& p, double v, juce::String& e) override { return a.setRange (hwnd(), p, v, e); }
        bool setToggle (const uia::ElementPath& p, bool on, juce::String& e) override { return a.setToggle (hwnd(), p, on, e); }
        bool invoke (const uia::ElementPath& p, juce::String& e) override { return a.invoke (hwnd(), p, e); }
        bool selectItem (const uia::ElementPath& p, const juce::String& t, juce::String& e) override { return a.selectItem (hwnd(), p, t, e); }
        juce::String readValue (const uia::ElementPath& p) override { return a.readValue (hwnd(), p); }
        bool readToggle (const uia::ElementPath& p, bool& s) override { return a.readToggle (hwnd(), p, s); }

        void clickAt (juce::Point<int> rel, bool dbl) override
        {
            bringToFront();
            const auto b = windowBounds();
            uia::Automation::clickAt ({ b.getX() + rel.x, b.getY() + rel.y }, dbl);
        }
        void typeText (const juce::String& t, bool enter) override { bringToFront(); uia::Automation::typeText (t, enter); }
        bool keyCombo (const juce::String& combo, juce::String& error) override
        {
            int vk = 0; bool ctrl = false, shift = false;
            if (! parseKeyCombo (combo, vk, ctrl, shift))
            {
                error = "Unsupported key combination \"" + combo + "\" (supported: ctrl/shift + letter, digit, F1-F12, Return, Tab, Escape, arrows ...).";
                return false;
            }
            bringToFront();
            uia::Automation::pressKeyCombo (vk, ctrl, shift);
            return true;
        }

        bool screenshot (int maxEdge, WindowScreenshot& out) override
        {
            auto img = uia::Automation::captureWindow (hwnd());
            if (img.isNull())
                return false;
            const double s = std::min (1.0, (double) maxEdge / (double) std::max (img.getWidth(), img.getHeight()));
            if (s < 1.0)
                img = img.rescaled (std::max (1, juce::roundToInt (img.getWidth() * s)),
                                    std::max (1, juce::roundToInt (img.getHeight() * s)), juce::Graphics::highResamplingQuality);
            juce::MemoryOutputStream mos;
            juce::PNGImageFormat png;
            if (! png.writeImageToStream (img, mos))
                return false;
            out.mediaType = "image/png";
            out.base64 = juce::Base64::toBase64 (mos.getData(), mos.getDataSize());
            out.width = img.getWidth();
            out.height = img.getHeight();
            out.scale = s;
            return true;
        }
        void bringToFront() override { uia::Automation::bringToFront (hwnd()); }

    private:
        std::function<void*()> window;
        uia::Automation a;
    };
}

std::shared_ptr<WindowBackend> makeUiaWindowBackend (std::function<void*()> window)
{
    return std::make_shared<UiaWindowBackend> (std::move (window));
}
} // namespace fa
