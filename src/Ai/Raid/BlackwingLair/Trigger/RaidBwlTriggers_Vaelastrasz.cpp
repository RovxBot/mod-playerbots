#include "RaidBwlTriggers.h"

#include "RaidBwlSpellIds.h"

namespace
{
bool HasBurningAdrenaline(PlayerbotAI* botAI, Unit* unit)
{
    if (!botAI || !unit)
    {
        return false;
    }

    if (botAI->HasAura(BwlSpellIds::BurningAdrenaline, unit) || botAI->HasAura(BwlSpellIds::BurningAdrenalineAlt, unit))
    {
        return true;
    }

    // Fallback for custom spell data.
    return botAI->HasAura("burning adrenaline", unit);
}
}  // namespace

bool BwlVaelastraszEncounterTrigger::IsActive()
{
    if (!helper.IsInBwl() || !bot->IsInCombat())
    {
        return false;
    }

    if (Unit* vael = AI_VALUE2(Unit*, "find target", "vaelastrasz the corrupt"))
    {
        return vael->IsAlive();
    }

    return false;
}

bool BwlVaelastraszBurningAdrenalineSelfTrigger::IsActive()
{
    if (!helper.IsInBwl() || !bot->IsInCombat())
    {
        return false;
    }

    Unit* vael = AI_VALUE2(Unit*, "find target", "vaelastrasz the corrupt");
    if (!vael || !vael->IsAlive())
    {
        return false;
    }

    return HasBurningAdrenaline(botAI, bot);
}

bool BwlVaelastraszMainTankBurningAdrenalineTrigger::IsActive()
{
    if (!helper.IsInBwl() || !bot->IsInCombat() || !botAI->IsAssistTankOfIndex(bot, 0))
    {
        return false;
    }

    Unit* vael = AI_VALUE2(Unit*, "find target", "vaelastrasz the corrupt");
    if (!vael || !vael->IsAlive())
    {
        return false;
    }

    Unit* mainTank = AI_VALUE(Unit*, "main tank");
    if (!mainTank || mainTank == bot)
    {
        return false;
    }

    if (HasBurningAdrenaline(botAI, bot))
    {
        return false;
    }

    return HasBurningAdrenaline(botAI, mainTank);
}

bool BwlVaelastraszPositioningTrigger::IsActive()
{
    if (!helper.IsInBwl() || !bot->IsInCombat())
    {
        return false;
    }

    Unit* vael = AI_VALUE2(Unit*, "find target", "vaelastrasz the corrupt");
    if (!vael || !vael->IsAlive())
    {
        return false;
    }

    // BA bots should run out; movement handled by dedicated trigger/action.
    if (HasBurningAdrenaline(botAI, bot))
    {
        return false;
    }

    return true;
}
