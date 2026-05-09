#include "RaidAq40Actions.h"

#include <limits>
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
uint32 constexpr kTwinBlizzardWindowMs = 5000;
uint32 constexpr kTwinExplodeBugWindowMs = 2500;
uint32 constexpr kTwinArcaneBurstWindowMs = 2500;

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
    float const side0Distance = geometry.room.sideTank[0].position.GetExactDist2d(x, y);
    float const side1Distance = geometry.room.sideTank[1].position.GetExactDist2d(x, y);
    return side0Distance <= side1Distance ? Aq40TwinEncounter::TwinSide::Side0
                                          : Aq40TwinEncounter::TwinSide::Side1;
}

char const* GetTwinTargetReason(Player* bot, PlayerbotAI* botAI,
                                Aq40TwinEncounter::TwinEncounterState const& state)
{
    if (Aq40TwinEncounter::IsThreatHoldWindowActive(state, Aq40TwinEncounter::TwinBoss::Veklor) &&
        !IsTwinWarlockProfile(bot))
    {
        return "hold_veknilash";
    }

    return IsTwinMeleeProfile(bot, botAI) ? "veknilash" : "veklor";
}

Unit* ResolveTwinTarget(Player* bot, PlayerbotAI* botAI, Aq40TwinEncounter::TwinEncounterState const& state,
                        GuidVector const& units, char const*& outReason)
{
    if (!bot || !botAI || IsTwinHealerProfile(bot, botAI))
        return nullptr;

    Unit* veklor = FindTwinBoss(botAI, units, Aq40TwinEncounter::TwinBoss::Veklor);
    Unit* veknilash = FindTwinBoss(botAI, units, Aq40TwinEncounter::TwinBoss::Veknilash);
    outReason = GetTwinTargetReason(bot, botAI, state);

    if (std::string(outReason) == "hold_veknilash")
        return veknilash ? veknilash : veklor;

    if (IsTwinMeleeProfile(bot, botAI))
        return veknilash ? veknilash : veklor;

    return veklor ? veklor : veknilash;
}

Aq40TwinEncounter::TwinAnchor const& GetCenterSpreadAnchor(Player* bot)
{
    Aq40TwinEncounter::TwinEncounterGeometry const& geometry = Aq40TwinEncounter::GetGeometry();
    size_t const slotIndex = bot ? (bot->GetGUID().GetRawValue() % geometry.room.centerSpread.size()) : 0u;
    return geometry.room.centerSpread[slotIndex].anchor;
}

Aq40TwinEncounter::TwinAnchor const& GetVeknilashSideAnchor(PlayerbotAI* botAI, GuidVector const& units)
{
    Aq40TwinEncounter::TwinEncounterGeometry const& geometry = Aq40TwinEncounter::GetGeometry();
    Unit* veknilash = FindTwinBoss(botAI, units, Aq40TwinEncounter::TwinBoss::Veknilash);
    Aq40TwinEncounter::TwinSide const side =
        veknilash ? GetTwinSideForPosition(veknilash->GetPositionX(), veknilash->GetPositionY())
                  : Aq40TwinEncounter::GetInitialSideForBoss(Aq40TwinEncounter::TwinBoss::Veknilash);
    return geometry.room.sideTank[ToSideIndex(side)];
}
}    // namespace

bool Aq40TwinPrePullStageAction::Execute(Event /*event*/)
{
    Aq40TwinEncounter::TwinEncounterState const* state = Aq40TwinEncounter::GetEncounterState(bot);
    if (!state || state->mode != Aq40TwinEncounter::TwinStrategyMode::StandardCompReady ||
        state->phase != Aq40TwinEncounter::TwinEncounterPhase::PrePull || state->assignments.empty())
    {
        return false;
    }

    return false;
}

bool Aq40TwinDualPullEngageAction::Execute(Event /*event*/)
{
    Aq40TwinEncounter::TwinEncounterState const* state = Aq40TwinEncounter::GetEncounterState(bot);
    if (!state || state->phase != Aq40TwinEncounter::TwinEncounterPhase::DualPullWindow ||
        IsTwinHealerProfile(bot, botAI))
    {
        return false;
    }

    GuidVector const encounterUnits = GetTwinEncounterUnits(botAI);
    char const* reason = "dual_pull";
    Unit* target = ResolveTwinTarget(bot, botAI, *state, encounterUnits, reason);
    if (!target)
        return false;

    if (target->GetEntry() == Aq40SpellIds::TwinVeklorNpcEntry && IsTwinWarlockProfile(bot))
    {
        float const distance = bot->GetDistance2d(target);
        if (distance < kTwinWarlockMinRange)
        {
            Aq40TwinEncounter::RequestImmediateMovementInterrupt(bot);
            Aq40Helpers::LogAq40Info(bot, "twin_position",
                "twin:dual_pull:warlock_backstep",
                "boss=twin phase=dual_pull_window reason=warlock_range_hold target=" +
                    Aq40Helpers::GetAq40LogUnit(target),
                1000);
            return MoveAway(target, kTwinWarlockPreferredRange - distance);
        }

        if (distance > kTwinWarlockMaxRange)
        {
            Aq40TwinEncounter::RequestImmediateMovementInterrupt(bot);
            Aq40Helpers::LogAq40Info(bot, "twin_position",
                "twin:dual_pull:warlock_stepin",
                "boss=twin phase=dual_pull_window reason=warlock_range_hold target=" +
                    Aq40Helpers::GetAq40LogUnit(target),
                1000);
            return MoveNear(target, kTwinWarlockPreferredRange, MovementPriority::MOVEMENT_COMBAT);
        }
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
        return false;

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
    if (!IsTwinEncounterActive(state) || !IsTwinWarlockProfile(bot) || IsTwinHealerProfile(bot, botAI))
        return false;

    GuidVector const encounterUnits = GetTwinEncounterUnits(botAI);
    Unit* veklor = FindTwinBoss(botAI, encounterUnits, Aq40TwinEncounter::TwinBoss::Veklor);
    if (!veklor)
        return false;

    if (AI_VALUE(Unit*, "current target") != veklor || bot->GetVictim() != veklor)
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

    bool const isAssignedWarlockTankProfile = IsTwinWarlockProfile(bot) && Aq40BossHelper::IsEncounterTank(bot, bot);
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