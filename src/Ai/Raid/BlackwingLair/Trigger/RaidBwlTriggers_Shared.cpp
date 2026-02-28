#include "RaidBwlTriggers.h"

#include "RaidBwlSpellIds.h"
#include "SharedDefines.h"

bool BwlMissingOnyxiaScaleCloakTrigger::IsActive()
{
    if (!helper.IsInBwl())
    {
        return false;
    }

    return !botAI->HasAura(BwlSpellIds::OnyxiaScaleCloakAura, bot);
}

bool BwlTrashDangerousEncounterTrigger::IsActive()
{
    return helper.IsDangerousTrashEncounterActive();
}

bool BwlTrashSafePositioningTrigger::IsActive()
{
    if (!helper.IsInBwl() || !bot->IsInCombat() || !helper.IsDangerousTrashEncounterActive())
    {
        return false;
    }

    Unit* target = AI_VALUE(Unit*, "current target");
    return target && target->IsAlive() && helper.IsDangerousTrash(target);
}

bool BwlDeathTalonSeetherEnrageTrigger::IsActive()
{
    if (!helper.IsInBwl() || !bot->IsInCombat() || bot->getClass() != CLASS_HUNTER)
    {
        return false;
    }

    return helper.HasEnragedDeathTalonSeetherNearbyOrAttacking();
}

bool BwlDeathTalonDetectMagicTrigger::IsActive()
{
    if (!helper.IsInBwl() || !bot->IsInCombat() || bot->getClass() != CLASS_MAGE)
    {
        return false;
    }

    return helper.HasUndetectedDeathTalonNearbyOrAttacking();
}
