#include "RaidAq40Actions.h"

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
uint32 constexpr kTwinArcaneBurstWindowMs = 2500;

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

TwinTargetIntent GetTwinTargetIntent(Player* bot, PlayerbotAI* botAI,
                                     Aq40TwinEncounter::TwinEncounterState const& state,
                                     Aq40TwinEncounter::TwinRoleAssignment const* assignment)
{
    if (assignment)
    {
        switch (assignment->cohort)
        {
            case Aq40TwinEncounter::TwinRoleCohort::WarlockTank:
                if (ShouldHoldTwinReserveTankAssignment(*assignment))
                    return TwinTargetIntent::HoldReserve;

                return TwinTargetIntent::Veklor;

            case Aq40TwinEncounter::TwinRoleCohort::MeleeTank:
                if (ShouldHoldTwinReserveTankAssignment(*assignment))
                    return TwinTargetIntent::HoldReserve;

                return TwinTargetIntent::Veknilash;

            case Aq40TwinEncounter::TwinRoleCohort::SideHealer:
            case Aq40TwinEncounter::TwinRoleCohort::RaidHealer:
                return TwinTargetIntent::None;

            case Aq40TwinEncounter::TwinRoleCohort::Hunter:
            case Aq40TwinEncounter::TwinRoleCohort::MeleeDps:
                return TwinTargetIntent::Veknilash;

            case Aq40TwinEncounter::TwinRoleCohort::RangedDps:
                if (Aq40TwinEncounter::IsThreatHoldWindowActive(state, Aq40TwinEncounter::TwinBoss::Veklor))
                    return TwinTargetIntent::HoldVeknilash;

                return TwinTargetIntent::Veklor;

            case Aq40TwinEncounter::TwinRoleCohort::None:
                break;
        }
    }

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
        Aq40Helpers::LogAq40Info(bot, "twin_position", "twin:dual_pull:reserve_hold", fields.str(), 1000);
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
    Aq40TwinEncounter::TwinRoleAssignment const* assignment = Aq40TwinEncounter::GetAssignmentForMember(state, bot->GetGUID());
    TwinTargetIntent const intent = GetTwinTargetIntent(bot, botAI, state, assignment);
    outReason = GetTwinTargetReason(intent);

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
    Aq40TwinEncounter::TwinEncounterState const* state = Aq40TwinEncounter::GetEncounterState(bot);
    bool const overlayChanged = SyncTwinWarlockTankOverlay(bot, botAI);
    if (!state || state->phase != Aq40TwinEncounter::TwinEncounterPhase::DualPullWindow ||
        IsTwinHealerProfile(bot, botAI))
    {
        return overlayChanged;
    }

    Aq40TwinEncounter::TwinRoleAssignment const* assignment = Aq40TwinEncounter::GetAssignmentForMember(*state, bot->GetGUID());
    if (assignment && IsTwinReserveTankHoldAssignment(*assignment))
        return HoldTwinReserveTankAtAnchor(bot, *state, *assignment) || overlayChanged;

    GuidVector const encounterUnits = GetTwinEncounterUnits(botAI);
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

bool Aq40TwinHealerSupportAction::Execute(Event /*event*/)
{
    Aq40TwinEncounter::TwinEncounterState const* state = Aq40TwinEncounter::GetEncounterState(bot);
    if (!IsTwinEncounterActive(state) || !IsTwinHealerProfile(bot, botAI) || !state->assignments.empty() ||
        Aq40TwinEncounter::HasActiveLockedPickupAnchor(bot))
    {
        return false;
    }

    Aq40TwinEncounter::TwinAnchor const& anchor = GetCenterSpreadAnchor(bot);
    if (bot->GetExactDist2d(anchor.position.GetPositionX(), anchor.position.GetPositionY()) <= kTwinAnchorTolerance)
        return false;

    Aq40TwinEncounter::RequestImmediateMovementInterrupt(bot);
    Aq40Helpers::LogAq40Info(bot, "spread_position",
        "twin:healer_support",
        "boss=twin phase=" + std::string(Aq40TwinEncounter::ToString(state->phase)) +
            " reason=center_spread",
        1000);
    return MoveInside(bot->GetMapId(), anchor.position.GetPositionX(), anchor.position.GetPositionY(),
        anchor.position.GetPositionZ(), kTwinAnchorTolerance, MovementPriority::MOVEMENT_COMBAT);
}

bool Aq40TwinChooseTargetAction::Execute(Event /*event*/)
{
    Aq40TwinEncounter::TwinEncounterState const* state = Aq40TwinEncounter::GetEncounterState(bot);
    if (!IsTwinEncounterActive(state) || IsTwinHealerProfile(bot, botAI))
        return false;

    GuidVector const encounterUnits = GetTwinEncounterUnits(botAI);
    char const* reason = "boss";
    Unit* target = ResolveTwinTarget(bot, botAI, *state, encounterUnits, reason);
    if (!target)
        return false;

    if (AI_VALUE(Unit*, "current target") == target && bot->GetVictim() == target)
        return false;

    Aq40Helpers::LogAq40Target(bot, "twin", reason, target, 1000);
    return Attack(target);
}

bool Aq40TwinHoldSplitAction::Execute(Event /*event*/)
{
    Aq40TwinEncounter::TwinEncounterState const* state = Aq40TwinEncounter::GetEncounterState(bot);
    if (!state || Aq40TwinEncounter::IsTerminalPhase(state->phase))
        return false;

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
    Aq40TwinEncounter::TwinAnchor const& safeAnchor =
        IsTwinMeleeProfile(bot, botAI) ? GetVeknilashSideAnchor(botAI, encounterUnits) : GetCenterSpreadAnchor(bot);
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
    if (assignment && ShouldHoldTwinReserveTankAssignment(*assignment))
    {
        return overlayChanged;
    }

    GuidVector const encounterUnits = GetTwinEncounterUnits(botAI);
    Unit* veklor = FindTwinBoss(botAI, encounterUnits, Aq40TwinEncounter::TwinBoss::Veklor);
    if (!veklor)
        return overlayChanged;

    if (AI_VALUE(Unit*, "current target") != veklor || bot->GetTarget() != veklor->GetGUID())
    {
        Aq40Helpers::LogAq40Target(bot, "twin", "warlock_pin", veklor, 1000);
        return Attack(veklor);
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
    if (!IsTwinEncounterActive(state) &&
        !(state && Aq40TwinEncounter::IsScriptedEventActive(*state, Aq40TwinEncounter::TwinScriptedEvent::Blizzard,
            kTwinBlizzardWindowMs)))
    {
        return false;
    }

    bool const hasBlizzardAura = Aq40SpellIds::HasAnyAura(botAI, bot, { Aq40SpellIds::TwinBlizzard }) ||
                                 botAI->HasAura("blizzard", bot);
    if (!hasBlizzardAura &&
        !Aq40TwinEncounter::IsScriptedEventActive(*state, Aq40TwinEncounter::TwinScriptedEvent::Blizzard,
            kTwinBlizzardWindowMs))
    {
        return false;
    }

    Aq40TwinEncounter::RequestImmediateMovementInterrupt(bot);
    if (botAI->DoSpecificAction("avoid aoe", Event(), true))
    {
        Aq40Helpers::LogAq40Info(bot, "avoid_hazard",
            "twin:blizzard:avoid_aoe",
            "boss=twin hazard=blizzard phase=" + std::string(Aq40TwinEncounter::ToString(state->phase)),
            1000);
        return true;
    }

    if (!hasBlizzardAura)
        return false;

    Aq40TwinEncounter::TwinAnchor const& fallbackAnchor =
        IsTwinMeleeProfile(bot, botAI) ? GetVeknilashSideAnchor(botAI, GetTwinEncounterUnits(botAI))
                                       : GetCenterSpreadAnchor(bot);
    if (bot->GetExactDist2d(fallbackAnchor.position.GetPositionX(), fallbackAnchor.position.GetPositionY()) <=
        kTwinAnchorTolerance)
    {
        return false;
    }

    Aq40Helpers::LogAq40Info(bot, "avoid_hazard",
        "twin:blizzard:fallback_anchor",
        "boss=twin hazard=blizzard phase=" + std::string(Aq40TwinEncounter::ToString(state->phase)) +
            " reason=fallback_anchor",
        1000);
    return MoveInside(bot->GetMapId(), fallbackAnchor.position.GetPositionX(), fallbackAnchor.position.GetPositionY(),
        fallbackAnchor.position.GetPositionZ(), kTwinAnchorTolerance, MovementPriority::MOVEMENT_COMBAT);
}

bool Aq40TwinDodgeExplodeBugAction::Execute(Event /*event*/)
{
    Aq40TwinEncounter::TwinEncounterState const* state = Aq40TwinEncounter::GetEncounterState(bot);
    if (!IsTwinEncounterActive(state) || Aq40BossHelper::IsEncounterTank(bot, bot))
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
    return distance < kTwinExplodeBugDangerRadius &&
           MoveAway(explodeBug, kTwinExplodeBugDangerRadius - distance);
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

    bool const isAssignedWarlockTankProfile = Aq40TwinEncounter::IsTwinDesignatedWarlockTank(bot);
    if (isAssignedWarlockTankProfile)
        return false;

    float const distance = bot->GetDistance2d(veklor);
    bool const arcaneWindow = Aq40TwinEncounter::IsScriptedEventActive(
        *state, Aq40TwinEncounter::TwinScriptedEvent::ArcaneBurst, kTwinArcaneBurstWindowMs);
    if (distance > kTwinArcaneBurstDangerRadius && !(arcaneWindow && distance <= kTwinArcaneBurstLooseRadius))
        return false;

    Aq40TwinEncounter::TwinAnchor const& safeAnchor =
        IsTwinMeleeProfile(bot, botAI) ? GetVeknilashSideAnchor(botAI, encounterUnits) : GetCenterSpreadAnchor(bot);
    if (bot->GetExactDist2d(safeAnchor.position.GetPositionX(), safeAnchor.position.GetPositionY()) <=
        kTwinAnchorTolerance)
    {
        return false;
    }

    Aq40TwinEncounter::RequestImmediateMovementInterrupt(bot);
    Aq40Helpers::LogAq40Info(bot, "avoid_hazard",
        "twin:veklor:avoid",
        "boss=twin hazard=veklor_proximity source=" + Aq40Helpers::GetAq40LogUnit(veklor),
        1000);
    return MoveInside(bot->GetMapId(), safeAnchor.position.GetPositionX(), safeAnchor.position.GetPositionY(),
        safeAnchor.position.GetPositionZ(), kTwinAnchorTolerance, MovementPriority::MOVEMENT_COMBAT);
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
    Aq40TwinEncounter::TwinAnchor const& holdAnchor =
        IsTwinMeleeProfile(bot, botAI) ? GetVeknilashSideAnchor(botAI, encounterUnits) : GetCenterSpreadAnchor(bot);
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