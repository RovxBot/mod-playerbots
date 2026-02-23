#include "RaidBwlActions.h"

#include <limits>

#include "SharedDefines.h"

bool BwlWarnOnyxiaScaleCloakAction::Execute(Event /*event*/)
{
    botAI->TellMasterNoFacing("Warning: missing Onyxia Scale Cloak aura in BWL.");
    return true;
}

bool BwlTrashChooseTargetAction::Execute(Event /*event*/)
{
    BwlBossHelper helper(botAI);
    Unit* bestTarget = nullptr;
    int bestPriority = std::numeric_limits<int>::max();
    Unit* fallbackTarget = nullptr;
    int fallbackPriority = std::numeric_limits<int>::max();

    bool const isCaster = botAI->IsCaster(bot);
    bool const isPhysical = !isCaster;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    GuidVector nearby = AI_VALUE(GuidVector, "nearest npcs");

    auto evaluate = [&](GuidVector const& units)
    {
        for (ObjectGuid const guid : units)
        {
            Unit* unit = botAI->GetUnit(guid);
            int const priority = helper.GetTrashPriority(unit);
            if (priority == std::numeric_limits<int>::max())
            {
                continue;
            }

            // Second pass: role-aware validity for "melee-only/caster-only" style trash handling.
            bool validForRole = true;
            int adjustedPriority = priority;

            if (isCaster && helper.IsMagicImmuneTrash(unit))
            {
                validForRole = false;
                adjustedPriority += 1000;
            }
            else if (botAI->IsTank(bot) && helper.IsDeathTalonCaptain(unit))
            {
                // Captain Aura of Flames reflects punishing fire damage.
                // If tank is low, avoid direct focus while aura is active.
                if (bot->GetHealthPct() < 45.0f && botAI->GetAura("aura of flames", unit, false, true))
                {
                    validForRole = false;
                    adjustedPriority += 1000;
                }
            }
            else if (isPhysical && helper.IsCasterPreferredTrash(unit))
            {
                // Keep tanks/melee on physical-friendly threats first where possible.
                adjustedPriority += 6;
            }

            if (priority < fallbackPriority)
            {
                fallbackPriority = priority;
                fallbackTarget = unit;
            }

            if (validForRole && adjustedPriority < bestPriority)
            {
                bestPriority = adjustedPriority;
                bestTarget = unit;
            }
        }
    };

    evaluate(attackers);
    evaluate(nearby);

    if (!bestTarget)
    {
        bestTarget = fallbackTarget;
        bestPriority = fallbackPriority;
    }

    if (!bestTarget || bestPriority == std::numeric_limits<int>::max())
    {
        return false;
    }

    if (AI_VALUE(Unit*, "current target") == bestTarget)
    {
        return false;
    }

    return Attack(bestTarget, false);
}

bool BwlTrashTranqSeetherAction::isUseful()
{
    if (bot->getClass() != CLASS_HUNTER)
    {
        return false;
    }
    BwlBossHelper helper(botAI);
    return helper.HasEnragedDeathTalonSeetherNearbyOrAttacking();
}

bool BwlTrashTranqSeetherAction::Execute(Event /*event*/)
{
    if (bot->getClass() != CLASS_HUNTER)
    {
        return false;
    }

    Unit* target = nullptr;
    BwlBossHelper helper(botAI);

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    GuidVector nearby = AI_VALUE(GuidVector, "nearest npcs");

    auto findEnragedSeether = [&](GuidVector const& units)
    {
        for (ObjectGuid const guid : units)
        {
            Unit* unit = botAI->GetUnit(guid);
            if (!helper.IsDeathTalonSeether(unit))
            {
                continue;
            }
            if (helper.HasEnragedDeathTalonSeether(unit))
            {
                return unit;
            }
        }
        return static_cast<Unit*>(nullptr);
    };

    target = findEnragedSeether(attackers);
    if (!target)
    {
        target = findEnragedSeether(nearby);
    }
    if (!target)
    {
        return false;
    }

    return botAI->CastSpell("tranquilizing shot", target);
}

bool BwlTrashDetectMagicAction::isUseful()
{
    if (bot->getClass() != CLASS_MAGE)
    {
        return false;
    }
    BwlBossHelper helper(botAI);
    return helper.HasUndetectedDeathTalonNearbyOrAttacking();
}

bool BwlTrashDetectMagicAction::Execute(Event /*event*/)
{
    if (bot->getClass() != CLASS_MAGE)
    {
        return false;
    }

    Unit* target = nullptr;
    int bestPriority = std::numeric_limits<int>::max();
    BwlBossHelper helper(botAI);

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    GuidVector nearby = AI_VALUE(GuidVector, "nearest npcs");

    auto evaluate = [&](GuidVector const& units)
    {
        for (ObjectGuid const guid : units)
        {
            Unit* unit = botAI->GetUnit(guid);
            if (!helper.IsDeathTalonMob(unit))
            {
                continue;
            }

            if (botAI->GetAura("detect magic", unit, false, true))
            {
                continue;
            }

            int const priority = helper.GetDetectMagicPriority(unit);
            if (priority < bestPriority)
            {
                bestPriority = priority;
                target = unit;
            }
        }
    };

    evaluate(attackers);
    evaluate(nearby);

    if (!target)
    {
        return false;
    }

    return botAI->CastSpell("detect magic", target);
}
