#include "RaidAq40Triggers.h"

#include <initializer_list>

#include "ObjectGuid.h"

namespace
{
bool IsAnyNamedUnit(PlayerbotAI* botAI, GuidVector const& attackers, std::initializer_list<char const*> names)
{
    for (ObjectGuid const guid : attackers)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit)
            continue;

        for (char const* name : names)
        {
            if (botAI->EqualLowercaseName(unit->GetName(), name))
                return true;
        }
    }

    return false;
}
}  // namespace

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

bool Aq40FankrissActiveTrigger::IsActive()
{
    if (!Aq40BossHelper::IsInAq40(bot) || !bot->IsInCombat())
        return false;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    for (ObjectGuid const guid : attackers)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (unit && botAI->EqualLowercaseName(unit->GetName(), "fankriss the unyielding"))
            return true;
    }

    return false;
}

bool Aq40FankrissSpawnedTrigger::IsActive()
{
    if (!Aq40FankrissActiveTrigger::IsActive())
        return false;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    for (ObjectGuid const guid : attackers)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (unit && botAI->EqualLowercaseName(unit->GetName(), "spawn of fankriss"))
            return true;
    }

    return false;
}

bool Aq40HuhuranActiveTrigger::IsActive()
{
    if (!Aq40BossHelper::IsInAq40(bot) || !bot->IsInCombat())
        return false;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    for (ObjectGuid const guid : attackers)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (unit && botAI->EqualLowercaseName(unit->GetName(), "princess huhuran"))
            return true;
    }

    return false;
}

bool Aq40HuhuranPoisonPhaseTrigger::IsActive()
{
    if (!Aq40HuhuranActiveTrigger::IsActive())
        return false;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    for (ObjectGuid const guid : attackers)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit || !botAI->EqualLowercaseName(unit->GetName(), "princess huhuran"))
            continue;

        // Phase transition baseline:
        // spread ranged during the dangerous poison volley/enrage window.
        if (unit->GetHealthPct() <= 32.0f)
            return true;

        if (botAI->HasAura("frenzy", unit) || botAI->HasAura("berserk", unit))
            return true;
    }

    return false;
}

bool Aq40TwinEmperorsActiveTrigger::IsActive()
{
    if (!Aq40BossHelper::IsInAq40(bot) || !bot->IsInCombat())
        return false;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    for (ObjectGuid const guid : attackers)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit)
            continue;

        if (botAI->EqualLowercaseName(unit->GetName(), "emperor vek'nilash") ||
            botAI->EqualLowercaseName(unit->GetName(), "emperor vek'lor"))
            return true;
    }

    return false;
}

bool Aq40TwinEmperorsRoleMismatchTrigger::IsActive()
{
    if (!Aq40TwinEmperorsActiveTrigger::IsActive())
        return false;

    Unit* currentTarget = AI_VALUE(Unit*, "current target");
    if (!currentTarget)
        return true;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    bool hasVeknilash = false;
    bool hasVeklor = false;
    for (ObjectGuid const guid : attackers)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit)
            continue;

        if (botAI->EqualLowercaseName(unit->GetName(), "emperor vek'nilash"))
            hasVeknilash = true;
        else if (botAI->EqualLowercaseName(unit->GetName(), "emperor vek'lor"))
            hasVeklor = true;
    }

    bool preferVeknilash = botAI->IsTank(bot) || !botAI->IsRanged(bot);
    if (preferVeknilash &&
        botAI->EqualLowercaseName(currentTarget->GetName(), "emperor vek'lor") && hasVeknilash)
        return true;

    if (!preferVeknilash &&
        botAI->EqualLowercaseName(currentTarget->GetName(), "emperor vek'nilash") && hasVeklor)
        return true;

    bool onEmperor = botAI->EqualLowercaseName(currentTarget->GetName(), "emperor vek'nilash") ||
                     botAI->EqualLowercaseName(currentTarget->GetName(), "emperor vek'lor");
    return !onEmperor;
}

bool Aq40CthunActiveTrigger::IsActive()
{
    if (!Aq40BossHelper::IsInAq40(bot) || !bot->IsInCombat())
        return false;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    return IsAnyNamedUnit(botAI, attackers,
                          { "c'thun", "eye of c'thun", "eye tentacle", "claw tentacle",
                            "giant eye tentacle", "giant claw tentacle", "flesh tentacle" });
}

bool Aq40CthunPhase2Trigger::IsActive()
{
    if (!Aq40CthunActiveTrigger::IsActive())
        return false;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    return IsAnyNamedUnit(botAI, attackers,
                          { "c'thun", "giant eye tentacle", "giant claw tentacle", "flesh tentacle" });
}

bool Aq40CthunAddsPresentTrigger::IsActive()
{
    if (!Aq40CthunActiveTrigger::IsActive())
        return false;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    return IsAnyNamedUnit(botAI, attackers,
                          { "eye tentacle", "claw tentacle", "giant eye tentacle", "giant claw tentacle",
                            "flesh tentacle" });
}

bool Aq40CthunDarkGlareTrigger::IsActive()
{
    if (!Aq40CthunActiveTrigger::IsActive())
        return false;

    if (Aq40CthunInStomachTrigger(botAI).IsActive())
        return false;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    for (ObjectGuid const guid : attackers)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit)
            continue;

        if (!botAI->EqualLowercaseName(unit->GetName(), "eye of c'thun"))
            continue;

        if (unit->GetCurrentSpell(CURRENT_GENERIC_SPELL) || botAI->HasAura("dark glare", unit))
            return true;
    }

    return false;
}

bool Aq40CthunInStomachTrigger::IsActive()
{
    return botAI->GetAura("digestive acid", bot, false, false) ||
           botAI->GetAura("digestive acid", bot, false, true, 1);
}

bool Aq40CthunVulnerableTrigger::IsActive()
{
    if (!Aq40CthunPhase2Trigger::IsActive())
        return false;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    for (ObjectGuid const guid : attackers)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit)
            continue;

        if (botAI->EqualLowercaseName(unit->GetName(), "c'thun") && botAI->HasAura("weakened", unit))
            return true;
    }

    return false;
}

bool Aq40CthunEyeCastTrigger::IsActive()
{
    if (!Aq40CthunActiveTrigger::IsActive() || Aq40CthunInStomachTrigger(botAI).IsActive())
        return false;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    for (ObjectGuid const guid : attackers)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit)
            continue;

        bool isEyeTentacle = botAI->EqualLowercaseName(unit->GetName(), "eye tentacle") ||
                             botAI->EqualLowercaseName(unit->GetName(), "giant eye tentacle");
        if (isEyeTentacle && unit->GetCurrentSpell(CURRENT_GENERIC_SPELL))
            return true;
    }

    return false;
}
