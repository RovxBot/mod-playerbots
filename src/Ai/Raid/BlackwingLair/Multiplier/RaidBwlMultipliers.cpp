#include "RaidBwlMultipliers.h"

#include "ChooseTargetActions.h"
#include "FollowActions.h"
#include "GenericSpellActions.h"
#include "MovementActions.h"
#include "Timer.h"

void BwlEncounterTargetingMultiplier::RefreshStateCache() const
{
    uint32 const cacheTick = getMSTime() / 100;
    if (cacheMSTime == cacheTick)
    {
        return;
    }

    cacheMSTime = cacheTick;
    cacheNefarianP1 = helper.IsNefarianPhaseOneActive();
    cacheNefarianP2 = helper.IsNefarianPhaseTwoActive();
    cacheAnyBossEncounter = helper.IsAnyBwlBossEncounterActive();
    cacheDangerousTrashEncounter = helper.IsDangerousTrashEncounterActive();
    cacheSeetherEnraged = helper.HasEnragedDeathTalonSeetherNearbyOrAttacking();
    cacheDeathTalonUndetected = helper.HasUndetectedDeathTalonNearbyOrAttacking();
}

float BwlEncounterTargetingMultiplier::GetValue(Action* action)
{
    if (!helper.IsInBwl())
    {
        return 1.0f;
    }

    RefreshStateCache();

    std::string const actionName = action->getName();

    if (cacheNefarianP1)
    {
        if (actionName == "bwl nefarian phase two choose target" || actionName == "bwl nefarian phase two position")
        {
            return 0.0f;
        }
    }

    if (cacheNefarianP2)
    {
        if (actionName == "bwl nefarian phase one choose target" || actionName == "bwl nefarian phase one tunnel position")
        {
            return 0.0f;
        }
    }

    if (cacheAnyBossEncounter || cacheDangerousTrashEncounter)
    {
        if (dynamic_cast<DpsAssistAction*>(action) || dynamic_cast<TankAssistAction*>(action) ||
            dynamic_cast<CastDebuffSpellOnAttackerAction*>(action))
        {
            return 0.0f;
        }
    }

    if (cacheSeetherEnraged && actionName == "bwl trash tranq seether")
    {
        return 1.5f;
    }

    if (cacheDeathTalonUndetected && actionName == "bwl trash detect magic")
    {
        return 1.4f;
    }

    return 1.0f;
}

void BwlEncounterPositioningMultiplier::RefreshStateCache() const
{
    uint32 const cacheTick = getMSTime() / 100;
    if (cacheMSTime == cacheTick)
    {
        return;
    }

    cacheMSTime = cacheTick;
    cacheAnyBossEncounter = helper.IsAnyBwlBossEncounterActive();
    cacheChromaggusTimeLapseCast = helper.IsChromaggusCastingTimeLapse();
}

float BwlEncounterPositioningMultiplier::GetValue(Action* action)
{
    if (!helper.IsInBwl())
    {
        return 1.0f;
    }

    RefreshStateCache();

    if (cacheAnyBossEncounter)
    {
        if (dynamic_cast<CombatFormationMoveAction*>(action) || dynamic_cast<FollowAction*>(action))
        {
            return 0.0f;
        }
    }

    if (cacheChromaggusTimeLapseCast && dynamic_cast<FleeAction*>(action))
    {
        return 0.0f;
    }

    return 1.0f;
}
