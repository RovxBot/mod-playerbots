#include "RaidAq40Actions.h"

#include <algorithm>
#include <cmath>
#include <list>
#include <sstream>
#include <string>

#include "CharmInfo.h"
#include "CreatureAI.h"
#include "Pet.h"
#include "SharedDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
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
float constexpr kTwinWarlockPrePullStageDistance = 48.0f;
float constexpr kTwinHealerPrePullStageDistance = 58.0f;
float constexpr kTwinTankPrePullStageDistance = 56.0f;
float constexpr kTwinBackupTankHoldDistance = 40.0f;
float constexpr kTwinSplitRiskDistance = 40.0f;

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
    anchor.Relocate(bossX + (dirX / length) * distance,
                    bossY + (dirY / length) * distance,
                    bossZ);
    return anchor;
}

Position GetTwinSideAnchor(uint32 sideIndex)
{
    return GetTwinInitialRadialAnchor(sideIndex, kTwinBackupTankHoldDistance);
}

void PinTwinTarget(PlayerbotAI* botAI, AiObjectContext* context, Unit* target)
{
    if (!botAI || !context || !target)
        return;

    ObjectGuid const guid = target->GetGUID();
    botAI->GetAiObjectContext()->GetValue<GuidVector>("prioritized targets")->Set({ guid });
    context->GetValue<ObjectGuid>("pull target")->Set(guid);
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

bool IsLocalTwinBugForMelee(Player* bot, Unit* bug, Aq40Helpers::TwinAssignments const& assignment)
{
    if (!bot || !bug)
        return false;

    if (bot->GetDistance2d(bug) > 12.0f)
        return false;

    if (!assignment.sideEmperor || !assignment.oppositeEmperor)
        return true;

    return bug->GetDistance2d(assignment.sideEmperor) <= bug->GetDistance2d(assignment.oppositeEmperor) + 5.0f;
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

    ClearTwinPrePullTargeting(bot, botAI, context);

    bool const isWarlockTank = Aq40BossHelper::IsDesignatedTwinWarlockTank(bot);
    bool const isMeleeTank = PlayerbotAI::IsTank(bot) && !PlayerbotAI::IsRanged(bot);
    bool const isHealer = botAI->IsHeal(bot);

    Position anchor;
    std::string stageKey;
    std::string stageFields;
    if (isWarlockTank || isMeleeTank || isHealer)
    {
        uint32 const sideIndex = Aq40Helpers::GetStableTwinRoleIndex(bot, botAI);
        float distance = kTwinTankPrePullStageDistance;
        std::string role = "tank";

        if (isWarlockTank)
        {
            distance = kTwinWarlockPrePullStageDistance;
            role = "warlock_tank";
        }
        else if (isHealer)
        {
            distance = kTwinHealerPrePullStageDistance;
            role = "healer";
        }

        anchor = GetTwinInitialRadialAnchor(sideIndex, distance);
        stageKey = "twins:" + role + ":" + std::to_string(sideIndex);

        std::ostringstream fields;
        fields << "boss=twins phase=prepull side=" << sideIndex
               << " role=" << role
               << " anchor_x=" << anchor.GetPositionX()
               << " anchor_y=" << anchor.GetPositionY();
        stageFields = fields.str();
    }
    else
    {
        anchor.Relocate(kTwinRoomCenterX, kTwinRoomCenterY, kTwinRoomCenterZ);
        stageKey = "twins:raid:center";
        stageFields = "boss=twins phase=prepull side=center";
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

// ---------------------------------------------------------------------------
// HealerSupportAction - supplies side-local focus targets, then gets out of
// the way so the normal class healer AI chooses spells and healing movement.
// ---------------------------------------------------------------------------
bool Aq40TwinEmperorsHealerSupportAction::Execute(Event /*event*/)
{
    if (!botAI->IsHeal(bot))
        return false;

    GuidVector encounterUnits = Aq40Helpers::GetTwinEncounterUnits(bot, botAI,
        context->GetValue<GuidVector>("attackers")->Get());
    Aq40Helpers::TwinAssignments assignment = Aq40Helpers::GetTwinAssignments(bot, botAI, encounterUnits);
    if (!assignment.veklor || !assignment.veknilash || !assignment.sideEmperor)
    {
        Aq40Helpers::LogAq40Warn(bot, "missing_state", "twins:healer_assignment",
            "boss=twins missing=healer_assignment");
        Aq40Helpers::ClearTwinHealerFocusTargets(bot, botAI);
        return false;
    }

    Aq40Helpers::ApplyTwinTemporaryCombatStrategies(bot, botAI);

    std::list<ObjectGuid> const focusTargets = Aq40Helpers::GetTwinHealerFocusTargets(bot, botAI, assignment);
    if (focusTargets.empty())
    {
        Aq40Helpers::LogAq40Warn(bot, "no_healer_focus",
            "twins:side:" + std::to_string(assignment.sideIndex),
            "boss=twins side=" + std::to_string(assignment.sideIndex) +
            " side_boss=" + Aq40Helpers::GetAq40LogUnit(assignment.sideEmperor));
        Aq40Helpers::ClearTwinHealerFocusTargets(bot, botAI);
        return false;
    }

    Aq40Helpers::ApplyTwinHealerFocusTargets(bot, botAI, focusTargets);
    Aq40Helpers::LogAq40Info(bot, "healer_focus",
        "twins:side:" + std::to_string(assignment.sideIndex) + ":count:" + std::to_string(focusTargets.size()),
        "boss=twins side=" + std::to_string(assignment.sideIndex) +
        " count=" + std::to_string(focusTargets.size()) +
        " side_boss=" + Aq40Helpers::GetAq40LogUnit(assignment.sideEmperor), 10000);

    return false;
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
// Hunter        -> dangerous bugs first, then Vek'nilash
// Healer        -> return false (class AI handles healing)
// ---------------------------------------------------------------------------
bool Aq40TwinEmperorsChooseTargetAction::Execute(Event /*event*/)
{
    GuidVector encounterUnits = Aq40Helpers::GetTwinEncounterUnits(bot, botAI,
        context->GetValue<GuidVector>("attackers")->Get());
    Aq40Helpers::TwinAssignments assignment = Aq40Helpers::GetTwinAssignments(bot, botAI, encounterUnits);
    if (!assignment.veklor && !assignment.veknilash)
    {
        Aq40Helpers::LogAq40Warn(bot, "missing_state", "twins:target_boss_units",
            "boss=twins missing=boss_units");
        return false;
    }

    Aq40Helpers::ApplyTwinTemporaryCombatStrategies(bot, botAI);

    bool const isWarlockTank = Aq40BossHelper::IsDesignatedTwinWarlockTank(bot);
    bool const isMeleeTank = !isWarlockTank && PlayerbotAI::IsTank(bot) && !PlayerbotAI::IsRanged(bot);

    // Tanks are handled by WarlockTankAction and HoldSplitAction.
    if (isWarlockTank || isMeleeTank)
        return false;

    if (botAI->IsHeal(bot))
        return false;

    bool const isHunter = bot->getClass() == CLASS_HUNTER;
    bool const isRangedDps = botAI->IsRanged(bot);

    Unit* desiredTarget = nullptr;
    bool isBugTarget = false;
    std::string reason;

    Unit* explodeBug = Aq40Helpers::FindTwinMarkedBug(bot, botAI, encounterUnits, Aq40SpellIds::TwinExplodeBug);
    if (explodeBug && explodeBug->IsAlive() &&
        (isRangedDps || isHunter || IsLocalTwinBugForMelee(bot, explodeBug, assignment)))
    {
        desiredTarget = explodeBug;
        isBugTarget = true;
        reason = "explode_bug";
    }

    // Ranged DPS and hunters prioritize mutated bugs. Melee only swaps if the
    // bug is already local, so it does not drag them across the split.
    if (!desiredTarget)
    {
        Unit* mutateBug = Aq40Helpers::FindTwinMarkedBug(bot, botAI, encounterUnits, Aq40SpellIds::TwinMutateBug);
        if (mutateBug && mutateBug->IsAlive() &&
            (isRangedDps || isHunter || IsLocalTwinBugForMelee(bot, mutateBug, assignment)))
        {
            desiredTarget = mutateBug;
            isBugTarget = true;
            reason = "mutate_bug";
        }
    }

    if (!desiredTarget)
    {
        // Hunters do primarily physical damage — Vek'lor is IMMUNE.
        // Fall back to Vek'nilash instead.
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
        return false;

    Unit* currentTarget = AI_VALUE(Unit*, "current target");
    if (currentTarget != desiredTarget)
        Aq40Helpers::LogAq40Target(bot, "twins", reason, desiredTarget);

    // Always keep the target pinned and set RTI for base AI integration.
    PinTwinTarget(botAI, context, desiredTarget);
    if (isBugTarget)
    {
        SetRtiTarget(botAI, "skull", desiredTarget);
        if (currentTarget != desiredTarget)
            Aq40Helpers::LogAq40Info(bot, "raid_marker",
                "twins:skull:" + Aq40Helpers::GetAq40LogUnit(desiredTarget),
                "boss=twins marker=skull target=" + Aq40Helpers::GetAq40LogUnit(desiredTarget));
    }
    else if (isRangedDps && !isHunter)
    {
        SetRtiTarget(botAI, "square", desiredTarget);   // Vek'lor = square
        if (currentTarget != desiredTarget)
            Aq40Helpers::LogAq40Info(bot, "raid_marker",
                "twins:square:" + Aq40Helpers::GetAq40LogUnit(desiredTarget),
                "boss=twins marker=square target=" + Aq40Helpers::GetAq40LogUnit(desiredTarget));
    }
    else if (!isRangedDps)
    {
        SetRtiTarget(botAI, "diamond", desiredTarget);  // Vek'nilash = diamond
        if (currentTarget != desiredTarget)
            Aq40Helpers::LogAq40Info(bot, "raid_marker",
                "twins:diamond:" + Aq40Helpers::GetAq40LogUnit(desiredTarget),
                "boss=twins marker=diamond target=" + Aq40Helpers::GetAq40LogUnit(desiredTarget));
    }

    // Redirect hunter pets to the hunter's target (prevent IMMUNE spam on Vek'lor).
    if (isHunter)
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

    float const distance = bot->GetDistance2d(desiredTarget);
    float const attackRange = isRangedDps ? 28.0f : 5.0f;

    // If out of range, move toward the target.
    if (distance > attackRange || !bot->IsWithinLOSInMap(desiredTarget))
    {
        float const moveRange = isRangedDps ? 25.0f : 3.0f;
        if (!isBugTarget && assignment.oppositeEmperor)
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
    {
        Aq40Helpers::LogAq40Warn(bot, "missing_state", "twins:tank_boss_units",
            "boss=twins missing=boss_units");
        return false;
    }

    Aq40Helpers::ApplyTwinTemporaryCombatStrategies(bot, botAI);
    bool const splitRisk =
        assignment.veklor && assignment.veknilash &&
        assignment.veklor->GetDistance2d(assignment.veknilash) < kTwinSplitRiskDistance;
    if (splitRisk)
    {
        std::ostringstream fields;
        fields << "boss=twins risk=split_failure separation="
               << static_cast<uint32>(assignment.veklor->GetDistance2d(assignment.veknilash))
               << " veklor=" << Aq40Helpers::GetAq40LogUnit(assignment.veklor)
               << " veknilash=" << Aq40Helpers::GetAq40LogUnit(assignment.veknilash);
        Aq40Helpers::LogAq40Warn(bot, "split_risk", "twins:bosses_close", fields.str());
    }
    bool const isWarlockTank = Aq40BossHelper::IsDesignatedTwinWarlockTank(bot);
    bool const isMeleeTank = !isWarlockTank && PlayerbotAI::IsTank(bot) && !PlayerbotAI::IsRanged(bot);

    if (!isWarlockTank && !isMeleeTank)
        return false;

    bool const isPrimaryTank = Aq40Helpers::IsTwinPrimaryTankOnActiveBoss(bot, assignment);

    if (isPrimaryTank && isMeleeTank && assignment.veknilash)
    {
        // Primary melee tank: pin Vek'nilash and drag away from Vek'lor.
        PinTwinTarget(botAI, context, assignment.veknilash);

        // Tank on the far side of Vek'nilash (away from Vek'lor) to naturally
        // pull Vek'nilash away and prevent Heal Brother.
        Position tankPos;
        bool hasFarPos = assignment.veklor &&
            GetFarSidePosition(bot, assignment.veknilash, assignment.veklor, 3.0f, tankPos);

        float const range = bot->GetDistance2d(assignment.veknilash);

        if (hasFarPos)
        {
            float const distToPos = bot->GetExactDist2d(tankPos.GetPositionX(), tankPos.GetPositionY());
            // If not in melee range or not at far-side position, move there.
            if (distToPos > 5.0f || range > 5.0f || !bot->IsWithinLOSInMap(assignment.veknilash))
            {
                Aq40Helpers::LogAq40Info(bot, "tank_position",
                    "twins:melee:" + Aq40Helpers::GetAq40LogUnit(assignment.veknilash),
                    "boss=twins tank=melee reason=far_side target=" +
                    Aq40Helpers::GetAq40LogUnit(assignment.veknilash));
                bool moved = MoveTo(bot->GetMapId(), tankPos.GetPositionX(), tankPos.GetPositionY(),
                                    tankPos.GetPositionZ(), false, false, false, true,
                                    MovementPriority::MOVEMENT_COMBAT, true, false);
                if (!moved && range > 5.0f)
                    return true;  // Still en route.
                return moved;
            }
        }
        else if (range > 5.0f || !bot->IsWithinLOSInMap(assignment.veknilash))
        {
            Aq40Helpers::LogAq40Warn(bot, "movement_failure",
                "twins:melee:no_far_side:" + Aq40Helpers::GetAq40LogUnit(assignment.veknilash),
                "boss=twins tank=melee reason=no_far_side target=" +
                Aq40Helpers::GetAq40LogUnit(assignment.veknilash));
            bool moved = MoveNear(assignment.veknilash, 3.0f, MovementPriority::MOVEMENT_COMBAT);
            if (!moved && range > 5.0f)
                return true;  // Still en route.
            return moved;
        }

        if (bot->GetVictim() != assignment.veknilash)
        {
            Aq40Helpers::LogAq40Info(bot, "tank_pickup",
                "twins:melee:" + Aq40Helpers::GetAq40LogUnit(assignment.veknilash),
                "boss=twins tank=melee target=" + Aq40Helpers::GetAq40LogUnit(assignment.veknilash));
            Attack(assignment.veknilash);
        }

        return false;  // Let class AI handle tanking abilities.
    }

    if (isPrimaryTank)
        return false;  // Primary warlock tank is handled by WarlockTankAction.

    // Backup tank: hold at assigned side anchor before optional casts.
    if (bot->GetVictim())
        bot->AttackStop();

    uint32 const sideIndex = Aq40Helpers::GetStableTwinRoleIndex(bot, botAI);
    Position anchor = GetTwinSideAnchor(sideIndex);

    if (bot->GetExactDist2d(anchor.GetPositionX(), anchor.GetPositionY()) > 8.0f)
    {
        Aq40Helpers::LogAq40Info(bot, "tank_position",
            "twins:backup:" + std::to_string(sideIndex),
            "boss=twins role=backup side=" + std::to_string(sideIndex));
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

    return true;  // Close enough - hold position.
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
    {
        Aq40Helpers::LogAq40Warn(bot, "missing_state", "twins:warlock_veklor",
            "boss=twins missing=veklor");
        return false;
    }

    Aq40Helpers::ApplyTwinTemporaryCombatStrategies(bot, botAI);

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
        {
            bool const casted = botAI->CastSpell("shadow ward", bot);
            if (casted)
                Aq40Helpers::LogAq40Info(bot, "tank_mitigation",
                    "twins:warlock_hold:" + Aq40Helpers::GetAq40LogUnit(assignment.veklor),
                    "boss=twins tank=warlock spell=shadow_ward role=hold");
        }
        Aq40Helpers::LogAq40Info(bot, "tank_position",
            "twins:warlock_hold:" + Aq40Helpers::GetAq40LogUnit(assignment.veklor),
            "boss=twins tank=warlock reason=opposite_side target=" +
            Aq40Helpers::GetAq40LogUnit(assignment.veklor));
        return true;
    }

    Unit* veklor = assignment.veklor;

    if (AI_VALUE(Unit*, "current target") != veklor)
        PinTwinTarget(botAI, context, veklor);

    // Positioning constants.
    // minRange = 19y — outside 18y Arcane Burst radius with 1y margin for
    //   movement jitter.
    // maxRange = 30y — standard Shadow Bolt range.
    float const desiredRange = 24.0f;
    float const minRange = 19.0f;
    float const maxRange = 30.0f;

    float const rangeToVeklor = bot->GetDistance2d(veklor);
    bool const hasLOS = bot->IsWithinLOSInMap(veklor);
    bool const splitRisk =
        assignment.veknilash && veklor->GetDistance2d(assignment.veknilash) < kTwinSplitRiskDistance;
    auto moveWarlockFarSide = [&](std::string const& reason) -> bool
    {
        Position anchorPosition;
        if (!GetFarSidePosition(bot, veklor, assignment.oppositeEmperor, desiredRange, anchorPosition))
            return false;

        float const distToAnchor = bot->GetExactDist2d(anchorPosition.GetPositionX(), anchorPosition.GetPositionY());
        if (distToAnchor <= 3.0f)
            return false;

        Aq40Helpers::LogAq40Info(bot, "tank_position",
            "twins:warlock_far_side:" + reason + ":" + Aq40Helpers::GetAq40LogUnit(veklor),
            "boss=twins tank=warlock reason=" + reason + " target=" + Aq40Helpers::GetAq40LogUnit(veklor));
        MoveTo(bot->GetMapId(), anchorPosition.GetPositionX(), anchorPosition.GetPositionY(),
               anchorPosition.GetPositionZ(), false, false, false, true,
               MovementPriority::MOVEMENT_COMBAT, true, false);
        return true;
    };

    if (rangeToVeklor < minRange && moveWarlockFarSide("arcane_burst_range"))
        return true;

    // --- CAST-FIRST PRIORITY ---
    // Threat generation is more important than perfect positioning.
    // If we're anywhere within 30y with LOS, try to cast immediately.
    // This prevents the bot from oscillating between movement branches
    // without ever entering the old narrow casting window.
    if (rangeToVeklor <= maxRange && hasLOS)
    {
        if (!botAI->HasAura("shadow ward", bot) && botAI->CanCastSpell("shadow ward", bot))
        {
            bool const casted = botAI->CastSpell("shadow ward", bot);
            if (casted)
                Aq40Helpers::LogAq40Info(bot, "tank_mitigation",
                    "twins:warlock_active:" + Aq40Helpers::GetAq40LogUnit(veklor),
                    "boss=twins tank=warlock spell=shadow_ward target=" +
                    Aq40Helpers::GetAq40LogUnit(veklor));
        }

        if (botAI->CanCastSpell("searing pain", veklor))
        {
            Aq40Helpers::LogAq40Info(bot, "tank_pickup",
                "twins:warlock:" + Aq40Helpers::GetAq40LogUnit(veklor),
                "boss=twins tank=warlock spell=searing_pain target=" +
                Aq40Helpers::GetAq40LogUnit(veklor));
            return botAI->CastSpell("searing pain", veklor);
        }

        if (splitRisk && moveWarlockFarSide("split_risk"))
            return true;

        if (!botAI->HasAura("curse of doom", veklor) && botAI->CanCastSpell("curse of doom", veklor))
        {
            Aq40Helpers::LogAq40Info(bot, "tank_pickup",
                "twins:warlock_curse:" + Aq40Helpers::GetAq40LogUnit(veklor),
                "boss=twins tank=warlock spell=curse_of_doom target=" +
                Aq40Helpers::GetAq40LogUnit(veklor), 30000);
            return botAI->CastSpell("curse of doom", veklor);
        }

        // On GCD or casting — ensure we're attacking and hold the action slot.
        // Don't fall through to positioning, which would waste the tick on
        // movement when we should be waiting for GCD to finish.
        if (bot->GetVictim() != veklor)
        {
            Aq40Helpers::LogAq40Target(bot, "twins", "warlock_tank", veklor);
            Attack(veklor);
        }

        // Only reposition if too close (Arcane Burst danger).
        if (rangeToVeklor < minRange)
        {
            Position retreatPos;
            if (GetFarSidePosition(bot, veklor, assignment.oppositeEmperor, desiredRange, retreatPos))
            {
                Aq40Helpers::LogAq40Info(bot, "avoid_hazard",
                    "twins:arcane_burst:" + Aq40Helpers::GetAq40LogUnit(veklor),
                    "boss=twins hazard=arcane_burst target=" + Aq40Helpers::GetAq40LogUnit(veklor));
                MoveTo(bot->GetMapId(), retreatPos.GetPositionX(), retreatPos.GetPositionY(),
                       retreatPos.GetPositionZ(), false, false, false, true,
                       MovementPriority::MOVEMENT_COMBAT, true, false);
            }
        }

        return true;  // Hold action slot — cast next tick.
    }

    // Far from Vek'lor or no LOS: move DIRECTLY toward boss.
    // Pre-cast Shadow Ward while approaching.
    if (rangeToVeklor > maxRange || !hasLOS)
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
        MoveNear(veklor, desiredRange, MovementPriority::MOVEMENT_COMBAT);
        return true;
    }

    // In range — soft reposition toward far-side anchor between casts.
    Position anchorPosition;
    if (GetFarSidePosition(bot, veklor, assignment.oppositeEmperor, desiredRange, anchorPosition) &&
        bot->GetExactDist2d(anchorPosition.GetPositionX(), anchorPosition.GetPositionY()) > 6.0f)
    {
        Aq40Helpers::LogAq40Info(bot, "tank_position",
            "twins:warlock_far_side:" + Aq40Helpers::GetAq40LogUnit(veklor),
            "boss=twins tank=warlock reason=far_side target=" + Aq40Helpers::GetAq40LogUnit(veklor));
        MoveTo(bot->GetMapId(), anchorPosition.GetPositionX(), anchorPosition.GetPositionY(),
               anchorPosition.GetPositionZ(), false, false, false, true,
               MovementPriority::MOVEMENT_COMBAT, true, false);
    }

    return true;
}
