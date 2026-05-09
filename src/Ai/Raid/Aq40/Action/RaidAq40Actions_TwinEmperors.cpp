#include "RaidAq40Actions.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <list>
#include <sstream>
#include <string>

#include "CharmInfo.h"
#include "CreatureAI.h"
#include "DynamicObject.h"
#include "Pet.h"
#include "SharedDefines.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "../RaidAq40BossHelper.h"
#include "RaidBossHelpers.h"
#include "../RaidAq40SpellIds.h"
#include "../Util/RaidAq40Helpers.h"
#include "../Util/RaidAq40TwinEmperors.h"

namespace
{
float constexpr kTwinRoomCenterX = -8954.855f;
float constexpr kTwinRoomCenterY = 1235.7107f;
float constexpr kTwinRoomCenterZ = -112.62047f;
float constexpr kTwinInitialVeklorX = -8868.31f;
float constexpr kTwinInitialVeklorY = 1205.97f;
float constexpr kTwinInitialVeklorZ = -104.231f;
float constexpr kTwinInitialVeknilashX = -9023.67f;
float constexpr kTwinInitialVeknilashY = 1176.24f;
float constexpr kTwinInitialVeknilashZ = -104.226f;
uint32 constexpr kTwinInitialVeklorSide = 1u;
float constexpr kTwinWarlockInitialPullDistance = 26.0f;
float constexpr kTwinSideHealerAnchorDistance = 120.0f;
float constexpr kTwinSideTankCornerDistance = 150.0f;

void GetTwinInitialBossPosition(uint32 sideIndex, float& bossX, float& bossY, float& bossZ)
{
    if (sideIndex == 1u)
    {
        bossX = kTwinInitialVeklorX;  bossY = kTwinInitialVeklorY;  bossZ = kTwinInitialVeklorZ;
    }
    else
    {
        bossX = kTwinInitialVeknilashX;  bossY = kTwinInitialVeknilashY;  bossZ = kTwinInitialVeknilashZ;
    }
}

Position GetTwinInitialRadialAnchor(uint32 sideIndex, float distance)
{
    float bossX, bossY, bossZ;
    GetTwinInitialBossPosition(sideIndex, bossX, bossY, bossZ);

    float const dirX = kTwinRoomCenterX - bossX;
    float const dirY = kTwinRoomCenterY - bossY;
    float const length = std::sqrt(dirX * dirX + dirY * dirY);
    if (length < 0.1f)
    {
        Position anchor;
        anchor.Relocate(bossX, bossY, bossZ);
        return anchor;
    }

    Position anchor;
    float const zRatio = std::min(distance / length, 1.0f);
    anchor.Relocate(bossX + (dirX / length) * distance,
                    bossY + (dirY / length) * distance,
                    bossZ + (kTwinRoomCenterZ - bossZ) * zRatio);
    return anchor;
}

Position GetTwinSideAnchor(uint32 sideIndex)
{
    return GetTwinInitialRadialAnchor(sideIndex, kTwinSideTankCornerDistance);
}

Position GetTwinSideHealerAnchor(uint32 sideIndex)
{
    return GetTwinInitialRadialAnchor(sideIndex, kTwinSideHealerAnchorDistance);
}

bool GetFarSidePosition(Player* bot, Unit* boss, Unit* otherBoss, float desiredRange, Position& outPosition);
bool GetTwinRecoveryStepTowardAnchor(Player* bot, Position const& anchor, float maxStepDistance, Position& outPosition);

bool GetTwinBossSideCastAnchorWithOffset(Player* bot, Unit* boss, uint32 sideIndex, float desiredRange,
                                         float angleOffset, Position& outPosition)
{
    if (!bot || !boss || !bot->GetMap())
        return false;

    Position sideAnchor = GetTwinSideAnchor(sideIndex);
    float const dirX = sideAnchor.GetPositionX() - boss->GetPositionX();
    float const dirY = sideAnchor.GetPositionY() - boss->GetPositionY();
    float const length = std::sqrt(dirX * dirX + dirY * dirY);
    if (length < 0.1f)
        return false;

    float const baseAngle = std::atan2(dirY, dirX) + angleOffset;
    float targetX = boss->GetPositionX() + std::cos(baseAngle) * desiredRange;
    float targetY = boss->GetPositionY() + std::sin(baseAngle) * desiredRange;
    float targetZ = boss->GetPositionZ();
    if (!bot->GetMap()->CheckCollisionAndGetValidCoords(bot, bot->GetPositionX(), bot->GetPositionY(),
            bot->GetPositionZ(), targetX, targetY, targetZ))
        return false;

    outPosition.Relocate(targetX, targetY, targetZ);
    return true;
}

bool IsTwinPickupAnchorCandidateSafe(Unit* boss, Unit* otherBoss, uint32 sideIndex,
                                     float desiredRange, Position const& candidate)
{
    if (!boss)
        return false;

    Position const sideAnchor = GetTwinSideAnchor(sideIndex);
    float const baseX = sideAnchor.GetPositionX() - boss->GetPositionX();
    float const baseY = sideAnchor.GetPositionY() - boss->GetPositionY();
    float const candidateX = candidate.GetPositionX() - boss->GetPositionX();
    float const candidateY = candidate.GetPositionY() - boss->GetPositionY();
    float const baseLength = std::sqrt(baseX * baseX + baseY * baseY);
    float const candidateLength = std::sqrt(candidateX * candidateX + candidateY * candidateY);
    if (baseLength < 0.1f || candidateLength < 0.1f)
        return false;

    float const projection = (candidateX * baseX + candidateY * baseY) / baseLength;
    if (projection <= 0.25f)
        return false;

    float const bossCenterDistance = boss->GetExactDist2d(kTwinRoomCenterX, kTwinRoomCenterY);
    float const candidateCenterX = candidate.GetPositionX() - kTwinRoomCenterX;
    float const candidateCenterY = candidate.GetPositionY() - kTwinRoomCenterY;
    float const candidateCenterDistance =
        std::sqrt(candidateCenterX * candidateCenterX + candidateCenterY * candidateCenterY);
    if (candidateCenterDistance + 1.5f < bossCenterDistance - desiredRange)
        return false;

    if (!otherBoss)
        return true;

    float const targetDistance = boss->GetDistance2d(candidate.GetPositionX(), candidate.GetPositionY());
    float const oppositeDistance = otherBoss->GetDistance2d(candidate.GetPositionX(), candidate.GetPositionY());
    return oppositeDistance + 4.0f >= targetDistance;
}

bool ShouldLockTwinPickupAnchor(Player* bot)
{
    return bot &&
           (Aq40TwinEmperors::IsTwinTeleportWindowActive(bot) || Aq40TwinEmperors::HasLockedPickupAnchor(bot));
}

bool ResolveTwinPickupAnchor(Player* bot, Unit* boss, Unit* otherBoss, uint32 sideIndex, float desiredRange,
                             float recoveryStepDistance, Position& outPosition, std::string& outPath)
{
    if (!bot || !boss)
        return false;

    if (Aq40TwinEmperors::GetTwinLockedPickupAnchor(bot, boss, sideIndex, outPosition))
    {
        if (IsTwinPickupAnchorCandidateSafe(boss, otherBoss, sideIndex, desiredRange, outPosition))
        {
            outPath = "locked_anchor";
            return true;
        }

        Aq40TwinEmperors::ClearTwinPickupState(bot);
    }

    static std::array<float, 5> const kSideAnchorOffsets = { 0.0f, 0.18f, -0.18f, 0.36f, -0.36f };
    for (float const offset : kSideAnchorOffsets)
    {
        Position candidate;
        if (!GetTwinBossSideCastAnchorWithOffset(bot, boss, sideIndex, desiredRange, offset, candidate))
            continue;
        if (!IsTwinPickupAnchorCandidateSafe(boss, otherBoss, sideIndex, desiredRange, candidate))
            continue;

        outPosition = candidate;
        outPath = (offset == 0.0f) ? "side_cast_anchor" : "side_cast_nudge";
        if (ShouldLockTwinPickupAnchor(bot))
            Aq40TwinEmperors::RememberTwinPickupAnchor(bot, boss, sideIndex, outPosition);
        return true;
    }

    Position farSidePosition;
    if (GetFarSidePosition(bot, boss, otherBoss, desiredRange, farSidePosition) &&
        IsTwinPickupAnchorCandidateSafe(boss, otherBoss, sideIndex, desiredRange, farSidePosition))
    {
        outPosition = farSidePosition;
        outPath = "far_side_anchor";
        if (ShouldLockTwinPickupAnchor(bot))
            Aq40TwinEmperors::RememberTwinPickupAnchor(bot, boss, sideIndex, outPosition);
        return true;
    }

    Position const sideAnchor = GetTwinSideAnchor(sideIndex);
    Position recoveryStep;
    if (GetTwinRecoveryStepTowardAnchor(bot, sideAnchor, recoveryStepDistance, recoveryStep) &&
        IsTwinPickupAnchorCandidateSafe(boss, otherBoss, sideIndex, desiredRange, recoveryStep))
    {
        outPosition = recoveryStep;
        outPath = "side_anchor_step";
        if (ShouldLockTwinPickupAnchor(bot))
            Aq40TwinEmperors::RememberTwinPickupAnchor(bot, boss, sideIndex, outPosition);
        return true;
    }

    return false;
}

bool GetTwinCentralCastAnchor(Player* bot, Unit* boss, float desiredRange, Position& outPosition)
{
    if (!bot || !boss || !bot->GetMap())
        return false;

    float const dirX = kTwinRoomCenterX - boss->GetPositionX();
    float const dirY = kTwinRoomCenterY - boss->GetPositionY();
    float const length = std::sqrt(dirX * dirX + dirY * dirY);
    if (length < 0.1f)
    {
        outPosition.Relocate(kTwinRoomCenterX, kTwinRoomCenterY, kTwinRoomCenterZ);
        return true;
    }

    float const anchorRange = std::min(desiredRange, length);
    float targetX = boss->GetPositionX() + (dirX / length) * anchorRange;
    float targetY = boss->GetPositionY() + (dirY / length) * anchorRange;
    float targetZ = boss->GetPositionZ();
    if (!bot->GetMap()->CheckCollisionAndGetValidCoords(bot, bot->GetPositionX(), bot->GetPositionY(),
            bot->GetPositionZ(), targetX, targetY, targetZ))
        return false;

    outPosition.Relocate(targetX, targetY, targetZ);
    return true;
}

void PinTwinTarget(PlayerbotAI* botAI, AiObjectContext* context, Unit* target)
{
    if (!botAI || !context || !target)
        return;

    ObjectGuid const guid = target->GetGUID();
    botAI->GetAiObjectContext()->GetValue<GuidVector>("prioritized targets")->Set({ guid });
    context->GetValue<ObjectGuid>("pull target")->Set(guid);
}

void ClearTwinPinnedTarget(PlayerbotAI* botAI, AiObjectContext* context)
{
    if (!botAI || !context)
        return;

    GuidVector emptyTargets;
    botAI->GetAiObjectContext()->GetValue<GuidVector>("prioritized targets")->Set(emptyTargets);
    context->GetValue<ObjectGuid>("pull target")->Set(ObjectGuid::Empty);
    context->GetValue<Unit*>("current target")->Set(nullptr);
}

bool IsTwinTankRole(Player* bot)
{
    Aq40Helpers::TwinRoleCohort const cohort = Aq40Helpers::GetTwinRoleCohort(bot, GET_PLAYERBOT_AI(bot));
    return cohort == Aq40Helpers::TwinRoleCohort::WarlockTank ||
           cohort == Aq40Helpers::TwinRoleCohort::MeleeTank;
}

bool StopTwinPetAttackOnTarget(Player* bot, Unit* target)
{
    if (!bot || !target)
        return false;

    Pet* pet = bot->GetPet();
    if (!pet || (pet->GetVictim() != target && pet->GetTarget() != target->GetGUID()))
        return false;

    pet->AttackStop();
    pet->SetTarget(ObjectGuid::Empty);
    if (CharmInfo* charmInfo = pet->GetCharmInfo())
        charmInfo->SetIsCommandAttack(false);

    return true;
}

void StopTwinDamageOn(Player* bot, PlayerbotAI* botAI, AiObjectContext* context, Unit* target)
{
    if (!bot || !botAI || !context || !target)
        return;

    bool stopped = false;

    if (bot->GetVictim() == target)
    {
        bot->AttackStop();
        stopped = true;
    }

    if (bot->GetTarget() == target->GetGUID())
    {
        bot->SetTarget(ObjectGuid::Empty);
        stopped = true;
    }

    if (AI_VALUE(Unit*, "current target") == target)
    {
        ClearTwinPinnedTarget(botAI, context);
        stopped = true;
    }

    stopped = StopTwinPetAttackOnTarget(bot, target) || stopped;

    if (stopped)
        botAI->RequestSpellInterrupt();
}

bool GetTwinCentralWaitAnchor(Player* bot, Unit* veklor, Position& anchor)
{
    if (!bot || !veklor)
        return false;

    if (!GetTwinCentralCastAnchor(bot, veklor, 28.0f, anchor))
        anchor.Relocate(kTwinRoomCenterX, kTwinRoomCenterY, kTwinRoomCenterZ);

    return true;
}

bool GetTwinSnapshot(Player* bot, PlayerbotAI* botAI, AiObjectContext* context,
                     Aq40Helpers::TwinEncounterSnapshot& outSnapshot)
{
    GuidVector attackers;
    if (context)
        attackers = context->GetValue<GuidVector>("attackers")->Get();

    return Aq40Helpers::GetTwinEncounterSnapshot(bot, botAI, attackers, outSnapshot);
}

bool PrepareTwinRecoveryHold(Player* bot, PlayerbotAI* botAI, AiObjectContext* context,
                             Aq40Helpers::TwinAssignments const& assignment, std::string const& reason,
                             Position& outAnchor, bool& outShouldMove)
{
    outShouldMove = false;
    if (!bot || !botAI || !context)
        return false;

    if (assignment.veklor)
        StopTwinDamageOn(bot, botAI, context, assignment.veklor);
    if (assignment.veknilash)
        StopTwinDamageOn(bot, botAI, context, assignment.veknilash);
    Aq40TwinEmperors::ClearLocalRti(botAI);

    outAnchor = Aq40Helpers::GetTwinRoomCenterPosition();
    if (assignment.veklor && (botAI->IsRanged(bot) || bot->getClass() == CLASS_HUNTER))
        GetTwinCentralWaitAnchor(bot, assignment.veklor, outAnchor);

    Aq40Helpers::LogAq40Info(bot, "recovery_hold", "twins:" + reason,
        "boss=twins state=" + reason, 3000);

    outShouldMove = bot->GetExactDist2d(outAnchor.GetPositionX(), outAnchor.GetPositionY()) > 5.0f;
    return true;
}

void ClearTwinPrePullTargeting(Player* bot, PlayerbotAI* botAI, AiObjectContext* context)
{
    if (!bot || !botAI || botAI->IsRealPlayer())
        return;

    if (bot->GetVictim())
        bot->AttackStop();

    if (Pet* pet = bot->GetPet())
    {
        pet->AttackStop();
        pet->SetTarget(ObjectGuid::Empty);

        if (CharmInfo* charmInfo = pet->GetCharmInfo())
            charmInfo->SetIsCommandAttack(false);
    }

    GuidVector emptyTargets;
    botAI->GetAiObjectContext()->GetValue<GuidVector>("prioritized targets")->Set(emptyTargets);

    if (context)
        context->GetValue<ObjectGuid>("pull target")->Set(ObjectGuid::Empty);
}

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

bool GetTwinRecoveryStepTowardAnchor(Player* bot, Position const& anchor, float maxStepDistance, Position& outPosition)
{
    if (!bot || !bot->GetMap())
        return false;

    float const dirX = anchor.GetPositionX() - bot->GetPositionX();
    float const dirY = anchor.GetPositionY() - bot->GetPositionY();
    float const dirZ = anchor.GetPositionZ() - bot->GetPositionZ();
    float const length = std::sqrt(dirX * dirX + dirY * dirY);
    if (length < 1.0f)
        return false;

    float const maxStep = std::min(maxStepDistance, length);
    float const minStep = std::min(4.0f, maxStep);
    static constexpr float kStepFractions[] = { 1.0f, 0.75f, 0.5f, 0.33f };
    for (float fraction : kStepFractions)
    {
        float const stepDistance = std::max(minStep, maxStep * fraction);
        float targetX = bot->GetPositionX() + (dirX / length) * stepDistance;
        float targetY = bot->GetPositionY() + (dirY / length) * stepDistance;
        float targetZ = bot->GetPositionZ() + (dirZ / std::max(length, 1.0f)) * stepDistance;
        if (!bot->GetMap()->CheckCollisionAndGetValidCoords(bot, bot->GetPositionX(), bot->GetPositionY(),
                bot->GetPositionZ(), targetX, targetY, targetZ))
        {
            continue;
        }

        outPosition.Relocate(targetX, targetY, targetZ);
        return true;
    }

    return false;
}

bool IsTwinNonInstantCastActive(Player* bot)
{
    if (!bot)
        return false;

    if (Spell* spell = bot->GetCurrentSpell(CURRENT_GENERIC_SPELL))
        return spell->GetCastTime() > 0;

    return bot->GetCurrentSpell(CURRENT_CHANNELED_SPELL) != nullptr;
}

void DisableTwinPetTauntAutocast(Pet* pet)
{
    if (!pet)
        return;

    for (PetSpellMap::const_iterator itr = pet->m_spells.begin(); itr != pet->m_spells.end(); ++itr)
    {
        if (itr->second.state == PETSPELL_REMOVED)
            continue;

        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(itr->first);
        if (!spellInfo || !spellInfo->IsAutocastable() || !spellInfo->SpellName[0])
            continue;

        std::string const spellName = spellInfo->SpellName[0];
        if (spellName != "Growl" && spellName != "Charge")
            continue;

        if (std::find(pet->m_autospells.begin(), pet->m_autospells.end(), itr->first) != pet->m_autospells.end())
            pet->ToggleAutocast(spellInfo, false);
    }
}

// Full encounter-local pet policy for Twin Emperors (issue #5).
// Enforces passive/follow during dangerous phases, stops pets that would
// drag into Vek'lor danger range, and disables taunt autocast.
// Returns true if the pet was put into hold state.
bool EnforceTwinPetPolicy(Player* bot, PlayerbotAI* botAI,
                          Aq40TwinEmperors::TwinEncounterPhase phase,
                          Unit* veklor, Unit* desiredTarget)
{
    if (!bot)
        return false;

    Pet* pet = bot->GetPet();
    if (!pet || !pet->IsAlive())
        return false;

    bool const holdPhase =
        phase == Aq40TwinEmperors::TwinEncounterPhase::TeleportWindow ||
        phase == Aq40TwinEmperors::TwinEncounterPhase::PickupRecovery ||
        phase == Aq40TwinEmperors::TwinEncounterPhase::EmergencySplitRecovery;

    float constexpr kPetVeklorSafeDistance = 21.0f;
    bool const petInVeklorDanger = veklor && veklor->IsAlive() &&
        pet->GetDistance2d(veklor) < kPetVeklorSafeDistance;
    bool const targetIsVeklor = desiredTarget && desiredTarget == veklor;
    bool const targetNearVeklor = veklor && veklor->IsAlive() && desiredTarget &&
        desiredTarget != veklor && desiredTarget->GetDistance2d(veklor) < kPetVeklorSafeDistance;

    bool const shouldStop = holdPhase || petInVeklorDanger || targetIsVeklor || targetNearVeklor;
    if (!shouldStop)
        return false;

    pet->AttackStop();
    pet->SetTarget(ObjectGuid::Empty);
    if (CharmInfo* charmInfo = pet->GetCharmInfo())
    {
        charmInfo->SetIsCommandAttack(false);
        charmInfo->SetIsFollowing(true);
        charmInfo->SetIsCommandFollow(true);
        charmInfo->SetIsAtStay(false);
        charmInfo->SetIsReturning(true);
    }

    DisableTwinPetTauntAutocast(pet);
    return true;
}

Unit* GetTwinPriorityBugTarget(Player* bot, PlayerbotAI* botAI, GuidVector const& encounterUnits)
{
    if (Unit* explodeBug = Aq40Helpers::FindTwinMarkedBug(bot, botAI, encounterUnits, Aq40SpellIds::TwinExplodeBug))
        return explodeBug;
    if (Unit* mutateBug = Aq40Helpers::FindTwinMarkedBug(bot, botAI, encounterUnits, Aq40SpellIds::TwinMutateBug))
        return mutateBug;
    return Aq40Helpers::FindTwinHostileBug(bot, botAI, encounterUnits);
}

char const* GetTwinSplitBandName(Aq40TwinEmperors::SplitBand band)
{
    switch (band)
    {
        case Aq40TwinEmperors::SplitBand::Warning:
            return "warning";
        case Aq40TwinEmperors::SplitBand::Urgent:
            return "urgent";
        case Aq40TwinEmperors::SplitBand::Terminal:
            return "terminal";
        case Aq40TwinEmperors::SplitBand::Stable:
        default:
            return "stable";
    }
}

// Pre-pull center-spread layout (issue #8): deterministic spread anchors for
// non-tank, non-dedicated-healer bots.  Melee DPS bias toward Vek'nilash's
// initial side for quick opener engage, ranged DPS bias toward Vek'lor's
// initial side for opener cast positions, non-dedicated healers stay near
// center perpendicular to the boss axis.
Position GetTwinPrePullCenterSpreadAnchor(Player* bot, bool isMeleeDps, bool isRangedDps)
{
    float const vnAngle = std::atan2(
        kTwinInitialVeknilashY - kTwinRoomCenterY,
        kTwinInitialVeknilashX - kTwinRoomCenterX);
    float const vlAngle = std::atan2(
        kTwinInitialVeklorY - kTwinRoomCenterY,
        kTwinInitialVeklorX - kTwinRoomCenterX);

    float baseAngle;
    float spreadRadius;
    if (isMeleeDps)
    {
        // Melee DPS stage toward Vek'nilash initial side for quick engage on pull
        baseAngle = vnAngle;
        spreadRadius = 14.0f;
    }
    else if (isRangedDps)
    {
        // Ranged DPS near center, biased toward Vek'lor for opener cast positions
        baseAngle = vlAngle;
        spreadRadius = 10.0f;
    }
    else
    {
        // Non-dedicated healers stay close to center, perpendicular to boss axis
        float const axisAngle = std::atan2(
            kTwinInitialVeklorY - kTwinInitialVeknilashY,
            kTwinInitialVeklorX - kTwinInitialVeknilashX);
        baseAngle = axisAngle + static_cast<float>(M_PI) * 0.5f;
        spreadRadius = 6.0f;
    }

    // Deterministic per-bot arc offset to prevent overstacking.
    // Uses a multiplicative hash of the GUID counter for stable slot distribution.
    uint32 const guidCounter = bot->GetGUID().GetCounter();
    uint32 const slotHash = static_cast<uint32>((guidCounter * 2654435769UL) >> 16) % 7;
    float const slotOffset = (static_cast<float>(slotHash) - 3.0f) * 0.2f;

    Position anchor;
    anchor.Relocate(
        kTwinRoomCenterX + std::cos(baseAngle + slotOffset) * spreadRadius,
        kTwinRoomCenterY + std::sin(baseAngle + slotOffset) * spreadRadius,
        kTwinRoomCenterZ);
    return anchor;
}

// Pre-teleport melee DPS staging anchor (issue #8): positions melee DPS on
// the receiving side where Vek'nilash will be (or already is) after teleport,
// at a safe distance that respects tank pickup geometry.  Tank pickup zone is
// within ~5y of the boss; melee DPS at 12y avoids stealing nearest-player
// pickup while staying close enough to engage quickly once pickup is established.
bool GetMeleeDpsPreTeleportAnchor(Player* bot, Unit* veknilash,
                                 uint32 veknilashSideIndex, Position& outAnchor)
{
    if (!bot || !veknilash)
        return false;

    float constexpr kMeleeDpsPreTeleportRange = 12.0f;
    float const dirX = kTwinRoomCenterX - veknilash->GetPositionX();
    float const dirY = kTwinRoomCenterY - veknilash->GetPositionY();
    float const length = std::sqrt(dirX * dirX + dirY * dirY);
    if (length < 0.1f)
        return false;

    float const baseAngle = std::atan2(dirY, dirX);

    // Per-bot arc offset to prevent stacking at the same point
    uint32 const guidCounter = bot->GetGUID().GetCounter();
    uint32 const slotHash = static_cast<uint32>((guidCounter * 2654435769UL) >> 16) % 5;
    float const arcOffset = (static_cast<float>(slotHash) - 2.0f) * 0.12f;

    outAnchor.Relocate(
        veknilash->GetPositionX() + std::cos(baseAngle + arcOffset) * kMeleeDpsPreTeleportRange,
        veknilash->GetPositionY() + std::sin(baseAngle + arcOffset) * kMeleeDpsPreTeleportRange,
        veknilash->GetPositionZ());
    return true;
}
}

bool Aq40TwinEmperorsPrePullStageAction::isUseful()
{
    return Aq40Helpers::IsTwinPrePullReady(bot, botAI);
}

bool Aq40TwinEmperorsPrePullStageAction::Execute(Event /*event*/)
{
    if (!Aq40Helpers::IsTwinPrePullReady(bot, botAI))
        return false;

    // Explicit supported-comp contract (issue #9): the current Twin strategy
    // requires exactly 2 designated warlock tanks and 2 melee tanks.  If the
    // comp does not satisfy this, block normal supported opener flow and log
    // the unsupported reason early — do not present fake pre-pull confidence.
    // NOTE: warrior Battle Shout Vek'lor tanking is not supported by this
    // strategy path.
    if (!Aq40Helpers::IsTwinSupportedCompAvailable(bot, botAI))
    {
        Aq40Helpers::LogAq40Warn(bot, "twin_pull_safety", "twins:unsupported_comp_prepull",
                                 "boss=twins gate=unsupported_comp phase=prepull", 30000);
        return false;
    }

    ClearTwinPrePullTargeting(bot, botAI, context);

    Aq40Helpers::TwinRoleCohort const cohort = Aq40Helpers::GetTwinRoleCohort(bot, botAI);
    bool const isWarlockTank = cohort == Aq40Helpers::TwinRoleCohort::WarlockTank;
    bool const isMeleeTank = cohort == Aq40Helpers::TwinRoleCohort::MeleeTank;
    bool const isHealer = botAI->IsHeal(bot);
    uint32 tankHealerSide = 0;
    bool const isTankHealer =
        isHealer && Aq40Helpers::GetTwinDedicatedTankHealerSide(bot, botAI, tankHealerSide);

    Position anchor;
    std::string stageKey;
    std::string stageFields;
    if (isWarlockTank || isMeleeTank || isTankHealer)
    {
        uint32 const sideIndex = isTankHealer ? tankHealerSide : Aq40Helpers::GetStableTwinRoleIndex(bot, botAI);
        std::string role = "tank";

        if (isWarlockTank)
        {
            anchor = sideIndex == kTwinInitialVeklorSide ?
                GetTwinInitialRadialAnchor(sideIndex, kTwinWarlockInitialPullDistance) :
                GetTwinSideAnchor(sideIndex);
            role = "warlock_tank";
        }
        else if (isTankHealer)
        {
            anchor = GetTwinSideHealerAnchor(sideIndex);
            role = "tank_healer";
        }
        else
        {
            anchor = GetTwinSideAnchor(sideIndex);
        }

        stageKey = "twins:" + role + ":" + std::to_string(sideIndex);

        std::ostringstream fields;
        fields << "boss=twins phase=prepull side=" << sideIndex
               << " staged_side=" << sideIndex
               << " role=" << role
               << " anchor_x=" << anchor.GetPositionX()
               << " anchor_y=" << anchor.GetPositionY();
        stageFields = fields.str();
    }
    else
    {
        // Center-spread opener layout (issue #8): remaining healers and ranged
        // DPS spread from center rather than overstacking on one exact point.
        // Melee DPS bias toward Vek'nilash for quick engage on pull.
        bool const isMeleeDps = !botAI->IsRanged(bot) && !isHealer;
        bool const isRangedDps = botAI->IsRanged(bot) && !isHealer;
        anchor = GetTwinPrePullCenterSpreadAnchor(bot, isMeleeDps, isRangedDps);

        std::string const role = isMeleeDps ? "melee_dps" : (isRangedDps ? "ranged_dps" : "raid_healer");
        stageKey = "twins:" + role + ":spread";
        std::ostringstream spreadFields;
        spreadFields << "boss=twins phase=prepull side=center staged_side=center"
                     << " role=" << role
                     << " anchor_x=" << anchor.GetPositionX()
                     << " anchor_y=" << anchor.GetPositionY();
        stageFields = spreadFields.str();
    }

    Aq40Helpers::LogAq40Info(bot, "prepull_stage", stageKey, stageFields, 30000);

    float constexpr kHoldTolerance = 4.0f;
    float const distToAnchor = bot->GetExactDist2d(anchor.GetPositionX(), anchor.GetPositionY());
    if (distToAnchor <= kHoldTolerance)
        return true;

    bool moved = MoveTo(bot->GetMapId(), anchor.GetPositionX(), anchor.GetPositionY(),
                  anchor.GetPositionZ(), false, false, false, true,
                  MovementPriority::MOVEMENT_COMBAT, true, false);
    return moved || distToAnchor > kHoldTolerance;
}

bool Aq40TwinEmperorsHealerSupportAction::Execute(Event /*event*/)
{
    if (!botAI->IsHeal(bot))
        return false;

    Aq40Helpers::ApplyTwinTemporaryCombatStrategies(bot, botAI);
    Aq40Helpers::TwinEncounterSnapshot snapshot;
    bool const haveSnapshot = GetTwinSnapshot(bot, botAI, context, snapshot);

    auto fallbackToRaidHealer = [&](std::string const& reason) -> bool
    {
        Aq40Helpers::ClearTwinHealerFocusTargets(bot, botAI);
        Aq40Helpers::LogAq40Info(bot, "healer_mode", "twins:raid_healer:" + reason,
            "boss=twins mode=raid_healer reason=" + reason, 10000);

        Position center = Aq40Helpers::GetTwinRoomCenterPosition();
        float constexpr kTwinRaidHealerCenterLeash = 25.0f;
        if (!bot->GetCurrentSpell(CURRENT_GENERIC_SPELL) && !bot->GetCurrentSpell(CURRENT_CHANNELED_SPELL) &&
            bot->GetExactDist2d(center.GetPositionX(), center.GetPositionY()) > kTwinRaidHealerCenterLeash)
        {
            Aq40Helpers::LogAq40Info(bot, "healer_position", "twins:raid_healer:center:" + reason,
                "boss=twins mode=raid_healer reason=" + reason + " path=center", 10000);
            return MoveTo(bot->GetMapId(), center.GetPositionX(), center.GetPositionY(), center.GetPositionZ(),
                          false, false, false, true, MovementPriority::MOVEMENT_COMBAT, true, false);
        }

        return false;
    };

    uint32 tankHealerSide = 0;
    if (!Aq40Helpers::GetTwinDedicatedTankHealerSide(bot, botAI, tankHealerSide))
        return fallbackToRaidHealer("no_dedicated_side");

    GuidVector encounterUnits = Aq40Helpers::GetTwinEncounterUnits(bot, botAI,
        context->GetValue<GuidVector>("attackers")->Get());
    Aq40Helpers::TwinAssignments assignment = Aq40Helpers::GetTwinAssignments(bot, botAI, encounterUnits);
    if (!assignment.veklor || !assignment.veknilash || !assignment.sideEmperor)
    {
        Aq40Helpers::LogAq40Warn(bot, "missing_state", "twins:healer_assignment",
            "boss=twins missing=healer_assignment");
        return fallbackToRaidHealer("missing_assignment");
    }

    // Strict fallback: only enter raid-healer mode for explicit Degraded state.
    // Temporary ownership ambiguity during a pickup window must NOT trigger
    // fallback — the healer stays on its assigned side and keeps retrying.
    if (haveSnapshot && Aq40Helpers::ShouldTwinHealerFallbackToRaidMode(snapshot, assignment))
        return fallbackToRaidHealer("degraded_recovery");

    std::list<ObjectGuid> const focusTargets = Aq40Helpers::GetTwinHealerFocusTargets(bot, botAI, assignment);
    if (focusTargets.empty())
    {
        // During pickup recovery, keep the healer on its side even without
        // resolved focus targets — do not fall back to raid-healer mode.
        if (haveSnapshot && snapshot.strategyMode == Aq40Helpers::TwinStrategyMode::PickupRecovery)
        {
            Aq40Helpers::LogAq40Info(bot, "healer_focus_wait",
                "twins:side:" + std::to_string(assignment.sideIndex),
                "boss=twins side=" + std::to_string(assignment.sideIndex) +
                " mode=tank_healer reason=pickup_focus_wait" +
                " side_boss=" + Aq40Helpers::GetAq40LogUnit(assignment.sideEmperor), 5000);
        }
        else
        {
            Aq40Helpers::LogAq40Warn(bot, "no_healer_focus",
                "twins:side:" + std::to_string(assignment.sideIndex),
                "boss=twins side=" + std::to_string(assignment.sideIndex) +
                " side_boss=" + Aq40Helpers::GetAq40LogUnit(assignment.sideEmperor));
            return fallbackToRaidHealer("no_local_focus");
        }
    }

    if (!focusTargets.empty())
    {
        Aq40Helpers::ApplyTwinHealerFocusTargets(bot, botAI, focusTargets);
        Aq40Helpers::LogAq40Info(bot, "healer_focus",
            "twins:side:" + std::to_string(assignment.sideIndex) + ":count:" + std::to_string(focusTargets.size()),
            "boss=twins side=" + std::to_string(assignment.sideIndex) +
            " mode=tank_healer" +
            " count=" + std::to_string(focusTargets.size()) +
            " side_boss=" + Aq40Helpers::GetAq40LogUnit(assignment.sideEmperor), 10000);
    }

    // --- Recovery ladder ---
    // The healer must remain on its assigned side.  When displaced, the ladder
    // tries four progressively softer anchors before giving up:
    //   1. Direct move to side healer anchor
    //   2. Staged step toward side healer anchor
    //   3. Center-to-side re-entry anchor (midway point)
    //   4. Local safe support anchor near the assigned side tank
    Position healerAnchor = Aq40Helpers::GetTwinRoomSideHealerAnchor(assignment.sideIndex);
    Position oppositeHealerAnchor = Aq40Helpers::GetTwinRoomSideHealerAnchor(1u - assignment.sideIndex);
    float const sideAnchorDistance =
        bot->GetExactDist2d(healerAnchor.GetPositionX(), healerAnchor.GetPositionY());
    float const oppositeAnchorDistance =
        bot->GetExactDist2d(oppositeHealerAnchor.GetPositionX(), oppositeHealerAnchor.GetPositionY());
    bool const wrongSide = oppositeAnchorDistance + 8.0f < sideAnchorDistance;
    bool const outsideSideLeash = Aq40Helpers::IsTwinHealerOutsideSideLeash(bot, assignment);
    float constexpr kTwinSideHealerAnchorLeash = 18.0f;
    bool const tooFarFromAnchor =
        sideAnchorDistance > kTwinSideHealerAnchorLeash;
    if (wrongSide || outsideSideLeash || tooFarFromAnchor)
    {
        std::string const recoveryReason = wrongSide ? "wrong_side" :
            (outsideSideLeash ? "outside_leash" : "side_anchor");
        if (IsTwinNonInstantCastActive(bot))
            botAI->RequestSpellInterrupt();

        Position moveTarget = healerAnchor;
        std::string recoveryPath = "healer_anchor";
        bool moved = false;

        // Step 1: Direct move to side healer anchor
        moved = MoveTo(bot->GetMapId(), moveTarget.GetPositionX(), moveTarget.GetPositionY(),
                        moveTarget.GetPositionZ(), false, false, false, true,
                        MovementPriority::MOVEMENT_COMBAT, true, false);

        // Step 2: Staged step toward side healer anchor
        if (!moved)
        {
            Position recoveryStep;
            if (GetTwinRecoveryStepTowardAnchor(bot, healerAnchor, 12.0f, recoveryStep))
            {
                recoveryPath = "side_anchor_step";
                moveTarget = recoveryStep;
                moved = MoveTo(bot->GetMapId(), moveTarget.GetPositionX(), moveTarget.GetPositionY(),
                               moveTarget.GetPositionZ(), false, false, false, true,
                               MovementPriority::MOVEMENT_COMBAT, true, false);
            }
        }

        // Step 3: Center-to-side re-entry anchor (midway between center and side anchor)
        if (!moved)
        {
            Position reentryAnchor = Aq40Helpers::GetTwinHealerCenterToSideReentryAnchor(assignment.sideIndex);
            float const reentryDistance =
                bot->GetExactDist2d(reentryAnchor.GetPositionX(), reentryAnchor.GetPositionY());
            if (reentryDistance > 3.0f)
            {
                recoveryPath = "center_reentry";
                moveTarget = reentryAnchor;
                moved = MoveTo(bot->GetMapId(), moveTarget.GetPositionX(), moveTarget.GetPositionY(),
                               moveTarget.GetPositionZ(), false, false, false, true,
                               MovementPriority::MOVEMENT_COMBAT, true, false);
            }

            // Also try stepping toward the re-entry anchor if direct move fails
            if (!moved)
            {
                Position reentryStep;
                if (GetTwinRecoveryStepTowardAnchor(bot, reentryAnchor, 12.0f, reentryStep))
                {
                    recoveryPath = "center_reentry_step";
                    moveTarget = reentryStep;
                    moved = MoveTo(bot->GetMapId(), moveTarget.GetPositionX(), moveTarget.GetPositionY(),
                                   moveTarget.GetPositionZ(), false, false, false, true,
                                   MovementPriority::MOVEMENT_COMBAT, true, false);
                }
            }
        }

        // Step 4: Local safe support anchor near the assigned side tank
        if (!moved)
        {
            Position tankSupportAnchor;
            if (Aq40Helpers::GetTwinHealerLocalTankSupportAnchor(bot, botAI, assignment, tankSupportAnchor))
            {
                float const supportDistance =
                    bot->GetExactDist2d(tankSupportAnchor.GetPositionX(), tankSupportAnchor.GetPositionY());
                if (supportDistance > 3.0f)
                {
                    recoveryPath = "local_tank_support";
                    moveTarget = tankSupportAnchor;
                    moved = MoveTo(bot->GetMapId(), moveTarget.GetPositionX(), moveTarget.GetPositionY(),
                                   moveTarget.GetPositionZ(), false, false, false, true,
                                   MovementPriority::MOVEMENT_COMBAT, true, false);
                }

                if (!moved)
                {
                    Position supportStep;
                    if (GetTwinRecoveryStepTowardAnchor(bot, tankSupportAnchor, 12.0f, supportStep))
                    {
                        recoveryPath = "local_tank_support_step";
                        moveTarget = supportStep;
                        moved = MoveTo(bot->GetMapId(), moveTarget.GetPositionX(), moveTarget.GetPositionY(),
                                       moveTarget.GetPositionZ(), false, false, false, true,
                                       MovementPriority::MOVEMENT_COMBAT, true, false);
                    }
                }
            }
        }

        Aq40Helpers::LogAq40Info(bot, "healer_position",
            "twins:tank_healer:" + std::to_string(assignment.sideIndex) + ":" + recoveryPath,
            "boss=twins mode=tank_healer side=" + std::to_string(assignment.sideIndex) +
            " staged_side=" + std::to_string(tankHealerSide) +
            " live_side=" + std::to_string(assignment.sideIndex) +
            " reason=" + recoveryReason +
            " path=" + recoveryPath, 5000);

        if (!moved)
        {
            Aq40Helpers::LogAq40Warn(bot, "healer_recovery_exhausted",
                "twins:tank_healer:" + std::to_string(assignment.sideIndex) + ":ladder",
                "boss=twins mode=tank_healer side=" + std::to_string(assignment.sideIndex) +
                " reason=" + recoveryReason + " path=ladder_exhausted", 5000);
        }

        return moved;
    }

    return false;
}

bool Aq40TwinEmperorsPostSwapHoldAction::Execute(Event /*event*/)
{
    // Issue #5: hunters now participate for pet control during post-swap hold.
    if (botAI->IsHeal(bot) || IsTwinTankRole(bot) || !botAI->IsRanged(bot))
        return false;

    bool const isHunter = bot->getClass() == CLASS_HUNTER;

    GuidVector encounterUnits = Aq40Helpers::GetTwinEncounterUnits(bot, botAI,
        context->GetValue<GuidVector>("attackers")->Get());
    Aq40Helpers::TwinAssignments assignment = Aq40Helpers::GetTwinAssignments(bot, botAI, encounterUnits);
    if (!assignment.veklor)
        return false;

    Unit* priorityBugTarget = GetTwinPriorityBugTarget(bot, botAI, encounterUnits);
    Aq40TwinEmperors::PublishRaidMarkers(bot, botAI, assignment.veklor, assignment.veknilash, priorityBugTarget);

    if (!Aq40Helpers::IsTwinPostSwapThreatHoldActive(bot, botAI, assignment))
        return false;

    // Enforce pet policy during post-swap hold (issue #5)
    Aq40TwinEmperors::TwinEncounterPhase const phase = Aq40TwinEmperors::GetEncounterPhase(bot);
    EnforceTwinPetPolicy(bot, botAI, phase, assignment.veklor,
        isHunter ? assignment.veknilash : nullptr);

    // Hunters: pet controlled above, but hunter can continue attacking Vek'nilash.
    // Don't need Vek'lor threat hold since they attack Vek'nilash. (issue #5)
    if (isHunter)
    {
        StopTwinPetAttackOnTarget(bot, assignment.veklor);
        Aq40Helpers::LogAq40Info(bot, "post_swap_hold",
            "twins:hunter_pet_hold:" + Aq40Helpers::GetAq40LogUnit(assignment.veklor),
            "boss=twins reason=hunter_pet_hold target=" + Aq40Helpers::GetAq40LogUnit(assignment.veklor), 3000);
        return true;
    }

    // Caster ranged: stop Vek'lor damage, move to central anchor
    StopTwinDamageOn(bot, botAI, context, assignment.veklor);
    Aq40TwinEmperors::ClearLocalRti(botAI);

    if (priorityBugTarget && priorityBugTarget->IsAlive())
        return false;

    std::ostringstream fields;
    fields << "boss=twins reason=post_swap_hold elapsed_ms="
           << Aq40Helpers::GetTwinPostSwapElapsedMs(bot, assignment)
           << " target=" << Aq40Helpers::GetAq40LogUnit(assignment.veklor);
    Aq40Helpers::LogAq40Info(bot, "post_swap_hold",
        "twins:veklor:" + Aq40Helpers::GetAq40LogUnit(assignment.veklor), fields.str(), 3000);

    Position anchor;
    if (GetTwinCentralWaitAnchor(bot, assignment.veklor, anchor) &&
        bot->GetExactDist2d(anchor.GetPositionX(), anchor.GetPositionY()) > 5.0f)
    {
        botAI->RequestSpellInterrupt();
        return MoveTo(bot->GetMapId(), anchor.GetPositionX(), anchor.GetPositionY(), anchor.GetPositionZ(),
                      false, false, false, true, MovementPriority::MOVEMENT_COMBAT, true, false);
    }

    return true;
}

bool Aq40TwinEmperorsDodgeBlizzardAction::Execute(Event /*event*/)
{
    GuidVector encounterUnits = Aq40Helpers::GetTwinEncounterUnits(bot, botAI,
        context->GetValue<GuidVector>("attackers")->Get());
    Aq40Helpers::TwinAssignments assignment = Aq40Helpers::GetTwinAssignments(bot, botAI, encounterUnits);
    if (!assignment.veklor && !assignment.veknilash)
        return false;

    Aura* areaAura = AI_VALUE(Aura*, "area debuff");
    bool blizzardAura = Aq40SpellIds::HasAnyAura(botAI, bot, { Aq40SpellIds::TwinBlizzard }) ||
                        botAI->HasAura("blizzard", bot);
    DynamicObject* blizzardObject = nullptr;
    float blizzardRadius = 8.0f;
    if (areaAura && !areaAura->IsRemoved() && !areaAura->IsExpired())
    {
        SpellInfo const* spellInfo = areaAura->GetSpellInfo();
        if (Aq40SpellIds::MatchesAnySpellId(spellInfo, { Aq40SpellIds::TwinBlizzard }) ||
            (spellInfo && std::string(spellInfo->SpellName[LOCALE_enUS]) == "Blizzard"))
        {
            blizzardAura = true;
            blizzardObject = areaAura->GetDynobjOwner();
            if (blizzardObject)
                blizzardRadius = std::max(blizzardObject->GetRadius(), blizzardRadius);
        }
    }

    if (!blizzardAura)
    {
        // Fall back to scripted event state (issue #6): the spell hook may
        // have fired before the aura is visible to this bot.  Only act if
        // the bot is within Blizzard danger range of Vek'lor.
        uint32 blizzardElapsed = 0;
        bool const scriptedBlizzard = Aq40TwinEmperors::IsScriptedBlizzardActive(bot, &blizzardElapsed);
        if (!scriptedBlizzard || !assignment.veklor || bot->GetDistance2d(assignment.veklor) > 30.0f)
            return false;

        // Use the scripted event — treat as blizzard-in-progress
        blizzardAura = true;
    }

    botAI->RequestSpellInterrupt();

    Position dangerPosition;
    if (blizzardObject)
        dangerPosition = blizzardObject->GetPosition();
    else if (assignment.veklor)
        dangerPosition = assignment.veklor->GetPosition();
    else
        dangerPosition = bot->GetPosition();

    Aq40Helpers::LogAq40Info(bot, "dodge_blizzard",
        "twins:blizzard:" + Aq40Helpers::GetAq40LogUnit(assignment.veklor),
        "boss=twins hazard=blizzard source=" + Aq40Helpers::GetAq40LogUnit(assignment.veklor), 3000);

    if (FleePosition(dangerPosition, blizzardRadius + 3.0f, 250U))
        return true;

    Position anchor = Aq40Helpers::GetTwinRoomCenterPosition();
    if (bot->GetExactDist2d(anchor.GetPositionX(), anchor.GetPositionY()) > 5.0f)
        return MoveTo(bot->GetMapId(), anchor.GetPositionX(), anchor.GetPositionY(), anchor.GetPositionZ(),
                      false, false, false, true, MovementPriority::MOVEMENT_COMBAT, true, false);

    return true;
}

bool Aq40TwinEmperorsDodgeExplodeBugAction::Execute(Event /*event*/)
{
    GuidVector encounterUnits = Aq40Helpers::GetTwinEncounterUnits(bot, botAI,
        context->GetValue<GuidVector>("attackers")->Get());
    Aq40Helpers::TwinAssignments assignment = Aq40Helpers::GetTwinAssignments(bot, botAI, encounterUnits);
    if (Aq40Helpers::IsTwinPrimaryTankOnActiveBoss(bot, assignment))
        return false;

    Unit* explodeBug = Aq40Helpers::FindTwinMarkedBug(bot, botAI, encounterUnits, Aq40SpellIds::TwinExplodeBug);
    if (!explodeBug || !explodeBug->IsAlive())
        return false;

    float constexpr kExplodeBugSafeDistance = 16.0f;
    if (bot->GetDistance2d(explodeBug) > kExplodeBugSafeDistance)
        return false;

    botAI->RequestSpellInterrupt();
    Aq40Helpers::LogAq40Info(bot, "dodge_explode_bug",
        "twins:explode_bug:" + Aq40Helpers::GetAq40LogUnit(explodeBug),
        "boss=twins hazard=explode_bug target=" + Aq40Helpers::GetAq40LogUnit(explodeBug), 3000);

    if (FleePosition(explodeBug->GetPosition(), kExplodeBugSafeDistance, 250U))
        return true;

    Position anchor = Aq40Helpers::GetTwinRoomCenterPosition();
    if (bot->GetExactDist2d(anchor.GetPositionX(), anchor.GetPositionY()) > 5.0f)
        return MoveTo(bot->GetMapId(), anchor.GetPositionX(), anchor.GetPositionY(), anchor.GetPositionZ(),
                      false, false, false, true, MovementPriority::MOVEMENT_COMBAT, true, false);

    return true;
}

bool Aq40TwinEmperorsAvoidVeklorAction::Execute(Event /*event*/)
{
    GuidVector encounterUnits = Aq40Helpers::GetTwinEncounterUnits(bot, botAI,
        context->GetValue<GuidVector>("attackers")->Get());
    Aq40Helpers::TwinAssignments assignment = Aq40Helpers::GetTwinAssignments(bot, botAI, encounterUnits);
    if (!assignment.veklor || !assignment.veklor->IsAlive())
        return false;

    Aq40Helpers::TwinEncounterSnapshot snapshot;
    bool const haveSnapshot = GetTwinSnapshot(bot, botAI, context, snapshot);
    bool const isActiveWarlockTank =
        Aq40Helpers::GetTwinRoleCohort(bot, botAI) == Aq40Helpers::TwinRoleCohort::WarlockTank &&
        Aq40Helpers::IsTwinPrimaryTankOnActiveBoss(bot, assignment);
    bool const isPickupWarlock =
        Aq40Helpers::GetTwinRoleCohort(bot, botAI) == Aq40Helpers::TwinRoleCohort::WarlockTank &&
        ((haveSnapshot && snapshot.veklor.botExecutorGuid == bot->GetGUID()) ||
         Aq40Helpers::GetStableTwinRoleIndex(bot, botAI) == assignment.veklorSideIndex) &&
        !Aq40Helpers::IsTwinWarlockPickupEstablished(bot, botAI, assignment);
    if (isActiveWarlockTank || isPickupWarlock)
        return false;

    float constexpr kArcaneBurstSafeDistance = 21.0f;
    // Scripted Arcane Burst event (issue #6): if the spell hook just fired,
    // widen the trigger check so bots that are near the boundary react before
    // the aura damage lands.
    uint32 arcaneBurstElapsed = 0;
    bool const scriptedArcaneBurst = Aq40TwinEmperors::IsScriptedArcaneBurstActive(bot, &arcaneBurstElapsed);
    float const effectiveDistance = scriptedArcaneBurst && arcaneBurstElapsed < 2000
        ? kArcaneBurstSafeDistance + 4.0f   // React earlier during scripted window
        : kArcaneBurstSafeDistance;
    if (bot->GetDistance2d(assignment.veklor) >= effectiveDistance)
        return false;

    StopTwinDamageOn(bot, botAI, context, assignment.veklor);
    // Enforce full pet policy — passive/follow, not just attack stop (issue #5)
    {
        Aq40TwinEmperors::TwinEncounterPhase const petPhase = Aq40TwinEmperors::GetEncounterPhase(bot);
        EnforceTwinPetPolicy(bot, botAI, petPhase, assignment.veklor, nullptr);
    }
    Aq40TwinEmperors::ClearLocalRti(botAI);
    Aq40Helpers::LogAq40Info(bot, "avoid_veklor",
        "twins:arcane_burst:" + Aq40Helpers::GetAq40LogUnit(assignment.veklor),
        "boss=twins hazard=arcane_burst target=" + Aq40Helpers::GetAq40LogUnit(assignment.veklor), 3000);

    botAI->RequestSpellInterrupt();
    if (FleePosition(assignment.veklor->GetPosition(), kArcaneBurstSafeDistance, 250U))
        return true;

    Position anchor;
    uint32 sideIndex = Aq40Helpers::GetStableTwinRoleIndex(bot, botAI);
    if (IsTwinTankRole(bot))
        anchor = Aq40Helpers::GetTwinRoomSideAnchor(sideIndex);
    else if (botAI->IsHeal(bot))
        anchor = Aq40Helpers::GetTwinRoomCenterPosition();
    else if (!GetTwinCentralWaitAnchor(bot, assignment.veklor, anchor))
        anchor = Aq40Helpers::GetTwinRoomCenterPosition();

    if (bot->GetExactDist2d(anchor.GetPositionX(), anchor.GetPositionY()) > 5.0f)
        return MoveTo(bot->GetMapId(), anchor.GetPositionX(), anchor.GetPositionY(), anchor.GetPositionZ(),
                      false, false, false, true, MovementPriority::MOVEMENT_COMBAT, true, false);

    return true;
}

bool Aq40TwinEmperorsChooseTargetAction::Execute(Event /*event*/)
{
    GuidVector encounterUnits = Aq40Helpers::GetTwinEncounterUnits(bot, botAI,
        context->GetValue<GuidVector>("attackers")->Get());
    Aq40Helpers::TwinAssignments assignment = Aq40Helpers::GetTwinAssignments(bot, botAI, encounterUnits);
    if (!assignment.veklor && !assignment.veknilash)
    {
        Aq40TwinEmperors::ClearLocalRti(botAI);
        Aq40Helpers::LogAq40Warn(bot, "missing_state", "twins:target_boss_units",
            "boss=twins missing=boss_units");
        return false;
    }

    Aq40Helpers::ApplyTwinTemporaryCombatStrategies(bot, botAI);
    Aq40Helpers::TwinEncounterSnapshot snapshot;
    bool const haveSnapshot = GetTwinSnapshot(bot, botAI, context, snapshot);

    Aq40Helpers::TwinRoleCohort const cohort = Aq40Helpers::GetTwinRoleCohort(bot, botAI);
    bool const isWarlockTank = cohort == Aq40Helpers::TwinRoleCohort::WarlockTank;
    bool const isMeleeTank = cohort == Aq40Helpers::TwinRoleCohort::MeleeTank;

    if (isWarlockTank || isMeleeTank)
        return false;

    if (botAI->IsHeal(bot))
        return false;

    bool const isHunter = bot->getClass() == CLASS_HUNTER;
    bool const isRangedDps = botAI->IsRanged(bot);

    Unit* desiredTarget = nullptr;
    bool isBugTarget = false;
    std::string reason;

    // Bug priority targeting (issue #5): role-specific DPS contract.
    // Caster ranged: all bugs (explode > mutate > hostile).
    // Hunters: explode/mutate marked bugs only, not generic hostile.
    // Melee DPS: no bug targeting — primary damage is on Vek'nilash.
    float constexpr kBugVeklorSafeDistance = 21.0f;

    Unit* explodeBug = Aq40Helpers::FindTwinMarkedBug(bot, botAI, encounterUnits, Aq40SpellIds::TwinExplodeBug);
    if (explodeBug && explodeBug->IsAlive() && (isRangedDps || isHunter))
    {
        // Safety: don't chase bugs into Vek'lor unsafe radius (issue #5)
        if (!assignment.veklor || !assignment.veklor->IsAlive() ||
            explodeBug->GetDistance2d(assignment.veklor) >= kBugVeklorSafeDistance)
        {
            desiredTarget = explodeBug;
            isBugTarget = true;
            reason = "explode_bug";
        }
    }

    if (!desiredTarget)
    {
        Unit* mutateBug = Aq40Helpers::FindTwinMarkedBug(bot, botAI, encounterUnits, Aq40SpellIds::TwinMutateBug);
        if (mutateBug && mutateBug->IsAlive() && (isRangedDps || isHunter))
        {
            if (!assignment.veklor || !assignment.veklor->IsAlive() ||
                mutateBug->GetDistance2d(assignment.veklor) >= kBugVeklorSafeDistance)
            {
                desiredTarget = mutateBug;
                isBugTarget = true;
                reason = "mutate_bug";
            }
        }
    }

    if (!desiredTarget)
    {
        // Only caster ranged handle generic hostile bugs, not hunters or melee (issue #5)
        Unit* hostileBug = Aq40Helpers::FindTwinHostileBug(bot, botAI, encounterUnits);
        if (hostileBug && hostileBug->IsAlive() && isRangedDps && !isHunter)
        {
            if (!assignment.veklor || !assignment.veklor->IsAlive() ||
                hostileBug->GetDistance2d(assignment.veklor) >= kBugVeklorSafeDistance)
            {
                desiredTarget = hostileBug;
                isBugTarget = true;
                reason = "hostile_bug";
            }
        }
    }

    Unit* priorityBugTarget = isBugTarget ? desiredTarget : GetTwinPriorityBugTarget(bot, botAI, encounterUnits);
    Aq40TwinEmperors::PublishRaidMarkers(bot, botAI, assignment.veklor, assignment.veknilash, priorityBugTarget);

    if (!desiredTarget)
    {
        if (isHunter)
        {
            desiredTarget = assignment.veknilash ? assignment.veknilash : assignment.veklor;
            reason = assignment.veknilash ? "hunter_veknilash" : "hunter_fallback";
            if (desiredTarget == assignment.veklor)
                Aq40Helpers::LogAq40Warn(bot, "wrong_immune_target", "twins:hunter_veklor",
                    "boss=twins reason=no_veknilash target=" + Aq40Helpers::GetAq40LogUnit(desiredTarget));
        }
        else if (isRangedDps)
        {
            desiredTarget = assignment.veklor ? assignment.veklor : assignment.veknilash;
            reason = assignment.veklor ? "ranged_veklor" : "ranged_fallback";
            if (desiredTarget == assignment.veknilash)
                Aq40Helpers::LogAq40Warn(bot, "wrong_immune_target", "twins:ranged_veknilash",
                    "boss=twins reason=no_veklor target=" + Aq40Helpers::GetAq40LogUnit(desiredTarget));
        }
        else
        {
            desiredTarget = assignment.veknilash ? assignment.veknilash : assignment.veklor;
            reason = assignment.veknilash ? "melee_veknilash" : "melee_fallback";
            if (desiredTarget == assignment.veklor)
                Aq40Helpers::LogAq40Warn(bot, "wrong_immune_target", "twins:melee_veklor",
                    "boss=twins reason=no_veknilash target=" + Aq40Helpers::GetAq40LogUnit(desiredTarget));
        }
    }

    if (!desiredTarget || !desiredTarget->IsAlive())
    {
        Aq40TwinEmperors::ClearLocalRti(botAI);
        if (haveSnapshot && snapshot.strategyMode != Aq40Helpers::TwinStrategyMode::Normal)
        {
            Position recoveryAnchor;
            bool shouldMove = false;
            if (!PrepareTwinRecoveryHold(bot, botAI, context, assignment,
                    snapshot.strategyMode == Aq40Helpers::TwinStrategyMode::PickupRecovery ?
                        "pickup_recovery" : "degraded_recovery",
                    recoveryAnchor, shouldMove))
                return false;

            if (!shouldMove)
                return true;

            botAI->RequestSpellInterrupt();
            return MoveTo(bot->GetMapId(), recoveryAnchor.GetPositionX(), recoveryAnchor.GetPositionY(),
                          recoveryAnchor.GetPositionZ(), false, false, false, true,
                          MovementPriority::MOVEMENT_COMBAT, true, false);
        }
        return false;
    }

    if (haveSnapshot && snapshot.strategyMode != Aq40Helpers::TwinStrategyMode::Normal && !isBugTarget)
    {
        // Pre-teleport melee DPS staging (issue #8): during teleport window or
        // pickup recovery, melee DPS moves toward the receiving side where
        // Vek'nilash will be, rather than collapsing to room center.  This
        // keeps melee ready to engage as soon as tank pickup is established.
        bool const isMeleeDps = !isRangedDps && !isHunter;
        if (isMeleeDps && assignment.veknilash && assignment.veknilash->IsAlive())
        {
            bool const teleportActive = Aq40TwinEmperors::IsTwinTeleportWindowActive(bot);
            bool const pickupRecovery =
                snapshot.strategyMode == Aq40Helpers::TwinStrategyMode::PickupRecovery;
            if (teleportActive || pickupRecovery)
            {
                if (assignment.veklor)
                    StopTwinDamageOn(bot, botAI, context, assignment.veklor);
                if (assignment.veknilash)
                    StopTwinDamageOn(bot, botAI, context, assignment.veknilash);
                Aq40TwinEmperors::ClearLocalRti(botAI);

                Position preTeleportAnchor;
                if (GetMeleeDpsPreTeleportAnchor(bot, assignment.veknilash,
                        assignment.veknilashSideIndex, preTeleportAnchor))
                {
                    float const distToAnchor = bot->GetExactDist2d(
                        preTeleportAnchor.GetPositionX(), preTeleportAnchor.GetPositionY());

                    Aq40Helpers::LogAq40Info(bot, "melee_pre_teleport",
                        "twins:melee_dps:receiving:" + std::to_string(assignment.veknilashSideIndex),
                        "boss=twins reason=pre_teleport_stage"
                        " side=" + std::to_string(assignment.veknilashSideIndex) +
                        " target=" + Aq40Helpers::GetAq40LogUnit(assignment.veknilash), 3000);

                    if (distToAnchor > 5.0f)
                    {
                        botAI->RequestSpellInterrupt();
                        return MoveTo(bot->GetMapId(), preTeleportAnchor.GetPositionX(),
                                      preTeleportAnchor.GetPositionY(),
                                      preTeleportAnchor.GetPositionZ(),
                                      false, false, false, true,
                                      MovementPriority::MOVEMENT_COMBAT, true, false);
                    }
                    return true;  // At anchor, hold position until pickup
                }
            }
        }

        // Generic recovery hold: non-melee or non-teleport recovery
        Position recoveryAnchor;
        bool shouldMove = false;
        if (!PrepareTwinRecoveryHold(bot, botAI, context, assignment,
                snapshot.strategyMode == Aq40Helpers::TwinStrategyMode::PickupRecovery ?
                    "pickup_recovery" : "degraded_recovery",
                recoveryAnchor, shouldMove))
            return false;

        if (!shouldMove)
            return true;

        botAI->RequestSpellInterrupt();
        return MoveTo(bot->GetMapId(), recoveryAnchor.GetPositionX(), recoveryAnchor.GetPositionY(),
                      recoveryAnchor.GetPositionZ(), false, false, false, true,
                      MovementPriority::MOVEMENT_COMBAT, true, false);
    }

    if (desiredTarget == assignment.veklor && isRangedDps && !isHunter &&
        Aq40Helpers::IsTwinPostSwapThreatHoldActive(bot, botAI, assignment))
    {
        StopTwinDamageOn(bot, botAI, context, assignment.veklor);
        Aq40TwinEmperors::ClearLocalRti(botAI);
        Position anchor;
        if (GetTwinCentralWaitAnchor(bot, assignment.veklor, anchor) &&
            bot->GetExactDist2d(anchor.GetPositionX(), anchor.GetPositionY()) > 5.0f)
        {
            botAI->RequestSpellInterrupt();
            return MoveTo(bot->GetMapId(), anchor.GetPositionX(), anchor.GetPositionY(), anchor.GetPositionZ(),
                          false, false, false, true, MovementPriority::MOVEMENT_COMBAT, true, false);
        }

        Aq40Helpers::LogAq40Info(bot, "post_swap_hold",
            "twins:choose_target:" + Aq40Helpers::GetAq40LogUnit(assignment.veklor),
            "boss=twins reason=veklor_threat_hold target=" + Aq40Helpers::GetAq40LogUnit(assignment.veklor), 3000);
        return true;
    }

    Unit* currentTarget = AI_VALUE(Unit*, "current target");
    if (currentTarget != desiredTarget)
        Aq40Helpers::LogAq40Target(bot, "twins", reason, desiredTarget);

    PinTwinTarget(botAI, context, desiredTarget);
    Aq40TwinEmperors::SyncLocalRti(botAI, desiredTarget, assignment.veklor, assignment.veknilash, priorityBugTarget);

    // Pet policy enforcement for hunters (issue #5).
    // During hold phases or when the pet would enter Vek'lor danger range,
    // recall the pet to passive/follow instead of assigning a target.
    if (isHunter)
    {
        Aq40TwinEmperors::TwinEncounterPhase const petPhase = Aq40TwinEmperors::GetEncounterPhase(bot);
        if (!EnforceTwinPetPolicy(bot, botAI, petPhase, assignment.veklor, desiredTarget))
        {
            DisableTwinPetTauntAutocast(bot->GetPet());
            if (Guardian* pet = bot->GetGuardianPet())
            {
                if (pet->IsAlive() && pet->GetVictim() != desiredTarget)
                {
                    pet->ClearUnitState(UNIT_STATE_FOLLOW);
                    pet->AttackStop();
                    pet->SetTarget(desiredTarget->GetGUID());
                    pet->GetCharmInfo()->SetIsCommandAttack(true);
                    pet->GetCharmInfo()->SetIsAtStay(false);
                    pet->GetCharmInfo()->SetIsFollowing(false);
                    pet->GetCharmInfo()->SetIsCommandFollow(false);
                    pet->GetCharmInfo()->SetIsReturning(false);
                    pet->ToCreature()->AI()->AttackStart(desiredTarget);
                }
            }
        }
    }

    float const distance = bot->GetDistance2d(desiredTarget);
    float const attackRange = isRangedDps ? 28.0f : 5.0f;

    if (distance > attackRange || !bot->IsWithinLOSInMap(desiredTarget))
    {
        float const moveRange = isRangedDps ? 25.0f : 3.0f;
        if (!isBugTarget && desiredTarget == assignment.veklor && isRangedDps && !isHunter)
        {
            Position attackPos;
            if (GetTwinCentralWaitAnchor(bot, desiredTarget, attackPos))
            {
                float const distToAttackPos =
                    bot->GetExactDist2d(attackPos.GetPositionX(), attackPos.GetPositionY());
                if (distToAttackPos > 4.0f)
                {
                    botAI->RequestSpellInterrupt();
                    bool moved = MoveTo(bot->GetMapId(), attackPos.GetPositionX(), attackPos.GetPositionY(),
                                        attackPos.GetPositionZ(), false, false, false, true,
                                        MovementPriority::MOVEMENT_COMBAT, true, false);
                    if (!moved && distance > attackRange)
                        return true;
                    return moved;
                }
            }
        }
        else if (!isBugTarget && assignment.oppositeEmperor)
        {
            Position attackPos;
            float const desiredBossRange = isRangedDps ? 28.0f : 3.0f;
            if (GetFarSidePosition(bot, desiredTarget, assignment.oppositeEmperor, desiredBossRange, attackPos))
            {
                float const distToAttackPos =
                    bot->GetExactDist2d(attackPos.GetPositionX(), attackPos.GetPositionY());
                if (distToAttackPos > 4.0f)
                {
                    bool moved = MoveTo(bot->GetMapId(), attackPos.GetPositionX(), attackPos.GetPositionY(),
                                        attackPos.GetPositionZ(), false, false, false, true,
                                        MovementPriority::MOVEMENT_COMBAT, true, false);
                    if (!moved && distance > attackRange)
                        return true;
                    return moved;
                }
            }
        }

        bool moved = MoveNear(desiredTarget, moveRange, MovementPriority::MOVEMENT_COMBAT);
        if (!moved && distance > attackRange)
            return true;
        return moved;
    }

    // Proactive central anchor maintenance for caster ranged on Vek'lor (issue #5).
    // Return to deterministic safe position immediately — do not wait for
    // Blizzard or Arcane Burst damage to prove the position was wrong.
    if (desiredTarget == assignment.veklor && isRangedDps && !isHunter)
    {
        Position attackPos;
        if (GetTwinCentralWaitAnchor(bot, desiredTarget, attackPos) &&
            bot->GetExactDist2d(attackPos.GetPositionX(), attackPos.GetPositionY()) > 5.0f)
        {
            botAI->RequestSpellInterrupt();
            return MoveTo(bot->GetMapId(), attackPos.GetPositionX(), attackPos.GetPositionY(), attackPos.GetPositionZ(),
                          false, false, false, true, MovementPriority::MOVEMENT_COMBAT, true, false);
        }
    }

    if (bot->GetVictim() != desiredTarget)
        Attack(desiredTarget);

    return false;
}

bool Aq40TwinEmperorsHoldSplitAction::Execute(Event /*event*/)
{
    GuidVector encounterUnits = Aq40Helpers::GetTwinEncounterUnits(bot, botAI,
        context->GetValue<GuidVector>("attackers")->Get());
    Aq40Helpers::TwinAssignments assignment = Aq40Helpers::GetTwinAssignments(bot, botAI, encounterUnits);
    if (!assignment.veklor && !assignment.veknilash)
    {
        Aq40Helpers::LogAq40Warn(bot, "missing_state", "twins:tank_boss_units",
            "boss=twins missing=boss_units");
        return false;
    }

    Aq40Helpers::ApplyTwinTemporaryCombatStrategies(bot, botAI);

    // --- Phase-driven split monitoring (issue #3) ---
    Aq40TwinEmperors::TwinEncounterPhase const encounterPhase = Aq40TwinEmperors::GetEncounterPhase(bot);
    float separation = 0.0f;
    bool healBrotherActive = false;

    if (assignment.veklor && assignment.veknilash)
    {
        separation = assignment.veklor->GetDistance2d(assignment.veknilash);
        Aq40TwinEmperors::SplitBand const splitBand = Aq40TwinEmperors::GetSplitBand(separation);
        if (splitBand != Aq40TwinEmperors::SplitBand::Stable)
        {
            std::ostringstream fields;
            fields << "boss=twins split_band=" << GetTwinSplitBandName(splitBand)
                   << " terminal=" << (splitBand == Aq40TwinEmperors::SplitBand::Terminal ? 1 : 0)
                   << " reason=tank_swap_failure separation=" << static_cast<uint32>(separation)
                   << " phase=" << Aq40TwinEmperors::GetPhaseToken(encounterPhase)
                   << " veklor=" << Aq40Helpers::GetAq40LogUnit(assignment.veklor)
                   << " veknilash=" << Aq40Helpers::GetAq40LogUnit(assignment.veknilash);
            Aq40Helpers::LogAq40Warn(bot, "split_risk",
                "twins:bosses_close:" + std::string(GetTwinSplitBandName(splitBand)), fields.str(), 3000);
        }

        // Detect Heal Brother and arm emergency split recovery
        // Primary detection: poll boss spell slots (existing behavior).
        // Secondary detection: use scripted event state from spell hook (issue #6).
        // The scripted path fires immediately on cast, before the next engine tick.
        for (Unit* boss : { assignment.veklor, assignment.veknilash })
        {
            if (!boss)
                continue;

            Spell* healSpell = boss->GetCurrentSpell(CURRENT_GENERIC_SPELL);
            if (!healSpell)
                healSpell = boss->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
            if (!healSpell ||
                !Aq40SpellIds::MatchesAnySpellId(healSpell->GetSpellInfo(), { Aq40SpellIds::TwinHealBrother }))
                continue;

            healBrotherActive = true;
            Aq40Helpers::LogAq40Warn(bot, "heal_brother_terminal",
                "twins:" + Aq40Helpers::GetAq40LogUnit(boss),
                "boss=twins terminal=1 reason=tank_swap_failure caster=" + Aq40Helpers::GetAq40LogUnit(boss), 10000);

            // Arm emergency split recovery on Heal Brother
            Aq40TwinEmperors::ArmEmergencySplitRecovery(bot, separation, "heal_brother");
        }

        // Scripted Heal Brother fallback (issue #6): if the spell hook fired
        // but the boss spell-slot poll above missed it (finished casting between
        // ticks), still treat as active.
        if (!healBrotherActive && Aq40TwinEmperors::IsScriptedHealBrotherActive(bot))
        {
            healBrotherActive = true;
            Aq40Helpers::LogAq40Warn(bot, "heal_brother_scripted",
                "twins:heal_brother_script",
                "boss=twins terminal=1 reason=heal_brother_script", 10000);
            // Emergency recovery is already armed by NoteTwinHealBrotherCast
        }

        // Arm emergency split recovery on terminal/urgent band
        if (splitBand == Aq40TwinEmperors::SplitBand::Terminal ||
            splitBand == Aq40TwinEmperors::SplitBand::Urgent)
        {
            Aq40TwinEmperors::ArmEmergencySplitRecovery(bot, separation,
                splitBand == Aq40TwinEmperors::SplitBand::Terminal ? "terminal_band" : "urgent_band");
        }

        // Clear emergency recovery when separation is stable again
        if (splitBand == Aq40TwinEmperors::SplitBand::Stable &&
            Aq40TwinEmperors::IsEmergencySplitRecoveryActive(bot) &&
            !healBrotherActive &&
            Aq40TwinEmperors::AreBothOwnersStable(bot))
        {
            Aq40TwinEmperors::ClearEmergencySplitRecovery(bot);
            Aq40Helpers::LogAq40Info(bot, "emergency_recovery_cleared",
                "twins:recovery_complete",
                "boss=twins separation=" + std::to_string(static_cast<uint32>(separation)), 5000);
        }
    }

    Aq40Helpers::TwinRoleCohort const cohort = Aq40Helpers::GetTwinRoleCohort(bot, botAI);
    bool const isWarlockTank = cohort == Aq40Helpers::TwinRoleCohort::WarlockTank;
    bool const isMeleeTank = cohort == Aq40Helpers::TwinRoleCohort::MeleeTank;

    if (!isWarlockTank && !isMeleeTank)
        return false;

    bool const isPrimaryTank = Aq40Helpers::IsTwinPrimaryTankOnActiveBoss(bot, assignment);

    // --- Primary melee tank: phase-driven Vek'nilash management (issue #3) ---
    if (isPrimaryTank && isMeleeTank && assignment.veknilash)
    {
        PinTwinTarget(botAI, context, assignment.veknilash);
        Aq40TwinEmperors::SyncLocalRti(botAI, assignment.veknilash, assignment.veklor, assignment.veknilash, nullptr);

        bool const botHasPickup =
            Aq40TwinEmperors::HasBossPickupAggro(bot, assignment.veknilash) &&
            Aq40TwinEmperors::IsPickupWindowSatisfied(bot, assignment.veknilash, false);
        if (botHasPickup)
        {
            Aq40TwinEmperors::NoteTwinPickupEstablished(bot, false);
            Aq40TwinEmperors::ClearTwinPickupState(bot);
        }

        float const range = bot->GetDistance2d(assignment.veknilash);
        bool const pickupEstablished = botHasPickup || Aq40Helpers::IsTwinMeleePickupEstablished(bot, botAI, assignment);
        std::string recoveryReason;
        bool const recoveryActive = Aq40TwinEmperors::RefreshMeleeRecoveryState(
            bot, botAI, assignment.veknilash, assignment.veklorSideIndex, pickupEstablished, &recoveryReason);

        // Phase-driven positioning:
        // During TeleportWindow/PickupRecovery → forced pickup anchor first
        // During Stable with established pickup → wall-facing orientation
        bool const inPickupPhase =
            encounterPhase == Aq40TwinEmperors::TwinEncounterPhase::TeleportWindow ||
            encounterPhase == Aq40TwinEmperors::TwinEncounterPhase::PickupRecovery;

        Position tankPos;
        std::string recoveryPath;
        bool hasRecoveryPos = false;

        if (inPickupPhase || recoveryActive || !pickupEstablished)
        {
            // Pickup phase: move to forced pickup anchor before engaging
            hasRecoveryPos = ResolveTwinPickupAnchor(
                bot, assignment.veknilash, assignment.veklor, assignment.veknilashSideIndex,
                3.0f, 16.0f, tankPos, recoveryPath);
        }
        else if (pickupEstablished)
        {
            // Stable phase with pickup: use wall-facing orientation (issue #3)
            // Tank stands between boss and wall so Uppercut knockback is center-safe
            hasRecoveryPos = Aq40TwinEmperors::GetVeknilashWallFacingAnchor(
                bot, assignment.veknilash, assignment.veknilashSideIndex, tankPos);
            if (hasRecoveryPos)
                recoveryPath = "wall_facing";
        }

        if (hasRecoveryPos)
        {
            float const distToPos = bot->GetExactDist2d(tankPos.GetPositionX(), tankPos.GetPositionY());
            if (recoveryActive || distToPos > 5.0f || range > 5.0f || !bot->IsWithinLOSInMap(assignment.veknilash))
            {
                std::string const reason = recoveryActive ? recoveryReason :
                    (inPickupPhase ? "pickup_anchor" : "orientation");
                Aq40Helpers::LogAq40Info(bot, "tank_position",
                    "twins:melee:" + recoveryPath + ":" + Aq40Helpers::GetAq40LogUnit(assignment.veknilash),
                    "boss=twins tank=melee reason=" + reason +
                    " phase=" + Aq40TwinEmperors::GetPhaseToken(encounterPhase) +
                    " staged_side=" + std::to_string(assignment.tankStageSide) +
                    " live_side=" + std::to_string(assignment.veknilashSideIndex) +
                    " path=" + recoveryPath +
                    " target=" + Aq40Helpers::GetAq40LogUnit(assignment.veknilash), 3000);

                // During pickup phase, position first before threat generation
                if (inPickupPhase && distToPos > 3.0f)
                {
                    bool moved = MoveTo(bot->GetMapId(), tankPos.GetPositionX(), tankPos.GetPositionY(),
                                        tankPos.GetPositionZ(), false, false, false, true,
                                        MovementPriority::MOVEMENT_COMBAT, true, false);
                    if (!moved && range > 5.0f)
                        return true;
                    return moved;
                }

                bool moved = MoveTo(bot->GetMapId(), tankPos.GetPositionX(), tankPos.GetPositionY(),
                                    tankPos.GetPositionZ(), false, false, false, true,
                                    MovementPriority::MOVEMENT_COMBAT, true, false);
                if (!moved && range > 5.0f)
                    return true;
                return moved;
            }
        }
        else if (range > 5.0f || !bot->IsWithinLOSInMap(assignment.veknilash))
        {
            // MoveNear as last-resort fallback with explicit logging (issue #3)
            Aq40Helpers::LogAq40Warn(bot, "movement_fallback",
                "twins:melee:move_near_fallback:" + Aq40Helpers::GetAq40LogUnit(assignment.veknilash),
                std::string("boss=twins tank=melee reason=move_near_last_resort") +
                " phase=" + Aq40TwinEmperors::GetPhaseToken(encounterPhase) +
                " staged_side=" + std::to_string(assignment.tankStageSide) +
                " live_side=" + std::to_string(assignment.veknilashSideIndex) +
                " path=move_near target=" + Aq40Helpers::GetAq40LogUnit(assignment.veknilash), 3000);
            bool moved = MoveNear(assignment.veknilash, 3.0f, MovementPriority::MOVEMENT_COMBAT);
            if (!moved && range > 5.0f)
                return true;
            return moved;
        }

        // In pickup phase, only attack once positioned
        if (inPickupPhase && !pickupEstablished)
        {
            float const distToPickupAnchor = hasRecoveryPos ?
                bot->GetExactDist2d(tankPos.GetPositionX(), tankPos.GetPositionY()) : 999.0f;
            if (distToPickupAnchor > 3.0f)
                return true;  // Still positioning, don't start threat yet
        }

        if (bot->GetVictim() != assignment.veknilash)
        {
            Aq40Helpers::LogAq40Info(bot, "tank_pickup",
                "twins:melee:" + Aq40Helpers::GetAq40LogUnit(assignment.veknilash),
                "boss=twins tank=melee target=" + Aq40Helpers::GetAq40LogUnit(assignment.veknilash));
            Attack(assignment.veknilash);
        }

        if (botHasPickup)
            Aq40TwinEmperors::ClearTwinPickupState(bot);

        return false;
    }

    if (isPrimaryTank)
        return false;

    // --- Reserve tank behavior (issue #3) ---
    // Reserve tanks stay parked and do not improvise.
    // Reserves are promoted only by encounter state (ProcessReserveTakeover),
    // not by incidental aggro.
    Aq40TwinEmperors::ClearLocalRti(botAI);
    if (bot->GetVictim())
        bot->AttackStop();

    uint32 const sideIndex = Aq40Helpers::GetStableTwinRoleIndex(bot, botAI);

    // During teleport window: move receiving-side reserve tanks inward (issue #3)
    bool const teleportWindowActive =
        encounterPhase == Aq40TwinEmperors::TwinEncounterPhase::TeleportWindow;

    if (teleportWindowActive && isMeleeTank && assignment.veklor && assignment.veknilash)
    {
        // Backup melee tank on the receiving side: move inward so they are
        // closest valid unit when Vek'nilash arrives
        Position receivingAnchor;
        if (Aq40TwinEmperors::GetMeleeReceivingAnchor(bot, assignment.veklor, assignment.veknilash,
                sideIndex, receivingAnchor))
        {
            float const distToReceiving =
                bot->GetExactDist2d(receivingAnchor.GetPositionX(), receivingAnchor.GetPositionY());
            if (distToReceiving > 4.0f)
            {
                Aq40Helpers::LogAq40Info(bot, "tank_position",
                    "twins:melee_receiving:" + std::to_string(sideIndex),
                    "boss=twins role=melee_receiving side=" + std::to_string(sideIndex) +
                    " phase=teleport_window");
                return MoveTo(bot->GetMapId(), receivingAnchor.GetPositionX(),
                              receivingAnchor.GetPositionY(), receivingAnchor.GetPositionZ(),
                              false, false, false, true, MovementPriority::MOVEMENT_COMBAT, true, false);
            }
        }
    }

    // During teleport window: warlock receiving side geometry (issue #3)
    // Designated warlock tank on receiving side moves inward toward melee tank
    // so they are second-closest valid unit for immediate Searing Pain
    if (teleportWindowActive && isWarlockTank && assignment.veklor && assignment.veknilash)
    {
        Position receivingAnchor;
        if (Aq40TwinEmperors::GetWarlockReceivingAnchor(bot, assignment.veklor, assignment.veknilash,
                sideIndex, receivingAnchor))
        {
            float const distToReceiving =
                bot->GetExactDist2d(receivingAnchor.GetPositionX(), receivingAnchor.GetPositionY());
            if (distToReceiving > 4.0f)
            {
                Aq40Helpers::LogAq40Info(bot, "tank_position",
                    "twins:warlock_receiving:" + std::to_string(sideIndex),
                    "boss=twins role=warlock_receiving side=" + std::to_string(sideIndex) +
                    " phase=teleport_window");
                return MoveTo(bot->GetMapId(), receivingAnchor.GetPositionX(),
                              receivingAnchor.GetPositionY(), receivingAnchor.GetPositionZ(),
                              false, false, false, true, MovementPriority::MOVEMENT_COMBAT, true, false);
            }
        }
    }

    // Default: park at side anchor
    Position anchor = GetTwinSideAnchor(sideIndex);

    if (bot->GetExactDist2d(anchor.GetPositionX(), anchor.GetPositionY()) > 8.0f)
    {
        Aq40Helpers::LogAq40Info(bot, "tank_position",
            "twins:backup:" + std::to_string(sideIndex),
            "boss=twins role=backup side=" + std::to_string(sideIndex) +
            " phase=" + Aq40TwinEmperors::GetPhaseToken(encounterPhase));
        return MoveTo(bot->GetMapId(), anchor.GetPositionX(), anchor.GetPositionY(),
                      anchor.GetPositionZ(), false, false, false, true,
                      MovementPriority::MOVEMENT_COMBAT, true, false);
    }

    if (isWarlockTank && !botAI->HasAura("shadow ward", bot) && botAI->CanCastSpell("shadow ward", bot))
    {
        bool const casted = botAI->CastSpell("shadow ward", bot);
        if (casted)
            Aq40Helpers::LogAq40Info(bot, "tank_mitigation",
                "twins:backup:shadow_ward:" + std::to_string(sideIndex),
                "boss=twins tank=warlock spell=shadow_ward role=backup");
    }

    return true;
}

bool Aq40TwinEmperorsWarlockTankAction::isUseful()
{
    if (Aq40Helpers::GetTwinRoleCohort(bot, botAI) != Aq40Helpers::TwinRoleCohort::WarlockTank)
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
    if (Aq40Helpers::GetTwinRoleCohort(bot, botAI) != Aq40Helpers::TwinRoleCohort::WarlockTank)
        return false;

    GuidVector encounterUnits = Aq40Helpers::GetTwinEncounterUnits(bot, botAI,
        context->GetValue<GuidVector>("attackers")->Get());
    Aq40Helpers::TwinAssignments assignment = Aq40Helpers::GetTwinAssignments(bot, botAI, encounterUnits);
    if (!assignment.veklor)
    {
        Aq40Helpers::LogAq40Warn(bot, "missing_state", "twins:warlock_veklor",
            "boss=twins missing=veklor");
        return false;
    }

    Aq40Helpers::ApplyTwinTemporaryCombatStrategies(bot, botAI);

    if (!Aq40Helpers::IsTwinPrimaryTankOnActiveBoss(bot, assignment))
        return false;

    Unit* veklor = assignment.veklor;
    Aq40TwinEmperors::TwinEncounterPhase const encounterPhase = Aq40TwinEmperors::GetEncounterPhase(bot);

    if (AI_VALUE(Unit*, "current target") != veklor)
        PinTwinTarget(botAI, context, veklor);
    Aq40TwinEmperors::SyncLocalRti(botAI, veklor, assignment.veklor, assignment.veknilash, nullptr);

    // Named constants for warlock tank positioning (issue #3)
    // desiredRange: optimal Searing Pain cast position
    // minRange: Arcane Burst safety boundary
    // maxRange: outer limit for threat generation
    float constexpr kWarlockDesiredRange = 24.0f;
    float constexpr kWarlockMinRange = 19.0f;
    float constexpr kWarlockMaxRange = 30.0f;

    bool const botHasPickup =
        Aq40TwinEmperors::HasBossPickupAggro(bot, veklor) &&
        Aq40TwinEmperors::IsPickupWindowSatisfied(bot, veklor, true);
    if (botHasPickup)
    {
        Aq40TwinEmperors::NoteTwinPickupEstablished(bot, true);
        Aq40TwinEmperors::ClearTwinPickupState(bot);
    }

    bool const inPickupPhase =
        encounterPhase == Aq40TwinEmperors::TwinEncounterPhase::TeleportWindow ||
        encounterPhase == Aq40TwinEmperors::TwinEncounterPhase::PickupRecovery;

    float const rangeToVeklor = bot->GetDistance2d(veklor);
    bool const hasLOS = bot->IsWithinLOSInMap(veklor);
    auto moveWarlockSideCastAnchor = [&](std::string const& reason) -> bool
    {
        Position anchorPosition;
        std::string recoveryPath;
        if (!ResolveTwinPickupAnchor(bot, veklor, assignment.oppositeEmperor, assignment.veklorSideIndex,
                                     kWarlockDesiredRange, 20.0f, anchorPosition, recoveryPath))
            return false;

        float const distToAnchor = bot->GetExactDist2d(anchorPosition.GetPositionX(), anchorPosition.GetPositionY());
        if (distToAnchor <= 3.0f)
            return false;

        Aq40Helpers::LogAq40Info(bot, "tank_position",
            "twins:warlock_side_anchor:" + reason + ":" + recoveryPath + ":" + Aq40Helpers::GetAq40LogUnit(veklor),
            "boss=twins tank=warlock reason=" + reason +
            " phase=" + Aq40TwinEmperors::GetPhaseToken(encounterPhase) +
            " path=" + recoveryPath +
            " target=" + Aq40Helpers::GetAq40LogUnit(veklor));
        MoveTo(bot->GetMapId(), anchorPosition.GetPositionX(), anchorPosition.GetPositionY(),
               anchorPosition.GetPositionZ(), false, false, false, true,
               MovementPriority::MOVEMENT_COMBAT, true, false);
        return true;
    };

    // During pickup phase: move to forced anchor position first,
    // start threat gen only after anchor step is satisfied (issue #3)
    if (inPickupPhase && !botHasPickup)
    {
        Position anchorPosition;
        std::string recoveryPath;
        if (ResolveTwinPickupAnchor(bot, veklor, assignment.oppositeEmperor, assignment.veklorSideIndex,
                                    kWarlockDesiredRange, 20.0f, anchorPosition, recoveryPath))
        {
            float const distToAnchor =
                bot->GetExactDist2d(anchorPosition.GetPositionX(), anchorPosition.GetPositionY());
            if (distToAnchor > 3.0f)
            {
                Aq40Helpers::LogAq40Info(bot, "tank_position",
                    "twins:warlock_pickup_anchor:" + recoveryPath + ":" + Aq40Helpers::GetAq40LogUnit(veklor),
                    "boss=twins tank=warlock reason=pickup_anchor" +
                    std::string(" phase=") + Aq40TwinEmperors::GetPhaseToken(encounterPhase) +
                    " path=" + recoveryPath +
                    " target=" + Aq40Helpers::GetAq40LogUnit(veklor));
                MoveTo(bot->GetMapId(), anchorPosition.GetPositionX(), anchorPosition.GetPositionY(),
                       anchorPosition.GetPositionZ(), false, false, false, true,
                       MovementPriority::MOVEMENT_COMBAT, true, false);
                return true;
            }
        }
        // Anchor satisfied or not found — proceed to threat generation below
    }

    if (rangeToVeklor < kWarlockMinRange && moveWarlockSideCastAnchor("arcane_burst_range"))
        return true;
    if (rangeToVeklor <= kWarlockMaxRange && hasLOS)
    {
        if (botAI->CanCastSpell("searing pain", veklor))
        {
            Aq40Helpers::LogAq40Info(bot, "tank_pickup",
                "twins:warlock:" + Aq40Helpers::GetAq40LogUnit(veklor),
                "boss=twins tank=warlock spell=searing_pain target=" +
                Aq40Helpers::GetAq40LogUnit(veklor));
            return botAI->CastSpell("searing pain", veklor);
        }

        if (!botAI->HasAura("shadow ward", bot) && botAI->CanCastSpell("shadow ward", bot))
        {
            bool const casted = botAI->CastSpell("shadow ward", bot);
            if (casted)
                Aq40Helpers::LogAq40Info(bot, "tank_mitigation",
                    "twins:warlock_active:" + Aq40Helpers::GetAq40LogUnit(veklor),
                    "boss=twins tank=warlock spell=shadow_ward target=" +
                    Aq40Helpers::GetAq40LogUnit(veklor));
        }

        if (bot->GetCurrentSpell(CURRENT_GENERIC_SPELL) || bot->GetCurrentSpell(CURRENT_CHANNELED_SPELL))
            return true;
        if (bot->GetVictim() != veklor)
        {
            Aq40Helpers::LogAq40Target(bot, "twins", "warlock_tank", veklor);
            Attack(veklor);
        }

        if (rangeToVeklor < kWarlockMinRange)
            moveWarlockSideCastAnchor("arcane_burst_range");
        else
            moveWarlockSideCastAnchor("side_anchor");

        return true;
    }

    if (rangeToVeklor > kWarlockMaxRange || !hasLOS)
    {
        if (!botAI->HasAura("shadow ward", bot) && botAI->CanCastSpell("shadow ward", bot))
        {
            bool const casted = botAI->CastSpell("shadow ward", bot);
            if (casted)
                Aq40Helpers::LogAq40Info(bot, "tank_mitigation",
                    "twins:warlock_approach:" + Aq40Helpers::GetAq40LogUnit(veklor),
                    "boss=twins tank=warlock spell=shadow_ward target=" +
                    Aq40Helpers::GetAq40LogUnit(veklor));
        }

        Aq40Helpers::LogAq40Info(bot, "tank_position",
            "twins:warlock_approach:" + Aq40Helpers::GetAq40LogUnit(veklor),
            "boss=twins tank=warlock reason=approach target=" + Aq40Helpers::GetAq40LogUnit(veklor));
        if (!moveWarlockSideCastAnchor("approach"))
        {
            // MoveNear as last-resort fallback with logging (issue #3)
            Aq40Helpers::LogAq40Warn(bot, "movement_fallback",
                "twins:warlock:move_near_fallback:" + Aq40Helpers::GetAq40LogUnit(veklor),
                "boss=twins tank=warlock reason=move_near_last_resort target=" +
                Aq40Helpers::GetAq40LogUnit(veklor));
            MoveNear(veklor, kWarlockDesiredRange, MovementPriority::MOVEMENT_COMBAT);
        }
        return true;
    }

    moveWarlockSideCastAnchor("side_anchor");

    return true;
}

// --- Emergency Split Recovery Action (issue #3) ---
// Fires when separation enters urgent/terminal bands or Heal Brother fires.
// Stops raid DPS on both bosses, stops pets, clears unsafe targets, and
// forces both tanks through re-separation routing until the split is stable.
bool Aq40TwinEmperorsEmergencySplitRecoveryAction::Execute(Event /*event*/)
{
    if (!Aq40TwinEmperors::IsEmergencySplitRecoveryActive(bot))
        return false;

    GuidVector encounterUnits = Aq40Helpers::GetTwinEncounterUnits(bot, botAI,
        context->GetValue<GuidVector>("attackers")->Get());
    Aq40Helpers::TwinAssignments assignment = Aq40Helpers::GetTwinAssignments(bot, botAI, encounterUnits);
    if (!assignment.veklor && !assignment.veknilash)
        return false;

    Aq40Helpers::TwinRoleCohort const cohort = Aq40Helpers::GetTwinRoleCohort(bot, botAI);
    bool const isWarlockTank = cohort == Aq40Helpers::TwinRoleCohort::WarlockTank;
    bool const isMeleeTank = cohort == Aq40Helpers::TwinRoleCohort::MeleeTank;
    bool const isPrimaryTank = Aq40Helpers::IsTwinPrimaryTankOnActiveBoss(bot, assignment);

    // For primary tanks: force re-separation routing
    if (isPrimaryTank)
    {
        if (isMeleeTank && assignment.veknilash)
        {
            // Pull Vek'nilash outward toward the wall/side
            Position recoveryAnchor;
            if (Aq40TwinEmperors::GetEmergencySplitRecoveryAnchor(bot, assignment.veknilash, true, recoveryAnchor))
            {
                float const distToAnchor =
                    bot->GetExactDist2d(recoveryAnchor.GetPositionX(), recoveryAnchor.GetPositionY());
                if (distToAnchor > 3.0f)
                {
                    Aq40Helpers::LogAq40Warn(bot, "emergency_split_recovery",
                        "twins:melee_tank_re_separate",
                        "boss=twins tank=melee reason=emergency_split_recovery"
                        " target=" + Aq40Helpers::GetAq40LogUnit(assignment.veknilash), 3000);
                    return MoveTo(bot->GetMapId(), recoveryAnchor.GetPositionX(),
                                  recoveryAnchor.GetPositionY(), recoveryAnchor.GetPositionZ(),
                                  false, false, false, true,
                                  MovementPriority::MOVEMENT_COMBAT, true, false);
                }
            }
            // Already at anchor — keep attacking to hold threat
            if (bot->GetVictim() != assignment.veknilash)
                bot->Attack(assignment.veknilash, true);
            return true;
        }

        if (isWarlockTank && assignment.veklor)
        {
            // Pull Vek'lor outward
            Position recoveryAnchor;
            if (Aq40TwinEmperors::GetEmergencySplitRecoveryAnchor(bot, assignment.veklor, false, recoveryAnchor))
            {
                float const distToAnchor =
                    bot->GetExactDist2d(recoveryAnchor.GetPositionX(), recoveryAnchor.GetPositionY());
                if (distToAnchor > 3.0f)
                {
                    Aq40Helpers::LogAq40Warn(bot, "emergency_split_recovery",
                        "twins:warlock_tank_re_separate",
                        "boss=twins tank=warlock reason=emergency_split_recovery"
                        " target=" + Aq40Helpers::GetAq40LogUnit(assignment.veklor), 3000);
                    return MoveTo(bot->GetMapId(), recoveryAnchor.GetPositionX(),
                                  recoveryAnchor.GetPositionY(), recoveryAnchor.GetPositionZ(),
                                  false, false, false, true,
                                  MovementPriority::MOVEMENT_COMBAT, true, false);
                }
            }
            // Already at anchor — hold position with threat
            if (botAI->CanCastSpell("searing pain", assignment.veklor))
                return botAI->CastSpell("searing pain", assignment.veklor);
            return true;
        }
    }

    // For all non-tank roles (and non-primary tanks):
    // Enforce pet policy, stop raid DPS on both bosses, clear unsafe targets (issue #5)
    {
        Aq40TwinEmperors::TwinEncounterPhase const petPhase = Aq40TwinEmperors::GetEncounterPhase(bot);
        EnforceTwinPetPolicy(bot, botAI, petPhase, assignment.veklor, nullptr);
    }
    if (assignment.veklor)
        StopTwinDamageOn(bot, botAI, context, assignment.veklor);
    if (assignment.veknilash)
        StopTwinDamageOn(bot, botAI, context, assignment.veknilash);

    Aq40TwinEmperors::ClearLocalRti(botAI);

    // Move to room center wait position
    Position waitAnchor;
    if (assignment.veklor)
        GetTwinCentralWaitAnchor(bot, assignment.veklor, waitAnchor);
    else
        waitAnchor = Aq40Helpers::GetTwinRoomCenterPosition();

    float const distToWait = bot->GetExactDist2d(waitAnchor.GetPositionX(), waitAnchor.GetPositionY());
    if (distToWait > 5.0f)
    {
        Aq40Helpers::LogAq40Info(bot, "emergency_split_recovery",
            "twins:dps_hold:" + Aq40Helpers::GetAq40LogRole(bot, botAI),
            "boss=twins reason=emergency_split_dps_hold role=" +
            Aq40Helpers::GetAq40LogRole(bot, botAI), 3000);
        botAI->RequestSpellInterrupt();
        return MoveTo(bot->GetMapId(), waitAnchor.GetPositionX(), waitAnchor.GetPositionY(),
                      waitAnchor.GetPositionZ(), false, false, false, true,
                      MovementPriority::MOVEMENT_COMBAT, true, false);
    }

    return true;
}
