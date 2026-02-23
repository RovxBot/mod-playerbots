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

    return helper.IsNefarianPhaseOneActive();
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
