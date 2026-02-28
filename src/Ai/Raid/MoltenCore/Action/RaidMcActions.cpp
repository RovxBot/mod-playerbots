#include "RaidMcActions.h"

#include <cmath>
#include <limits>
#include <vector>

#include "Playerbots.h"
#include "Pet.h"
#include "RtiTargetValue.h"
#include "RaidMcHelpers.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

static constexpr float LIVING_BOMB_DISTANCE = 20.0f;
static constexpr float INFERNO_DISTANCE = 20.0f;

// don't get hit by Arcane Explosion but still be in casting range
static constexpr float ARCANE_EXPLOSION_DISTANCE = 26.0f;

// dedicated tank positions; prevents assist tanks from positioning Core Ragers on steep walls on pull
static const Position GOLEMAGG_TANK_POSITION{795.7308, -994.8848, -207.18661};
static const Position CORE_RAGER_TANK_POSITION{846.6453, -1019.0639, -198.9819};

static constexpr float GOLEMAGGS_TRUST_DISTANCE = 30.0f;
static constexpr float CORE_RAGER_STEP_DISTANCE = 5.0f;
static constexpr float RAGNAROS_MELEE_STEP_OUT_DISTANCE = 22.0f;
static constexpr float PI = 3.14159265358979323846f;
static constexpr float RAGNAROS_RANGED_RING_RADIUS = 34.0f;
static constexpr float RAGNAROS_HEALER_RING_RADIUS = 30.0f;
static constexpr float RAGNAROS_RING_SPACING = 8.0f;
static constexpr float RAGNAROS_RING_HEIGHT_TOLERANCE = 18.0f;
static constexpr float RAGNAROS_RING_MOVE_TOLERANCE = 1.8f;

using namespace MoltenCoreHelpers;

namespace
{
bool IsSafeRagnarosAnchor(Player* bot, float x, float y, float bossZ, float& zOut)
{
    Map* map = bot->GetMap();
    if (!map)
    {
        return false;
    }

    float const ground = map->GetHeight(x, y, bot->GetPositionZ() + 20.0f);
    float const water = map->GetWaterLevel(x, y);
    float const z = std::max(ground, water);
    if (!std::isfinite(z))
    {
        return false;
    }

    // Ragnaros room has lava; reject anchors in liquid.
    if (map->IsInWater(bot->GetPhaseMask(), x, y, z, bot->GetCollisionHeight()))
    {
        return false;
    }

    if (std::fabs(z - bossZ) > RAGNAROS_RING_HEIGHT_TOLERANCE)
    {
        return false;
    }

    zOut = z;
    return true;
}

std::vector<Position> BuildRagnarosRingAnchors(Player* bot, Unit* ragnaros, float radius)
{
    std::vector<Position> anchors;
    if (!bot || !ragnaros)
    {
        return anchors;
    }

    float const step = RAGNAROS_RING_SPACING / radius;  // ~8y spacing along arc
    for (float angle = 0.0f; angle < (2.0f * PI); angle += step)
    {
        float const testX = ragnaros->GetPositionX() + std::cos(angle) * radius;
        float const testY = ragnaros->GetPositionY() + std::sin(angle) * radius;
        float testZ = 0.0f;
        if (IsSafeRagnarosAnchor(bot, testX, testY, ragnaros->GetPositionZ(), testZ))
        {
            anchors.emplace_back(testX, testY, testZ, 0.0f);
        }
    }

    return anchors;
}
}

bool McMoveFromGroupAction::Execute(Event /*event*/)
{
    return MoveFromGroup(LIVING_BOMB_DISTANCE);
}

bool McMoveFromBaronGeddonAction::Execute(Event /*event*/)
{
    if (Unit* boss = AI_VALUE2(Unit*, "find target", "baron geddon"))
    {
        float distToTravel = INFERNO_DISTANCE - bot->GetDistance2d(boss);
        if (distToTravel > 0)
        {
            // Stop current spell first
            bot->AttackStop();
            bot->InterruptNonMeleeSpells(false);

            return MoveAway(boss, distToTravel);
        }
    }
    return false;
}

bool McShazzrahMoveAwayAction::Execute(Event /*event*/)
{
    if (Unit* boss = AI_VALUE2(Unit*, "find target", "shazzrah"))
    {
        float distToTravel = ARCANE_EXPLOSION_DISTANCE - bot->GetDistance2d(boss);
        if (distToTravel > 0)
            return MoveAway(boss, distToTravel);
    }
    return false;
}

bool McGolemaggMarkBossAction::Execute(Event /*event*/)
{
    if (Unit* boss = AI_VALUE2(Unit*, "find target", "golemagg the incinerator"))
    {
        if (Group* group = bot->GetGroup())
        {
            ObjectGuid currentSkullGuid = group->GetTargetIcon(RtiTargetValue::skullIndex);
            if (currentSkullGuid.IsEmpty() || currentSkullGuid != boss->GetGUID())
            {
                group->SetTargetIcon(RtiTargetValue::skullIndex, bot->GetGUID(), boss->GetGUID());
                return true;
            }
        }
    }
    return false;
}

bool McGolemaggTankAction::MoveUnitToPosition(Unit* target, const Position& tankPosition, float maxDistance,
                                              float stepDistance)
{
    if (bot->GetVictim() != target)
        return Attack(target);
    if (target->GetVictim() == bot)
    {
        float distanceToTankPosition = bot->GetExactDist2d(tankPosition.GetPositionX(), tankPosition.GetPositionY());
        if (distanceToTankPosition > maxDistance)
        {
            float dX = tankPosition.GetPositionX() - bot->GetPositionX();
            float dY = tankPosition.GetPositionY() - bot->GetPositionY();
            float dist = sqrt(dX * dX + dY * dY);
            float moveX = bot->GetPositionX() + (dX / dist) * stepDistance;
            float moveY = bot->GetPositionY() + (dY / dist) * stepDistance;
            return MoveTo(bot->GetMapId(), moveX, moveY, bot->GetPositionZ(), false, false,
                          false, false, MovementPriority::MOVEMENT_COMBAT, true,
                          true);
        }
    }
    else if (botAI->DoSpecificAction("taunt spell", Event(), true))
        return true;
    return false;
}

bool McGolemaggTankAction::FindCoreRagers(Unit*& coreRager1, Unit*& coreRager2) const
{
    coreRager1 = coreRager2 = nullptr;
    for (auto const& target : AI_VALUE(GuidVector, "possible targets no los"))
    {
        Unit* unit = botAI->GetUnit(target);
        if (unit && unit->IsAlive() && unit->GetEntry() == NPC_CORE_RAGER)
        {
            if (coreRager1 == nullptr)
                coreRager1 = unit;
            else if (coreRager2 == nullptr)
            {
                coreRager2 = unit;
                break; // There should be no third Core Rager.
            }
        }
    }
    return coreRager1 != nullptr && coreRager2 != nullptr;
}

bool McGolemaggMainTankAttackGolemaggAction::Execute(Event /*event*/)
{
    // At this point, we know we are not the last living tank in the group.
    if (Unit* boss = AI_VALUE2(Unit*, "find target", "golemagg the incinerator"))
    {
        Unit* coreRager1;
        Unit* coreRager2;
        if (!FindCoreRagers(coreRager1, coreRager2))
            return false; // safety check

        // We only need to move if the Core Ragers still have Golemagg's Trust
        if (coreRager1->HasAura(SPELL_GOLEMAGGS_TRUST) || coreRager2->HasAura(SPELL_GOLEMAGGS_TRUST))
            return MoveUnitToPosition(boss, GOLEMAGG_TANK_POSITION, boss->GetCombatReach());
    }
    return false;
}

bool McGolemaggAssistTankAttackCoreRagerAction::Execute(Event event)
{
    Unit* boss = AI_VALUE2(Unit*, "find target", "golemagg the incinerator");
    if (!boss)
        return false;

    // Step 0: Filter additional assist tanks. We only need 2.
    bool isFirstAssistTank = PlayerbotAI::IsAssistTankOfIndex(bot, 0, true);
    bool isSecondAssistTank = PlayerbotAI::IsAssistTankOfIndex(bot, 1, true);
    if (!isFirstAssistTank && !isSecondAssistTank)
        return Attack(boss);

    // Step 1: Find both Core Ragers
    Unit* coreRager1;
    Unit* coreRager2;
    if (!FindCoreRagers(coreRager1, coreRager2))
        return false; // safety check

    // Step 2: Assign Core Rager to bot
    Unit* myCoreRager = nullptr;
    Unit* otherCoreRager = nullptr;
    if (isFirstAssistTank)
    {
        myCoreRager = coreRager1;
        otherCoreRager = coreRager2;
    }
    else // isSecondAssistTank is always true here
    {
        myCoreRager = coreRager2;
        otherCoreRager = coreRager1;
    }

    // Step 3: Select the right target
    if (myCoreRager->GetVictim() != bot)
    {
        // Step 3.1: My Core Rager isn't attacking me. Attack until it does.
        if (bot->GetVictim() != myCoreRager)
            return Attack(myCoreRager);
        return botAI->DoSpecificAction("taunt spell", event, true);
    }

    Unit* otherCoreRagerVictim = otherCoreRager->GetVictim();
    if (otherCoreRagerVictim) // Core Rager victim can be NULL
    {
        // Step 3.2: Check if the other Core Rager isn't attacking its assist tank.
        Player* otherCoreRagerPlayerVictim = otherCoreRagerVictim->ToPlayer();
        if (otherCoreRagerPlayerVictim &&
            !PlayerbotAI::IsAssistTankOfIndex(otherCoreRagerPlayerVictim, 0, true) &&
            !PlayerbotAI::IsAssistTankOfIndex(otherCoreRagerPlayerVictim, 1, true))
        {
            // Assume we are the only assist tank or the other assist tank is dead => pick up other Core Rager!
            if (bot->GetVictim() != otherCoreRager)
                return Attack(otherCoreRager);
            return botAI->DoSpecificAction("taunt spell", event, true);
        }
    }

    if (bot->GetVictim() != myCoreRager)
        return Attack(myCoreRager); // Step 3.3: Attack our Core Rager in case we previously switched in 3.2.

    // Step 4: Prevent Golemagg's Trust on Core Ragers
    if (myCoreRager->HasAura(SPELL_GOLEMAGGS_TRUST) ||
        (otherCoreRagerVictim == bot && otherCoreRager->HasAura(SPELL_GOLEMAGGS_TRUST)))
    {
        // Step 4.1: Move Core Ragers to dedicated tank position (only if Golemagg is far enough away from said position)
        float bossDistanceToCoreRagerTankPosition = boss->GetExactDist2d(
            CORE_RAGER_TANK_POSITION.GetPositionX(), CORE_RAGER_TANK_POSITION.GetPositionY());
        if (bossDistanceToCoreRagerTankPosition > GOLEMAGGS_TRUST_DISTANCE)
        {
            float distanceToTankPosition = bot->GetExactDist2d(CORE_RAGER_TANK_POSITION.GetPositionX(),
                                                               CORE_RAGER_TANK_POSITION.GetPositionY());
            if (distanceToTankPosition > CORE_RAGER_STEP_DISTANCE)
                return MoveUnitToPosition(myCoreRager, CORE_RAGER_TANK_POSITION, CORE_RAGER_STEP_DISTANCE);
        }

        // Step 4.2: if boss is too close to tank position, or we are already there, move away from Golemagg to try to out-range Golemagg's Trust
        return MoveAway(boss, CORE_RAGER_STEP_DISTANCE, true);
    }

    return false;
}

bool McRagnarosPositionAction::Execute(Event /*event*/)
{
    Unit* ragnaros = AI_VALUE2(Unit*, "find target", "ragnaros");
    if (!ragnaros || !ragnaros->IsAlive() || ragnaros->HasAura(SPELL_RAGSUBMERGE))
    {
        return false;
    }

    float targetX = ragnaros->GetPositionX();
    float targetY = ragnaros->GetPositionY();
    float targetZ = ragnaros->GetPositionZ();
    float const facing = ragnaros->GetOrientation();
    uint32 const slot = botAI->GetGroupSlotIndex(bot);

    float angleOffset = 0.0f;
    float distance = 0.0f;

    if (botAI->IsMainTank(bot))
    {
        angleOffset = 0.0f;
        distance = 7.0f;
    }
    else if (botAI->IsAssistTankOfIndex(bot, 0))
    {
        angleOffset = PI / 8.0f;
        distance = 10.0f;
    }
    else if (botAI->IsRanged(bot) || botAI->IsHeal(bot))
    {
        // Fixed ring anchors with ~8y spacing around Ragnaros; only safe non-lava points are used.
        float const radius = botAI->IsHeal(bot) ? RAGNAROS_HEALER_RING_RADIUS : RAGNAROS_RANGED_RING_RADIUS;
        std::vector<Position> anchors = BuildRagnarosRingAnchors(bot, ragnaros, radius);
        if (!anchors.empty())
        {
            Position const& anchor = anchors[slot % anchors.size()];
            targetX = anchor.GetPositionX();
            targetY = anchor.GetPositionY();
            targetZ = anchor.GetPositionZ();

            if (bot->GetExactDist2d(targetX, targetY) <= RAGNAROS_RING_MOVE_TOLERANCE)
            {
                return false;
            }

            if (MoveTo(bot->GetMapId(), targetX, targetY, targetZ, false, false, false, false,
                       MovementPriority::MOVEMENT_COMBAT))
            {
                return true;
            }

            return MoveInside(bot->GetMapId(), targetX, targetY, targetZ, 2.0f, MovementPriority::MOVEMENT_COMBAT);
        }

        // Fallback if no valid ring anchor could be found.
        float spread = ((slot % 8) - 3.5f) * 0.18f;
        angleOffset = PI + spread;
        distance = radius;
    }
    else
    {
        float spread = ((slot % 5) - 2.0f) * 0.10f;
        angleOffset = PI + spread;
        distance = 7.5f;
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

bool McRagnarosMeleeStepOutAction::Execute(Event /*event*/)
{
    Unit* ragnaros = AI_VALUE2(Unit*, "find target", "ragnaros");
    if (!ragnaros || !ragnaros->IsAlive() || ragnaros->HasAura(SPELL_RAGSUBMERGE))
    {
        return false;
    }

    float const currentDistance = bot->GetDistance2d(ragnaros);
    float const distToTravel = RAGNAROS_MELEE_STEP_OUT_DISTANCE - currentDistance;
    if (distToTravel <= 0.0f)
    {
        return false;
    }

    return MoveAway(ragnaros, distToTravel, true);
}

bool McRagnarosSonsTargetAction::Execute(Event /*event*/)
{
    GuidVector attackers = context->GetValue<GuidVector>("attackers")->Get();
    GuidVector nearby = context->GetValue<GuidVector>("nearest npcs")->Get();

    Unit* best = nullptr;
    float bestDist = std::numeric_limits<float>::max();

    auto consider = [&](GuidVector const& units)
    {
        for (ObjectGuid const guid : units)
        {
            Unit* unit = botAI->GetUnit(guid);
            if (!unit || !unit->IsAlive() || unit->GetEntry() != NPC_SON_OF_FLAME)
            {
                continue;
            }

            float const dist = bot->GetExactDist2d(unit);
            if (!best || dist < bestDist)
            {
                best = unit;
                bestDist = dist;
            }
        }
    };

    consider(attackers);
    consider(nearby);

    if (!best)
    {
        return false;
    }

    if (AI_VALUE(Unit*, "current target") == best)
    {
        return false;
    }

    return Attack(best, false);
}

bool McDisableHunterPetGrowlAction::Execute(Event /*event*/)
{
    if (bot->GetMapId() != 409 || bot->getClass() != CLASS_HUNTER)
    {
        return false;
    }

    Pet* pet = bot->GetPet();
    if (!pet)
    {
        return false;
    }

    bool changed = false;
    for (PetSpellMap::const_iterator itr = pet->m_spells.begin(); itr != pet->m_spells.end(); ++itr)
    {
        if (itr->second.state == PETSPELL_REMOVED)
        {
            continue;
        }

        SpellInfo const* info = sSpellMgr->GetSpellInfo(itr->first);
        if (!info || !info->IsAutocastable() || !info->SpellName[0])
        {
            continue;
        }

        bool isAuto = false;
        for (unsigned int autospell : pet->m_autospells)
        {
            if (autospell == info->Id)
            {
                isAuto = true;
                break;
            }
        }

        if (!strcmpi(info->SpellName[0], "growl") && isAuto)
        {
            pet->ToggleAutocast(info, false);
            changed = true;
        }
        else if (!strcmpi(info->SpellName[0], "cower") && !isAuto)
        {
            pet->ToggleAutocast(info, true);
            changed = true;
        }
    }

    return changed;
}
