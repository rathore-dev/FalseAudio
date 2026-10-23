#include "UIAutomation.h"

#if JUCE_WINDOWS
 #define NOMINMAX
 #include <windows.h>
 #include <ole2.h>
 #include <UIAutomation.h>
 #pragma comment (lib, "ole32.lib")
 #pragma comment (lib, "oleaut32.lib")
 #pragma comment (lib, "uiautomationcore.lib")
#endif

namespace fa::uia
{
//==============================================================================
juce::var PathStep::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("type", controlType);
    o->setProperty ("id", automationId);
    o->setProperty ("name", name);
    o->setProperty ("class", className);
    o->setProperty ("index", index);
    return juce::var (o);
}

PathStep PathStep::fromVar (const juce::var& v)
{
    PathStep s;
    s.controlType  = v.getProperty ("type", {}).toString();
    s.automationId = v.getProperty ("id", {}).toString();
    s.name         = v.getProperty ("name", {}).toString();
    s.className    = v.getProperty ("class", {}).toString();
    s.index        = v.getProperty ("index", 0);
    return s;
}

juce::String ElementPath::describe() const
{
    if (steps.empty())
        return "(none)";
    const auto& last = steps.back();
    juce::String s = last.controlType;
    if (last.name.isNotEmpty())              s << " \"" << last.name << "\"";
    else if (last.automationId.isNotEmpty()) s << " #" << last.automationId;
    if (last.index > 0)                      s << " [" << last.index << "]";
    return s;
}

juce::var ElementPath::toVar() const
{
    juce::Array<juce::var> a;
    for (auto& s : steps)
        a.add (s.toVar());
    return a;
}

ElementPath ElementPath::fromVar (const juce::var& v)
{
    ElementPath p;
    if (auto* a = v.getArray())
        for (auto& s : *a)
            p.steps.push_back (PathStep::fromVar (s));
    return p;
}

#if JUCE_WINDOWS
//==============================================================================
namespace
{
    template <typename T>
    struct ComPtr
    {
        T* p = nullptr;
        ComPtr() = default;
        ~ComPtr() { reset(); }
        ComPtr (const ComPtr&) = delete;
        ComPtr& operator= (const ComPtr&) = delete;
        ComPtr (ComPtr&& o) noexcept : p (o.p) { o.p = nullptr; }
        ComPtr& operator= (ComPtr&& o) noexcept { if (this != &o) { reset(); p = o.p; o.p = nullptr; } return *this; }
        void reset() { if (p != nullptr) { p->Release(); p = nullptr; } }
        T** put() { reset(); return &p; }
        T* operator->() const { return p; }
        explicit operator bool() const { return p != nullptr; }
    };

    struct ComScope
    {
        HRESULT hr;
        ComScope()  { hr = CoInitializeEx (nullptr, COINIT_MULTITHREADED); }
        ~ComScope() { if (SUCCEEDED (hr)) CoUninitialize(); }
    };

    juce::String fromBstr (BSTR b)
    {
        juce::String s (b != nullptr ? juce::String (b) : juce::String());
        if (b != nullptr)
            SysFreeString (b);
        return s;
    }

    juce::String controlTypeName (CONTROLTYPEID id)
    {
        switch (id)
        {
            case UIA_ButtonControlTypeId:      return "Button";
            case UIA_CheckBoxControlTypeId:    return "CheckBox";
            case UIA_ComboBoxControlTypeId:    return "ComboBox";
            case UIA_EditControlTypeId:        return "Edit";
            case UIA_SliderControlTypeId:      return "Slider";
            case UIA_SpinnerControlTypeId:     return "Spinner";
            case UIA_TextControlTypeId:        return "Text";
            case UIA_PaneControlTypeId:        return "Pane";
            case UIA_WindowControlTypeId:      return "Window";
            case UIA_ListControlTypeId:        return "List";
            case UIA_ListItemControlTypeId:    return "ListItem";
            case UIA_TabControlTypeId:         return "Tab";
            case UIA_TabItemControlTypeId:     return "TabItem";
            case UIA_CustomControlTypeId:      return "Custom";
            case UIA_GroupControlTypeId:       return "Group";
            case UIA_RadioButtonControlTypeId: return "RadioButton";
            case UIA_MenuItemControlTypeId:    return "MenuItem";
            case UIA_DataItemControlTypeId:    return "DataItem";
            case UIA_DocumentControlTypeId:    return "Document";
            case UIA_ImageControlTypeId:       return "Image";
            case UIA_HyperlinkControlTypeId:   return "Hyperlink";
            case UIA_TreeItemControlTypeId:    return "TreeItem";
            case UIA_ToolBarControlTypeId:     return "ToolBar";
            default:                           return "Type" + juce::String ((int) id);
        }
    }

    struct Element
    {
        ComPtr<IUIAutomationElement> e;

        juce::String type() const
        {
            CONTROLTYPEID id = 0;
            if (e) e->get_CurrentControlType (&id);
            return controlTypeName (id);
        }
        juce::String name() const         { BSTR b = nullptr; if (e) e->get_CurrentName (&b); return fromBstr (b); }
        juce::String automationId() const { BSTR b = nullptr; if (e) e->get_CurrentAutomationId (&b); return fromBstr (b); }
        juce::String className() const    { BSTR b = nullptr; if (e) e->get_CurrentClassName (&b); return fromBstr (b); }
        HWND hwnd() const                 { UIA_HWND h = nullptr; if (e) e->get_CurrentNativeWindowHandle (&h); return (HWND) h; }
        juce::Rectangle<int> bounds() const
        {
            RECT r {};
            if (e) e->get_CurrentBoundingRectangle (&r);
            return { (int) r.left, (int) r.top, (int) (r.right - r.left), (int) (r.bottom - r.top) };
        }

        template <typename Pattern>
        ComPtr<Pattern> pattern (PATTERNID id) const
        {
            ComPtr<Pattern> p;
            if (e)
                e->GetCurrentPatternAs (id, __uuidof (Pattern), reinterpret_cast<void**> (p.put()));
            return p;
        }
    };
}

struct Automation::Impl
{
    ComPtr<IUIAutomation> create() const
    {
        ComPtr<IUIAutomation> a;
        CoCreateInstance (__uuidof (CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, __uuidof (IUIAutomation),
                          reinterpret_cast<void**> (a.put()));
        return a;
    }

    static bool matches (const Element& el, const PathStep& step)
    {
        if (el.type() != step.controlType)
            return false;
        if (step.automationId.isNotEmpty())
            return el.automationId() == step.automationId;
        return true;
    }

    /** Index of `el` among its parent's children with the same type + automationId. */
    static int siblingIndex (IUIAutomationTreeWalker* walker, IUIAutomationElement* parent, const Element& el, IUIAutomation* a)
    {
        PathStep probe { el.type(), el.automationId(), {}, {}, 0 };
        int idx = 0;
        Element child;
        walker->GetFirstChildElement (parent, child.e.put());
        while (child.e)
        {
            BOOL same = FALSE;
            a->CompareElements (child.e.p, el.e.p, &same);
            if (same)
                return idx;
            if (matches (child, probe))
                ++idx;
            Element next;
            walker->GetNextSiblingElement (child.e.p, next.e.put());
            child = std::move (next);
        }
        return 0;
    }

    Element resolve (IUIAutomation* a, HWND root, const ElementPath& path) const
    {
        Element cur;
        if (FAILED (a->ElementFromHandle (root, cur.e.put())) || ! cur.e)
            return {};

        ComPtr<IUIAutomationTreeWalker> walker;
        a->get_ControlViewWalker (walker.put());

        for (auto& step : path.steps)
        {
            Element found, fallbackByName;
            int matchCount = 0;

            Element child;
            walker->GetFirstChildElement (cur.e.p, child.e.put());
            while (child.e)
            {
                if (matches (child, step))
                {
                    if (matchCount == step.index && ! found.e)
                    {
                        found.e.p = child.e.p;
                        found.e.p->AddRef();
                    }
                    if (step.name.isNotEmpty() && child.name() == step.name && ! fallbackByName.e)
                    {
                        fallbackByName.e.p = child.e.p;
                        fallbackByName.e.p->AddRef();
                    }
                    ++matchCount;
                }
                Element next;
                walker->GetNextSiblingElement (child.e.p, next.e.put());
                child = std::move (next);
            }

            // Prefer a name match when the layout shifted (e.g. a band was added before it).
            if (fallbackByName.e && (! found.e || found.name() != step.name))
                cur = std::move (fallbackByName);
            else if (found.e)
                cur = std::move (found);
            else
                return {};
        }
        return cur;
    }
};

Automation::Automation() : impl (std::make_unique<Impl>()) {}
Automation::~Automation() = default;

bool Automation::isAvailable() const noexcept
{
    ComScope com;
    return (bool) impl->create();
}

void* Automation::topLevelWindowAt (juce::Point<int> p) const
{
    auto h = WindowFromPoint ({ p.x, p.y });
    return h != nullptr ? (void*) GetAncestor (h, GA_ROOT) : nullptr;
}

ElementInfo Automation::elementAt (juce::Point<int> p, void* rootWindow) const
{
    ComScope com;
    ElementInfo info;
    auto a = impl->create();
    if (! a)
        return info;

    Element el;
    if (FAILED (a->ElementFromPoint ({ p.x, p.y }, el.e.put())) || ! el.e)
        return info;

    info.valid = true;
    info.controlType = el.type();
    info.name = el.name();
    info.automationId = el.automationId();
    info.className = el.className();
    info.screenBounds = el.bounds();

    if (auto vp = el.pattern<IUIAutomationValuePattern> (UIA_ValuePatternId))
    {
        info.supportsValue = true;
        BSTR b = nullptr;
        vp->get_CurrentValue (&b);
        info.value = fromBstr (b);
    }
    if (auto rp = el.pattern<IUIAutomationRangeValuePattern> (UIA_RangeValuePatternId))
    {
        info.supportsRange = true;
        rp->get_CurrentMinimum (&info.rangeMin);
        rp->get_CurrentMaximum (&info.rangeMax);
        if (info.value.isEmpty())
        {
            double v = 0;
            rp->get_CurrentValue (&v);
            info.value = juce::String (v);
        }
    }
    info.supportsToggle    = (bool) el.pattern<IUIAutomationTogglePattern> (UIA_TogglePatternId);
    info.supportsInvoke    = (bool) el.pattern<IUIAutomationInvokePattern> (UIA_InvokePatternId);
    info.supportsSelection = (bool) el.pattern<IUIAutomationSelectionItemPattern> (UIA_SelectionItemPatternId);
    info.supportsExpand    = (bool) el.pattern<IUIAutomationExpandCollapsePattern> (UIA_ExpandCollapsePatternId);

    // Build the path from the root window down to this element.
    Element root;
    if (rootWindow == nullptr || FAILED (a->ElementFromHandle ((HWND) rootWindow, root.e.put())) || ! root.e)
        return info;

    ComPtr<IUIAutomationTreeWalker> walker;
    a->get_ControlViewWalker (walker.put());

    std::vector<PathStep> reversed;
    Element cur;
    cur.e.p = el.e.p;
    cur.e.p->AddRef();

    for (int guard = 0; guard < 64; ++guard)
    {
        BOOL isRoot = FALSE;
        a->CompareElements (cur.e.p, root.e.p, &isRoot);
        if (isRoot)
            break;

        Element parent;
        walker->GetParentElement (cur.e.p, parent.e.put());
        if (! parent.e)
        {
            reversed.clear(); // not inside the root window
            break;
        }

        PathStep step;
        step.controlType  = cur.type();
        step.automationId = cur.automationId();
        step.name         = cur.name();
        step.className    = cur.className();
        step.index        = Impl::siblingIndex (walker.p, parent.e.p, cur, a.p);
        reversed.push_back (step);
        cur = std::move (parent);
    }

    info.path.steps.assign (reversed.rbegin(), reversed.rend());
    return info;
}

bool Automation::find (void* rootWindow, const ElementPath& path, ElementInfo& out) const
{
    ComScope com;
    auto a = impl->create();
    if (! a)
        return false;
    auto el = impl->resolve (a.p, (HWND) rootWindow, path);
    if (! el.e)
        return false;
    out.valid = true;
    out.path = path;
    out.controlType = el.type();
    out.name = el.name();
    out.automationId = el.automationId();
    out.screenBounds = el.bounds();
    return true;
}

bool Automation::setText (void* rootWindow, const ElementPath& path, const juce::String& text, juce::String& error) const
{
    ComScope com;
    auto a = impl->create();
    auto el = a ? impl->resolve (a.p, (HWND) rootWindow, path) : Element();
    if (! el.e)
    {
        error = "Control not found: " + path.describe();
        return false;
    }

    if (auto vp = el.pattern<IUIAutomationValuePattern> (UIA_ValuePatternId))
    {
        auto bstr = SysAllocString (text.toWideCharPointer());
        const auto hr = vp->SetValue (bstr);
        SysFreeString (bstr);
        if (SUCCEEDED (hr))
        {
            // Most numeric boxes only commit on Enter / focus change.
            el.e->SetFocus();
            pressKeyCombo (VK_RETURN);
            return true;
        }
    }

    // Fallback: focus, select all, type.
    el.e->SetFocus();
    const auto b = el.bounds();
    clickAt (b.getCentre());
    pressKeyCombo ('A', true);
    typeText (text, true);
    return true;
}

bool Automation::setRange (void* rootWindow, const ElementPath& path, double value, juce::String& error) const
{
    ComScope com;
    auto a = impl->create();
    auto el = a ? impl->resolve (a.p, (HWND) rootWindow, path) : Element();
    if (! el.e)
    {
        error = "Control not found: " + path.describe();
        return false;
    }
    if (auto rp = el.pattern<IUIAutomationRangeValuePattern> (UIA_RangeValuePatternId))
        if (SUCCEEDED (rp->SetValue (value)))
            return true;

    error = path.describe() + " does not accept numeric values.";
    return false;
}

bool Automation::setToggle (void* rootWindow, const ElementPath& path, bool on, juce::String& error) const
{
    ComScope com;
    auto a = impl->create();
    auto el = a ? impl->resolve (a.p, (HWND) rootWindow, path) : Element();
    if (! el.e)
    {
        error = "Control not found: " + path.describe();
        return false;
    }
    if (auto tp = el.pattern<IUIAutomationTogglePattern> (UIA_TogglePatternId))
    {
        ToggleState state = ToggleState_Off;
        tp->get_CurrentToggleState (&state);
        if ((state == ToggleState_On) != on)
            tp->Toggle();
        return true;
    }
    error = path.describe() + " is not a toggle.";
    return false;
}

bool Automation::invoke (void* rootWindow, const ElementPath& path, juce::String& error) const
{
    ComScope com;
    auto a = impl->create();
    auto el = a ? impl->resolve (a.p, (HWND) rootWindow, path) : Element();
    if (! el.e)
    {
        error = "Control not found: " + path.describe();
        return false;
    }
    if (auto ip = el.pattern<IUIAutomationInvokePattern> (UIA_InvokePatternId))
        if (SUCCEEDED (ip->Invoke()))
            return true;
    if (auto sp = el.pattern<IUIAutomationSelectionItemPattern> (UIA_SelectionItemPatternId))
        if (SUCCEEDED (sp->Select()))
            return true;

    clickAt (el.bounds().getCentre());
    return true;
}

bool Automation::selectItem (void* rootWindow, const ElementPath& path, const juce::String& itemText, juce::String& error) const
{
    ComScope com;
    auto a = impl->create();
    auto el = a ? impl->resolve (a.p, (HWND) rootWindow, path) : Element();
    if (! el.e)
    {
        error = "Control not found: " + path.describe();
        return false;
    }

    auto ec = el.pattern<IUIAutomationExpandCollapsePattern> (UIA_ExpandCollapsePatternId);
    if (ec)
        ec->Expand();

    VARIANT v;
    VariantInit (&v);
    v.vt = VT_BSTR;
    v.bstrVal = SysAllocString (itemText.toWideCharPointer());
    ComPtr<IUIAutomationCondition> cond;
    a->CreatePropertyCondition (UIA_NamePropertyId, v, cond.put());
    VariantClear (&v);

    Element item;
    el.e->FindFirst (TreeScope_Descendants, cond.p, item.e.put());

    bool ok = false;
    if (item.e)
    {
        if (auto sp = item.pattern<IUIAutomationSelectionItemPattern> (UIA_SelectionItemPatternId))
            ok = SUCCEEDED (sp->Select());
        if (! ok)
            if (auto ip = item.pattern<IUIAutomationInvokePattern> (UIA_InvokePatternId))
                ok = SUCCEEDED (ip->Invoke());
    }
    if (! ok)
    {
        if (auto vp = el.pattern<IUIAutomationValuePattern> (UIA_ValuePatternId))
        {
            auto bstr = SysAllocString (itemText.toWideCharPointer());
            ok = SUCCEEDED (vp->SetValue (bstr));
            SysFreeString (bstr);
        }
    }
    if (ec)
        ec->Collapse();

    if (! ok)
        error = "Could not select \"" + itemText + "\" in " + path.describe();
    return ok;
}

juce::String Automation::readValue (void* rootWindow, const ElementPath& path) const
{
    ComScope com;
    auto a = impl->create();
    auto el = a ? impl->resolve (a.p, (HWND) rootWindow, path) : Element();
    if (! el.e)
        return {};
    if (auto vp = el.pattern<IUIAutomationValuePattern> (UIA_ValuePatternId))
    {
        BSTR b = nullptr;
        vp->get_CurrentValue (&b);
        return fromBstr (b);
    }
    return el.name();
}

//==============================================================================
void Automation::clickAt (juce::Point<int> p, bool doubleClick)
{
    SetCursorPos (p.x, p.y);
    INPUT in[2] {};
    in[0].type = INPUT_MOUSE;
    in[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    in[1].type = INPUT_MOUSE;
    in[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput (2, in, sizeof (INPUT));
    if (doubleClick)
    {
        Sleep (30);
        SendInput (2, in, sizeof (INPUT));
    }
    Sleep (40);
}

void Automation::typeText (const juce::String& text, bool pressEnter)
{
    std::vector<INPUT> inputs;
    for (auto c : text)
    {
        INPUT down {}, up {};
        down.type = up.type = INPUT_KEYBOARD;
        down.ki.wScan = up.ki.wScan = (WORD) c;
        down.ki.dwFlags = KEYEVENTF_UNICODE;
        up.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        inputs.push_back (down);
        inputs.push_back (up);
    }
    if (! inputs.empty())
        SendInput ((UINT) inputs.size(), inputs.data(), sizeof (INPUT));
    if (pressEnter)
        pressKeyCombo (VK_RETURN);
    Sleep (30);
}

void Automation::pressKeyCombo (int vk, bool ctrl, bool shift)
{
    std::vector<INPUT> in;
    auto key = [&in] (WORD k, bool up)
    {
        INPUT i {};
        i.type = INPUT_KEYBOARD;
        i.ki.wVk = k;
        i.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
        in.push_back (i);
    };
    if (ctrl)  key (VK_CONTROL, false);
    if (shift) key (VK_SHIFT, false);
    key ((WORD) vk, false);
    key ((WORD) vk, true);
    if (shift) key (VK_SHIFT, true);
    if (ctrl)  key (VK_CONTROL, true);
    SendInput ((UINT) in.size(), in.data(), sizeof (INPUT));
}

juce::Rectangle<int> Automation::windowScreenBounds (void* window)
{
    RECT r {};
    if (window != nullptr)
        GetWindowRect ((HWND) window, &r);
    return { (int) r.left, (int) r.top, (int) (r.right - r.left), (int) (r.bottom - r.top) };
}

juce::Point<int> Automation::mousePosition()
{
    POINT p {};
    GetCursorPos (&p);
    return { (int) p.x, (int) p.y };
}

bool Automation::isMouseButtonDown()
{
    return (GetAsyncKeyState (VK_LBUTTON) & 0x8000) != 0;
}

#else
//==============================================================================
struct Automation::Impl {};
Automation::Automation() : impl (std::make_unique<Impl>()) {}
Automation::~Automation() = default;
bool Automation::isAvailable() const noexcept { return false; }
void* Automation::topLevelWindowAt (juce::Point<int>) const { return nullptr; }
ElementInfo Automation::elementAt (juce::Point<int>, void*) const { return {}; }
bool Automation::find (void*, const ElementPath&, ElementInfo&) const { return false; }
bool Automation::setText (void*, const ElementPath&, const juce::String&, juce::String& e) const { e = "Windows only"; return false; }
bool Automation::setRange (void*, const ElementPath&, double, juce::String& e) const { e = "Windows only"; return false; }
bool Automation::setToggle (void*, const ElementPath&, bool, juce::String& e) const { e = "Windows only"; return false; }
bool Automation::invoke (void*, const ElementPath&, juce::String& e) const { e = "Windows only"; return false; }
bool Automation::selectItem (void*, const ElementPath&, const juce::String&, juce::String& e) const { e = "Windows only"; return false; }
juce::String Automation::readValue (void*, const ElementPath&) const { return {}; }
void Automation::clickAt (juce::Point<int>, bool) {}
void Automation::typeText (const juce::String&, bool) {}
void Automation::pressKeyCombo (int, bool, bool) {}
juce::Rectangle<int> Automation::windowScreenBounds (void*) { return {}; }
juce::Point<int> Automation::mousePosition() { return {}; }
bool Automation::isMouseButtonDown() { return false; }
#endif
} // namespace fa::uia
