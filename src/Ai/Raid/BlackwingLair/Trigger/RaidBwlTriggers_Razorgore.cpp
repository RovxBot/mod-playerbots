#include "RaidBwlTriggers.h"

bool BwlRazorgoreEncounterTrigger::IsActive()
{
    if (!helper.IsInBwl())
    {
        return false;
    }

    Unit* razorgore = AI_VALUE2(Unit*, "find target", "razorgore the untamed");
    return razorgore && razorgore->IsAlive();
}
