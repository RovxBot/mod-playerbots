#include "RaidBwlTriggers.h"

bool BwlNefarianPhaseOneTrigger::IsActive()
{
    if (!helper.IsInBwl() || !bot->IsInCombat())
    {
        return false;
    }

    return helper.IsNefarianPhaseOneActive();
}

bool BwlNefarianPhaseOneTunnelPositioningTrigger::IsActive()
{
    if (!helper.IsInBwl() || !bot->IsInCombat())
    {
        return false;
    }

    if (!helper.IsNefarianPhaseOneActive())
    {
        return false;
    }

    GuidVector attackers = context->GetValue<GuidVector>("attackers")->Get();
    if (helper.HasNefarianPhaseOneAddsInUnits(attackers))
    {
        return false;
    }

    GuidVector nearby = context->GetValue<GuidVector>("nearest npcs")->Get();
    if (helper.HasNefarianPhaseOneAddsInUnits(nearby))
    {
        return false;
    }

    Unit* currentTarget = AI_VALUE(Unit*, "current target");
    if (helper.IsNefarianPhaseOneAdd(currentTarget))
    {
        return false;
    }

    return true;
}

bool BwlNefarianPhaseTwoTrigger::IsActive()
{
    if (!helper.IsInBwl() || !bot->IsInCombat())
    {
        return false;
    }

    return helper.IsNefarianPhaseTwoActive();
}

bool BwlNefarianPhaseTwoPositioningTrigger::IsActive()
{
    if (!helper.IsInBwl() || !bot->IsInCombat())
    {
        return false;
    }

    return helper.IsNefarianPhaseTwoActive();
}
