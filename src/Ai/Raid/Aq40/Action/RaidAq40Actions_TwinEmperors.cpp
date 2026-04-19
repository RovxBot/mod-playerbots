#include "RaidAq40Actions.h"

#include <cmath>

#include "SharedDefines.h"
#include "../RaidAq40BossHelper.h"
#include "RaidBossHelpers.h"
#include "../RaidAq40SpellIds.h"
#include "../Util/RaidAq40Helpers.h"

namespace
{
// Room geometry constants - grounded in repo travel-node data.
float constexpr kTwinRoomCenterX = -8954.855f;
float constexpr kTwinRoomCenterY = 1235.7107f;
float constexpr kTwinRoomCenterZ = -112.62047f;
float constexpr kTwinInitialVeklorX = -8868.31f;
float constexpr kTwinInitialVeklorY = 1205.97f;
float constexpr kTwinInitialVeklorZ = -104.231f;
float constexpr kTwinInitialVeknilashX = -9023.67f;
float constexpr kTwinInitialVeknilashY = 1176.24f;
float constexpr kTwinInitialVeknilashZ = -104.226f;

Position GetTwinSideAnchor(uint32 sideIndex)
{
    // Stage 40y from boss toward room center — outside ~25y aggro range
    // but close enough to engage quickly on pull.
    float bossX, bossY, bossZ;
    if (sideIndex == 1u)
    {
        bossX = kTwinInitialVeklorX;  bossY = kTwinInitialVeklorY;  bossZ = kTwinInitialVeklorZ;
    }
    else
    {
        bossX = kTwinInitialVeknilashX;  bossY = kTwinInitialVeknilashY;  bossZ = kTwinInitialVeknilashZ;
    }

    float const dirX = kTwinRoomCenterX - bossX;
    float const dirY = kTwinRoomCenterY - bossY;
    float const length = std::sqrt(dirX * dirX + dirY * dirY);
    if (length < 0.1f)
    {
        Position anchor;
        anchor.Relocate(bossX, bossY, bossZ);
        return anchor;
    }

    float constexpr kStageDistance = 40.0f;
    Position anchor;
    anchor.Relocate(bossX + (dirX / length) * kStageDistance,
                    bossY + (dirY / length) * kStageDistance,
                    bossZ);
    return anchor;
}

void PinTwinTarget(PlayerbotAI* botAI, AiObjectContext* context, Unit* target)
{
    if (!botAI || !context || !target)
        return;

    ObjectGuid const guid = target->GetGUID();
    botAI->GetAiObjectContext()->GetValue<GuidVector>("prioritized targets")->Set({ guid });
    context->GetValue<ObjectGuid>("pull target")->Set(guid);
}

// Compute a position on the far side of boss, away from otherBoss, at desiredRange.
bool GetFarSidePosition(Player* bot, Unit* boss, Unit* otherBoss,
                        float desiredRange, Position& outPosition)
{
    if (!bot || !boss || !bot->GetMap())
        return false;

    float dirX, dirY;
    if (otherBoss)
    {
        dirX = boss->GetPositionX() - otherBoss->GetPositionX();
        dirY = boss->GetPositionY() - otherBoss->GetPositionY();
    }
    else
    {
        dirX = boss->GetPositionX() - kTwinRoomCenterX;
        dirY = boss->GetPositionY() - kTwinRoomCenterY;
    }

    float const length = std::sqrt(dirX * dirX + dirY * dirY);
    if (length < 0.1f)
        return false;

    float const baseAngle = std::atan2(dirY, dirX);
    static constexpr float kRetryOffsets[] = { 0.0f, 0.2f, -0.2f, 0.4f, -0.4f };
    for (float offset : kRetryOffsets)
    {
        float const angle = baseAngle + offset;
        float targetX = boss->GetPositionX() + desiredRange * std::cos(angle);
        float targetY = boss->GetPositionY() + desiredRange * std::sin(angle);
        float targetZ = boss->GetPositionZ();
        if (bot->GetMap()->CheckCollisionAndGetValidCoords(bot, bot->GetPositionX(), bot->GetPositionY(),
                bot->GetPositionZ(), targetX, targetY, targetZ))
        {
            outPosition.Relocate(targetX, targetY, targetZ);
            return true;
        }
    }
    return false;
}
}  // namespace

// ---------------------------------------------------------------------------
// PrePullStageAction - on room entry, tanks move to assigned sides, raid
// holds center.  This confirms tank selection before the pull.
// ---------------------------------------------------------------------------
bool Aq40TwinEmperorsPrePullStageAction::isUseful()
{
    return Aq40Helpers::IsTwinPrePullReady(bot, botAI);
}

bool Aq40TwinEmperorsPrePullStageAction::Execute(Event /*event*/)
{
    if (!Aq40Helpers::IsTwinPrePullReady(bot, botAI))
        return false;

    bool const isWarlockTank = Aq40BossHelper::IsDesignatedTwinWarlockTank(bot);
    bool const isMeleeTank = PlayerbotAI::IsTank(bot) && !PlayerbotAI::IsRanged(bot);

    if (isWarlockTank || isMeleeTank)
    {
        // Tanks move to their assigned side anchor.
        uint32 const sideIndex = Aq40Helpers::GetStableTwinRoleIndex(bot, botAI);
        Position anchor = GetTwinSideAnchor(sideIndex);

        if (bot->GetExactDist2d(anchor.GetPositionX(), anchor.GetPositionY()) <= 5.0f)
        {
            // Mark bosses for the raid while holding position.
            GuidVector preUnits = Aq40Helpers::GetTwinEncounterUnits(bot, botAI,
                context->GetValue<GuidVector>("attackers")->Get());
            Aq40Helpers::TwinAssignments preAssign = Aq40Helpers::GetTwinAssignments(bot, botAI, preUnits);
            if (preAssign.veknilash)
                MarkTargetWithDiamond(bot, preAssign.veknilash);
            if (preAssign.veklor)
                MarkTargetWithSquare(bot, preAssign.veklor);
            return true;  // Already in position - hold.
        }

        bool moved = MoveTo(bot->GetMapId(), anchor.GetPositionX(), anchor.GetPositionY(),
                      anchor.GetPositionZ(), false, false, false, true,
                      MovementPriority::MOVEMENT_COMBAT, true, false);
        // Still en route — hold action slot even if MoveTo dedup returned false.
        return moved || bot->GetExactDist2d(anchor.GetPositionX(), anchor.GetPositionY()) > 5.0f;
    }

    // Everyone else moves to room center and holds.
    if (bot->GetExactDist2d(kTwinRoomCenterX, kTwinRoomCenterY) <= 5.0f)
        return true;  // Already in position - hold.

    {
        bool moved = MoveTo(bot->GetMapId(), kTwinRoomCenterX, kTwinRoomCenterY, kTwinRoomCenterZ,
                      false, false, false, true,
                      MovementPriority::MOVEMENT_COMBAT, true, false);
        return moved || bot->GetExactDist2d(kTwinRoomCenterX, kTwinRoomCenterY) > 5.0f;
    }
}

// ---------------------------------------------------------------------------
// ChooseTargetAction - pin correct target, move into range, and attack.
// Runs every tick.  Returns false only when the bot is on the correct
// target AND in range, so the class AI can take over spell casting.
//
// Warlock tank  -> defer to WarlockTankAction
// Melee tank    -> defer to HoldSplit for positioning
// Ranged DPS    -> Vek'lor
// Melee DPS     -> Vek'nilash
// Hunter        -> mutate bugs first, then Vek'lor
// Healer        -> return false (class AI handles healing)
// ---------------------------------------------------------------------------
bool Aq40TwinEmperorsChooseTargetAction::Execute(Event /*event*/)
{
    GuidVector encounterUnits = Aq40Helpers::GetTwinEncounterUnits(bot, botAI,
        context->GetValue<GuidVector>("attackers")->Get());
    Aq40Helpers::TwinAssignments assignment = Aq40Helpers::GetTwinAssignments(bot, botAI, encounterUnits);
    if (!assignment.veklor && !assignment.veknilash)
        return false;

    bool const isWarlockTank = Aq40BossHelper::IsDesignatedTwinWarlockTank(bot);
    bool const isMeleeTank = !isWarlockTank && PlayerbotAI::IsTank(bot) && !PlayerbotAI::IsRanged(bot);

    // Tanks are handled by WarlockTankAction and HoldSplitAction.
    if (isWarlockTank || isMeleeTank)
        return false;

    bool const isHealer = botAI->IsHeal(bot);
    if (isHealer)
    {
        // Position healers on their assigned side, 28y from boss on the far side
        // (outside 18y Arcane Burst range, within 40y heal range of tanks).
        Unit* healBoss = assignment.sideEmperor;
        if (!healBoss)
            return false;

        Position healPos;
        if (!GetFarSidePosition(bot, healBoss, assignment.oppositeEmperor, 28.0f, healPos))
            return false;

        if (bot->GetExactDist2d(healPos.GetPositionX(), healPos.GetPositionY()) <= 5.0f)
            return false;  // In position — let class AI handle healing.

        bool moved = MoveNear(healBoss, 28.0f, MovementPriority::MOVEMENT_COMBAT);
        if (!moved && bot->GetExactDist2d(healPos.GetPositionX(), healPos.GetPositionY()) > 5.0f)
            return true;  // Still en route.
        return moved;
    }

    bool const isHunter = bot->getClass() == CLASS_HUNTER;
    bool const isRangedDps = botAI->IsRanged(bot);

    Unit* desiredTarget = nullptr;

    // Hunters prioritize mutate bugs threatening the raid.
    if (isHunter)
    {
        Unit* mutateBug = Aq40BossHelper::FindUnitByAnyName(botAI, encounterUnits, { "mutate bug" });
        if (mutateBug && mutateBug->IsAlive())
            desiredTarget = mutateBug;
    }

    if (!desiredTarget)
    {
        if (isRangedDps)
            desiredTarget = assignment.veklor ? assignment.veklor : assignment.veknilash;
        else
            desiredTarget = assignment.veknilash ? assignment.veknilash : assignment.veklor;
    }

    if (!desiredTarget || !desiredTarget->IsAlive())
        return false;

    // Always keep the target pinned and set RTI for base AI integration.
    PinTwinTarget(botAI, context, desiredTarget);
    if (isRangedDps)
        SetRtiTarget(botAI, "square", desiredTarget);   // Vek'lor = square
    else
        SetRtiTarget(botAI, "diamond", desiredTarget);  // Vek'nilash = diamond

    float const distance = bot->GetDistance2d(desiredTarget);
    float const attackRange = isRangedDps ? 28.0f : 5.0f;

    // If out of range, move toward the target.
    if (distance > attackRange || !bot->IsWithinLOSInMap(desiredTarget))
    {
        float const moveRange = isRangedDps ? 25.0f : 3.0f;
        bool moved = MoveNear(desiredTarget, moveRange, MovementPriority::MOVEMENT_COMBAT);
        // Still en route — hold action slot even if MoveTo dedup returned false.
        if (!moved && distance > attackRange)
            return true;
        return moved;
    }

    // In range - attack if not already, then return false so class AI casts spells.
    if (bot->GetVictim() != desiredTarget)
        Attack(desiredTarget);

    return false;
}

// ---------------------------------------------------------------------------
// HoldSplitAction - tank positioning.
// Primary melee tank: pin Vek'nilash, move to melee range, attack.
// Backup tanks: hold at assigned side anchor.
// Backup warlock: pre-cast Shadow Ward while waiting.
// ---------------------------------------------------------------------------
bool Aq40TwinEmperorsHoldSplitAction::Execute(Event /*event*/)
{
    GuidVector encounterUnits = Aq40Helpers::GetTwinEncounterUnits(bot, botAI,
        context->GetValue<GuidVector>("attackers")->Get());
    Aq40Helpers::TwinAssignments assignment = Aq40Helpers::GetTwinAssignments(bot, botAI, encounterUnits);
    if (!assignment.veklor && !assignment.veknilash)
        return false;

    bool const isWarlockTank = Aq40BossHelper::IsDesignatedTwinWarlockTank(bot);
    bool const isMeleeTank = !isWarlockTank && PlayerbotAI::IsTank(bot) && !PlayerbotAI::IsRanged(bot);

    if (!isWarlockTank && !isMeleeTank)
        return false;

    bool const isPrimaryTank = Aq40Helpers::IsTwinPrimaryTankOnActiveBoss(bot, assignment);

    if (isPrimaryTank && isMeleeTank && assignment.veknilash)
    {
        // Primary melee tank: pin Vek'nilash and engage.
        PinTwinTarget(botAI, context, assignment.veknilash);

        float const range = bot->GetDistance2d(assignment.veknilash);
        if (range > 5.0f || !bot->IsWithinLOSInMap(assignment.veknilash))
        {
            bool moved = MoveNear(assignment.veknilash, 3.0f, MovementPriority::MOVEMENT_COMBAT);
            if (!moved && range > 5.0f)
                return true;  // Still en route.
            return moved;
        }

        if (bot->GetVictim() != assignment.veknilash)
            Attack(assignment.veknilash);

        return false;  // Let class AI handle tanking abilities.
    }

    if (isPrimaryTank)
        return false;  // Primary warlock tank is handled by WarlockTankAction.

    // Backup tank: hold at assigned side anchor.
    // Pre-cast Shadow Ward if warlock.
    if (isWarlockTank && !botAI->HasAura("shadow ward", bot) && botAI->CanCastSpell("shadow ward", bot))
        botAI->CastSpell("shadow ward", bot);

    // Stop attacking - backup tanks should not engage the wrong boss type.
    if (bot->GetVictim())
        bot->AttackStop();

    uint32 const sideIndex = Aq40Helpers::GetStableTwinRoleIndex(bot, botAI);
    Position anchor = GetTwinSideAnchor(sideIndex);

    if (bot->GetExactDist2d(anchor.GetPositionX(), anchor.GetPositionY()) <= 8.0f)
        return true;  // Close enough - hold position.

    return MoveTo(bot->GetMapId(), anchor.GetPositionX(), anchor.GetPositionY(),
                  anchor.GetPositionZ(), false, false, false, true,
                  MovementPriority::MOVEMENT_COMBAT, true, false);
}

// ---------------------------------------------------------------------------
// WarlockTankAction - warlock-specific threat generation and positioning on
// Vek'lor.  Shadow Ward for damage mitigation, Searing Pain for threat.
// When Vek'lor is not on this warlock's side, hold and pre-cast Shadow Ward.
// ---------------------------------------------------------------------------
bool Aq40TwinEmperorsWarlockTankAction::isUseful()
{
    if (!Aq40BossHelper::IsDesignatedTwinWarlockTank(bot))
        return false;

    GuidVector encounterUnits = Aq40Helpers::GetTwinEncounterUnits(bot, botAI,
        context->GetValue<GuidVector>("attackers")->Get());
    Aq40Helpers::TwinAssignments assignment = Aq40Helpers::GetTwinAssignments(bot, botAI, encounterUnits);
    if (!assignment.veklor)
        return false;

    return Aq40Helpers::IsTwinPrimaryTankOnActiveBoss(bot, assignment);
}

bool Aq40TwinEmperorsWarlockTankAction::Execute(Event /*event*/)
{
    if (!Aq40BossHelper::IsDesignatedTwinWarlockTank(bot))
        return false;

    GuidVector encounterUnits = Aq40Helpers::GetTwinEncounterUnits(bot, botAI,
        context->GetValue<GuidVector>("attackers")->Get());
    Aq40Helpers::TwinAssignments assignment = Aq40Helpers::GetTwinAssignments(bot, botAI, encounterUnits);
    if (!assignment.veklor)
        return false;

    if (!Aq40Helpers::IsTwinPrimaryTankOnActiveBoss(bot, assignment))
        return false;

    // If Vek'lor is on the far side AND far away, don't chase across the room.
    // The other warlock tank handles pickup.  But if we're within 45y of Vek'lor
    // (e.g. still near room center after pull), engage regardless of side check.
    // Pre-cast Shadow Ward while holding so we're ready when it teleports back.
    if (assignment.veknilash &&
        !Aq40Helpers::IsLikelyOnSameTwinSide(bot, assignment.veklor, assignment.veknilash) &&
        bot->GetDistance2d(assignment.veklor) > 45.0f)
    {
        if (!botAI->HasAura("shadow ward", bot) && botAI->CanCastSpell("shadow ward", bot))
            botAI->CastSpell("shadow ward", bot);
        return true;
    }

    Unit* veklor = assignment.veklor;

    if (AI_VALUE(Unit*, "current target") != veklor)
        PinTwinTarget(botAI, context, veklor);

    // Positioning: hold at range on far side of Vek'lor, away from Vek'nilash.
    float const desiredRange = 24.0f;
    float const minRange = 21.0f;
    float const maxRange = 30.0f;

    float const rangeToVeklor = bot->GetDistance2d(veklor);
    bool const hasLOS = bot->IsWithinLOSInMap(veklor);

    // Far from Vek'lor: move DIRECTLY toward boss via shortest path.
    // Do NOT use far-side anchor here — it sends the warlock toward the wall.
    if (rangeToVeklor > maxRange || !hasLOS)
    {
        if (!botAI->HasAura("shadow ward", bot) && botAI->CanCastSpell("shadow ward", bot))
            botAI->CastSpell("shadow ward", bot);

        MoveNear(veklor, desiredRange, MovementPriority::MOVEMENT_COMBAT);
        return true;
    }

    // Too close — back away using far-side anchor.
    if (rangeToVeklor < minRange)
    {
        if (!botAI->HasAura("shadow ward", bot) && botAI->CanCastSpell("shadow ward", bot))
            botAI->CastSpell("shadow ward", bot);

        bot->AttackStop();
        bot->InterruptNonMeleeSpells(true);

        Position retreatPos;
        if (GetFarSidePosition(bot, veklor, assignment.oppositeEmperor, desiredRange, retreatPos))
            MoveTo(bot->GetMapId(), retreatPos.GetPositionX(), retreatPos.GetPositionY(),
                   retreatPos.GetPositionZ(), false, false, false, true,
                   MovementPriority::MOVEMENT_COMBAT, true, false);
        else
            MoveTo(veklor, desiredRange, MovementPriority::MOVEMENT_COMBAT);

        return true;
    }

    // In casting range (21-30y) with LOS — threat generation is #1 priority.
    if (!botAI->HasAura("shadow ward", bot) && botAI->CanCastSpell("shadow ward", bot))
        return botAI->CastSpell("shadow ward", bot);

    if (botAI->CanCastSpell("searing pain", veklor))
        return botAI->CastSpell("searing pain", veklor);

    if (!botAI->HasAura("curse of doom", veklor) && botAI->CanCastSpell("curse of doom", veklor))
        return botAI->CastSpell("curse of doom", veklor);

    // Soft reposition toward far-side anchor between casts.
    Position anchorPosition;
    if (GetFarSidePosition(bot, veklor, assignment.oppositeEmperor, desiredRange, anchorPosition) &&
        bot->GetExactDist2d(anchorPosition.GetPositionX(), anchorPosition.GetPositionY()) > 6.0f)
        MoveTo(bot->GetMapId(), anchorPosition.GetPositionX(), anchorPosition.GetPositionY(),
               anchorPosition.GetPositionZ(), false, false, false, true,
               MovementPriority::MOVEMENT_COMBAT, true, false);

    if (bot->GetVictim() != veklor)
        Attack(veklor);

    return true;
}
