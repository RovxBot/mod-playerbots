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

bool IsWyrmguardThreateningRaid(PlayerbotAI* botAI, Player* bot, Unit* unit)
{
    if (!botAI || !bot || !IsDeathTalonWyrmguard(unit))
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

bool IsPositionRiskyPull(PlayerbotAI* botAI, Unit* currentTarget, float x, float y)
{
    if (!botAI)
    {
        return false;
    }

    GuidVector nearby;
    if (AiObjectContext* ctx = botAI->GetAiObjectContext())
    {
        nearby = ctx->GetValue<GuidVector>("nearest npcs")->Get();
    }
    for (ObjectGuid const& guid : nearby)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit || !unit->IsAlive() || unit == currentTarget)
        {
            continue;
        }

        // Respect packs that are not yet in combat; avoid pathing close enough to body-pull.
        if (unit->IsInCombat())
        {
            continue;
        }

        if (unit->GetDistance2d(x, y) < 12.0f)
        {
            return true;
        }
    }

    return false;
}

bool GetRaidCenter(PlayerbotAI* botAI, Player* bot, float& outX, float& outY)
{
    if (!botAI || !bot)
    {
        return false;
    }

    GuidVector nearbyPlayers;
    if (AiObjectContext* ctx = botAI->GetAiObjectContext())
    {
        nearbyPlayers = ctx->GetValue<GuidVector>("nearest friendly players")->Get();
    }
    float sumX = bot->GetPositionX();
    float sumY = bot->GetPositionY();
    uint32 count = 1;

    for (ObjectGuid const& guid : nearbyPlayers)
    {
        Unit* unit = botAI->GetUnit(guid);
        Player* player = unit ? unit->ToPlayer() : nullptr;
        if (!player || player == bot || !player->IsAlive() || !player->IsInWorld())
        {
            continue;
        }

        if (bot->GetDistance2d(player) > 45.0f)
        {
            continue;
        }

        sumX += player->GetPositionX();
        sumY += player->GetPositionY();
        ++count;
    }

    outX = sumX / count;
    outY = sumY / count;
    return true;
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
    bool const isTank = botAI->IsTank(bot);
    uint8 const myAttackers = AI_VALUE(uint8, "my attacker count");

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
            else if (isTank && helper.IsDeathTalonCaptain(unit))
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

            // Tanks: peel Wyrmguards from non-tanks, but do not exceed two active attackers.
            if (isTank && IsDeathTalonWyrmguard(unit))
            {
                if (IsWyrmguardThreateningRaid(botAI, bot, unit))
                {
                    if (myAttackers >= 2)
                    {
                        validForRole = false;
                        adjustedPriority += 1000;
                    }
                    else
                    {
                        adjustedPriority -= 30;
                    }
                }
                else if (unit->GetVictim() == bot)
                {
                    adjustedPriority -= 10;
                }
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

    float angle = facing;
    float distance = 0.0f;

    if (botAI->IsMainTank(bot))
    {
        // MT should drag Wyrmguards away from raid center to keep frontal cleave off the group.
        float raidX = 0.0f;
        float raidY = 0.0f;
        if (GetRaidCenter(botAI, bot, raidX, raidY))
        {
            angle = std::atan2(targetY - raidY, targetX - raidX);
        }
        distance = 7.0f;
    }
    else if (botAI->IsAssistTankOfIndex(bot, 0))
    {
        float raidX = 0.0f;
        float raidY = 0.0f;
        float awayAngle = facing;
        if (GetRaidCenter(botAI, bot, raidX, raidY))
        {
            awayAngle = std::atan2(targetY - raidY, targetX - raidX);
        }
        angle = awayAngle + static_cast<float>(M_PI / 5.0f);
        distance = 8.0f;
    }
    else if (botAI->IsRanged(bot) || botAI->IsHeal(bot))
    {
        // Casters/healers stay at range and behind the mob, never in front.
        float spread = ((slot % 6) - 2.5f) * 0.10f;
        angle = facing + static_cast<float>(M_PI) + spread;
        distance = botAI->IsHeal(bot) ? 16.0f : 19.0f;
    }
    else
    {
        // Melee DPS anchors behind with slight spread; never take a frontal lane.
        float spread = ((slot % 4) - 1.5f) * 0.16f;
        angle = facing + static_cast<float>(M_PI) + spread;
        distance = 6.0f;
    }

    // Prevent pathing into nearby unengaged packs while trying to maintain ideal range.
    float chosenX = targetX + std::cos(angle) * distance;
    float chosenY = targetY + std::sin(angle) * distance;
    while (distance > 7.0f && IsPositionRiskyPull(botAI, target, chosenX, chosenY))
    {
        distance -= 2.0f;
        chosenX = targetX + std::cos(angle) * distance;
        chosenY = targetY + std::sin(angle) * distance;
    }

    if (IsPositionRiskyPull(botAI, target, chosenX, chosenY))
    {
        return false;
    }

    if (MoveTo(bot->GetMapId(), chosenX, chosenY, targetZ, false, false, false, false, MovementPriority::MOVEMENT_COMBAT))
    {
        return true;
    }

    return MoveInside(bot->GetMapId(), chosenX, chosenY, targetZ, 2.0f, MovementPriority::MOVEMENT_COMBAT);
}

bool BwlWyrmguardControlAction::Execute(Event /*event*/)
{
    BwlBossHelper helper(botAI);
    if (!helper.IsDangerousTrashEncounterActive() || !botAI->IsTank(bot))
    {
        return false;
    }

    if (AI_VALUE(uint8, "my attacker count") >= 2)
    {
        return false;
    }

    Unit* peelTarget = nullptr;
    auto findPeelTarget = [&](GuidVector const& units)
    {
        for (ObjectGuid const& guid : units)
        {
            Unit* unit = botAI->GetUnit(guid);
            if (IsWyrmguardThreateningRaid(botAI, bot, unit))
            {
                peelTarget = unit;
                return true;
            }
        }
        return false;
    };

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    if (!findPeelTarget(attackers))
    {
        GuidVector nearby = AI_VALUE(GuidVector, "nearest npcs");
        findPeelTarget(nearby);
    }

    if (!peelTarget)
    {
        return false;
    }

    if (AI_VALUE(Unit*, "current target") != peelTarget)
    {
        Attack(peelTarget, false);
    }

    return botAI->DoSpecificAction("taunt spell", Event(), true);
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
