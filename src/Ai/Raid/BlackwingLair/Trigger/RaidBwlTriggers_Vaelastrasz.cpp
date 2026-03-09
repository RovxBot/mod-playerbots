#include "RaidBwlTriggers.h"

bool BwlVaelastraszEncounterTrigger::IsActive()
{
    if (!helper.IsInBwl() || !bot->IsInCombat())
    {
        return false;
    }

    return helper.FindAliveTarget("vaelastrasz the corrupt") != nullptr;
}

bool BwlVaelastraszBurningAdrenalineSelfTrigger::IsActive()
{
    if (!helper.IsInBwl() || !bot->IsInCombat())
    {
        return false;
    }

    if (!helper.FindAliveTarget("vaelastrasz the corrupt"))
    {
        return false;
    }

    return helper.HasBurningAdrenaline(bot);
}

bool BwlVaelastraszMainTankBurningAdrenalineTrigger::IsActive()
{
    if (!helper.IsInBwl() || !bot->IsInCombat() ||
        (!helper.IsEncounterBackupTank(bot, 0) && !helper.IsEncounterBackupTank(bot, 1)))
    {
        return false;
    }

    if (!helper.FindAliveTarget("vaelastrasz the corrupt"))
    {
        return false;
    }

    Player* mainTank = helper.GetEncounterPrimaryTank();
    if (!mainTank || mainTank == bot)
    {
        return false;
    }

    if (helper.HasBurningAdrenaline(bot))
    {
        return false;
    }

    return helper.HasBurningAdrenaline(mainTank);
}

bool BwlVaelastraszPositioningTrigger::IsActive()
{
    if (!helper.IsInBwl() || !bot->IsInCombat())
    {
        return false;
    }

    if (!helper.FindAliveTarget("vaelastrasz the corrupt"))
    {
        return false;
    }

    // BA bots should run out; movement handled by dedicated trigger/action.
    if (helper.HasBurningAdrenaline(bot))
    {
        return false;
    }

    return true;
}
