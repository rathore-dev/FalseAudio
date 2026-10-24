#pragma once

// Lets each worker keep its tests in its own file (tests/<Area>Tests.cpp):
//
//   #include "TestRegistry.h"
//   FA_TEST (gearDatabaseLoads)
//   {
//       fa::test::check (db.size() > 0, "Database loads", juce::String (db.size()));
//   }
//
// CoreTests.cpp's main() runs every registered test after its own.

#include <juce_core/juce_core.h>
#include <functional>
#include <iostream>
#include <vector>

namespace fa::test
{
inline int& failureCount() { static int n = 0; return n; }

inline void check (bool ok, const juce::String& what, const juce::String& detail = {})
{
    std::cout << (ok ? "  [PASS] " : "  [FAIL] ") << what << (detail.isEmpty() ? "" : "  (" + detail + ")") << "\n";
    if (! ok)
        ++failureCount();
}

struct Registered { const char* name; std::function<void()> fn; };

inline std::vector<Registered>& registry() { static std::vector<Registered> r; return r; }

struct Registrar
{
    Registrar (const char* name, std::function<void()> fn) { registry().push_back ({ name, std::move (fn) }); }
};

/** Runs all registered tests; returns the number of failures they produced. */
inline int runRegistered()
{
    for (auto& t : registry())
    {
        std::cout << t.name << "\n";
        t.fn();
    }
    return failureCount();
}
} // namespace fa::test

#define FA_TEST(name) \
    static void name(); \
    static fa::test::Registrar name##_registrar (#name, name); \
    static void name()
