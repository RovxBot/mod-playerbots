#include "RaidAq40Actions.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <list>
#include <sstream>
#include <string>

#include "../RaidAq40SpellIds.h"
#include "../Util/RaidAq40Helpers_Shared.h"
#include "../Util/RaidAq40TwinEncounter.h"

namespace
{
float constexpr kTwinExplodeBugDangerRadius = 17.0f;
float constexpr kTwinArcaneBurstDangerRadius = 18.0f;
float constexpr kTwinArcaneBurstLooseRadius = 24.0f;
float constexpr kTwinWarlockMinRange = 19.0f;
float constexpr kTwinWarlockMaxRange = 30.0f;
float constexpr kTwinWarlockPreferredRange = 24.0f;
float constexpr kTwinMeleeContactRange = 5.0f;
float constexpr kTwinAnchorTolerance = 4.0f;
float constexpr kTwinFacingTolerance = 0.15f;
float constexpr kPi = 3.14159265358979323846f;
uint32 constexpr kTwinWarlockThreatLeadMs = 4500;
uint32 constexpr kTwinBlizzardWindowMs = 5000;
uint32 constexpr kTwinExplodeBugWindowMs = 2500;
uint32 constexpr kTwinMutateBugWindowMs = 2500;
uint32 constexpr kTwinArcaneBurstWindowMs = 2500;
float constexpr kTwinSplitWarningDistance = 75.0f;
float constexpr kTwinSplitUrgentDistance = 65.0f;
float constexpr kTwinBossParkWarningError = 8.0f;
float constexpr kTwinBossParkUrgentError = 16.0f;
float constexpr kTwinHealerAnchorNearDistance = 10.0f;
float constexpr kTwinHealerStepRecoveryDistance = 18.0f;
float constexpr kTwinHealerCenterReentryDistance = 12.0f;
float constexpr kTwinRangedBugServiceRange = 32.0f;
float constexpr kTwinRangedGenericBugServiceRange = 22.0f;
float constexpr kTwinHunterMarkedBugServiceRange = 26.0f;
float constexpr kTwinMeleeBugServiceRange = 7.5f;
float constexpr kTwinRangedBugArcaneSafeRadius = kTwinArcaneBurstDangerRadius + 2.0f;
float constexpr kTwinHunterBugArcaneSafeRadius = kTwinArcaneBurstLooseRadius + 4.0f;
float constexpr kTwinMeleeBugArcaneSafeRadius = kTwinArcaneBurstLooseRadius;
float constexpr kTwinExplodeBugServiceSafeRadius = kTwinExplodeBugDangerRadius + 2.0f;

struct TwinAnchorOffsetPattern
{
    float forward = 0.0f;
    float lateral = 0.0f;
};

struct TwinPrePullAnchorChoice
{
    Aq40TwinEncounter::TwinAnchor anchor;
    char const* label = "room_center";
};

TwinPrePullAnchorChoice GetTwinStableAnchorChoice(Aq40TwinEncounter::TwinEncounterState const& state,
                                                  Aq40TwinEncounter::TwinRoleAssignment const& assignment);

struct Direction2d
{
    float x = 0.0f;
    float y = 0.0f;
    float length = 0.0f;
};

enum class TwinTargetIntent : uint8
{
    None = 0,
    Veklor,
    Veknilash,
    HoldReserve,
    HoldVeknilash,
};

enum class TwinBugPriority : uint8
{
    Explode = 0,
    Mutate = 1,
    Hostile = 2,
    None = 255,
};

struct TwinBugSelection
{
    Unit* target = nullptr;
    TwinBugPriority priority = TwinBugPriority::None;
    float serviceDistance = std::numeric_limits<float>::max();
};

float ComputeFacing(Position const& from, Position const& to)
{
    return std::atan2(to.GetPositionY() - from.GetPositionY(), to.GetPositionX() - from.GetPositionX());
}

float GetFacingDelta(float targetFacing, float currentFacing)
{
    float const delta = std::fabs(Position::NormalizeOrientation(targetFacing - currentFacing));
    return delta > kPi ? (2.0f * kPi - delta) : delta;
}

Direction2d GetDirection2d(Position const& from, Position const& to)
{
    Direction2d direction;
    float const dx = to.GetPositionX() - from.GetPositionX();
    float const dy = to.GetPositionY() - from.GetPositionY();
    direction.length = std::sqrt(dx * dx + dy * dy);

    if (direction.length >= 0.01f)
    {
        direction.x = dx / direction.length;
        direction.y = dy / direction.length;
    }

    return direction;
}

Aq40TwinEncounter::TwinAnchor BuildOffsetAnchor(Aq40TwinEncounter::TwinAnchor const& origin, Position const& toward,
                                                float forwardDistance, float lateralDistance,
                                                Position const& facingTarget, float preferredRange = 0.0f)
{
    Direction2d const direction = GetDirection2d(origin.position, toward);
    Position position;

    if (direction.length < 0.01f)
    {
        position.Relocate(origin.position.GetPositionX(), origin.position.GetPositionY(), origin.position.GetPositionZ());
    }
    else
    {
        float const rightX = direction.y;
        float const rightY = -direction.x;
        float const zRatio = std::min(std::fabs(forwardDistance) / direction.length, 1.0f);
        position.Relocate(origin.position.GetPositionX() + direction.x * forwardDistance + rightX * lateralDistance,
            origin.position.GetPositionY() + direction.y * forwardDistance + rightY * lateralDistance,
            origin.position.GetPositionZ() +
                (toward.GetPositionZ() - origin.position.GetPositionZ()) * zRatio);
    }

    Aq40TwinEncounter::TwinAnchor anchor;
    anchor.position = position;
    anchor.preferredRange = preferredRange > 0.0f ? preferredRange : origin.preferredRange;
    anchor.facing = ComputeFacing(position, facingTarget);
    return anchor;
}

GuidVector GetTwinEncounterUnits(PlayerbotAI* botAI)
{
    if (!botAI || !botAI->GetAiObjectContext())
        return GuidVector();

    return Aq40BossHelper::GetEncounterUnits(
        botAI, botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get());
}

Unit* FindTwinUnitByEntry(PlayerbotAI* botAI, GuidVector const& units, uint32 entry)
{
    if (!botAI)
        return nullptr;

    Player* bot = botAI->GetBot();
    if (!bot)
        return nullptr;

    for (ObjectGuid const guid : units)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit || !unit->IsAlive() || !unit->IsInWorld() || unit->IsFriendlyTo(bot) ||
            unit->GetMapId() != bot->GetMapId())
        {
            continue;
        }

        if (unit->GetEntry() == entry)
            return unit;
    }

    return nullptr;
}

Unit* FindTwinBoss(PlayerbotAI* botAI, GuidVector const& units, Aq40TwinEncounter::TwinBoss boss)
{
    return FindTwinUnitByEntry(botAI, units,
        boss == Aq40TwinEncounter::TwinBoss::Veklor ? Aq40SpellIds::TwinVeklorNpcEntry
                                                    : Aq40SpellIds::TwinVeknilashNpcEntry);
}

Unit* FindNearestTwinBug(Player* bot, PlayerbotAI* botAI, GuidVector const& units, float maxDistance)
{
    if (!bot || !botAI)
        return nullptr;

    Unit* nearestBug = nullptr;
    float nearestDistance = std::numeric_limits<float>::max();
    for (ObjectGuid const guid : units)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit || !unit->IsAlive() || !Aq40SpellIds::IsTwinBugEntry(unit->GetEntry()))
            continue;

        float const distance = bot->GetDistance2d(unit);
        if (distance > maxDistance || distance >= nearestDistance)
            continue;

        nearestBug = unit;
        nearestDistance = distance;
    }

    return nearestBug;
}

bool IsTwinEncounterActive(Aq40TwinEncounter::TwinEncounterState const* state)
{
    return state && Aq40TwinEncounter::IsActivePhase(state->phase) &&
           !Aq40TwinEncounter::IsTerminalPhase(state->phase);
}

bool IsTwinWarlockProfile(Player* bot)
{
    return bot && bot->getClass() == CLASS_WARLOCK;
}

bool IsTwinHealerProfile(Player* bot, PlayerbotAI* botAI)
{
    return bot && botAI && botAI->IsHeal(bot);
}

bool IsTwinMeleeProfile(Player* bot, PlayerbotAI* botAI)
{
    if (!bot || !botAI)
        return false;

    if (Aq40BossHelper::IsEncounterTank(bot, bot))
        return true;

    if (bot->getClass() == CLASS_HUNTER)
        return true;

    return !PlayerbotAI::IsRanged(bot) && !botAI->IsHeal(bot);
}

size_t ToSideIndex(Aq40TwinEncounter::TwinSide side)
{
    return side == Aq40TwinEncounter::TwinSide::Side1 ? 1u : 0u;
}

Aq40TwinEncounter::TwinSide GetTwinSideForPosition(float x, float y)
{
    Aq40TwinEncounter::TwinEncounterGeometry const& geometry = Aq40TwinEncounter::GetGeometry();
    float const side0Distance = geometry.bossPark[0].position.GetExactDist2d(x, y);
    float const side1Distance = geometry.bossPark[1].position.GetExactDist2d(x, y);
    return side0Distance <= side1Distance ? Aq40TwinEncounter::TwinSide::Side0
                                          : Aq40TwinEncounter::TwinSide::Side1;
}

GuidVector GetTwinPrePullUnits(PlayerbotAI* botAI)
{
    GuidVector units;
    if (!botAI || !botAI->GetAiObjectContext())
        return units;

    auto* context = botAI->GetAiObjectContext();
    GuidVector const attackers = context->GetValue<GuidVector>("attackers")->Get();
    units.insert(units.end(), attackers.begin(), attackers.end());

    GuidVector const& possibleTargetsNoLos = context->GetValue<GuidVector>("possible targets no los")->Get();
    for (ObjectGuid const guid : possibleTargetsNoLos)
    {
        if (std::find(units.begin(), units.end(), guid) == units.end())
            units.push_back(guid);
    }

    return units;
}

Unit* FindTwinBossForPrePull(PlayerbotAI* botAI, Aq40TwinEncounter::TwinBoss boss)
{
    if (!botAI)
        return nullptr;

    Unit* directTarget = AI_VALUE2(Unit*, "find target",
        boss == Aq40TwinEncounter::TwinBoss::Veklor ? "emperor vek'lor" : "emperor vek'nilash");
    if (directTarget)
        return directTarget;

    return FindTwinBoss(botAI, GetTwinPrePullUnits(botAI), boss);
}

bool IsTwinOpeningWarlockTankAssignment(Aq40TwinEncounter::TwinRoleAssignment const& assignment)
{
    return assignment.cohort == Aq40TwinEncounter::TwinRoleCohort::WarlockTank &&
           assignment.stableSide == Aq40TwinEncounter::GetInitialSideForBoss(Aq40TwinEncounter::TwinBoss::Veklor);
}

bool IsTwinOpeningMeleeTankAssignment(Aq40TwinEncounter::TwinRoleAssignment const& assignment)
{
    return assignment.cohort == Aq40TwinEncounter::TwinRoleCohort::MeleeTank &&
           assignment.stableSide == Aq40TwinEncounter::GetInitialSideForBoss(Aq40TwinEncounter::TwinBoss::Veknilash);
}

bool IsTwinReserveTankHoldAssignment(Aq40TwinEncounter::TwinRoleAssignment const& assignment)
{
    if (assignment.cohort == Aq40TwinEncounter::TwinRoleCohort::WarlockTank)
        return !IsTwinOpeningWarlockTankAssignment(assignment);

    if (assignment.cohort == Aq40TwinEncounter::TwinRoleCohort::MeleeTank)
        return !IsTwinOpeningMeleeTankAssignment(assignment);

    return false;
}

bool ShouldStartTwinOpeningPullFromPrePull(Aq40TwinEncounter::TwinRoleAssignment const& assignment)
{
    return IsTwinOpeningWarlockTankAssignment(assignment) || IsTwinOpeningMeleeTankAssignment(assignment);
}

bool ShouldHoldTwinReserveTankAssignment(Aq40TwinEncounter::TwinRoleAssignment const& assignment)
{
    return IsTwinReserveTankHoldAssignment(assignment);
}

bool IsTwinWarlockTankController(Aq40TwinEncounter::TwinEncounterState const& state,
                                 Aq40TwinEncounter::TwinRoleAssignment const& assignment)
{
    return assignment.cohort == Aq40TwinEncounter::TwinRoleCohort::WarlockTank &&
           Aq40TwinEncounter::IsPrimaryController(
               state, Aq40TwinEncounter::TwinBoss::Veklor, assignment.memberGuid);
}

bool IsTwinMeleeTankController(Aq40TwinEncounter::TwinEncounterState const& state,
                               Aq40TwinEncounter::TwinRoleAssignment const& assignment)
{
    return assignment.cohort == Aq40TwinEncounter::TwinRoleCohort::MeleeTank &&
           Aq40TwinEncounter::IsPrimaryController(
               state, Aq40TwinEncounter::TwinBoss::Veknilash, assignment.memberGuid);
}

bool ShouldHoldTwinReserveTankAssignmentNow(Aq40TwinEncounter::TwinEncounterState const& state,
                                            Aq40TwinEncounter::TwinRoleAssignment const& assignment)
{
    if (!ShouldHoldTwinReserveTankAssignment(assignment))
        return false;

    if (assignment.cohort == Aq40TwinEncounter::TwinRoleCohort::WarlockTank)
        return !IsTwinWarlockTankController(state, assignment);

    if (assignment.cohort == Aq40TwinEncounter::TwinRoleCohort::MeleeTank)
        return !IsTwinMeleeTankController(state, assignment);

    return true;
}

bool IsTwinPrimaryTankController(Aq40TwinEncounter::TwinEncounterState const& state,
                                 Aq40TwinEncounter::TwinRoleAssignment const* assignment)
{
    if (!assignment)
        return false;

    if (assignment->cohort == Aq40TwinEncounter::TwinRoleCohort::WarlockTank)
        return IsTwinWarlockTankController(state, *assignment);

    if (assignment->cohort == Aq40TwinEncounter::TwinRoleCohort::MeleeTank)
        return IsTwinMeleeTankController(state, *assignment);

    return false;
}

bool ShouldSuppressTwinWarlockVeklorThreat(Player* bot,
                                           Aq40TwinEncounter::TwinEncounterState const& state,
                                           Aq40TwinEncounter::TwinRoleAssignment const* assignment)
{
    if (!IsTwinWarlockProfile(bot))
        return false;

    if (assignment && assignment->cohort == Aq40TwinEncounter::TwinRoleCohort::WarlockTank &&
        IsTwinWarlockTankController(state, *assignment))
    {
        return false;
    }

    if (Aq40TwinEncounter::IsSwapPrepActive(state))
        return true;

    return !Aq40TwinEncounter::IsPickupEstablished(state, Aq40TwinEncounter::TwinBoss::Veklor) ||
           Aq40TwinEncounter::IsThreatHoldWindowActive(state, Aq40TwinEncounter::TwinBoss::Veklor);
}

TwinTargetIntent GetTwinTargetIntent(Player* bot, PlayerbotAI* botAI,
                                     Aq40TwinEncounter::TwinEncounterState const& state,
                                     Aq40TwinEncounter::TwinRoleAssignment const* assignment)
{
    if (assignment)
    {
        switch (assignment->cohort)
        {
            case Aq40TwinEncounter::TwinRoleCohort::WarlockTank:
                if (ShouldHoldTwinReserveTankAssignmentNow(state, *assignment))
                    return TwinTargetIntent::HoldReserve;

                if (!IsTwinWarlockTankController(state, *assignment))
                    return TwinTargetIntent::None;

                return TwinTargetIntent::Veklor;

            case Aq40TwinEncounter::TwinRoleCohort::MeleeTank:
                if (ShouldHoldTwinReserveTankAssignmentNow(state, *assignment))
                    return TwinTargetIntent::HoldReserve;

                if (!IsTwinMeleeTankController(state, *assignment))
                    return TwinTargetIntent::None;

                return TwinTargetIntent::Veknilash;

            case Aq40TwinEncounter::TwinRoleCohort::SideHealer:
            case Aq40TwinEncounter::TwinRoleCohort::RaidHealer:
                return TwinTargetIntent::None;

            case Aq40TwinEncounter::TwinRoleCohort::Hunter:
            case Aq40TwinEncounter::TwinRoleCohort::MeleeDps:
                return TwinTargetIntent::Veknilash;

            case Aq40TwinEncounter::TwinRoleCohort::RangedDps:
                if (ShouldSuppressTwinWarlockVeklorThreat(bot, state, assignment))
                    return TwinTargetIntent::HoldVeknilash;

                if (Aq40TwinEncounter::IsThreatHoldWindowActive(state, Aq40TwinEncounter::TwinBoss::Veklor))
                    return TwinTargetIntent::HoldVeknilash;

                return TwinTargetIntent::Veklor;

            case Aq40TwinEncounter::TwinRoleCohort::None:
                break;
        }
    }

    if (ShouldSuppressTwinWarlockVeklorThreat(bot, state, assignment))
        return TwinTargetIntent::HoldVeknilash;

    if (Aq40TwinEncounter::IsThreatHoldWindowActive(state, Aq40TwinEncounter::TwinBoss::Veklor) &&
        !IsTwinWarlockProfile(bot))
    {
        return TwinTargetIntent::HoldVeknilash;
    }

    return IsTwinMeleeProfile(bot, botAI) ? TwinTargetIntent::Veknilash : TwinTargetIntent::Veklor;
}

char const* GetTwinTargetReason(TwinTargetIntent intent)
{
    switch (intent)
    {
        case TwinTargetIntent::Veklor:
            return "veklor";
        case TwinTargetIntent::Veknilash:
            return "veknilash";
        case TwinTargetIntent::HoldReserve:
            return "hold_reserve";
        case TwinTargetIntent::HoldVeknilash:
            return "hold_veknilash";
        case TwinTargetIntent::None:
            return "none";
    }

    return "none";
}

Aq40TwinEncounter::TwinRoleCohort GetTwinEffectiveBugServiceCohort(
    Aq40TwinEncounter::TwinRoleAssignment const* assignment, Player* bot, PlayerbotAI* botAI)
{
    if (assignment)
        return assignment->cohort;

    if (bot && bot->getClass() == CLASS_HUNTER)
        return Aq40TwinEncounter::TwinRoleCohort::Hunter;

    return IsTwinMeleeProfile(bot, botAI) ? Aq40TwinEncounter::TwinRoleCohort::MeleeDps
                                          : Aq40TwinEncounter::TwinRoleCohort::RangedDps;
}

bool IsTwinBugServiceRole(Aq40TwinEncounter::TwinRoleAssignment const* assignment, Player* bot,
                          PlayerbotAI* botAI)
{
    switch (GetTwinEffectiveBugServiceCohort(assignment, bot, botAI))
    {
        case Aq40TwinEncounter::TwinRoleCohort::RangedDps:
        case Aq40TwinEncounter::TwinRoleCohort::Hunter:
        case Aq40TwinEncounter::TwinRoleCohort::MeleeDps:
            return true;

        default:
            return false;
    }
}

bool IsTwinBugServiceWindow(Player* bot, Aq40TwinEncounter::TwinEncounterState const& state)
{
    return state.phase == Aq40TwinEncounter::TwinEncounterPhase::Stable &&
           state.recovery.splitBand == Aq40TwinEncounter::TwinSplitBand::Stable &&
           !Aq40TwinEncounter::IsSwapPrepActive(state) &&
           !Aq40TwinEncounter::IsAnyThreatHoldWindowActive(state) &&
           !Aq40TwinEncounter::HasActiveLockedPickupAnchor(bot);
}

float GetTwinBugServiceRange(Player* bot, PlayerbotAI* botAI,
                             Aq40TwinEncounter::TwinRoleAssignment const* assignment,
                             TwinBugPriority priority)
{
    switch (GetTwinEffectiveBugServiceCohort(assignment, bot, botAI))
    {
        case Aq40TwinEncounter::TwinRoleCohort::RangedDps:
            return priority == TwinBugPriority::Hostile ? kTwinRangedGenericBugServiceRange
                                                        : kTwinRangedBugServiceRange;

        case Aq40TwinEncounter::TwinRoleCohort::Hunter:
            return priority == TwinBugPriority::Hostile ? 0.0f : kTwinHunterMarkedBugServiceRange;

        case Aq40TwinEncounter::TwinRoleCohort::MeleeDps:
            return kTwinMeleeBugServiceRange;

        default:
            return 0.0f;
    }
}

float GetTwinBugArcaneSafeRadius(Player* bot, PlayerbotAI* botAI,
                                 Aq40TwinEncounter::TwinRoleAssignment const* assignment)
{
    switch (GetTwinEffectiveBugServiceCohort(assignment, bot, botAI))
    {
        case Aq40TwinEncounter::TwinRoleCohort::Hunter:
            return kTwinHunterBugArcaneSafeRadius;

        case Aq40TwinEncounter::TwinRoleCohort::MeleeDps:
            return kTwinMeleeBugArcaneSafeRadius;

        case Aq40TwinEncounter::TwinRoleCohort::RangedDps:
            return kTwinRangedBugArcaneSafeRadius;

        default:
            return kTwinArcaneBurstDangerRadius;
    }
}

bool DoesTwinRoleAllowBugPriority(Player* bot, PlayerbotAI* botAI,
                                  Aq40TwinEncounter::TwinRoleAssignment const* assignment,
                                  TwinBugPriority priority)
{
    switch (GetTwinEffectiveBugServiceCohort(assignment, bot, botAI))
    {
        case Aq40TwinEncounter::TwinRoleCohort::RangedDps:
        case Aq40TwinEncounter::TwinRoleCohort::MeleeDps:
            return priority != TwinBugPriority::None;

        case Aq40TwinEncounter::TwinRoleCohort::Hunter:
            return priority == TwinBugPriority::Explode || priority == TwinBugPriority::Mutate;

        default:
            return false;
    }
}

bool IsTwinBugMarkedBySpell(PlayerbotAI* botAI, Unit* bug, uint32 spellId)
{
    if (!botAI || !bug)
        return false;

    Spell* spell = bug->GetCurrentSpell(CURRENT_GENERIC_SPELL);
    return (spell && Aq40SpellIds::MatchesAnySpellId(spell->GetSpellInfo(), { spellId })) ||
           Aq40SpellIds::HasAnyAura(botAI, bug, { spellId });
}

TwinBugPriority GetTwinBugPriority(Player* bot, PlayerbotAI* botAI,
                                   Aq40TwinEncounter::TwinEncounterState const& state, Unit* bug,
                                   bool allowScriptedStickyPriority = false)
{
    if (!bot || !botAI || !bug || !bug->IsAlive() || !Aq40SpellIds::IsTwinBugEntry(bug->GetEntry()))
        return TwinBugPriority::None;

    if (IsTwinBugMarkedBySpell(botAI, bug, Aq40SpellIds::TwinExplodeBug))
        return TwinBugPriority::Explode;

    if (IsTwinBugMarkedBySpell(botAI, bug, Aq40SpellIds::TwinMutateBug))
        return TwinBugPriority::Mutate;

    if (allowScriptedStickyPriority)
    {
        if (Aq40TwinEncounter::IsScriptedEventActive(
                state, Aq40TwinEncounter::TwinScriptedEvent::ExplodeBug, kTwinExplodeBugWindowMs))
        {
            return TwinBugPriority::Explode;
        }

        if (Aq40TwinEncounter::IsScriptedEventActive(
                state, Aq40TwinEncounter::TwinScriptedEvent::MutateBug, kTwinMutateBugWindowMs))
        {
            return TwinBugPriority::Mutate;
        }
    }

    return TwinBugPriority::Hostile;
}

char const* GetTwinBugReason(TwinBugPriority priority)
{
    switch (priority)
    {
        case TwinBugPriority::Explode:
            return "explode_bug";
        case TwinBugPriority::Mutate:
            return "mutate_bug";
        case TwinBugPriority::Hostile:
            return "bug_cleanup";
        case TwinBugPriority::None:
            return "none";
    }

    return "none";
}

Position GetTwinBugServiceOrigin(Player* bot, Aq40TwinEncounter::TwinEncounterState const& state,
                                 Aq40TwinEncounter::TwinRoleAssignment const* assignment)
{
    if (assignment)
        return GetTwinStableAnchorChoice(state, *assignment).anchor.position;

    Position origin;
    if (bot)
    {
        origin.Relocate(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());
    }

    return origin;
}

Aq40TwinEncounter::TwinSide GetTwinBugServiceSide(Player* bot,
                                                  Aq40TwinEncounter::TwinRoleAssignment const* assignment,
                                                  Position const& origin)
{
    if (assignment && Aq40TwinEncounter::IsKnownSide(assignment->stableSide))
        return assignment->stableSide;

    if (!bot)
        return Aq40TwinEncounter::TwinSide::Unknown;

    return GetTwinSideForPosition(origin.GetPositionX(), origin.GetPositionY());
}

bool IsTwinBugSafeForService(Player* bot, PlayerbotAI* botAI,
                             Aq40TwinEncounter::TwinRoleAssignment const* assignment, Unit* veklor,
                             Unit* bug, TwinBugPriority priority, Position const& origin,
                             Aq40TwinEncounter::TwinSide serviceSide)
{
    if (!bot || !botAI || !bug || !bug->IsAlive() || priority == TwinBugPriority::None)
        return false;

    if (!DoesTwinRoleAllowBugPriority(bot, botAI, assignment, priority))
        return false;

    if (!bot->IsWithinLOSInMap(bug))
        return false;

    float const maxServiceDistance = GetTwinBugServiceRange(bot, botAI, assignment, priority);
    if (maxServiceDistance <= 0.0f)
        return false;

    float const serviceDistance = origin.GetExactDist2d(bug->GetPositionX(), bug->GetPositionY());
    if (serviceDistance > maxServiceDistance)
        return false;

    Aq40TwinEncounter::TwinSide const bugSide =
        GetTwinSideForPosition(bug->GetPositionX(), bug->GetPositionY());
    if (Aq40TwinEncounter::IsKnownSide(serviceSide) && bugSide != serviceSide)
        return false;

    if (veklor && bug->GetDistance2d(veklor) < GetTwinBugArcaneSafeRadius(bot, botAI, assignment))
        return false;

    if (priority == TwinBugPriority::Explode && bot->GetDistance2d(bug) < kTwinExplodeBugServiceSafeRadius)
        return false;

    return true;
}

Unit* FindTwinBugServiceTarget(Player* bot, PlayerbotAI* botAI,
                               Aq40TwinEncounter::TwinEncounterState const& state,
                               Aq40TwinEncounter::TwinRoleAssignment const* assignment,
                               GuidVector const& units, Unit* currentTarget, Unit* currentVictim,
                               char const*& outReason)
{
    if (!IsTwinBugServiceWindow(bot, state) || !IsTwinBugServiceRole(assignment, bot, botAI))
        return nullptr;

    Unit* veklor = FindTwinBoss(botAI, units, Aq40TwinEncounter::TwinBoss::Veklor);
    Position const serviceOrigin = GetTwinBugServiceOrigin(bot, state, assignment);
    Aq40TwinEncounter::TwinSide const serviceSide =
        GetTwinBugServiceSide(bot, assignment, serviceOrigin);

    TwinBugSelection best;
    for (ObjectGuid const guid : units)
    {
        Unit* bug = botAI->GetUnit(guid);
        TwinBugPriority const priority = GetTwinBugPriority(bot, botAI, state, bug);
        if (!IsTwinBugSafeForService(
            bot, botAI, assignment, veklor, bug, priority, serviceOrigin, serviceSide))
        {
            continue;
        }

        float const serviceDistance =
            serviceOrigin.GetExactDist2d(bug->GetPositionX(), bug->GetPositionY());
        if (!best.target || priority < best.priority ||
            (priority == best.priority && serviceDistance < best.serviceDistance))
        {
            best.target = bug;
            best.priority = priority;
            best.serviceDistance = serviceDistance;
        }
    }

    Unit* stickyTarget = nullptr;
    if (currentVictim && Aq40SpellIds::IsTwinBugEntry(currentVictim->GetEntry()))
        stickyTarget = currentVictim;
    else if (currentTarget && Aq40SpellIds::IsTwinBugEntry(currentTarget->GetEntry()))
        stickyTarget = currentTarget;

    if (stickyTarget)
    {
        TwinBugPriority const stickyPriority =
            GetTwinBugPriority(bot, botAI, state, stickyTarget, true);
        if (IsTwinBugSafeForService(bot, botAI, assignment, veklor, stickyTarget, stickyPriority,
            serviceOrigin, serviceSide) &&
            (!best.target || stickyPriority < best.priority ||
                (stickyPriority == best.priority &&
                    serviceOrigin.GetExactDist2d(
                        stickyTarget->GetPositionX(), stickyTarget->GetPositionY()) <=
                        best.serviceDistance + 3.0f)))
        {
            best.target = stickyTarget;
            best.priority = stickyPriority;
        }
    }

    if (!best.target)
        return nullptr;

    outReason = GetTwinBugReason(best.priority);
    return best.target;
}

bool SyncTwinHunterPetBugSafety(Player* bot, PlayerbotAI* botAI, Unit* target)
{
    if (!bot || !botAI || bot->getClass() != CLASS_HUNTER)
        return false;

    Pet* pet = bot->GetPet();
    if (!pet)
        return false;

    bool changed = false;
    bool const shouldHoldPet = target && Aq40SpellIds::IsTwinBugEntry(target->GetEntry());
    if (shouldHoldPet)
    {
        bool needsFollow = false;
        if (pet->GetVictim())
        {
            pet->AttackStop();
            changed = true;
            needsFollow = true;
        }

        if (pet->GetReactState() != REACT_PASSIVE)
        {
            pet->SetReactState(REACT_PASSIVE);
            changed = true;
            needsFollow = true;
        }

        if (needsFollow)
            botAI->PetFollow();
    }
    else if (target && target->GetEntry() == Aq40SpellIds::TwinVeknilashNpcEntry &&
             pet->GetReactState() == REACT_PASSIVE)
    {
        pet->SetReactState(REACT_DEFENSIVE);
        changed = true;
    }

    if (changed)
    {
        Aq40Helpers::LogAq40Info(bot, "twin_pet",
            shouldHoldPet ? "twin:hunter_pet:hold_bug" : "twin:hunter_pet:resume_boss",
            std::string("boss=twin target=") + Aq40Helpers::GetAq40LogUnit(target), 1000);
    }

    return changed;
}

Aq40TwinEncounter::TwinSide GetTwinAssignedSide(Aq40TwinEncounter::TwinEncounterState const& state,
                                                ObjectGuid memberGuid)
{
    Aq40TwinEncounter::TwinRoleAssignment const* assignment =
        Aq40TwinEncounter::GetAssignmentForMember(state, memberGuid);
    return assignment ? assignment->stableSide : Aq40TwinEncounter::TwinSide::Unknown;
}

Aq40TwinEncounter::TwinSide GetTwinExpectedOwnerSide(Aq40TwinEncounter::TwinEncounterState const& state,
                                                     Aq40TwinEncounter::TwinBoss boss)
{
    return GetTwinAssignedSide(state, Aq40TwinEncounter::GetOwnership(state, boss).expectedOwner);
}

Aq40TwinEncounter::TwinRoleAssignment const* GetTwinAssignmentForSideAndCohort(
    Aq40TwinEncounter::TwinEncounterState const& state, Aq40TwinEncounter::TwinSide side,
    Aq40TwinEncounter::TwinRoleCohort cohort)
{
    for (Aq40TwinEncounter::TwinRoleAssignment const& assignment : state.assignments)
    {
        if (assignment.stableSide == side && assignment.cohort == cohort)
            return &assignment;
    }

    return nullptr;
}

bool IsTwinHealerDegradedFallback(Aq40TwinEncounter::TwinEncounterState const& state,
                                  Aq40TwinEncounter::TwinRoleAssignment const* assignment)
{
    return assignment && assignment->cohort == Aq40TwinEncounter::TwinRoleCohort::SideHealer &&
           (state.mode == Aq40TwinEncounter::TwinStrategyMode::Degraded ||
            state.phase == Aq40TwinEncounter::TwinEncounterPhase::Degraded);
}

bool IsTwinSideHealerMode(Aq40TwinEncounter::TwinEncounterState const& state,
                          Aq40TwinEncounter::TwinRoleAssignment const* assignment)
{
    return assignment && assignment->cohort == Aq40TwinEncounter::TwinRoleCohort::SideHealer &&
           !IsTwinHealerDegradedFallback(state, assignment);
}

Aq40TwinEncounter::TwinAnchor MakeMidpointAnchor(Aq40TwinEncounter::TwinAnchor const& first,
                                                 Aq40TwinEncounter::TwinAnchor const& second,
                                                 Position const& facingTarget)
{
    Aq40TwinEncounter::TwinAnchor anchor;
    anchor.position.Relocate((first.position.GetPositionX() + second.position.GetPositionX()) * 0.5f,
        (first.position.GetPositionY() + second.position.GetPositionY()) * 0.5f,
        (first.position.GetPositionZ() + second.position.GetPositionZ()) * 0.5f);
    anchor.preferredRange = std::max(first.preferredRange, second.preferredRange);
    anchor.facing = ComputeFacing(anchor.position, facingTarget);
    return anchor;
}

Aq40TwinEncounter::TwinAnchor GetTwinSideHealerLocalTankSupportAnchor(
    Aq40TwinEncounter::TwinEncounterState const& state, Aq40TwinEncounter::TwinSide side)
{
    Aq40TwinEncounter::TwinEncounterGeometry const& geometry = Aq40TwinEncounter::GetGeometry();
    size_t const sideIndex = Aq40TwinEncounter::IsKnownSide(side) ? ToSideIndex(side) : 0u;

    Aq40TwinEncounter::TwinRoleAssignment const* warlockAssignment =
        GetTwinAssignmentForSideAndCohort(state, side, Aq40TwinEncounter::TwinRoleCohort::WarlockTank);
    Aq40TwinEncounter::TwinRoleAssignment const* meleeAssignment =
        GetTwinAssignmentForSideAndCohort(state, side, Aq40TwinEncounter::TwinRoleCohort::MeleeTank);

    if (warlockAssignment && meleeAssignment)
    {
        Aq40TwinEncounter::TwinAnchor const warlockAnchor =
            GetTwinStableAnchorChoice(state, *warlockAssignment).anchor;
        Aq40TwinEncounter::TwinAnchor const meleeAnchor =
            GetTwinStableAnchorChoice(state, *meleeAssignment).anchor;
        return MakeMidpointAnchor(warlockAnchor, meleeAnchor, geometry.bossPark[sideIndex].position);
    }

    if (warlockAssignment)
        return GetTwinStableAnchorChoice(state, *warlockAssignment).anchor;

    if (meleeAssignment)
        return GetTwinStableAnchorChoice(state, *meleeAssignment).anchor;

    return geometry.sidePrep[sideIndex];
}

TwinPrePullAnchorChoice GetTwinSideHealerRecoveryAnchorChoice(
    Aq40TwinEncounter::TwinEncounterState const& state,
    Aq40TwinEncounter::TwinRoleAssignment const& assignment, Player* bot)
{
    Aq40TwinEncounter::TwinEncounterGeometry const& geometry = Aq40TwinEncounter::GetGeometry();
    size_t const sideIndex = Aq40TwinEncounter::IsKnownSide(assignment.stableSide) ? ToSideIndex(assignment.stableSide) : 0u;
    Aq40TwinEncounter::TwinAnchor const& sideAnchor = geometry.sideHealer[sideIndex];
    Aq40TwinEncounter::TwinAnchor const supportAnchor =
        GetTwinSideHealerLocalTankSupportAnchor(state, assignment.stableSide);
    Aq40TwinEncounter::TwinAnchor const steppedAnchor =
        MakeMidpointAnchor(sideAnchor, supportAnchor, geometry.bossPark[sideIndex].position);
    Aq40TwinEncounter::TwinAnchor const reentryAnchor = BuildOffsetAnchor(
        geometry.roomCenter, sideAnchor.position, kTwinHealerCenterReentryDistance, 0.0f,
        geometry.bossPark[sideIndex].position);

    if (!bot)
        return { sideAnchor, "side_healer_anchor" };

    float const distanceToSide =
        bot->GetExactDist2d(sideAnchor.position.GetPositionX(), sideAnchor.position.GetPositionY());
    float const distanceToStepped =
        bot->GetExactDist2d(steppedAnchor.position.GetPositionX(), steppedAnchor.position.GetPositionY());
    float const distanceToReentry =
        bot->GetExactDist2d(reentryAnchor.position.GetPositionX(), reentryAnchor.position.GetPositionY());
    float const distanceToSupport =
        bot->GetExactDist2d(supportAnchor.position.GetPositionX(), supportAnchor.position.GetPositionY());

    if (distanceToSide <= kTwinHealerAnchorNearDistance)
        return { sideAnchor, "side_healer_anchor" };

    if (distanceToStepped <= kTwinHealerStepRecoveryDistance)
        return { steppedAnchor, "stepped_side_anchor" };

    if (distanceToReentry <= distanceToSupport)
        return { reentryAnchor, "center_side_reentry" };

    return { supportAnchor, "local_tank_support" };
}

std::list<ObjectGuid> BuildTwinSideHealerFocusTargets(Aq40TwinEncounter::TwinEncounterState const& state,
                                                      Aq40TwinEncounter::TwinSide side)
{
    std::list<ObjectGuid> focusTargets;
    Aq40TwinEncounter::TwinRoleAssignment const* warlockAssignment =
        GetTwinAssignmentForSideAndCohort(state, side, Aq40TwinEncounter::TwinRoleCohort::WarlockTank);
    Aq40TwinEncounter::TwinRoleAssignment const* meleeAssignment =
        GetTwinAssignmentForSideAndCohort(state, side, Aq40TwinEncounter::TwinRoleCohort::MeleeTank);
    ObjectGuid const warlockGuid = warlockAssignment ? warlockAssignment->memberGuid : ObjectGuid::Empty;
    ObjectGuid const meleeGuid = meleeAssignment ? meleeAssignment->memberGuid : ObjectGuid::Empty;

    auto const pushTarget = [&focusTargets](ObjectGuid guid)
    {
        if (!guid.IsEmpty() && std::find(focusTargets.begin(), focusTargets.end(), guid) == focusTargets.end())
            focusTargets.push_back(guid);
    };

    if (side == GetTwinExpectedOwnerSide(state, Aq40TwinEncounter::TwinBoss::Veklor))
    {
        pushTarget(warlockGuid);
        pushTarget(meleeGuid);
        return focusTargets;
    }

    pushTarget(meleeGuid);
    pushTarget(warlockGuid);
    return focusTargets;
}

bool SyncTwinHealerFocusTargets(Player* bot, PlayerbotAI* botAI,
                                Aq40TwinEncounter::TwinEncounterState const& state,
                                std::list<ObjectGuid> const& desiredTargets, char const* reason)
{
    if (!bot || !botAI || !botAI->GetAiObjectContext())
        return false;

    auto* context = botAI->GetAiObjectContext();
    auto* focusValue = context->GetValue<std::list<ObjectGuid>>("focus heal targets");
    bool changed = false;

    if (focusValue->Get() != desiredTargets)
    {
        focusValue->Set(desiredTargets);
        changed = true;
    }

    bool const shouldEnable = !desiredTargets.empty();
    if (shouldEnable && !botAI->HasStrategy("focus heal targets", BOT_STATE_COMBAT))
    {
        botAI->ChangeStrategy("+focus heal targets", BOT_STATE_COMBAT);
        changed = true;
    }
    else if (!shouldEnable && botAI->HasStrategy("focus heal targets", BOT_STATE_COMBAT))
    {
        botAI->ChangeStrategy("-focus heal targets", BOT_STATE_COMBAT);
        changed = true;
    }

    if (changed)
    {
        std::ostringstream fields;
        fields << "boss=twin phase=" << Aq40TwinEncounter::ToString(state.phase)
               << " mode=" << Aq40TwinEncounter::ToString(state.mode)
               << " reason=" << reason
               << " targets=" << desiredTargets.size();
        Aq40Helpers::LogAq40Info(bot, "twin_strategy",
            shouldEnable ? "twin:side_healer:focus_sync" : "twin:side_healer:focus_clear",
            fields.str(), 1000);
    }

    return changed;
}

bool FaceTwinAnchorIfNeeded(Player* bot, Aq40TwinEncounter::TwinAnchor const& anchor)
{
    if (!bot)
        return false;

    if (GetFacingDelta(anchor.facing, bot->GetOrientation()) <= kTwinFacingTolerance)
        return false;

    bot->SetFacingTo(anchor.facing);
    return true;
}

bool HasTwinBossAggroLead(Player* bot, PlayerbotAI* botAI,
                          Aq40TwinEncounter::TwinEncounterState const& state,
                          Aq40TwinEncounter::TwinBoss boss, Unit* target)
{
    if (!bot || !botAI)
        return false;

    if (target && (target->GetVictim() == bot || botAI->HasAggro(target)))
        return true;

    ObjectGuid const botGuid = bot->GetGUID();
    Aq40TwinEncounter::TwinStableOwnership const& ownership = Aq40TwinEncounter::GetOwnership(state, boss);
    if (ownership.candidateOwner == botGuid || ownership.stableOwner == botGuid)
        return true;

    return Aq40TwinEncounter::GetPickupOwner(state, boss) == botGuid;
}

bool ReleaseTwinPinnedTarget(Player* bot, PlayerbotAI* botAI, Unit* target,
                             Aq40TwinEncounter::TwinEncounterState const& state,
                             char const* eventKey, char const* reason)
{
    if (!bot || !botAI || !target)
        return false;

    if (AI_VALUE(Unit*, "current target") != target && bot->GetTarget() != target->GetGUID() &&
        bot->GetVictim() != target)
    {
        return false;
    }

    Aq40TwinEncounter::RequestImmediateMovementInterrupt(bot);
    bot->AttackStop();
    if (botAI->GetAiObjectContext())
        botAI->GetAiObjectContext()->GetValue<Unit*>("current target")->Set(nullptr);
    bot->SetTarget(ObjectGuid::Empty);
    bot->SetSelection(ObjectGuid());

    Aq40Helpers::LogAq40Info(bot, "twin_strategy", eventKey,
        "boss=twin phase=" + std::string(Aq40TwinEncounter::ToString(state.phase)) +
            " reason=" + reason +
            " target=" + Aq40Helpers::GetAq40LogUnit(target),
        1000);
    return true;
}

bool HoldTwinReserveTankAtAnchor(Player* bot, Aq40TwinEncounter::TwinEncounterState const& state,
                                 Aq40TwinEncounter::TwinRoleAssignment const& assignment)
{
    TwinPrePullAnchorChoice const anchorChoice = GetTwinPrePullAnchorChoice(assignment);
    Aq40TwinEncounter::TwinAnchor const& anchor = anchorChoice.anchor;

    if (bot->GetExactDist2d(anchor.position.GetPositionX(), anchor.position.GetPositionY()) > kTwinAnchorTolerance)
    {
        Aq40TwinEncounter::RequestImmediateMovementInterrupt(bot);
        std::ostringstream fields;
        fields << "boss=twin phase=" << Aq40TwinEncounter::ToString(state.phase)
               << " cohort=" << Aq40TwinEncounter::ToString(assignment.cohort)
               << " side=" << Aq40TwinEncounter::ToString(assignment.stableSide)
               << " slot=" << static_cast<uint32>(assignment.slotIndex)
               << " anchor=" << anchorChoice.label;
        Aq40Helpers::LogAq40Info(bot, "twin_position", "twin:reserve_hold", fields.str(), 1000);
        return MoveInside(bot->GetMapId(), anchor.position.GetPositionX(), anchor.position.GetPositionY(),
            anchor.position.GetPositionZ(), kTwinAnchorTolerance, MovementPriority::MOVEMENT_COMBAT);
    }

    return FaceTwinAnchorIfNeeded(bot, anchor);
}

Unit* ResolveTwinTarget(Player* bot, PlayerbotAI* botAI, Aq40TwinEncounter::TwinEncounterState const& state,
                        GuidVector const& units, char const*& outReason)
{
    if (!bot || !botAI || IsTwinHealerProfile(bot, botAI))
        return nullptr;

    Unit* veklor = FindTwinBoss(botAI, units, Aq40TwinEncounter::TwinBoss::Veklor);
    Unit* veknilash = FindTwinBoss(botAI, units, Aq40TwinEncounter::TwinBoss::Veknilash);
    Unit* currentTarget = botAI->GetAiObjectContext()
        ? botAI->GetAiObjectContext()->GetValue<Unit*>("current target")->Get()
        : nullptr;
    Unit* currentVictim = bot->GetVictim();
    Aq40TwinEncounter::TwinRoleAssignment const* assignment =
        Aq40TwinEncounter::GetAssignmentForMember(state, bot->GetGUID());
    TwinTargetIntent const intent = GetTwinTargetIntent(bot, botAI, state, assignment);
    outReason = GetTwinTargetReason(intent);

    if (intent != TwinTargetIntent::HoldReserve && intent != TwinTargetIntent::None)
    {
        if (Unit* bugTarget = FindTwinBugServiceTarget(
                bot, botAI, state, assignment, units, currentTarget, currentVictim, outReason))
        {
            return bugTarget;
        }
    }

    switch (intent)
    {
        case TwinTargetIntent::HoldVeknilash:
        case TwinTargetIntent::Veknilash:
            return veknilash ? veknilash : veklor;

        case TwinTargetIntent::Veklor:
            return veklor ? veklor : veknilash;

        case TwinTargetIntent::HoldReserve:
        case TwinTargetIntent::None:
            return nullptr;
    }

    return nullptr;
}

Aq40TwinEncounter::TwinAnchor const& GetCenterSpreadAnchor(uint8 slotIndex)
{
    Aq40TwinEncounter::TwinEncounterGeometry const& geometry = Aq40TwinEncounter::GetGeometry();
    return geometry.centerSpread[slotIndex % geometry.centerSpread.size()].anchor;
}

Aq40TwinEncounter::TwinAnchor const& GetCenterSpreadAnchor(Player* bot)
{
    Aq40TwinEncounter::TwinEncounterGeometry const& geometry = Aq40TwinEncounter::GetGeometry();
    size_t const slotIndex = bot ? (bot->GetGUID().GetRawValue() % geometry.centerSpread.size()) : 0u;
    return GetCenterSpreadAnchor(static_cast<uint8>(slotIndex));
}

Aq40TwinEncounter::TwinAnchor const& GetVeknilashSideAnchor(PlayerbotAI* botAI, GuidVector const& units)
{
    Aq40TwinEncounter::TwinEncounterGeometry const& geometry = Aq40TwinEncounter::GetGeometry();
    Unit* veknilash = FindTwinBoss(botAI, units, Aq40TwinEncounter::TwinBoss::Veknilash);
    Aq40TwinEncounter::TwinSide const side =
        veknilash ? GetTwinSideForPosition(veknilash->GetPositionX(), veknilash->GetPositionY())
                  : Aq40TwinEncounter::GetInitialSideForBoss(Aq40TwinEncounter::TwinBoss::Veknilash);
    return geometry.bossPark[ToSideIndex(side)];
}

TwinPrePullAnchorChoice GetTwinHazardRecoveryAnchorChoice(
    Player* bot, PlayerbotAI* botAI, Aq40TwinEncounter::TwinEncounterState const& state,
    Aq40TwinEncounter::TwinRoleAssignment const* assignment, GuidVector const& units)
{
    if (IsTwinSideHealerMode(state, assignment))
        return GetTwinSideHealerRecoveryAnchorChoice(state, *assignment, bot);

    if (assignment)
        return GetTwinStableAnchorChoice(state, *assignment);

    if (IsTwinMeleeProfile(bot, botAI))
        return { GetVeknilashSideAnchor(botAI, units), "veknilash_side" };

    return { GetCenterSpreadAnchor(bot), "center_spread" };
}

bool ClearTwinPrePullIntent(Player* bot, PlayerbotAI* botAI)
{
    if (!bot || !botAI || !botAI->GetAiObjectContext())
        return false;

    auto* context = botAI->GetAiObjectContext();
    bool changed = false;

    if (context->GetValue<Unit*>("old target")->Get())
    {
        context->GetValue<Unit*>("old target")->Set(nullptr);
        changed = true;
    }

    if (context->GetValue<Unit*>("current target")->Get())
    {
        context->GetValue<Unit*>("current target")->Set(nullptr);
        changed = true;
    }

    if (!context->GetValue<GuidVector>("prioritized targets")->Get().empty())
    {
        context->GetValue<GuidVector>("prioritized targets")->Reset();
        changed = true;
    }

    if (!context->GetValue<ObjectGuid>("pull target")->Get().IsEmpty())
    {
        context->GetValue<ObjectGuid>("pull target")->Set(ObjectGuid::Empty);
        changed = true;
    }

    if (!context->GetValue<ObjectGuid>("pull strategy target")->Get().IsEmpty())
    {
        context->GetValue<ObjectGuid>("pull strategy target")->Set(ObjectGuid::Empty);
        changed = true;
    }

    if (bot->GetTarget())
    {
        bot->SetTarget();
        bot->SetSelection(ObjectGuid());
        changed = true;
    }

    std::list<ObjectGuid> const& focusHealTargets = context->GetValue<std::list<ObjectGuid>>("focus heal targets")->Get();
    if (!focusHealTargets.empty())
    {
        context->GetValue<std::list<ObjectGuid>>("focus heal targets")->Set(std::list<ObjectGuid>());
        changed = true;
    }

    if (botAI->HasStrategy("focus heal targets", BOT_STATE_COMBAT))
    {
        botAI->ChangeStrategy("-focus heal targets", BOT_STATE_COMBAT);
        changed = true;
    }

    if (!context->GetValue<std::string>("rti")->Get().empty())
    {
        context->GetValue<std::string>("rti")->Set("");
        changed = true;
    }

    if (!context->GetValue<std::string>("rti cc")->Get().empty())
    {
        context->GetValue<std::string>("rti cc")->Set("");
        changed = true;
    }

    if (context->GetValue<Unit*>("rti target")->Get())
    {
        context->GetValue<Unit*>("rti target")->Set(nullptr);
        changed = true;
    }

    if (context->GetValue<Unit*>("rti cc target")->Get())
    {
        context->GetValue<Unit*>("rti cc target")->Set(nullptr);
        changed = true;
    }

    if (Pet* pet = bot->GetPet())
    {
        bool petNeedsFollow = pet->GetVictim() || pet->IsInCombat();
        if (CharmInfo* charmInfo = pet->GetCharmInfo())
            petNeedsFollow = petNeedsFollow || charmInfo->IsCommandAttack() || !charmInfo->IsReturning();

        if (petNeedsFollow)
        {
            botAI->PetFollow();
            changed = true;
        }
    }

    return changed;
}

Aq40TwinEncounter::TwinAnchor GetTwinMeleeDpsStageAnchor(Aq40TwinEncounter::TwinSide side, uint8 slotIndex)
{
    static std::array<TwinAnchorOffsetPattern, 4> const kPatterns = {
        TwinAnchorOffsetPattern{ 3.5f, -2.5f },
        TwinAnchorOffsetPattern{ 4.5f, 2.5f },
        TwinAnchorOffsetPattern{ 6.0f, -4.5f },
        TwinAnchorOffsetPattern{ 7.5f, 4.5f },
    };

    Aq40TwinEncounter::TwinEncounterGeometry const& geometry = Aq40TwinEncounter::GetGeometry();
    TwinAnchorOffsetPattern const& pattern = kPatterns[slotIndex % kPatterns.size()];
    Aq40TwinEncounter::TwinAnchor const& bossPark = geometry.bossPark[ToSideIndex(side)];
    return BuildOffsetAnchor(bossPark, geometry.roomCenter.position, pattern.forward, pattern.lateral,
        bossPark.position);
}

Aq40TwinEncounter::TwinAnchor GetTwinHunterStageAnchor(Aq40TwinEncounter::TwinSide side, uint8 slotIndex)
{
    static std::array<TwinAnchorOffsetPattern, 4> const kPatterns = {
        TwinAnchorOffsetPattern{ 12.0f, -4.0f },
        TwinAnchorOffsetPattern{ 14.0f, 4.0f },
        TwinAnchorOffsetPattern{ 16.0f, -7.0f },
        TwinAnchorOffsetPattern{ 18.0f, 7.0f },
    };

    Aq40TwinEncounter::TwinEncounterGeometry const& geometry = Aq40TwinEncounter::GetGeometry();
    TwinAnchorOffsetPattern const& pattern = kPatterns[slotIndex % kPatterns.size()];
    Aq40TwinEncounter::TwinAnchor const& bossPark = geometry.bossPark[ToSideIndex(side)];
    return BuildOffsetAnchor(bossPark, geometry.roomCenter.position, pattern.forward, pattern.lateral,
        bossPark.position);
}

Aq40TwinEncounter::TwinAnchor GetTwinRangedStageAnchor(Aq40TwinEncounter::TwinSide side, uint8 slotIndex)
{
    static std::array<TwinAnchorOffsetPattern, 5> const kPatterns = {
        TwinAnchorOffsetPattern{ -2.0f, -4.0f },
        TwinAnchorOffsetPattern{ -1.0f, 4.0f },
        TwinAnchorOffsetPattern{ 2.0f, -8.0f },
        TwinAnchorOffsetPattern{ 3.0f, 8.0f },
        TwinAnchorOffsetPattern{ 5.0f, 0.0f },
    };

    Aq40TwinEncounter::TwinEncounterGeometry const& geometry = Aq40TwinEncounter::GetGeometry();
    TwinAnchorOffsetPattern const& pattern = kPatterns[slotIndex % kPatterns.size()];
    Aq40TwinEncounter::TwinAnchor const& base = geometry.stableVeklorWarlock[ToSideIndex(side)];
    Aq40TwinEncounter::TwinAnchor const& bossPark = geometry.bossPark[ToSideIndex(side)];
    return BuildOffsetAnchor(base, geometry.roomCenter.position, pattern.forward, pattern.lateral,
        bossPark.position, kTwinWarlockPreferredRange);
}

TwinPrePullAnchorChoice GetTwinPrePullAnchorChoice(Aq40TwinEncounter::TwinRoleAssignment const& assignment)
{
    Aq40TwinEncounter::TwinEncounterGeometry const& geometry = Aq40TwinEncounter::GetGeometry();
    size_t const sideIndex = Aq40TwinEncounter::IsKnownSide(assignment.stableSide) ? ToSideIndex(assignment.stableSide) : 0u;

    switch (assignment.cohort)
    {
        case Aq40TwinEncounter::TwinRoleCohort::WarlockTank:
            if (assignment.stableSide == Aq40TwinEncounter::GetInitialSideForBoss(Aq40TwinEncounter::TwinBoss::Veklor))
                return { geometry.sidePrep[sideIndex], "side_prep" };
            return { geometry.reserveWarlockPrep[sideIndex], "reserve_warlock_prep" };

        case Aq40TwinEncounter::TwinRoleCohort::MeleeTank:
            if (assignment.stableSide ==
                Aq40TwinEncounter::GetInitialSideForBoss(Aq40TwinEncounter::TwinBoss::Veknilash))
            {
                return { geometry.sidePrep[sideIndex], "side_prep" };
            }
            return { geometry.reserveMeleeProxy[sideIndex], "reserve_melee_proxy" };

        case Aq40TwinEncounter::TwinRoleCohort::SideHealer:
            return { geometry.sideHealer[sideIndex], "side_healer_anchor" };

        case Aq40TwinEncounter::TwinRoleCohort::RaidHealer:
            return { GetCenterSpreadAnchor(assignment.slotIndex), "center_spread" };

        case Aq40TwinEncounter::TwinRoleCohort::RangedDps:
            return { GetTwinRangedStageAnchor(assignment.stableSide, assignment.slotIndex),
                "stable_veklor_warlock" };

        case Aq40TwinEncounter::TwinRoleCohort::Hunter:
            return { GetTwinHunterStageAnchor(assignment.stableSide, assignment.slotIndex),
                "veknilash_hunter_bias" };

        case Aq40TwinEncounter::TwinRoleCohort::MeleeDps:
            return { GetTwinMeleeDpsStageAnchor(assignment.stableSide, assignment.slotIndex),
                "veknilash_melee_pack" };

        case Aq40TwinEncounter::TwinRoleCohort::None:
            break;
    }

    return { geometry.roomCenter, "room_center" };
}

TwinPrePullAnchorChoice GetTwinStableAnchorChoice(Aq40TwinEncounter::TwinEncounterState const& state,
                                                  Aq40TwinEncounter::TwinRoleAssignment const& assignment)
{
    Aq40TwinEncounter::TwinEncounterGeometry const& geometry = Aq40TwinEncounter::GetGeometry();
    size_t const sideIndex = Aq40TwinEncounter::IsKnownSide(assignment.stableSide) ? ToSideIndex(assignment.stableSide) : 0u;
    bool const swapPrepActive = Aq40TwinEncounter::IsSwapPrepActive(state);

    switch (assignment.cohort)
    {
        case Aq40TwinEncounter::TwinRoleCohort::WarlockTank:
            if (ShouldHoldTwinReserveTankAssignmentNow(state, assignment))
                return { geometry.reserveWarlockPrep[sideIndex], "reserve_warlock_prep" };
            return { geometry.stableVeklorWarlock[sideIndex], "stable_veklor_warlock" };

        case Aq40TwinEncounter::TwinRoleCohort::MeleeTank:
            if (swapPrepActive && IsTwinMeleeTankController(state, assignment) &&
                assignment.stableSide == GetTwinExpectedOwnerSide(state, Aq40TwinEncounter::TwinBoss::Veklor))
            {
                return { geometry.reserveMeleeProxy[sideIndex], "reserve_melee_proxy" };
            }

            if (ShouldHoldTwinReserveTankAssignmentNow(state, assignment))
                return { geometry.reserveMeleeProxy[sideIndex], "reserve_melee_proxy" };
            return { geometry.bossPark[sideIndex], "boss_park" };

        case Aq40TwinEncounter::TwinRoleCohort::SideHealer:
            return { geometry.sideHealer[sideIndex], "side_healer_anchor" };

        case Aq40TwinEncounter::TwinRoleCohort::RaidHealer:
            return { GetCenterSpreadAnchor(assignment.slotIndex), "center_spread" };

        case Aq40TwinEncounter::TwinRoleCohort::RangedDps:
            return { GetTwinRangedStageAnchor(assignment.stableSide, assignment.slotIndex),
                "stable_veklor_warlock" };

        case Aq40TwinEncounter::TwinRoleCohort::Hunter:
            return { GetTwinHunterStageAnchor(assignment.stableSide, assignment.slotIndex),
                "veknilash_hunter_bias" };

        case Aq40TwinEncounter::TwinRoleCohort::MeleeDps:
            return { GetTwinMeleeDpsStageAnchor(assignment.stableSide, assignment.slotIndex),
                "veknilash_melee_pack" };

        case Aq40TwinEncounter::TwinRoleCohort::None:
            break;
    }

    return { geometry.roomCenter, "room_center" };
}

Aq40TwinEncounter::TwinAnchor GetTwinSplitHoldAnchor(Player* bot, PlayerbotAI* botAI,
                                                     Aq40TwinEncounter::TwinEncounterState const& state,
                                                     GuidVector const& units)
{
    if (bot)
    {
        if (Aq40TwinEncounter::TwinRoleAssignment const* assignment =
                Aq40TwinEncounter::GetAssignmentForMember(state, bot->GetGUID()))
        {
            switch (assignment->cohort)
            {
                case Aq40TwinEncounter::TwinRoleCohort::MeleeTank:
                case Aq40TwinEncounter::TwinRoleCohort::Hunter:
                case Aq40TwinEncounter::TwinRoleCohort::MeleeDps:
                    return GetTwinStableAnchorChoice(state, *assignment).anchor;

                default:
                    break;
            }
        }
    }

    return GetVeknilashSideAnchor(botAI, units);
}

Aq40TwinEncounter::TwinSplitBand GetTwinSplitRiskBand(Unit const* veklor, Unit const* veknilash,
                                                      Aq40TwinEncounter::TwinAnchor const& anchor)
{
    if (!veknilash)
        return Aq40TwinEncounter::TwinSplitBand::Stable;

    float const emperorDistance = veklor ? veklor->GetDistance2d(veknilash) : std::numeric_limits<float>::max();
    float const bossParkError =
        veknilash->GetExactDist2d(anchor.position.GetPositionX(), anchor.position.GetPositionY());

    if (emperorDistance <= kTwinSplitUrgentDistance || bossParkError >= kTwinBossParkUrgentError)
        return Aq40TwinEncounter::TwinSplitBand::Urgent;

    if (emperorDistance <= kTwinSplitWarningDistance || bossParkError >= kTwinBossParkWarningError)
        return Aq40TwinEncounter::TwinSplitBand::Warning;

    return Aq40TwinEncounter::TwinSplitBand::Stable;
}

void UpdateTwinSplitBandForMeleeTank(Player* bot, Aq40TwinEncounter::TwinEncounterState& state,
                                     Unit* veklor, Unit* veknilash,
                                     Aq40TwinEncounter::TwinRoleAssignment const& assignment)
{
    if (!bot || assignment.cohort != Aq40TwinEncounter::TwinRoleCohort::MeleeTank ||
        !IsTwinMeleeTankController(state, assignment))
    {
        return;
    }

    if (state.phase != Aq40TwinEncounter::TwinEncounterPhase::DualPullWindow &&
        state.phase != Aq40TwinEncounter::TwinEncounterPhase::Stable)
    {
        return;
    }

    Aq40TwinEncounter::TwinAnchor const anchor = GetTwinStableAnchorChoice(state, assignment).anchor;
    Aq40TwinEncounter::TwinSplitBand const band = GetTwinSplitRiskBand(veklor, veknilash, anchor);
    if (!Aq40TwinEncounter::SetSplitBand(state, band))
        return;

    float const emperorDistance = veklor && veknilash ? veklor->GetDistance2d(veknilash) : 0.0f;
    float const bossParkError =
        veknilash ? veknilash->GetExactDist2d(anchor.position.GetPositionX(), anchor.position.GetPositionY()) : 0.0f;

    std::ostringstream fields;
    fields << "boss=twin phase=" << Aq40TwinEncounter::ToString(state.phase)
           << " side=" << Aq40TwinEncounter::ToString(assignment.stableSide)
           << " controller=" << Aq40Helpers::GetAq40LogUnit(bot)
           << " emperor_distance=" << emperorDistance
           << " veknilash_anchor_error=" << bossParkError
           << " split_band=" << Aq40TwinEncounter::ToString(band);

    if (band == Aq40TwinEncounter::TwinSplitBand::Stable)
    {
        Aq40Helpers::LogAq40Info(bot, "twin_split_band", "twin:split_band:stable", fields.str(), 1000);
        return;
    }

    Aq40Helpers::LogAq40Warn(bot, "twin_split_band",
        std::string("twin:split_band:") + Aq40TwinEncounter::ToString(band), fields.str(), 1000);
}

bool MaintainTwinAssignedAnchor(Player* bot, Aq40TwinEncounter::TwinEncounterState const& state,
                                Aq40TwinEncounter::TwinRoleAssignment const& assignment,
                                Aq40TwinEncounter::TwinAnchor const& anchor,
                                char const* eventKey, char const* anchorLabel)
{
    if (!bot)
        return false;

    if (bot->GetExactDist2d(anchor.position.GetPositionX(), anchor.position.GetPositionY()) >
        kTwinAnchorTolerance)
    {
        Aq40TwinEncounter::RequestImmediateMovementInterrupt(bot);
        std::ostringstream fields;
        fields << "boss=twin phase=" << Aq40TwinEncounter::ToString(state.phase)
               << " cohort=" << Aq40TwinEncounter::ToString(assignment.cohort)
               << " side=" << Aq40TwinEncounter::ToString(assignment.stableSide)
               << " slot=" << static_cast<uint32>(assignment.slotIndex)
               << " anchor=" << anchorLabel;
        Aq40Helpers::LogAq40Info(bot, "twin_position", eventKey, fields.str(), 1000);
        return MoveInside(bot->GetMapId(), anchor.position.GetPositionX(), anchor.position.GetPositionY(),
            anchor.position.GetPositionZ(), kTwinAnchorTolerance, MovementPriority::MOVEMENT_COMBAT);
    }

    return FaceTwinAnchorIfNeeded(bot, anchor);
}

bool MoveTwinToHazardAnchor(Player* bot, PlayerbotAI* /*botAI*/,
                            Aq40TwinEncounter::TwinEncounterState const& state,
                            Aq40TwinEncounter::TwinRoleAssignment const* assignment,
                            TwinPrePullAnchorChoice const& anchorChoice,
                            char const* eventKey, char const* reason)
{
    if (!bot)
        return false;

    if (assignment)
    {
        return MaintainTwinAssignedAnchor(
            bot, state, *assignment, anchorChoice.anchor, eventKey, anchorChoice.label);
    }

    if (bot->GetExactDist2d(anchorChoice.anchor.position.GetPositionX(),
            anchorChoice.anchor.position.GetPositionY()) <= kTwinAnchorTolerance)
    {
        return false;
    }

    Aq40TwinEncounter::RequestImmediateMovementInterrupt(bot);

    std::ostringstream fields;
    fields << "boss=twin phase=" << Aq40TwinEncounter::ToString(state.phase)
           << " reason=" << reason
           << " anchor=" << anchorChoice.label;
    Aq40Helpers::LogAq40Info(bot, "avoid_hazard", eventKey, fields.str(), 1000);

    return MoveInside(bot->GetMapId(), anchorChoice.anchor.position.GetPositionX(),
        anchorChoice.anchor.position.GetPositionY(), anchorChoice.anchor.position.GetPositionZ(),
        kTwinAnchorTolerance, MovementPriority::MOVEMENT_COMBAT);
}

Aura* GetTwinBlizzardAura(Player* bot, PlayerbotAI* botAI)
{
    if (!bot || !botAI)
        return nullptr;

    if (Aura* aura = Aq40SpellIds::GetAnyAura(bot, { Aq40SpellIds::TwinBlizzard }))
        return aura;

    return botAI->GetAura("blizzard", bot);
}

bool SyncTwinWarlockTankOverlay(Player* bot, PlayerbotAI* botAI)
{
    if (!bot || !botAI || bot->getClass() != CLASS_WARLOCK)
        return false;

    bool const shouldUseTankOverlay = Aq40TwinEncounter::ShouldUseTwinWarlockTankStrategy(bot);
    if (!Aq40TwinEncounter::SyncTwinWarlockTankStrategy(bot))
        return false;

    Aq40TwinEncounter::TwinEncounterState const* state = Aq40TwinEncounter::GetEncounterState(bot);
    std::ostringstream fields;
    fields << "boss=twin strategy=tank action=" << (shouldUseTankOverlay ? "enable" : "disable");
    if (state)
    {
        fields << " phase=" << Aq40TwinEncounter::ToString(state->phase)
               << " mode=" << Aq40TwinEncounter::ToString(state->mode);
    }

    Aq40Helpers::LogAq40Info(bot, "twin_strategy",
        shouldUseTankOverlay ? "twin:warlock_tank_overlay:enable" : "twin:warlock_tank_overlay:disable",
        fields.str(), 1000);
    return true;
}
}    // namespace

bool Aq40TwinPrePullStageAction::Execute(Event /*event*/)
{
    Aq40TwinEncounter::TwinEncounterState const* state = Aq40TwinEncounter::GetEncounterState(bot);
    bool const overlayChanged = SyncTwinWarlockTankOverlay(bot, botAI);
    if (!state || state->mode != Aq40TwinEncounter::TwinStrategyMode::StandardCompReady ||
        state->phase != Aq40TwinEncounter::TwinEncounterPhase::PrePull || state->assignments.empty())
    {
        return overlayChanged;
    }

    Aq40TwinEncounter::TwinRoleAssignment const* assignment = Aq40TwinEncounter::GetAssignmentForMember(*state, bot->GetGUID());
    if (!assignment)
        return overlayChanged;

    bool const clearedIntent = ClearTwinPrePullIntent(bot, botAI);
    TwinPrePullAnchorChoice const anchorChoice = GetTwinPrePullAnchorChoice(*assignment);
    Aq40TwinEncounter::TwinAnchor const& anchor = anchorChoice.anchor;

    float const distance = bot->GetExactDist2d(anchor.position.GetPositionX(), anchor.position.GetPositionY());
    if (distance > kTwinAnchorTolerance)
    {
        Aq40TwinEncounter::RequestImmediateMovementInterrupt(bot);
        std::ostringstream fields;
        fields << "boss=twin phase=prepull cohort=" << Aq40TwinEncounter::ToString(assignment->cohort)
               << " side=" << Aq40TwinEncounter::ToString(assignment->stableSide)
               << " slot=" << static_cast<uint32>(assignment->slotIndex)
               << " anchor=" << anchorChoice.label;
        Aq40Helpers::LogAq40Info(bot, "twin_position", "twin:prepull:stage", fields.str(), 1000);
        return MoveInside(bot->GetMapId(), anchor.position.GetPositionX(), anchor.position.GetPositionY(),
            anchor.position.GetPositionZ(), kTwinAnchorTolerance, MovementPriority::MOVEMENT_NORMAL);
    }

    if (GetFacingDelta(anchor.facing, bot->GetOrientation()) > kTwinFacingTolerance)
    {
        bot->SetFacingTo(anchor.facing);
        return true;
    }

    if (ShouldStartTwinOpeningPullFromPrePull(*assignment) && !bot->IsInCombat())
    {
        Aq40TwinEncounter::TwinBoss const openerBoss =
            IsTwinOpeningWarlockTankAssignment(*assignment) ? Aq40TwinEncounter::TwinBoss::Veklor
                                                            : Aq40TwinEncounter::TwinBoss::Veknilash;
        Unit* target = FindTwinBossForPrePull(botAI, openerBoss);
        if (target)
        {
            Aq40Helpers::LogAq40Target(bot, "twin", "prepull_dual_pull", target, 1000);
            return Attack(target);
        }
    }

    return overlayChanged || clearedIntent;
}

bool Aq40TwinDualPullEngageAction::Execute(Event /*event*/)
{
    Aq40TwinEncounter::TwinEncounterState* state = Aq40TwinEncounter::GetEncounterState(bot);
    bool const overlayChanged = SyncTwinWarlockTankOverlay(bot, botAI);
    if (!state || state->phase != Aq40TwinEncounter::TwinEncounterPhase::DualPullWindow ||
        IsTwinHealerProfile(bot, botAI))
    {
        return overlayChanged;
    }

    Aq40TwinEncounter::TwinRoleAssignment const* assignment = Aq40TwinEncounter::GetAssignmentForMember(*state, bot->GetGUID());
    if (assignment && ShouldHoldTwinReserveTankAssignmentNow(*state, *assignment))
        return HoldTwinReserveTankAtAnchor(bot, *state, *assignment) || overlayChanged;

    GuidVector const encounterUnits = GetTwinEncounterUnits(botAI);
    Unit* veklor = FindTwinBoss(botAI, encounterUnits, Aq40TwinEncounter::TwinBoss::Veklor);
    Unit* veknilash = FindTwinBoss(botAI, encounterUnits, Aq40TwinEncounter::TwinBoss::Veknilash);
    if (assignment)
        UpdateTwinSplitBandForMeleeTank(bot, *state, veklor, veknilash, *assignment);

    char const* reason = "dual_pull";
    Unit* target = ResolveTwinTarget(bot, botAI, *state, encounterUnits, reason);
    if (!target)
        return overlayChanged;

    if (assignment && IsTwinOpeningWarlockTankAssignment(*assignment) &&
        target->GetEntry() == Aq40SpellIds::TwinVeklorNpcEntry)
    {
        Aq40TwinEncounter::TwinEncounterGeometry const& geometry = Aq40TwinEncounter::GetGeometry();
        size_t const sideIndex = ToSideIndex(assignment->stableSide);
        Aq40TwinEncounter::TwinAnchor const& holdAnchor = geometry.sidePrep[sideIndex];
        Aq40TwinEncounter::TwinAnchor const& settleAnchor = geometry.stableVeklorWarlock[sideIndex];
        bool const hasThreatLead = HasTwinBossAggroLead(
            bot, botAI, *state, Aq40TwinEncounter::TwinBoss::Veklor, target);
        bool const shouldRotateInward = hasThreatLead &&
            (Aq40TwinEncounter::GetPhaseElapsedMs(*state) >= kTwinWarlockThreatLeadMs ||
             bot->GetDistance2d(target) < kTwinWarlockMinRange);

        if (AI_VALUE(Unit*, "current target") != target || bot->GetTarget() != target->GetGUID())
        {
            Aq40Helpers::LogAq40Target(bot, "twin", "dual_pull_veklor", target, 1000);
            return Attack(target);
        }

        Aq40TwinEncounter::TwinAnchor const& anchor = shouldRotateInward ? settleAnchor : holdAnchor;
        if (bot->GetExactDist2d(anchor.position.GetPositionX(), anchor.position.GetPositionY()) > kTwinAnchorTolerance)
        {
            Aq40TwinEncounter::RequestImmediateMovementInterrupt(bot);
            Aq40Helpers::LogAq40Info(bot, "twin_position",
                shouldRotateInward ? "twin:dual_pull:veklor_rotate_inward" : "twin:dual_pull:veklor_hold_cast",
                "boss=twin phase=dual_pull_window side=" + std::string(Aq40TwinEncounter::ToString(assignment->stableSide)) +
                    " anchor=" + (shouldRotateInward ? "stable_veklor_warlock" : "side_prep"),
                1000);
            return MoveInside(bot->GetMapId(), anchor.position.GetPositionX(), anchor.position.GetPositionY(),
                anchor.position.GetPositionZ(), kTwinAnchorTolerance, MovementPriority::MOVEMENT_COMBAT);
        }

        if (FaceTwinAnchorIfNeeded(bot, anchor))
            return true;

        float const distance = bot->GetDistance2d(target);
        if (!shouldRotateInward && distance > kTwinWarlockMaxRange + 2.0f)
        {
            Aq40TwinEncounter::RequestImmediateMovementInterrupt(bot);
            Aq40Helpers::LogAq40Info(bot, "twin_position",
                "twin:dual_pull:veklor_stepin",
                "boss=twin phase=dual_pull_window reason=searing_pain_pull_range",
                1000);
            return MoveNear(target, kTwinWarlockPreferredRange, MovementPriority::MOVEMENT_COMBAT);
        }

        if (shouldRotateInward && distance < kTwinWarlockMinRange)
        {
            Aq40TwinEncounter::RequestImmediateMovementInterrupt(bot);
            Aq40Helpers::LogAq40Info(bot, "twin_position",
                "twin:dual_pull:warlock_backstep",
                "boss=twin phase=dual_pull_window reason=stable_veklor_warlock target=" +
                    Aq40Helpers::GetAq40LogUnit(target),
                1000);
            return MoveAway(target, kTwinWarlockPreferredRange - distance);
        }

        if (shouldRotateInward && distance > kTwinWarlockMaxRange)
        {
            Aq40TwinEncounter::RequestImmediateMovementInterrupt(bot);
            Aq40Helpers::LogAq40Info(bot, "twin_position",
                "twin:dual_pull:warlock_stepin",
                "boss=twin phase=dual_pull_window reason=stable_veklor_warlock target=" +
                    Aq40Helpers::GetAq40LogUnit(target),
                1000);
            return MoveNear(target, kTwinWarlockPreferredRange, MovementPriority::MOVEMENT_COMBAT);
        }

        return overlayChanged;
    }

    if (assignment && IsTwinOpeningMeleeTankAssignment(*assignment) &&
        target->GetEntry() == Aq40SpellIds::TwinVeknilashNpcEntry)
    {
        Aq40TwinEncounter::TwinAnchor const& bossPark =
            Aq40TwinEncounter::GetGeometry().bossPark[ToSideIndex(assignment->stableSide)];
        if (AI_VALUE(Unit*, "current target") != target || bot->GetTarget() != target->GetGUID())
        {
            Aq40Helpers::LogAq40Target(bot, "twin", "dual_pull_veknilash", target, 1000);
            return Attack(target);
        }

        if (HasTwinBossAggroLead(bot, botAI, *state, Aq40TwinEncounter::TwinBoss::Veknilash, target))
        {
            if (bot->GetExactDist2d(bossPark.position.GetPositionX(), bossPark.position.GetPositionY()) >
                kTwinAnchorTolerance)
            {
                Aq40TwinEncounter::RequestImmediateMovementInterrupt(bot);
                Aq40Helpers::LogAq40Info(bot, "twin_position",
                    "twin:dual_pull:veknilash_park",
                    "boss=twin phase=dual_pull_window side=" +
                        std::string(Aq40TwinEncounter::ToString(assignment->stableSide)) +
                        " anchor=boss_park",
                    1000);
                return MoveInside(bot->GetMapId(), bossPark.position.GetPositionX(), bossPark.position.GetPositionY(),
                    bossPark.position.GetPositionZ(), kTwinAnchorTolerance, MovementPriority::MOVEMENT_COMBAT);
            }

            if (FaceTwinAnchorIfNeeded(bot, bossPark))
                return true;

            return overlayChanged;
        }

        if (bot->GetDistance2d(target) > 8.0f)
        {
            Aq40TwinEncounter::RequestImmediateMovementInterrupt(bot);
            Aq40Helpers::LogAq40Info(bot, "twin_position",
                "twin:dual_pull:melee_stepin",
                "boss=twin phase=dual_pull_window reason=veknilash_pickup target=" +
                    Aq40Helpers::GetAq40LogUnit(target),
                1000);
            return MoveNear(target, kTwinMeleeContactRange, MovementPriority::MOVEMENT_COMBAT);
        }

        return overlayChanged;
    }

    if (target->GetEntry() == Aq40SpellIds::TwinVeknilashNpcEntry && IsTwinMeleeProfile(bot, botAI) &&
        bot->GetDistance2d(target) > 8.0f)
    {
        Aq40TwinEncounter::RequestImmediateMovementInterrupt(bot);
        Aq40Helpers::LogAq40Info(bot, "twin_position",
            "twin:dual_pull:melee_stepin",
            "boss=twin phase=dual_pull_window reason=melee_engage target=" + Aq40Helpers::GetAq40LogUnit(target),
            1000);
        return MoveNear(target, kTwinMeleeContactRange, MovementPriority::MOVEMENT_COMBAT);
    }

    if (AI_VALUE(Unit*, "current target") == target && bot->GetVictim() == target)
        return overlayChanged;

    Aq40Helpers::LogAq40Target(bot, "twin", reason, target, 1000);
    return Attack(target);
}

bool Aq40TwinSwapPrepStageAction::Execute(Event /*event*/)
{
    Aq40TwinEncounter::TwinEncounterState* state = Aq40TwinEncounter::GetEncounterState(bot);
    bool const overlayChanged = SyncTwinWarlockTankOverlay(bot, botAI);
    if (!state || !IsTwinEncounterActive(state) || !Aq40TwinEncounter::IsSwapPrepActive(*state))
        return overlayChanged;

    uint32 const nowMs = getMSTime();
    bool const armedSwapPrep = Aq40TwinEncounter::ArmSwapPrep(*state, nowMs);
    if (armedSwapPrep)
    {
        std::ostringstream fields;
        fields << "boss=twin phase=" << Aq40TwinEncounter::ToString(state->phase)
               << " next_teleport_earliest_ms="
               << (state->nextTeleportEarliestAtMs > nowMs ? state->nextTeleportEarliestAtMs - nowMs : 0u)
               << " next_teleport_latest_ms="
               << (state->nextTeleportLatestAtMs > nowMs ? state->nextTeleportLatestAtMs - nowMs : 0u)
               << " veklor_expected_side="
               << Aq40TwinEncounter::ToString(GetTwinExpectedOwnerSide(*state, Aq40TwinEncounter::TwinBoss::Veklor))
               << " veknilash_expected_side="
               << Aq40TwinEncounter::ToString(GetTwinExpectedOwnerSide(*state, Aq40TwinEncounter::TwinBoss::Veknilash));
        Aq40Helpers::LogAq40Info(bot, "twin_swap_prep", "twin:swap_prep:arm", fields.str(), 1000);
    }

    Aq40TwinEncounter::TwinRoleAssignment const* assignment =
        Aq40TwinEncounter::GetAssignmentForMember(*state, bot->GetGUID());
    if (!assignment || IsTwinHealerProfile(bot, botAI))
        return armedSwapPrep || overlayChanged;

    if (assignment->cohort == Aq40TwinEncounter::TwinRoleCohort::WarlockTank)
    {
        GuidVector const encounterUnits = GetTwinEncounterUnits(botAI);
        if (Unit* veklor = FindTwinBoss(botAI, encounterUnits, Aq40TwinEncounter::TwinBoss::Veklor))
        {
            if (ShouldSuppressTwinWarlockVeklorThreat(bot, *state, assignment) &&
                ReleaseTwinPinnedTarget(
                    bot, botAI, veklor, *state, "twin:swap_prep:release_veklor", "swap_prep_hold"))
            {
                return true;
            }
        }
    }

    TwinPrePullAnchorChoice const anchorChoice = GetTwinStableAnchorChoice(*state, *assignment);
    char const* eventKey = "twin:swap_prep:hold";
    switch (assignment->cohort)
    {
        case Aq40TwinEncounter::TwinRoleCohort::WarlockTank:
            eventKey = "twin:swap_prep:warlock_hold";
            break;
        case Aq40TwinEncounter::TwinRoleCohort::MeleeTank:
            eventKey = "twin:swap_prep:melee_proxy";
            break;
        case Aq40TwinEncounter::TwinRoleCohort::Hunter:
        case Aq40TwinEncounter::TwinRoleCohort::RangedDps:
            eventKey = "twin:swap_prep:ranged_hold";
            break;
        case Aq40TwinEncounter::TwinRoleCohort::MeleeDps:
            eventKey = "twin:swap_prep:melee_stage";
            break;
        default:
            break;
    }

    return MaintainTwinAssignedAnchor(
               bot, *state, *assignment, anchorChoice.anchor, eventKey, anchorChoice.label) ||
           armedSwapPrep || overlayChanged;
}

bool Aq40TwinHealerSupportAction::Execute(Event /*event*/)
{
    Aq40TwinEncounter::TwinEncounterState const* state = Aq40TwinEncounter::GetEncounterState(bot);
    if (!IsTwinEncounterActive(state) || !IsTwinHealerProfile(bot, botAI))
    {
        return false;
    }

    Aq40TwinEncounter::TwinRoleAssignment const* assignment =
        Aq40TwinEncounter::GetAssignmentForMember(*state, bot->GetGUID());
    if (IsTwinSideHealerMode(*state, assignment))
    {
        bool const focusChanged = SyncTwinHealerFocusTargets(
            bot, botAI, *state, BuildTwinSideHealerFocusTargets(*state, assignment->stableSide),
            Aq40TwinEncounter::IsSwapPrepActive(*state) ? "swap_prep_preload" : "side_tank_package");
        TwinPrePullAnchorChoice const anchorChoice =
            GetTwinSideHealerRecoveryAnchorChoice(*state, *assignment, bot);
        return MaintainTwinAssignedAnchor(bot, *state, *assignment, anchorChoice.anchor,
                   "twin:side_healer:hold", anchorChoice.label) ||
               focusChanged;
    }

    bool const focusChanged = SyncTwinHealerFocusTargets(bot, botAI, *state, std::list<ObjectGuid>(),
        IsTwinHealerDegradedFallback(*state, assignment) ? "degraded_fallback" : "raid_healer_clear");
    if (assignment)
    {
        if (assignment->cohort == Aq40TwinEncounter::TwinRoleCohort::RaidHealer)
        {
            TwinPrePullAnchorChoice const anchorChoice = GetTwinStableAnchorChoice(*state, *assignment);
            return MaintainTwinAssignedAnchor(
                       bot, *state, *assignment, anchorChoice.anchor, "twin:raid_healer:hold",
                       anchorChoice.label) ||
                   focusChanged;
        }

        if (IsTwinHealerDegradedFallback(*state, assignment))
        {
            return MaintainTwinAssignedAnchor(bot, *state, *assignment,
                       GetCenterSpreadAnchor(assignment->slotIndex),
                       "twin:side_healer:degraded_fallback", "center_spread") ||
                   focusChanged;
        }
    }

    if (!state->assignments.empty())
        return focusChanged;

    Aq40TwinEncounter::TwinAnchor const& anchor = GetCenterSpreadAnchor(bot);
    if (bot->GetExactDist2d(anchor.position.GetPositionX(), anchor.position.GetPositionY()) <= kTwinAnchorTolerance)
        return focusChanged;

    Aq40TwinEncounter::RequestImmediateMovementInterrupt(bot);
    Aq40Helpers::LogAq40Info(bot, "spread_position",
        "twin:healer_support",
        "boss=twin phase=" + std::string(Aq40TwinEncounter::ToString(state->phase)) +
            " reason=center_spread",
        1000);
    return MoveInside(bot->GetMapId(), anchor.position.GetPositionX(), anchor.position.GetPositionY(),
        anchor.position.GetPositionZ(), kTwinAnchorTolerance, MovementPriority::MOVEMENT_COMBAT) ||
           focusChanged;
}

bool Aq40TwinChooseTargetAction::Execute(Event /*event*/)
{
    Aq40TwinEncounter::TwinEncounterState* state = Aq40TwinEncounter::GetEncounterState(bot);
    if (!IsTwinEncounterActive(state) || IsTwinHealerProfile(bot, botAI))
        return false;

    Aq40TwinEncounter::TwinRoleAssignment const* assignment =
        Aq40TwinEncounter::GetAssignmentForMember(*state, bot->GetGUID());
    if (assignment && ShouldHoldTwinReserveTankAssignmentNow(*state, *assignment))
        return HoldTwinReserveTankAtAnchor(bot, *state, *assignment);

    GuidVector const encounterUnits = GetTwinEncounterUnits(botAI);
    Unit* veklor = FindTwinBoss(botAI, encounterUnits, Aq40TwinEncounter::TwinBoss::Veklor);
    Unit* veknilash = FindTwinBoss(botAI, encounterUnits, Aq40TwinEncounter::TwinBoss::Veknilash);
    if (assignment && assignment->cohort == Aq40TwinEncounter::TwinRoleCohort::MeleeTank &&
        !IsTwinMeleeTankController(*state, *assignment))
    {
        return veknilash ? ReleaseTwinPinnedTarget(bot, botAI, veknilash, *state,
                               "twin:melee_tank:release_veknilash", "ownership_gate")
                         : false;
    }

    if (assignment)
        UpdateTwinSplitBandForMeleeTank(bot, *state, veklor, veknilash, *assignment);

    char const* reason = "boss";
    Unit* target = ResolveTwinTarget(bot, botAI, *state, encounterUnits, reason);
    if (!target)
        return false;

    bool const petChanged = SyncTwinHunterPetBugSafety(bot, botAI, target);

    if (AI_VALUE(Unit*, "current target") != target || bot->GetTarget() != target->GetGUID())
    {
        Aq40Helpers::LogAq40Target(bot, "twin", reason, target, 1000);
        return Attack(target) || petChanged;
    }

    if (state->phase == Aq40TwinEncounter::TwinEncounterPhase::Stable && assignment &&
        !Aq40TwinEncounter::HasActiveLockedPickupAnchor(bot))
    {
        switch (assignment->cohort)
        {
            case Aq40TwinEncounter::TwinRoleCohort::MeleeTank:
                if (target->GetEntry() == Aq40SpellIds::TwinVeknilashNpcEntry)
                {
                    if (HasTwinBossAggroLead(bot, botAI, *state, Aq40TwinEncounter::TwinBoss::Veknilash, target))
                    {
                        TwinPrePullAnchorChoice const anchorChoice = GetTwinStableAnchorChoice(*state, *assignment);
                        if (MaintainTwinAssignedAnchor(bot, *state, *assignment, anchorChoice.anchor,
                                "twin:stable:veknilash_park", anchorChoice.label))
                        {
                            return true;
                        }
                    }
                    else if (bot->GetDistance2d(target) > 8.0f)
                    {
                        Aq40TwinEncounter::RequestImmediateMovementInterrupt(bot);
                        Aq40Helpers::LogAq40Info(bot, "twin_position",
                            "twin:stable:melee_stepin",
                            "boss=twin phase=stable reason=veknilash_hold target=" +
                                Aq40Helpers::GetAq40LogUnit(target),
                            1000);
                        return MoveNear(target, kTwinMeleeContactRange, MovementPriority::MOVEMENT_COMBAT);
                    }
                }
                break;

            case Aq40TwinEncounter::TwinRoleCohort::RangedDps:
            case Aq40TwinEncounter::TwinRoleCohort::Hunter:
            {
                TwinPrePullAnchorChoice const anchorChoice = GetTwinStableAnchorChoice(*state, *assignment);
                char const* eventKey = assignment->cohort == Aq40TwinEncounter::TwinRoleCohort::RangedDps
                                           ? "twin:stable:ranged_hold"
                                           : "twin:stable:hunter_hold";
                if (MaintainTwinAssignedAnchor(bot, *state, *assignment, anchorChoice.anchor,
                        eventKey, anchorChoice.label))
                {
                    return true;
                }
                break;
            }

            default:
                break;
        }
    }

    if (bot->GetVictim() == target)
        return petChanged;

    Aq40Helpers::LogAq40Target(bot, "twin", reason, target, 1000);
    return Attack(target) || petChanged;
}

bool Aq40TwinHoldSplitAction::Execute(Event /*event*/)
{
    Aq40TwinEncounter::TwinEncounterState const* state = Aq40TwinEncounter::GetEncounterState(bot);
    if (!state || Aq40TwinEncounter::IsTerminalPhase(state->phase))
        return false;

    Aq40TwinEncounter::TwinRoleAssignment const* assignment =
        Aq40TwinEncounter::GetAssignmentForMember(*state, bot->GetGUID());

    if (Aq40TwinEncounter::TwinLockedPickupAnchor const* lockedAnchor = Aq40TwinEncounter::GetLockedPickupAnchor(bot))
    {
        if (Aq40TwinEncounter::IsLockedPickupAnchorExpired(*lockedAnchor) ||
            bot->GetExactDist2d(lockedAnchor->anchor.position.GetPositionX(),
                lockedAnchor->anchor.position.GetPositionY()) <= kTwinAnchorTolerance)
        {
            return false;
        }

        Aq40TwinEncounter::RequestImmediateMovementInterrupt(bot);
        Aq40Helpers::LogAq40Info(bot, "twin_position",
            "twin:hold_split:locked_anchor",
            "boss=twin phase=" + std::string(Aq40TwinEncounter::ToString(state->phase)) +
                " reason=locked_pickup_anchor",
            1000);
        return MoveInside(bot->GetMapId(), lockedAnchor->anchor.position.GetPositionX(),
            lockedAnchor->anchor.position.GetPositionY(), lockedAnchor->anchor.position.GetPositionZ(),
            kTwinAnchorTolerance, MovementPriority::MOVEMENT_COMBAT);
    }

    if (state->recovery.splitBand != Aq40TwinEncounter::TwinSplitBand::Warning &&
        state->recovery.splitBand != Aq40TwinEncounter::TwinSplitBand::Urgent)
    {
        return false;
    }

    GuidVector const encounterUnits = GetTwinEncounterUnits(botAI);
    Aq40TwinEncounter::TwinAnchor safeAnchor;
    if (IsTwinSideHealerMode(*state, assignment))
        safeAnchor = GetTwinSideHealerRecoveryAnchorChoice(*state, *assignment, bot).anchor;
    else if (IsTwinMeleeProfile(bot, botAI))
        safeAnchor = GetTwinSplitHoldAnchor(bot, botAI, *state, encounterUnits);
    else
        safeAnchor = GetCenterSpreadAnchor(bot);

    if (bot->GetExactDist2d(safeAnchor.position.GetPositionX(), safeAnchor.position.GetPositionY()) <=
        kTwinAnchorTolerance)
    {
        return false;
    }

    Aq40TwinEncounter::RequestImmediateMovementInterrupt(bot);
    Aq40Helpers::LogAq40Info(bot, "twin_position",
        "twin:hold_split:safe_anchor",
        "boss=twin phase=" + std::string(Aq40TwinEncounter::ToString(state->phase)) +
            " reason=split_risk_reanchor",
        1000);
    return MoveInside(bot->GetMapId(), safeAnchor.position.GetPositionX(), safeAnchor.position.GetPositionY(),
        safeAnchor.position.GetPositionZ(), kTwinAnchorTolerance, MovementPriority::MOVEMENT_COMBAT);
}

bool Aq40TwinWarlockTankAction::Execute(Event /*event*/)
{
    Aq40TwinEncounter::TwinEncounterState const* state = Aq40TwinEncounter::GetEncounterState(bot);
    bool const overlayChanged = SyncTwinWarlockTankOverlay(bot, botAI);
    if (!IsTwinEncounterActive(state) || !Aq40TwinEncounter::ShouldUseTwinWarlockTankStrategy(bot) ||
        IsTwinHealerProfile(bot, botAI))
    {
        return overlayChanged;
    }

    Aq40TwinEncounter::TwinRoleAssignment const* assignment = Aq40TwinEncounter::GetAssignmentForMember(*state, bot->GetGUID());
    if (assignment && ShouldHoldTwinReserveTankAssignmentNow(*state, *assignment))
    {
        return HoldTwinReserveTankAtAnchor(bot, *state, *assignment) || overlayChanged;
    }

    GuidVector const encounterUnits = GetTwinEncounterUnits(botAI);
    Unit* veklor = FindTwinBoss(botAI, encounterUnits, Aq40TwinEncounter::TwinBoss::Veklor);
    if (!veklor)
        return overlayChanged;

    if (assignment && !IsTwinWarlockTankController(*state, *assignment))
    {
        return ReleaseTwinPinnedTarget(bot, botAI, veklor, *state,
                   "twin:warlock_tank:release_veklor", "ownership_gate") ||
               overlayChanged;
    }

    if (AI_VALUE(Unit*, "current target") != veklor || bot->GetTarget() != veklor->GetGUID())
    {
        Aq40Helpers::LogAq40Target(bot, "twin", "warlock_pin", veklor, 1000);
        return Attack(veklor);
    }

    if (state->phase == Aq40TwinEncounter::TwinEncounterPhase::Stable && assignment &&
        !Aq40TwinEncounter::HasActiveLockedPickupAnchor(bot) &&
        HasTwinBossAggroLead(bot, botAI, *state, Aq40TwinEncounter::TwinBoss::Veklor, veklor))
    {
        TwinPrePullAnchorChoice const anchorChoice = GetTwinStableAnchorChoice(*state, *assignment);
        if (MaintainTwinAssignedAnchor(bot, *state, *assignment, anchorChoice.anchor,
                "twin:stable:veklor_hold", anchorChoice.label))
        {
            return true;
        }
    }

    float const distance = bot->GetDistance2d(veklor);
    if (distance < kTwinWarlockMinRange)
    {
        Aq40TwinEncounter::RequestImmediateMovementInterrupt(bot);
        Aq40Helpers::LogAq40Info(bot, "twin_position",
            "twin:warlock_tank:backstep",
            "boss=twin phase=" + std::string(Aq40TwinEncounter::ToString(state->phase)) +
                " reason=veklor_range_hold",
            1000);
        return MoveAway(veklor, kTwinWarlockPreferredRange - distance);
    }

    if (distance > kTwinWarlockMaxRange)
    {
        Aq40TwinEncounter::RequestImmediateMovementInterrupt(bot);
        Aq40Helpers::LogAq40Info(bot, "twin_position",
            "twin:warlock_tank:stepin",
            "boss=twin phase=" + std::string(Aq40TwinEncounter::ToString(state->phase)) +
                " reason=veklor_range_hold",
            1000);
        return MoveNear(veklor, kTwinWarlockPreferredRange, MovementPriority::MOVEMENT_COMBAT);
    }

    return false;
}

bool Aq40TwinDodgeBlizzardAction::Execute(Event /*event*/)
{
    Aq40TwinEncounter::TwinEncounterState const* state = Aq40TwinEncounter::GetEncounterState(bot);
    if (!state)
        return false;

    bool const scriptedWindow = Aq40TwinEncounter::IsScriptedEventActive(
        *state, Aq40TwinEncounter::TwinScriptedEvent::Blizzard, kTwinBlizzardWindowMs);
    if (!IsTwinEncounterActive(state) && !scriptedWindow)
        return false;

    Aura* blizzardAura = GetTwinBlizzardAura(bot, botAI);
    DynamicObject* blizzardDynObj = blizzardAura ? blizzardAura->GetDynobjOwner() : nullptr;
    bool const hasBlizzardAura = blizzardAura != nullptr;
    bool const hasBlizzardDynObj = blizzardDynObj && blizzardDynObj->IsInWorld();
    if (!hasBlizzardAura && !hasBlizzardDynObj && !scriptedWindow)
        return false;

    Aq40TwinEncounter::RequestImmediateMovementInterrupt(bot);
    if (hasBlizzardDynObj)
    {
        float const hazardRadius = blizzardDynObj->GetRadius();
        if (hazardRadius > 0.0f && bot->GetDistance(blizzardDynObj) <= hazardRadius &&
            FleePosition(blizzardDynObj->GetPosition(), hazardRadius, 250U))
        {
            Aq40Helpers::LogAq40Info(bot, "avoid_hazard",
                "twin:blizzard:dynamic_object",
                "boss=twin hazard=blizzard phase=" + std::string(Aq40TwinEncounter::ToString(state->phase)) +
                    " reason=dynamic_object",
                1000);
            return true;
        }
    }

    if (botAI->DoSpecificAction("avoid aoe", Event(), true))
    {
        Aq40Helpers::LogAq40Info(bot, "avoid_hazard",
            "twin:blizzard:avoid_aoe",
            "boss=twin hazard=blizzard phase=" + std::string(Aq40TwinEncounter::ToString(state->phase)) +
                " reason=" + (hasBlizzardDynObj ? std::string("dynamic_object")
                                                 : (hasBlizzardAura ? std::string("aura")
                                                                    : std::string("scripted_window"))),
            1000);
        return true;
    }

    if (!hasBlizzardAura && !hasBlizzardDynObj)
        return false;

    GuidVector const encounterUnits = GetTwinEncounterUnits(botAI);
    Aq40TwinEncounter::TwinRoleAssignment const* assignment =
        Aq40TwinEncounter::GetAssignmentForMember(*state, bot->GetGUID());
    TwinPrePullAnchorChoice const anchorChoice =
        GetTwinHazardRecoveryAnchorChoice(bot, botAI, *state, assignment, encounterUnits);
    return MoveTwinToHazardAnchor(bot, botAI, *state, assignment, anchorChoice,
        "twin:blizzard:fallback_anchor", hasBlizzardDynObj ? "dynamic_object" : "aura");
}

bool Aq40TwinDodgeExplodeBugAction::Execute(Event /*event*/)
{
    Aq40TwinEncounter::TwinEncounterState const* state = Aq40TwinEncounter::GetEncounterState(bot);
    if (!state || !IsTwinEncounterActive(state))
        return false;

    Aq40TwinEncounter::TwinRoleAssignment const* assignment =
        Aq40TwinEncounter::GetAssignmentForMember(*state, bot->GetGUID());
    if (IsTwinPrimaryTankController(*state, assignment))
        return false;

    GuidVector const encounterUnits = GetTwinEncounterUnits(botAI);
    Unit* explodeBug = FindNearestTwinBug(bot, botAI, encounterUnits, kTwinExplodeBugDangerRadius);
    if (!explodeBug)
        return false;

    Spell* spell = explodeBug->GetCurrentSpell(CURRENT_GENERIC_SPELL);
    bool const scriptedWindow = Aq40TwinEncounter::IsScriptedEventActive(
        *state, Aq40TwinEncounter::TwinScriptedEvent::ExplodeBug, kTwinExplodeBugWindowMs);
    bool const castingExplosion = spell && Aq40SpellIds::MatchesAnySpellId(spell->GetSpellInfo(), { Aq40SpellIds::TwinExplodeBug });
    if (!scriptedWindow && !castingExplosion)
        return false;

    Aq40TwinEncounter::RequestImmediateMovementInterrupt(bot);
    Aq40Helpers::LogAq40Info(bot, "avoid_hazard",
        "twin:explode_bug:flee",
        "boss=twin hazard=explode_bug source=" + Aq40Helpers::GetAq40LogUnit(explodeBug),
        1000);
    if (FleePosition(explodeBug->GetPosition(), kTwinExplodeBugDangerRadius, 250U))
        return true;

    float const distance = bot->GetDistance2d(explodeBug);
    if (distance < kTwinExplodeBugDangerRadius &&
        MoveAway(explodeBug, kTwinExplodeBugDangerRadius - distance))
    {
        return true;
    }

    TwinPrePullAnchorChoice const anchorChoice =
        GetTwinHazardRecoveryAnchorChoice(bot, botAI, *state, assignment, encounterUnits);
    return MoveTwinToHazardAnchor(bot, botAI, *state, assignment, anchorChoice,
        "twin:explode_bug:recover", "explode_bug_ring");
}

bool Aq40TwinAvoidVeklorAction::Execute(Event /*event*/)
{
    Aq40TwinEncounter::TwinEncounterState const* state = Aq40TwinEncounter::GetEncounterState(bot);
    if (!state || Aq40TwinEncounter::IsTerminalPhase(state->phase))
        return false;

    GuidVector const encounterUnits = GetTwinEncounterUnits(botAI);
    Unit* veklor = FindTwinBoss(botAI, encounterUnits, Aq40TwinEncounter::TwinBoss::Veklor);
    if (!veklor)
        return false;

    Aq40TwinEncounter::TwinRoleAssignment const* assignment =
        Aq40TwinEncounter::GetAssignmentForMember(*state, bot->GetGUID());
    bool const isActiveVeklorController = assignment &&
                                          assignment->cohort == Aq40TwinEncounter::TwinRoleCohort::WarlockTank &&
                                          IsTwinWarlockTankController(*state, *assignment);
    if (isActiveVeklorController)
        return false;

    float const distance = bot->GetDistance2d(veklor);
    bool const arcaneWindow = Aq40TwinEncounter::IsScriptedEventActive(
        *state, Aq40TwinEncounter::TwinScriptedEvent::ArcaneBurst, kTwinArcaneBurstWindowMs);
    if (distance > kTwinArcaneBurstDangerRadius && !(arcaneWindow && distance <= kTwinArcaneBurstLooseRadius))
        return false;

    TwinPrePullAnchorChoice const anchorChoice =
        GetTwinHazardRecoveryAnchorChoice(bot, botAI, *state, assignment, encounterUnits);
    return MoveTwinToHazardAnchor(bot, botAI, *state, assignment, anchorChoice,
        "twin:veklor:avoid", arcaneWindow ? "arcane_burst_window" : "veklor_proximity");
}

bool Aq40TwinPostSwapHoldAction::Execute(Event /*event*/)
{
    Aq40TwinEncounter::TwinEncounterState const* state = Aq40TwinEncounter::GetEncounterState(bot);
    if (!state || Aq40TwinEncounter::IsTerminalPhase(state->phase) ||
        (state->phase != Aq40TwinEncounter::TwinEncounterPhase::TeleportWindow &&
         state->phase != Aq40TwinEncounter::TwinEncounterPhase::PickupRecovery &&
         !Aq40TwinEncounter::IsAnyThreatHoldWindowActive(*state)))
    {
        return false;
    }

    if (Aq40TwinEncounter::TwinLockedPickupAnchor const* lockedAnchor = Aq40TwinEncounter::GetLockedPickupAnchor(bot))
    {
        if (!Aq40TwinEncounter::IsLockedPickupAnchorExpired(*lockedAnchor) &&
            bot->GetExactDist2d(lockedAnchor->anchor.position.GetPositionX(),
                lockedAnchor->anchor.position.GetPositionY()) > kTwinAnchorTolerance)
        {
            Aq40TwinEncounter::RequestImmediateMovementInterrupt(bot);
            Aq40Helpers::LogAq40Info(bot, "twin_position",
                "twin:post_swap:locked_anchor",
                "boss=twin phase=" + std::string(Aq40TwinEncounter::ToString(state->phase)) +
                    " reason=locked_pickup_anchor",
                1000);
            return MoveInside(bot->GetMapId(), lockedAnchor->anchor.position.GetPositionX(),
                lockedAnchor->anchor.position.GetPositionY(), lockedAnchor->anchor.position.GetPositionZ(),
                kTwinAnchorTolerance, MovementPriority::MOVEMENT_COMBAT);
        }
    }

    GuidVector const encounterUnits = GetTwinEncounterUnits(botAI);
    Aq40TwinEncounter::TwinRoleAssignment const* assignment =
        Aq40TwinEncounter::GetAssignmentForMember(*state, bot->GetGUID());
    Aq40TwinEncounter::TwinAnchor holdAnchor;
    if (IsTwinSideHealerMode(*state, assignment))
        holdAnchor = GetTwinSideHealerRecoveryAnchorChoice(*state, *assignment, bot).anchor;
    else if (IsTwinMeleeProfile(bot, botAI))
        holdAnchor = GetVeknilashSideAnchor(botAI, encounterUnits);
    else
        holdAnchor = GetCenterSpreadAnchor(bot);

    if (bot->GetExactDist2d(holdAnchor.position.GetPositionX(), holdAnchor.position.GetPositionY()) <=
        kTwinAnchorTolerance)
    {
        return false;
    }

    Aq40TwinEncounter::RequestImmediateMovementInterrupt(bot);
    Aq40Helpers::LogAq40Info(bot, "twin_position",
        "twin:post_swap:hold_anchor",
        "boss=twin phase=" + std::string(Aq40TwinEncounter::ToString(state->phase)) +
            " reason=post_swap_hold",
        1000);
    return MoveInside(bot->GetMapId(), holdAnchor.position.GetPositionX(), holdAnchor.position.GetPositionY(),
        holdAnchor.position.GetPositionZ(), kTwinAnchorTolerance, MovementPriority::MOVEMENT_COMBAT);
}