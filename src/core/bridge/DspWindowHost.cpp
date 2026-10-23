#include "DspWindowHost.h"

#if JUCE_WINDOWS
 #define WIN32_LEAN_AND_MEAN
 #define NOMINMAX
 #include <windows.h>
 #include <psapi.h>
 #pragma comment (lib, "user32.lib")
 #pragma comment (lib, "psapi.lib")
#endif

namespace fa
{
DspWindowHost::DspWindowHost()
{
    setOpaque (false);
}

DspWindowHost::~DspWindowHost()
{
    detach();
}

#if JUCE_WINDOWS
static juce::String processNameFor (DWORD pid)
{
    juce::String name;
    if (auto h = OpenProcess (PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid))
    {
        wchar_t buffer[MAX_PATH] {};
        DWORD size = MAX_PATH;
        if (QueryFullProcessImageNameW (h, 0, buffer, &size))
            name = juce::File (juce::String (buffer)).getFileName();
        CloseHandle (h);
    }
    return name;
}

static BOOL CALLBACK enumWindowsProc (HWND hwnd, LPARAM lParam)
{
    auto* out = reinterpret_cast<std::vector<DspWindowHost::WindowInfo>*> (lParam);

    if (! IsWindowVisible (hwnd) || GetWindow (hwnd, GW_OWNER) != nullptr)
        return TRUE;

    wchar_t title[512] {};
    if (GetWindowTextW (hwnd, title, 512) == 0)
        return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId (hwnd, &pid);
    if (pid == GetCurrentProcessId())
        return TRUE;

    // Skip tool windows and the shell
    const auto ex = GetWindowLongPtrW (hwnd, GWL_EXSTYLE);
    if ((ex & WS_EX_TOOLWINDOW) != 0)
        return TRUE;

    DspWindowHost::WindowInfo info;
    info.handle = hwnd;
    info.title = juce::String (title);
    info.processId = (juce::uint32) pid;
    info.processName = processNameFor (pid);
    if (info.processName.equalsIgnoreCase ("explorer.exe") && info.title == "Program Manager")
        return TRUE;

    out->push_back (info);
    return TRUE;
}
#endif

std::vector<DspWindowHost::WindowInfo> DspWindowHost::listWindows()
{
    std::vector<WindowInfo> out;
   #if JUCE_WINDOWS
    EnumWindows (enumWindowsProc, reinterpret_cast<LPARAM> (&out));
    std::sort (out.begin(), out.end(), [] (auto& a, auto& b) { return a.processName.compareIgnoreCase (b.processName) < 0; });
   #endif
    return out;
}

bool DspWindowHost::attach (const WindowInfo& window, juce::String& error)
{
   #if JUCE_WINDOWS
    detach();

    auto* peer = getPeer();
    if (peer == nullptr)
    {
        error = "Open this tab first so the host window exists.";
        return false;
    }

    auto hwnd = (HWND) window.handle;
    if (! IsWindow (hwnd))
    {
        error = "That window no longer exists.";
        return false;
    }

    RECT r {};
    GetWindowRect (hwnd, &r);
    savedX = r.left; savedY = r.top; savedW = r.right - r.left; savedH = r.bottom - r.top;
    savedStyle = GetWindowLongPtrW (hwnd, GWL_STYLE);
    savedExStyle = GetWindowLongPtrW (hwnd, GWL_EXSTYLE);
    savedParent = GetParent (hwnd);

    // Turn it into a borderless child of our window.
    auto style = savedStyle;
    style &= ~(WS_POPUP | WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU);
    style |= WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
    SetWindowLongPtrW (hwnd, GWL_STYLE, style);
    SetWindowLongPtrW (hwnd, GWL_EXSTYLE, savedExStyle & ~(WS_EX_APPWINDOW | WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE));

    if (SetParent (hwnd, (HWND) peer->getNativeHandle()) == nullptr && GetLastError() != 0)
    {
        SetWindowLongPtrW (hwnd, GWL_STYLE, savedStyle);
        SetWindowLongPtrW (hwnd, GWL_EXSTYLE, savedExStyle);
        error = "Windows refused to embed this application (it may be running as administrator; "
                "run FalseAudio as administrator too).";
        return false;
    }

    hosted = hwnd;
    info = window;
    ShowWindow (hwnd, SW_SHOW);
    reposition();
    startTimerHz (4);
    repaint();
    if (onStateChanged)
        onStateChanged();
    return true;
   #else
    juce::ignoreUnused (window);
    error = "Embedding other applications is only supported on Windows.";
    return false;
   #endif
}

void DspWindowHost::detach()
{
    stopTimer();
   #if JUCE_WINDOWS
    if (hosted != nullptr)
    {
        auto hwnd = (HWND) hosted;
        if (IsWindow (hwnd))
        {
            SetParent (hwnd, (HWND) savedParent);
            SetWindowLongPtrW (hwnd, GWL_STYLE, savedStyle);
            SetWindowLongPtrW (hwnd, GWL_EXSTYLE, savedExStyle);
            SetWindowPos (hwnd, nullptr, savedX, savedY, savedW, savedH, SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        }
    }
   #endif
    const bool was = hosted != nullptr;
    hosted = nullptr;
    info = {};
    if (was && onStateChanged)
        onStateChanged();
    repaint();
}

bool DspWindowHost::launchAndAttach (const juce::File& exe, juce::String& error)
{
   #if JUCE_WINDOWS
    STARTUPINFOW si {};
    si.cb = sizeof (si);
    PROCESS_INFORMATION pi {};
    auto cmd = ("\"" + exe.getFullPathName() + "\"").toWideCharPointer();
    std::wstring cmdLine (cmd);

    if (! CreateProcessW (nullptr, cmdLine.data(), nullptr, nullptr, FALSE, 0, nullptr,
                          exe.getParentDirectory().getFullPathName().toWideCharPointer(), &si, &pi))
    {
        error = "Could not start " + exe.getFileName();
        return false;
    }
    pendingProcessId = pi.dwProcessId;
    pendingSince = juce::Time::getMillisecondCounter();
    CloseHandle (pi.hThread);
    CloseHandle (pi.hProcess);
    startTimerHz (4);
    return true;
   #else
    juce::ignoreUnused (exe);
    error = "Only supported on Windows.";
    return false;
   #endif
}

void DspWindowHost::timerCallback()
{
   #if JUCE_WINDOWS
    if (pendingProcessId != 0)
    {
        for (auto& w : listWindows())
        {
            if (w.processId == pendingProcessId)
            {
                pendingProcessId = 0;
                juce::String err;
                attach (w, err);
                return;
            }
        }
        if (juce::Time::getMillisecondCounter() - pendingSince > 20000)
        {
            pendingProcessId = 0;
            stopTimer();
        }
        return;
    }

    if (hosted != nullptr && ! IsWindow ((HWND) hosted))
    {
        hosted = nullptr;
        info = {};
        stopTimer();
        if (onStateChanged)
            onStateChanged();
        repaint();
        return;
    }
    reposition();
   #endif
}

void DspWindowHost::reposition()
{
   #if JUCE_WINDOWS
    if (hosted == nullptr)
        return;
    auto* peer = getPeer();
    if (peer == nullptr)
        return;

    // Component bounds in the peer's physical pixels.
    const auto area = peer->getComponent().getLocalArea (this, getLocalBounds());
    const auto scale = peer->getPlatformScaleFactor();
    const auto px = (area.toDouble() * scale).toNearestInt();

    SetWindowPos ((HWND) hosted, HWND_TOP, px.getX(), px.getY(), px.getWidth(), px.getHeight(),
                  SWP_NOACTIVATE | SWP_FRAMECHANGED | (isShowing() ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
   #endif
}

void DspWindowHost::paint (juce::Graphics& g)
{
    if (hosted != nullptr)
        return;
    g.setColour (juce::Colours::white.withAlpha (0.35f));
    g.setFont (15.0f);
    g.drawFittedText ("No DSP software attached", getLocalBounds(), juce::Justification::centred, 2);
}

void DspWindowHost::resized()                { reposition(); }
void DspWindowHost::parentHierarchyChanged() { reposition(); }
} // namespace fa
