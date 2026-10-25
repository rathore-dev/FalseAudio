#include <JuceHeader.h>
#include "core/Session.h"
#include "ui/MainShell.h"
#include "ui/theme/LookAndFeel.h"

#if JUCE_WINDOWS
 #define NOMINMAX
 #define WIN32_LEAN_AND_MEAN
 #include <windows.h>
 #include <dwmapi.h>
 #pragma comment (lib, "dwmapi.lib")
#endif

namespace
{
class StartupThread : public juce::Thread
{
public:
    StartupThread (fa::Session& s, std::function<void()> d) : juce::Thread ("FalseAudio startup"), session (s), done (std::move (d)) {}
    void run() override { session.initialise(); done(); }
private:
    fa::Session& session;
    std::function<void()> done;
};

class StartupView : public juce::Component, private juce::Timer
{
public:
    StartupView() { startTimerHz (30); }
    void paint (juce::Graphics& g) override
    {
        namespace th = fa::ui::theme;
        g.fillAll (th::bg0);
        auto r = getLocalBounds().toFloat();
        auto c = r.getCentre();
        g.setColour (th::textDim.brighter (0.8f));
        g.setFont (th::display (34.0f));
        g.drawText ("FalseAudio", r.withHeight (60.0f).withCentre (c.translated (0, -40)), juce::Justification::centred);
        g.setColour (th::textDim);
        g.setFont (th::font (14.0f));
        g.drawText ("Starting audio...", r.withHeight (24.0f).withCentre (c.translated (0, 10)), juce::Justification::centred);
        auto bar = juce::Rectangle<float> (220.0f, 4.0f).withCentre (c.translated (0, 48));
        g.setColour (th::bg0.brighter (0.15f));
        g.fillRoundedRectangle (bar, 2.0f);
        const float w = 70.0f;
        const float x = (float) std::fmod (juce::Time::getMillisecondCounter() * 0.25, (double) (bar.getWidth() + w)) - w;
        g.saveState();
        g.reduceClipRegion (bar.toNearestInt());
        g.setColour (th::accent);
        g.fillRoundedRectangle (bar.getX() + x, bar.getY(), w, 4.0f, 2.0f);
        g.restoreState();
    }
private:
    void timerCallback() override { repaint(); }
};

/** Main window: native title bar tinted to the theme (Windows 11 DWM), remembers its size and
    position, enforces the minimum size and hosts the shell. */
class MainWindow : public juce::DocumentWindow,
                   private juce::Timer
{
public:
    MainWindow (fa::Session& session_, juce::PropertiesFile& props)
        : DocumentWindow ("FalseAudio", fa::ui::theme::bg0, DocumentWindow::allButtons),
          session (session_), settings (props)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new StartupView(), true);
        setResizable (true, false);
        setResizeLimits (1200, 720, 16384, 16384);

        const auto state = settings.getValue ("windowState");
        if (state.isNotEmpty())
            restoreWindowStateFromString (state);
        else
            centreWithSize (1440, 880);

        setVisible (true);
        applyNativeChrome();
        startTimer (120);   // DWM sometimes needs the attributes re-applied once the frame exists
    }

    ~MainWindow() override
    {
        settings.setValue ("windowState", getWindowStateAsString());
        settings.saveIfNeeded();
    }

    void showShell() { setContentOwned (new fa::ui::MainShell (session), false); }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

    void activeWindowStatusChanged() override
    {
        DocumentWindow::activeWindowStatusChanged();
        applyNativeChrome();
    }

private:
    void timerCallback() override
    {
        applyNativeChrome();
        if (++chromeApplications >= 4)
            stopTimer();
    }

    int chromeApplications = 0;
    void applyNativeChrome()
    {
       #if JUCE_WINDOWS
        if (auto* peer = getPeer())
        {
            auto hwnd = (HWND) peer->getNativeHandle();
            const BOOL dark = TRUE;
            ::DwmSetWindowAttribute (hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof (dark));

            auto toColorRef = [] (juce::Colour c) { return (COLORREF) (RGB (c.getRed(), c.getGreen(), c.getBlue())); };
            const COLORREF caption = toColorRef (fa::ui::theme::bg0);
            const COLORREF text = toColorRef (fa::ui::theme::textDim);
            const COLORREF border = toColorRef (fa::ui::theme::border);
            // Windows 11 only; silently ignored on 10.
            ::DwmSetWindowAttribute (hwnd, 35 /* DWMWA_CAPTION_COLOR */, &caption, sizeof (caption));
            ::DwmSetWindowAttribute (hwnd, 36 /* DWMWA_TEXT_COLOR */, &text, sizeof (text));
            ::DwmSetWindowAttribute (hwnd, 34 /* DWMWA_BORDER_COLOR */, &border, sizeof (border));
        }
       #endif
    }

    fa::Session& session;
    juce::PropertiesFile& settings;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
};
} // namespace

class FalseAudioApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return ProjectInfo::projectName; }
    const juce::String getApplicationVersion() override { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override          { return true; }

    void initialise (const juce::String&) override
    {
        installCrashLogger();

        lookAndFeel = std::make_unique<fa::ui::FaLookAndFeel>();
        juce::LookAndFeel::setDefaultLookAndFeel (lookAndFeel.get());

        juce::PropertiesFile::Options o;
        o.applicationName = "FalseAudio";
        o.filenameSuffix = ".window";
        o.folderName = "FalseAudio";
        o.osxLibrarySubFolder = "Application Support";
        o.storageFormat = juce::PropertiesFile::storeAsXML;
        windowSettings = std::make_unique<juce::PropertiesFile> (o);

        session = std::make_unique<fa::Session>();
        window = std::make_unique<MainWindow> (*session, *windowSettings);

        // Audio device scan takes seconds: run it off the message thread; window shows "Starting audio".
        startupThread = std::make_unique<StartupThread> (*session, [this]
        {
            juce::MessageManager::callAsync ([this]
            {
                if (window != nullptr)
                    window->showShell();
            });
        });
        startupThread->startThread();
    }

    void shutdown() override
    {
        if (startupThread != nullptr)
            startupThread->stopThread (30000);
        startupThread.reset();
        window.reset();
        session.reset();
        windowSettings.reset();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        lookAndFeel.reset();
    }

    void systemRequestedQuit() override { quit(); }

    void anotherInstanceStarted (const juce::String&) override
    {
        if (window != nullptr)
            window->toFront (true);
    }

private:
    /** Writes a stack trace to %APPDATA%/FalseAudio/crash.log on an unhandled fault, so field
        problems can be diagnosed without a debugger attached. */
    static void installCrashLogger()
    {
        juce::SystemStats::setApplicationCrashHandler ([] (void*)
        {
            auto file = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                            .getChildFile ("FalseAudio").getChildFile ("crash.log");
            file.getParentDirectory().createDirectory();
            file.appendText ("==== " + juce::Time::getCurrentTime().toString (true, true) + "  FalseAudio "
                             + ProjectInfo::versionString + "\n" + juce::SystemStats::getStackBacktrace() + "\n");
        });
    }

    std::unique_ptr<fa::ui::FaLookAndFeel> lookAndFeel;
    std::unique_ptr<juce::PropertiesFile> windowSettings;
    std::unique_ptr<fa::Session> session;
    std::unique_ptr<MainWindow> window;
    std::unique_ptr<StartupThread> startupThread;
};

START_JUCE_APPLICATION (FalseAudioApplication)
