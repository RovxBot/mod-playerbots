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
    Position anchor;
    if (sideIndex == 1u)
        anchor.Relocate(kTwinInitialVeklorX, kTwinInitialVeklorY, kTwinInitialVeklorZ);
    else
        anchor.Relocate(kTwinInitialVeknilashX, kTwinInitialVeknilashY, kTwinInitialVeknilashZ);
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
            return true;  // Already in position - hold.

        return MoveTo(bot->GetMapId(), anchor.GetPositionX(), anchor.GetPositionY(),
                      anchor.GetPositionZ(), false, false, false, true,
                      MovementPriority::MOVEMENT_COMBAT, true, false);
    }

    // Everyone else moves to room center and holds.
    if (bot->GetExactDist2d(kTwinRoomCenterX, kTwinRoomCenterY) <= 5.0f)
        return true;  // Already in position - hold.

    return MoveTo(bot->GetMapId(), kTwinRoomCenterX, kTwinRoomCenterY, kTwinRoomCenterZ,
                  false, false, false, true,
                  MovementPriority::MOVEMENT_COMBAT, true, false);
}

// ---------------------------------------------------------------------------
// ChooseTargetAction - assign the correct target based on role and which
// boss is currently on this bot's assigned side of the room.
//
// Warlock tank -> Vek'lor if primary tank for it, else hold.
// Melee tank   -> Vek'nilash if primary tank for it, else hold.
// Ranged DPS   -> Vek'lor (follow it wherever it is).
// Melee DPS    -> Vek'nilash (follow it wherever it is).
// ---------------------------------------------------------------------------
bool Aq40TwinEmperorsChooseTargetAction::Execute(Event /*event*/)
{
    GuidVector encounterUnits = Aq40Helpers::GetTwinEncounterUnits(bot, botAI,
        context->GetValue<GuidVector>("attackers")->Get());
    Aq40Helpers::TwinAssignments assignment = Aq40Helpers::GetTwinAssignments(bot, botAI, encounterUnits);
    if (!assignment.veklor || !assignment.veknilash)
        return false;

    bool const isWarlockTank = Aq40BossHelper::IsDesignatedTwinWarlockTank(bot);
    bool const isMeleeTank = !isWarlockTank && PlayerbotAI::IsTank(bot) && !PlayerbotAI::IsRanged(bot);
    bool const isRangedDps = !isWarlockTank && !isMeleeTank && botAI->IsRanged(bot) && !botAI->IsHeal(bot);

    Unit* desiredTarget = nullptr;

    if (isWarlockTank)
    {
        // Warlock tank targets Vek'lor when it's the primary tank.
        // Backup warlocks hold and don't attack.
        if (Aq40Helpers::IsTwinPrimaryTankOnActiveBoss(bot, assignment))
            desiredTarget = assignment.veklor;
        else
            return true;  // Backup warlock - hold, don't attack.
    }
    else if (isMeleeTank)
    {
        // Melee tank targets Vek'nilash when it's the primary tank.
        if (Aq40Helpers::IsTwinPrimaryTankOnActiveBoss(bot, assignment))
            desiredTarget = assignment.veknilash;
        else
            return true;  // Backup tank - hold on assigned side.
    }
    else if (isRangedDps)
    {
        desiredTarget = assignment.veklor;
    }
    else if (!botAI->IsHeal(bot))
    {
        // Melee DPS.
        desiredTarget = assignment.veknilash;
    }
    else
    {
        // Healer - don't override target selection.
        return false;
    }

    if (!desiredTarget)
        return false;

    Unit* currentTarget = AI_VALUE(Unit*, "current target");
    if (currentTarget == desiredTarget)
        return false;

    PinTwinTarget(botAI, context, desiredTarget);
    return Attack(desiredTarget);
}

// ---------------------------------------------------------------------------
// HoldSplitAction - tanks stay on their assigned side of the room.
// Primary tank: move to engage range with the correct boss.
// Backup tank: hold at side anchor, wait for next teleport.
// ---------------------------------------------------------------------------
bool Aq40TwinEmperorsHoldSplitAction::Execute(Event /*event*/)
{
    GuidVector encounterUnits = Aq40Helpers::GetTwinEncounterUnits(bot, botAI,
        context->GetValue<GuidVector>("attackers")->Get());
    Aq40Helpers::TwinAssignments assignment = Aq40Helpers::GetTwinAssignments(bot, botAI, encounterUnits);
    if (!assignment.veklor || !assignment.veknilash)
        return false;

    bool const isWarlockTank = Aq40BossHelper::IsDesignatedTwinWarlockTank(bot);
    bool const isMeleeTank = !isWarlockTank && PlayerbotAI::IsTank(bot) && !PlayerbotAI::IsRanged(bot);

    if (!isWarlockTank && !isMeleeTank)
        return false;  // Only tanks need hold-split positioning.

    bool const isPrimaryTank = Aq40Helpers::IsTwinPrimaryTankOnActiveBoss(bot, assignment);

    if (isPrimaryTank)
    {
        // Primary melee tank: move to melee range of Vek'nilash.
        // Warlock tank positioning is handled by WarlockTankAction.
        if (isMeleeTank)
        {
            float const range = bot->GetDistance2d(assignment.veknilash);
            if (range > 5.0f)
                return MoveTo(assignment.veknilash, 3.0f, MovementPriority::MOVEMENT_COMBAT);
        }
        return false;
    }

    // Backup tank: hold at assigned side anchor, wait for teleport.
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

    // If Vek'lor is on the far side (closer to opposite boss than to us),
    // don't chase across the room.  The other warlock tank handles pickup.
    // Pre-cast Shadow Ward while holding so we're ready when it teleports back.
    if (assignment.veknilash &&
        !Aq40Helpers::IsLikelyOnSameTwinSide(bot, assignment.veklor, assignment.veknilash))
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

    Position anchorPosition;
    bool const hasAnchor = GetFarSidePosition(bot, veklor, assignment.oppositeEmperor,
                                              desiredRange, anchorPosition);
    float const rangeToVeklor = bot->GetDistance2d(veklor);
    bool const hasLOS = bot->IsWithinLOSInMap(veklor);

    // Hard reposition if out of LOS or range.
    if (!hasLOS || rangeToVeklor < minRange || rangeToVeklor > maxRange)
    {
        if (!botAI->HasAura("shadow ward", bot) && botAI->CanCastSpell("shadow ward", bot))
            botAI->CastSpell("shadow ward", bot);

        bot->AttackStop();
        bot->InterruptNonMeleeSpells(true);

        if (hasAnchor)
            MoveTo(bot->GetMapId(), anchorPosition.GetPositionX(), anchorPosition.GetPositionY(),
                   anchorPosition.GetPositionZ(), false, false, false, true,
                   MovementPriority::MOVEMENT_COMBAT, true, false);
        else
            MoveTo(veklor, desiredRange, MovementPriority::MOVEMENT_COMBAT);

        return true;
    }

    // In range - threat generation and utility.
    if (!botAI->HasAura("shadow ward", bot) && botAI->CanCastSpell("shadow ward", bot))
        return botAI->CastSpell("shadow ward", bot);

    if (botAI->CanCastSpell("searing pain", veklor))
        return botAI->CastSpell("searing pain", veklor);

    if (!botAI->HasAura("curse of doom", veklor) && botAI->CanCastSpell("curse of doom", veklor))
        return botAI->CastSpell("curse of doom", veklor);

    // Soft reposition toward anchor between casts.
    if (hasAnchor && bot->GetExactDist2d(anchorPosition.GetPositionX(), anchorPosition.GetPositionY()) > 6.0f)
        MoveTo(bot->GetMapId(), anchorPosition.GetPositionX(), anchorPosition.GetPositionY(),
               anchorPosition.GetPositionZ(), false, false, false, true,
               MovementPriority::MOVEMENT_COMBAT, true, false);

    if (bot->GetVictim() != veklor)
        Attack(veklor);

    return true;
}
