#include "RaidBwlTriggers.h"

#include "RaidBwlSpellIds.h"
#include "SharedDefines.h"

namespace
{
bool IsWyrmguard(Unit* unit)
{
    return unit && unit->IsAlive() && unit->GetTypeId() == TYPEID_UNIT && unit->GetEntry() == BwlCreatureIds::DeathTalonWyrmguard;
}

bool ShouldTankPeelWyrmguard(PlayerbotAI* botAI, Player* bot, Unit* unit)
{
    if (!botAI || !bot || !IsWyrmguard(unit))
    {
        return false;
    }

    Unit* victim = unit->GetVictim();
    if (!victim || victim == bot)
    {
        return false;
    }

    Player* victimPlayer = victim->ToPlayer();
    if (victimPlayer && botAI->IsTank(victimPlayer))
    {
        return false;
    }

    return true;
}
}  // namespace

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

bool BwlWyrmguardControlTrigger::IsActive()
{
    if (!helper.IsInBwl() || !bot->IsInCombat() || !helper.IsDangerousTrashEncounterActive() || !botAI->IsTank(bot))
    {
        return false;
    }

    if (AI_VALUE(uint8, "my attacker count") >= 2)
    {
        return false;
    }

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    for (ObjectGuid const& guid : attackers)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (ShouldTankPeelWyrmguard(botAI, bot, unit))
        {
            return true;
        }
    }

    GuidVector nearby = AI_VALUE(GuidVector, "nearest npcs");
    for (ObjectGuid const& guid : nearby)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (ShouldTankPeelWyrmguard(botAI, bot, unit))
        {
            return true;
        }
    }

    return false;
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
