#include "RaidBwlTriggers.h"

bool BwlRazorgoreEncounterTrigger::IsActive()
{
    if (!helper.IsInBwl() || !bot->IsInCombat())
    {
        return false;
    }

    if (Unit* razorgore = AI_VALUE2(Unit*, "find target", "razorgore the untamed"))
    {
        return razorgore->IsAlive();
    }

    // Initial pull support before Razorgore is clearly available in target lists.
    if (Unit* grethok = AI_VALUE2(Unit*, "find target", "grethok the controller"))
    {
        return grethok->IsAlive();
    }

    return false;
}
