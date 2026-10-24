// GearDatabase overloads, kept in their own file so the lookup-based planner (and its tests) do not
// depend on the database implementation at link time.

#include "InstallPlanner.h"

namespace fa::install
{
Plan buildPlan (const Installation& inst, const SystemConfig& config, const gear::GearDatabase& db)
{
    return buildPlan (inst, config, ProductLookup ([&db] (const juce::String& id) { return db.find (id); }));
}

void applyToConfig (const Plan& plan, const Installation& inst, const gear::GearDatabase& db, SystemConfig& config)
{
    applyToConfig (plan, inst, ProductLookup ([&db] (const juce::String& id) { return db.find (id); }), config);
}
} // namespace fa::install
