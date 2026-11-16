// Session::pushConfigToDsp: the demo (virtual) DSP is automatic, so the whole config is queued
// for a background write and no manual steps are returned.

#include "TestRegistry.h"
#include "core/Session.h"

using namespace fa;
using fa::test::check;

FA_TEST (sessionPushConfigToVirtualDsp)
{
    Session s;                                   // demo mode, virtual car; no audio initialisation needed
    bool automatic = false;
    const auto steps = s.pushConfigToDsp (automatic);
    check (automatic, "demo DSP is written automatically");
    check (steps.isEmpty(), "no manual steps for an automatic DSP");
    check (s.getDsp().isAutomatic(), "virtual DSP is automatic");

    // pushing twice in a row (queue already draining) must stay safe
    bool again = false;
    s.pushConfigToDsp (again);
    check (again, "second push accepted");
    juce::Thread::sleep (100);
}
