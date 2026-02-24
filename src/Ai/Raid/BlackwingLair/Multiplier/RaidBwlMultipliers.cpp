#include "RaidBwlMultipliers.h"

#include "ChooseTargetActions.h"
#include "FollowActions.h"
#include "GenericSpellActions.h"
#include "MovementActions.h"

float BwlEncounterTargetingMultiplier::GetValue(Action* action)
{
    if (!helper.IsInBwl())
    {
        return 1.0f;
    }

    std::string const actionName = action->getName();

    if (helper.IsNefarianPhaseOneActive())
    {
        if (actionName == "bwl nefarian phase two choose target" || actionName == "bwl nefarian phase two position")
        {
            return 0.0f;
        }
    }

    if (helper.IsNefarianPhaseTwoActive())
    {
        if (actionName == "bwl nefarian phase one choose target" || actionName == "bwl nefarian phase one tunnel position")
        {
            return 0.0f;
        }
    }

    if (helper.IsAnyBwlBossEncounterActive() || helper.IsDangerousTrashEncounterActive())
    {
        if (dynamic_cast<DpsAssistAction*>(action) || dynamic_cast<TankAssistAction*>(action) ||
            dynamic_cast<CastDebuffSpellOnAttackerAction*>(action))
        {
            return 0.0f;
        }
    }

    if (helper.HasEnragedDeathTalonSeetherNearbyOrAttacking() && actionName == "bwl trash tranq seether")
    {
        return 1.5f;
    }

    if (helper.HasUndetectedDeathTalonNearbyOrAttacking() && actionName == "bwl trash detect magic")
    {
        return 1.4f;
    }

    return 1.0f;
}

float BwlEncounterPositioningMultiplier::GetValue(Action* action)
{
    if (!helper.IsInBwl())
    {
        return 1.0f;
    }

    if (helper.IsAnyBwlBossEncounterActive())
    {
        if (dynamic_cast<CombatFormationMoveAction*>(action) || dynamic_cast<FollowAction*>(action))
        {
            return 0.0f;
        }
    }

    if (helper.IsChromaggusCastingTimeLapse() && dynamic_cast<FleeAction*>(action))
    {
        return 0.0f;
    }

    return 1.0f;
}
