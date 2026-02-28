#include "RaidBwlActions.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "AiFactory.h"
#include "SharedDefines.h"

namespace
{
std::string ToLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool IsDeathTalonWyrmguard(Unit* unit)
{
    if (!unit || !unit->IsAlive())
    {
        return false;
    }

    if (unit->GetTypeId() == TYPEID_UNIT && unit->GetEntry() == BwlCreatureIds::DeathTalonWyrmguard)
    {
        return true;
    }

    return ToLower(unit->GetName()).find("death talon wyrmguard") != std::string::npos;
}

std::vector<std::string> GetPreferredCasterSchools(Player* bot)
{
    std::vector<std::string> schools;
    if (!bot)
    {
        return schools;
    }

    int const tab = AiFactory::GetPlayerSpecTab(bot);

    switch (bot->getClass())
    {
        case CLASS_MAGE:
            if (tab == MAGE_TAB_FIRE)
                schools = {"fire"};
            else if (tab == MAGE_TAB_FROST)
                schools = {"frost"};
            else
                schools = {"arcane"};
            break;
        case CLASS_WARLOCK:
            if (tab == WARLOCK_TAB_DESTRUCTION)
                schools = {"fire", "shadow"};
            else
                schools = {"shadow", "fire"};
            break;
        case CLASS_PRIEST:
            schools = (tab == PRIEST_TAB_SHADOW) ? std::vector<std::string>{"shadow"} : std::vector<std::string>{"holy"};
            break;
        case CLASS_SHAMAN:
            schools = {"nature", "frost", "fire"};
            break;
        case CLASS_DRUID:
            schools = {"nature", "arcane"};
            break;
        case CLASS_PALADIN:
            schools = {"holy"};
            break;
        default:
            break;
    }

    return schools;
}

bool HasMatchingWyrmguardVulnerability(Player* bot, Unit* unit)
{
    if (!bot || !IsDeathTalonWyrmguard(unit))
    {
        return false;
    }

    std::vector<std::string> const schools = GetPreferredCasterSchools(bot);
    if (schools.empty())
    {
        return false;
    }

    Unit::AuraApplicationMap const& auraMap = unit->GetAppliedAuras();
    for (Unit::AuraApplicationMap::const_iterator it = auraMap.begin(); it != auraMap.end(); ++it)
    {
        Aura* aura = it->second ? it->second->GetBase() : nullptr;
        if (!aura || !aura->GetSpellInfo() || !aura->GetSpellInfo()->SpellName[0])
        {
            continue;
        }

        std::string const auraName = ToLower(aura->GetSpellInfo()->SpellName[0]);
        if (auraName.find("vulnerability") == std::string::npos && auraName.find("vulnerable") == std::string::npos)
        {
            continue;
        }

        for (std::vector<std::string>::const_iterator schoolIt = schools.begin(); schoolIt != schools.end(); ++schoolIt)
        {
            if (!schoolIt->empty() && auraName.find(*schoolIt) != std::string::npos)
            {
                return true;
            }
        }
    }

    return false;
}
}  // namespace

bool BwlWarnOnyxiaScaleCloakAction::Execute(Event /*event*/)
{
    if (botAI->HasAura(BwlSpellIds::OnyxiaScaleCloakAura, bot))
    {
        return false;
    }

    Aura* const aura = bot->AddAura(BwlSpellIds::OnyxiaScaleCloakAura, bot);
    if (!aura)
    {
        botAI->TellMasterNoFacing("Warning: failed to apply Onyxia Scale Cloak aura in BWL.");
        return false;
    }

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
            else if (isCaster && HasMatchingWyrmguardVulnerability(bot, unit))
            {
                // Strongly prefer Wyrmguards we are naturally effective against after Detect Magic reveals vulnerability.
                adjustedPriority -= 20;
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

bool BwlTrashSafePositionAction::Execute(Event /*event*/)
{
    BwlBossHelper helper(botAI);
    if (!helper.IsDangerousTrashEncounterActive())
    {
        return false;
    }

    Unit* target = AI_VALUE(Unit*, "current target");
    if (!target || !target->IsAlive() || !helper.IsDangerousTrash(target))
    {
        return false;
    }

    float targetX = target->GetPositionX();
    float targetY = target->GetPositionY();
    float targetZ = bot->GetPositionZ();
    float const facing = target->GetOrientation();
    uint32 const slot = botAI->GetGroupSlotIndex(bot);

    float angleOffset = 0.0f;
    float distance = 0.0f;

    if (botAI->IsMainTank(bot))
    {
        // MT holds in front, but not too deep to reduce extra frontal clipping from nearby mobs.
        angleOffset = 0.0f;
        distance = 5.0f;
    }
    else if (botAI->IsAssistTankOfIndex(bot, 0))
    {
        // OT anchors on side so only one tank is exposed to the frontal arc.
        angleOffset = static_cast<float>(M_PI / 2.0f);
        distance = 7.5f;
    }
    else if (botAI->IsRanged(bot) || botAI->IsHeal(bot))
    {
        float spread = ((slot % 6) - 2.5f) * 0.12f;
        angleOffset = static_cast<float>(-M_PI / 2.0f) + spread;
        distance = botAI->IsHeal(bot) ? 18.0f : 22.0f;
    }
    else
    {
        // Melee DPS stays behind-side on dangerous trash to avoid frontal cleave/parry-haste risk.
        float spread = ((slot % 4) - 1.5f) * 0.20f;
        angleOffset = static_cast<float>(-M_PI / 2.0f) + spread;
        distance = 6.0f;
    }

    float angle = facing + angleOffset;
    targetX += std::cos(angle) * distance;
    targetY += std::sin(angle) * distance;

    if (MoveTo(bot->GetMapId(), targetX, targetY, targetZ, false, false, false, false, MovementPriority::MOVEMENT_COMBAT))
    {
        return true;
    }

    return MoveInside(bot->GetMapId(), targetX, targetY, targetZ, 2.0f, MovementPriority::MOVEMENT_COMBAT);
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
