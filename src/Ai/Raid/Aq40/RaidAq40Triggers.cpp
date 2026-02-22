#include "RaidAq40Triggers.h"

#include "ObjectGuid.h"

bool Aq40EngageTrigger::IsActive()
{
    if (!Aq40BossHelper::IsInAq40(bot))
        return false;

    if (!bot->IsInCombat())
        return false;

    return !AI_VALUE(GuidVector, "attackers").empty();
}

bool Aq40SkeramActiveTrigger::IsActive()
{
    if (!Aq40BossHelper::IsInAq40(bot) || !bot->IsInCombat())
        return false;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    for (ObjectGuid const guid : attackers)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (unit && botAI->EqualLowercaseName(unit->GetName(), "the prophet skeram"))
            return true;
    }

    return false;
}

bool Aq40SkeramBlinkTrigger::IsActive()
{
    Unit* currentTarget = AI_VALUE(Unit*, "current target");
    if (!currentTarget || !botAI->EqualLowercaseName(currentTarget->GetName(), "the prophet skeram"))
        return false;

    return !AI_VALUE2(bool, "has aggro", "current target");
}

bool Aq40SkeramArcaneExplosionTrigger::IsActive()
{
    if (!Aq40SkeramActiveTrigger::IsActive())
        return false;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    for (ObjectGuid const guid : attackers)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit || !botAI->EqualLowercaseName(unit->GetName(), "the prophet skeram"))
            continue;

        if (unit->GetCurrentSpell(CURRENT_GENERIC_SPELL))
            return true;
    }

    return false;
}

bool Aq40SkeramMindControlTrigger::IsActive()
{
    if (!Aq40SkeramActiveTrigger::IsActive())
        return false;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    for (ObjectGuid const guid : attackers)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit)
            continue;

        // "True Fulfillment" can force players/bots into hostile behavior.
        if (unit->IsPlayer() && (unit->IsCharmed() || botAI->HasAura("true fulfillment", unit)))
            return true;
    }

    return false;
}

bool Aq40SkeramSplitTrigger::IsActive()
{
    if (!Aq40SkeramActiveTrigger::IsActive())
        return false;

    uint32 skeramCount = 0;
    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    for (ObjectGuid const guid : attackers)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (unit && botAI->EqualLowercaseName(unit->GetName(), "the prophet skeram"))
            ++skeramCount;
    }

    return skeramCount >= 2;
}

bool Aq40SkeramExecutePhaseTrigger::IsActive()
{
    if (!Aq40SkeramActiveTrigger::IsActive())
        return false;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    for (ObjectGuid const guid : attackers)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (unit && botAI->EqualLowercaseName(unit->GetName(), "the prophet skeram") && unit->GetHealthPct() <= 25.0f)
            return true;
    }

    return false;
}

bool Aq40SarturaActiveTrigger::IsActive()
{
    if (!Aq40BossHelper::IsInAq40(bot) || !bot->IsInCombat())
        return false;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    for (ObjectGuid const guid : attackers)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit)
            continue;

        if (botAI->EqualLowercaseName(unit->GetName(), "battleguard sartura") ||
            botAI->EqualLowercaseName(unit->GetName(), "sartura's royal guard"))
            return true;
    }

    return false;
}

bool Aq40SarturaWhirlwindTrigger::IsActive()
{
    if (!Aq40SarturaActiveTrigger::IsActive() || botAI->IsTank(bot))
        return false;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    for (ObjectGuid const guid : attackers)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit)
            continue;

        bool isSarturaMob = botAI->EqualLowercaseName(unit->GetName(), "battleguard sartura") ||
                            botAI->EqualLowercaseName(unit->GetName(), "sartura's royal guard");
        if (!isSarturaMob)
            continue;

        // Use broad detection to avoid core-script dependencies:
        // either active cast window or whirlwind aura.
        bool spinning = unit->GetCurrentSpell(CURRENT_GENERIC_SPELL) || botAI->HasAura("whirlwind", unit);
        if (!spinning)
            continue;

        if (bot->GetDistance2d(unit) <= 14.0f)
            return true;
    }

    return false;
}
