#pragma once

#include <JuceHeader.h>
#include "core/Session.h"
#include "theme/Theme.h"
#include "theme/Icons.h"

namespace fa::ui
{
class AppShell;

/** Base class for every page shown from the navigation rail.
    CONTRACT (shared by all UI workers - do not change without coordinating):
     - constructed once with the Session; lives for the app's lifetime
     - pageShown()/pageHidden() bracket visibility (start/stop timers there)
     - read state from the Session; react to Session change messages by overriding sessionChanged()
     - never block the message thread */
class Page : public juce::Component,
             private juce::ChangeListener
{
public:
    explicit Page (Session& s) : session (s) { session.addChangeListener (this); }
    ~Page() override { session.removeChangeListener (this); }

    virtual juce::String getTitle() const = 0;
    virtual juce::String getSubtitle() const { return {}; }
    virtual icons::Id getIcon() const = 0;

    virtual void pageShown() {}
    virtual void pageHidden() {}
    virtual void sessionChanged() {}

    /** Sub-tabs (container pages such as Car and Expert). Plain pages have none. */
    virtual bool showTab (const juce::String& /*key*/) { return false; }
    virtual juce::String getCurrentTab() const { return {}; }

    /** Called by the shell (and by container pages for their children). */
    virtual void setShell (AppShell* s) { shell = s; }

    /** Set by the shell; lets pages navigate ("Go to Rig") and show sheets/toasts. */
    AppShell* shell = nullptr;

protected:
    Session& session;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { sessionChanged(); }
};
} // namespace fa::ui
