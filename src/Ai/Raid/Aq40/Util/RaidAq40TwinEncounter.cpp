#include "RaidAq40TwinEncounter.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "Playerbots.h"
#include "Timer.h"

namespace Aq40TwinEncounter
{
namespace
{
float constexpr kPi = 3.14159265358979323846f;
float constexpr kRoomCenterX = -8954.855f;
float constexpr kRoomCenterY = 1235.7107f;
float constexpr kRoomCenterZ = -112.62047f;
float constexpr kInitialVeklorX = -8868.31f;
float constexpr kInitialVeklorY = 1205.97f;
float constexpr kInitialVeklorZ = -104.231f;
float constexpr kInitialVeknilashX = -9023.67f;
float constexpr kInitialVeknilashY = 1176.24f;
float constexpr kInitialVeknilashZ = -104.226f;
uint32 constexpr kInitialVeklorSideIndex = 1u;
uint32 constexpr kInitialVeknilashSideIndex = 0u;
float constexpr kSideTankAnchorDistance = 150.0f;
float constexpr kSideHealerAnchorDistance = 120.0f;
float constexpr kVeklorPullRange = 26.0f;
float constexpr kVeknilashPullRange = 5.0f;
float constexpr kVeklorReceivingRange = 22.0f;
float constexpr kVeknilashReceivingRange = 5.0f;
float constexpr kWallFacingRange = 3.5f;

std::unordered_map<uint32, TwinEncounterState> sTwinStateByInstance;
std::unordered_map<uint64, TwinLockedPickupAnchor> sTwinPickupAnchorByBot;

size_t ToIndex(TwinBoss boss)
{
    return boss == TwinBoss::Veknilash ? 1u : 0u;
}

uint32 ResolveNow(uint32 nowMs)
{
    return nowMs ? nowMs : getMSTime();
}

uint32 GetElapsedSince(uint32 startMs, uint32 nowMs)
{
    return startMs ? getMSTimeDiff(startMs, nowMs) : 0;
}

bool IsActiveUntil(uint32 expiresAtMs, uint32 nowMs)
{
    return expiresAtMs && nowMs < expiresAtMs;
}

bool HasMeaningfulOwnership(TwinStableOwnership const& ownership)
{
    return !ownership.expectedOwner.IsEmpty() || !ownership.reserveOwner.IsEmpty() ||
           !ownership.candidateOwner.IsEmpty() || !ownership.stableOwner.IsEmpty() || ownership.stableSinceMs ||
           ownership.reservePromotionUsed || ownership.lastValidConfirmationMs;
}

bool HasMeaningfulRecovery(TwinBossRecoveryState const& recovery)
{
    return recovery.threatHoldUntilMs || recovery.pickupEstablished || !recovery.pickupOwner.IsEmpty() ||
           recovery.pickupEstablishedAtMs || recovery.pickupLostAtMs;
}

bool HasMeaningfulHazards(TwinScriptedHazardWindows const& hazards)
{
    return hazards.teleportAtMs || hazards.blizzardAtMs || hazards.arcaneBurstAtMs || hazards.healBrotherAtMs ||
           hazards.explodeBugAtMs || hazards.mutateBugAtMs || hazards.uppercutAtMs ||
           hazards.unbalancingStrikeAtMs;
}

bool HasMeaningfulEncounterState(TwinEncounterState const& state)
{
    if (state.mode != TwinStrategyMode::Inactive || state.modeEnteredAtMs ||
        state.phase != TwinEncounterPhase::PrePull || state.phaseEnteredAtMs || !state.assignments.empty() ||
        state.recovery.splitBand != TwinSplitBand::Stable || state.recovery.splitBandEnteredAtMs ||
        HasMeaningfulHazards(state.scriptedHazards))
    {
        return true;
    }

    for (TwinBoss boss : { TwinBoss::Veklor, TwinBoss::Veknilash })
    {
        if (HasMeaningfulOwnership(GetOwnership(state, boss)) || HasMeaningfulRecovery(GetRecoveryState(state, boss)))
            return true;
    }

    return false;
}

uint64 GetBotKey(Player const* bot)
{
    return bot ? bot->GetGUID().GetRawValue() : 0;
}

TwinAnchor MakeAnchor(float x, float y, float z, float preferredRange = 0.0f, float facing = 0.0f)
{
    TwinAnchor anchor;
    anchor.position.Relocate(x, y, z);
    anchor.preferredRange = preferredRange;
    anchor.facing = facing;
    return anchor;
}

float ComputeFacing(Position const& from, Position const& to)
{
    return std::atan2(to.GetPositionY() - from.GetPositionY(), to.GetPositionX() - from.GetPositionX());
}

Position BuildRadialPosition(float originX, float originY, float originZ, float distance, bool towardCenter)
{
    Position position;
    float dirX = towardCenter ? (kRoomCenterX - originX) : (originX - kRoomCenterX);
    float dirY = towardCenter ? (kRoomCenterY - originY) : (originY - kRoomCenterY);
    float length = std::sqrt(dirX * dirX + dirY * dirY);

    if (length < 0.01f)
    {
        position.Relocate(originX, originY, originZ);
        return position;
    }

    float const zRatio = towardCenter ? std::min(distance / length, 1.0f) : 0.0f;
    position.Relocate(originX + (dirX / length) * distance,
                      originY + (dirY / length) * distance,
                      originZ + (kRoomCenterZ - originZ) * zRatio);
    return position;
}

TwinAnchor BuildRadialAnchor(float originX, float originY, float originZ, float distance, bool towardCenter)
{
    Position center;
    center.Relocate(kRoomCenterX, kRoomCenterY, kRoomCenterZ);

    Position position = BuildRadialPosition(originX, originY, originZ, distance, towardCenter);
    TwinAnchor anchor;
    anchor.position = position;
    anchor.preferredRange = distance;
    anchor.facing = towardCenter ? ComputeFacing(position, center) : ComputeFacing(position, MakeAnchor(originX, originY, originZ).position);
    return anchor;
}

TwinCenterSpreadSlot BuildCenterSpreadSlot(uint8 slotIndex, float radius, float angleDegrees)
{
    float const angleRadians = angleDegrees * kPi / 180.0f;
    TwinCenterSpreadSlot slot;
    slot.slotIndex = slotIndex;
    slot.anchor = MakeAnchor(kRoomCenterX + std::cos(angleRadians) * radius,
                             kRoomCenterY + std::sin(angleRadians) * radius,
                             kRoomCenterZ,
                             radius,
                             angleRadians + kPi);
    return slot;
}

TwinEncounterGeometry BuildGeometry()
{
    TwinEncounterGeometry geometry;

    geometry.room.center = MakeAnchor(kRoomCenterX, kRoomCenterY, kRoomCenterZ);

    geometry.room.sideTank[kInitialVeklorSideIndex] =
        BuildRadialAnchor(kInitialVeklorX, kInitialVeklorY, kInitialVeklorZ, kSideTankAnchorDistance, true);
    geometry.room.sideTank[kInitialVeknilashSideIndex] =
        BuildRadialAnchor(kInitialVeknilashX, kInitialVeknilashY, kInitialVeknilashZ, kSideTankAnchorDistance, true);

    geometry.room.sideHealer[kInitialVeklorSideIndex] =
        BuildRadialAnchor(kInitialVeklorX, kInitialVeklorY, kInitialVeklorZ, kSideHealerAnchorDistance, true);
    geometry.room.sideHealer[kInitialVeknilashSideIndex] =
        BuildRadialAnchor(kInitialVeknilashX, kInitialVeknilashY, kInitialVeknilashZ, kSideHealerAnchorDistance, true);

    geometry.room.centerSpread = {
        BuildCenterSpreadSlot(0, 18.0f, 20.0f),
        BuildCenterSpreadSlot(1, 26.0f, 80.0f),
        BuildCenterSpreadSlot(2, 18.0f, 140.0f),
        BuildCenterSpreadSlot(3, 26.0f, 200.0f),
        BuildCenterSpreadSlot(4, 18.0f, 260.0f),
        BuildCenterSpreadSlot(5, 26.0f, 320.0f),
    };

    geometry.synchronizedPull.veklorTank =
        BuildRadialAnchor(kInitialVeklorX, kInitialVeklorY, kInitialVeklorZ, kVeklorPullRange, true);
    geometry.synchronizedPull.veknilashTank =
        BuildRadialAnchor(kInitialVeknilashX, kInitialVeknilashY, kInitialVeknilashZ, kVeknilashPullRange, true);

    geometry.teleportReceiving.warlockTank[kInitialVeklorSideIndex] =
        BuildRadialAnchor(kInitialVeklorX, kInitialVeklorY, kInitialVeklorZ, kVeklorReceivingRange, true);
    geometry.teleportReceiving.meleeTank[kInitialVeklorSideIndex] =
        BuildRadialAnchor(kInitialVeklorX, kInitialVeklorY, kInitialVeklorZ, kVeknilashReceivingRange, true);
    geometry.teleportReceiving.warlockTank[kInitialVeknilashSideIndex] =
        BuildRadialAnchor(kInitialVeknilashX, kInitialVeknilashY, kInitialVeknilashZ, kVeklorReceivingRange, true);
    geometry.teleportReceiving.meleeTank[kInitialVeknilashSideIndex] =
        BuildRadialAnchor(kInitialVeknilashX, kInitialVeknilashY, kInitialVeknilashZ, kVeknilashReceivingRange, true);

    geometry.wallFacing.veknilashTank[kInitialVeklorSideIndex] =
        BuildRadialAnchor(kInitialVeklorX, kInitialVeklorY, kInitialVeklorZ, kWallFacingRange, false);
    geometry.wallFacing.veknilashTank[kInitialVeknilashSideIndex] =
        BuildRadialAnchor(kInitialVeknilashX, kInitialVeknilashY, kInitialVeknilashZ, kWallFacingRange, false);

    return geometry;
}

TwinEncounterGeometry const sTwinGeometry = BuildGeometry();
}    // namespace

TwinBoss GetOtherBoss(TwinBoss boss)
{
    switch (boss)
    {
        case TwinBoss::Veklor: return TwinBoss::Veknilash;
        case TwinBoss::Veknilash: return TwinBoss::Veklor;
    }

    return TwinBoss::Veklor;
}

TwinSide GetInitialSideForBoss(TwinBoss boss)
{
    return boss == TwinBoss::Veklor ? TwinSide::Side1 : TwinSide::Side0;
}

TwinSide GetOppositeSide(TwinSide side)
{
    switch (side)
    {
        case TwinSide::Side0: return TwinSide::Side1;
        case TwinSide::Side1: return TwinSide::Side0;
        case TwinSide::Unknown: return TwinSide::Unknown;
    }

    return TwinSide::Unknown;
}

bool IsKnownSide(TwinSide side)
{
    return side == TwinSide::Side0 || side == TwinSide::Side1;
}

TwinEncounterGeometry const& GetGeometry()
{
    return sTwinGeometry;
}

TwinStableOwnership& GetOwnership(TwinEncounterState& state, TwinBoss boss)
{
    return state.ownership[ToIndex(boss)];
}

TwinStableOwnership const& GetOwnership(TwinEncounterState const& state, TwinBoss boss)
{
    return state.ownership[ToIndex(boss)];
}

TwinBossRecoveryState& GetRecoveryState(TwinEncounterState& state, TwinBoss boss)
{
    return state.recovery.boss[ToIndex(boss)];
}

TwinBossRecoveryState const& GetRecoveryState(TwinEncounterState const& state, TwinBoss boss)
{
    return state.recovery.boss[ToIndex(boss)];
}

bool SetMode(TwinEncounterState& state, TwinStrategyMode mode, uint32 nowMs)
{
    uint32 const now = ResolveNow(nowMs);
    if (state.mode == mode)
    {
        if (!state.modeEnteredAtMs)
        {
            state.modeEnteredAtMs = now;
            return true;
        }

        return false;
    }

    state.mode = mode;
    state.modeEnteredAtMs = now;
    return true;
}

bool CanTransitionPhase(TwinEncounterPhase from, TwinEncounterPhase to)
{
    if (from == to || to == TwinEncounterPhase::PrePull || to == TwinEncounterPhase::TerminalFailure ||
        to == TwinEncounterPhase::Degraded)
    {
        return true;
    }

    switch (from)
    {
        case TwinEncounterPhase::PrePull:
            return to == TwinEncounterPhase::DualPullWindow || to == TwinEncounterPhase::Stable;
        case TwinEncounterPhase::DualPullWindow:
            return to == TwinEncounterPhase::Stable || to == TwinEncounterPhase::TeleportWindow ||
                   to == TwinEncounterPhase::PickupRecovery;
        case TwinEncounterPhase::Stable:
            return to == TwinEncounterPhase::TeleportWindow || to == TwinEncounterPhase::PickupRecovery;
        case TwinEncounterPhase::TeleportWindow:
            return to == TwinEncounterPhase::PickupRecovery || to == TwinEncounterPhase::Stable;
        case TwinEncounterPhase::PickupRecovery:
            return to == TwinEncounterPhase::Stable || to == TwinEncounterPhase::TeleportWindow;
        case TwinEncounterPhase::TerminalFailure:
            return false;
        case TwinEncounterPhase::Degraded:
            return to == TwinEncounterPhase::PickupRecovery || to == TwinEncounterPhase::Stable ||
                   to == TwinEncounterPhase::TeleportWindow;
    }

    return false;
}

bool SetPhase(TwinEncounterState& state, TwinEncounterPhase phase, uint32 nowMs)
{
    uint32 const now = ResolveNow(nowMs);
    if (state.phase == phase)
    {
        if (!state.phaseEnteredAtMs)
        {
            state.phaseEnteredAtMs = now;
            return true;
        }

        return false;
    }

    if (!CanTransitionPhase(state.phase, phase))
        return false;

    state.phase = phase;
    state.phaseEnteredAtMs = now;
    return true;
}

bool IsActivePhase(TwinEncounterPhase phase)
{
    return phase != TwinEncounterPhase::PrePull;
}

bool IsRecoveryPhase(TwinEncounterPhase phase)
{
    return phase == TwinEncounterPhase::TeleportWindow || phase == TwinEncounterPhase::PickupRecovery ||
           phase == TwinEncounterPhase::Degraded;
}

bool IsTerminalPhase(TwinEncounterPhase phase)
{
    return phase == TwinEncounterPhase::TerminalFailure;
}

uint32 GetPhaseElapsedMs(TwinEncounterState const& state, uint32 nowMs)
{
    return GetElapsedSince(state.phaseEnteredAtMs, ResolveNow(nowMs));
}

uint32 GetScriptedEventAtMs(TwinEncounterState const& state, TwinScriptedEvent event)
{
    switch (event)
    {
        case TwinScriptedEvent::Teleport: return state.scriptedHazards.teleportAtMs;
        case TwinScriptedEvent::Blizzard: return state.scriptedHazards.blizzardAtMs;
        case TwinScriptedEvent::ArcaneBurst: return state.scriptedHazards.arcaneBurstAtMs;
        case TwinScriptedEvent::HealBrother: return state.scriptedHazards.healBrotherAtMs;
        case TwinScriptedEvent::ExplodeBug: return state.scriptedHazards.explodeBugAtMs;
        case TwinScriptedEvent::MutateBug: return state.scriptedHazards.mutateBugAtMs;
        case TwinScriptedEvent::Uppercut: return state.scriptedHazards.uppercutAtMs;
        case TwinScriptedEvent::UnbalancingStrike: return state.scriptedHazards.unbalancingStrikeAtMs;
    }

    return 0;
}

bool IsScriptedEventActive(TwinEncounterState const& state, TwinScriptedEvent event, uint32 windowMs, uint32 nowMs,
                           uint32* outElapsedMs)
{
    if (outElapsedMs)
        *outElapsedMs = 0;

    if (!windowMs)
        return false;

    uint32 const eventAtMs = GetScriptedEventAtMs(state, event);
    if (!eventAtMs)
        return false;

    uint32 const elapsedMs = GetElapsedSince(eventAtMs, ResolveNow(nowMs));
    if (outElapsedMs)
        *outElapsedMs = elapsedMs;

    return elapsedMs <= windowMs;
}

bool IsScriptedEventActive(Player const* bot, TwinScriptedEvent event, uint32 windowMs, uint32 nowMs,
                           uint32* outElapsedMs)
{
    TwinEncounterState const* state = GetEncounterState(bot);
    return state && IsScriptedEventActive(*state, event, windowMs, nowMs, outElapsedMs);
}

bool SetExpectedOwner(TwinEncounterState& state, TwinBoss boss, ObjectGuid ownerGuid)
{
    TwinStableOwnership& ownership = GetOwnership(state, boss);
    if (ownership.expectedOwner == ownerGuid)
        return false;

    ownership.expectedOwner = ownerGuid;
    return true;
}

bool SetReserveOwner(TwinEncounterState& state, TwinBoss boss, ObjectGuid ownerGuid)
{
    TwinStableOwnership& ownership = GetOwnership(state, boss);
    if (ownership.reserveOwner == ownerGuid)
        return false;

    ownership.reserveOwner = ownerGuid;
    return true;
}

bool SetCandidateOwner(TwinEncounterState& state, TwinBoss boss, ObjectGuid ownerGuid)
{
    TwinStableOwnership& ownership = GetOwnership(state, boss);
    if (ownership.candidateOwner == ownerGuid)
        return false;

    ownership.candidateOwner = ownerGuid;
    return true;
}

void ClearCandidateOwner(TwinEncounterState& state, TwinBoss boss)
{
    GetOwnership(state, boss).candidateOwner = ObjectGuid::Empty;
}

bool ConfirmOwner(TwinEncounterState& state, TwinBoss boss, ObjectGuid ownerGuid, uint32 nowMs)
{
    if (ownerGuid.IsEmpty())
        return false;

    TwinStableOwnership& ownership = GetOwnership(state, boss);
    uint32 const now = ResolveNow(nowMs);
    bool changed = false;

    if (ownership.candidateOwner != ownerGuid)
    {
        ownership.candidateOwner = ownerGuid;
        changed = true;
    }

    if (ownership.lastValidConfirmationMs != now)
    {
        ownership.lastValidConfirmationMs = now;
        changed = true;
    }

    return changed;
}

bool SetStableOwner(TwinEncounterState& state, TwinBoss boss, ObjectGuid ownerGuid, uint32 nowMs)
{
    if (ownerGuid.IsEmpty())
        return false;

    TwinStableOwnership& ownership = GetOwnership(state, boss);
    uint32 const now = ResolveNow(nowMs);
    bool changed = ConfirmOwner(state, boss, ownerGuid, now);

    if (ownership.stableOwner != ownerGuid)
    {
        ownership.stableOwner = ownerGuid;
        ownership.stableSinceMs = now;
        changed = true;
    }
    else if (!ownership.stableSinceMs)
    {
        ownership.stableSinceMs = now;
        changed = true;
    }

    return changed;
}

void ClearStableOwner(TwinEncounterState& state, TwinBoss boss)
{
    TwinStableOwnership& ownership = GetOwnership(state, boss);
    ownership.stableOwner = ObjectGuid::Empty;
    ownership.stableSinceMs = 0;
}

void ResetStableOwnership(TwinEncounterState& state, TwinBoss boss, bool keepAssignments)
{
    TwinStableOwnership& ownership = GetOwnership(state, boss);
    if (!keepAssignments)
    {
        ownership = TwinStableOwnership();
        return;
    }

    ObjectGuid const expectedOwner = ownership.expectedOwner;
    ObjectGuid const reserveOwner = ownership.reserveOwner;
    bool const reservePromotionUsed = ownership.reservePromotionUsed;

    ownership = TwinStableOwnership();
    ownership.expectedOwner = expectedOwner;
    ownership.reserveOwner = reserveOwner;
    ownership.reservePromotionUsed = reservePromotionUsed;
}

void ResetAllStableOwnership(TwinEncounterState& state, bool keepAssignments)
{
    ResetStableOwnership(state, TwinBoss::Veklor, keepAssignments);
    ResetStableOwnership(state, TwinBoss::Veknilash, keepAssignments);
}

bool HasStableOwner(TwinEncounterState const& state, TwinBoss boss)
{
    return !GetOwnership(state, boss).stableOwner.IsEmpty();
}

bool HasCandidateOwner(TwinEncounterState const& state, TwinBoss boss)
{
    return !GetOwnership(state, boss).candidateOwner.IsEmpty();
}

bool IsStableOwner(TwinEncounterState const& state, TwinBoss boss, ObjectGuid ownerGuid)
{
    return !ownerGuid.IsEmpty() && GetOwnership(state, boss).stableOwner == ownerGuid;
}

bool CanPromoteReserveOwner(TwinEncounterState const& state, TwinBoss boss)
{
    TwinStableOwnership const& ownership = GetOwnership(state, boss);
    return !ownership.reserveOwner.IsEmpty() && !ownership.reservePromotionUsed;
}

bool PromoteReserveOwner(TwinEncounterState& state, TwinBoss boss, uint32 nowMs)
{
    if (!CanPromoteReserveOwner(state, boss))
        return false;

    TwinStableOwnership& ownership = GetOwnership(state, boss);
    uint32 const now = ResolveNow(nowMs);
    ObjectGuid const promotedOwner = ownership.reserveOwner;

    ownership.expectedOwner = promotedOwner;
    ownership.reserveOwner = ObjectGuid::Empty;
    ownership.candidateOwner = promotedOwner;
    ownership.stableOwner = ObjectGuid::Empty;
    ownership.stableSinceMs = 0;
    ownership.reservePromotionUsed = true;
    ownership.lastValidConfirmationMs = now;
    return true;
}

uint32 GetStableOwnershipAgeMs(TwinEncounterState const& state, TwinBoss boss, uint32 nowMs)
{
    TwinStableOwnership const& ownership = GetOwnership(state, boss);
    return ownership.stableOwner.IsEmpty() ? 0 : GetElapsedSince(ownership.stableSinceMs, ResolveNow(nowMs));
}

uint32 GetTimeSinceOwnershipConfirmationMs(TwinEncounterState const& state, TwinBoss boss, uint32 nowMs)
{
    return GetElapsedSince(GetOwnership(state, boss).lastValidConfirmationMs, ResolveNow(nowMs));
}

void ArmThreatHoldWindow(TwinEncounterState& state, TwinBoss boss, uint32 durationMs, uint32 nowMs)
{
    TwinBossRecoveryState& recovery = GetRecoveryState(state, boss);
    uint32 const now = ResolveNow(nowMs);
    recovery.threatHoldUntilMs = durationMs ? now + durationMs : 0;
}

void ClearThreatHoldWindow(TwinEncounterState& state, TwinBoss boss)
{
    GetRecoveryState(state, boss).threatHoldUntilMs = 0;
}

bool IsThreatHoldWindowActive(TwinEncounterState const& state, TwinBoss boss, uint32 nowMs)
{
    return IsActiveUntil(GetRecoveryState(state, boss).threatHoldUntilMs, ResolveNow(nowMs));
}

uint32 GetThreatHoldRemainingMs(TwinEncounterState const& state, TwinBoss boss, uint32 nowMs)
{
    TwinBossRecoveryState const& recovery = GetRecoveryState(state, boss);
    uint32 const now = ResolveNow(nowMs);
    return IsActiveUntil(recovery.threatHoldUntilMs, now) ? (recovery.threatHoldUntilMs - now) : 0;
}

bool IsAnyThreatHoldWindowActive(TwinEncounterState const& state, uint32 nowMs)
{
    return IsThreatHoldWindowActive(state, TwinBoss::Veklor, nowMs) ||
           IsThreatHoldWindowActive(state, TwinBoss::Veknilash, nowMs);
}

uint32 GetMaxThreatHoldRemainingMs(TwinEncounterState const& state, uint32 nowMs)
{
    return std::max(GetThreatHoldRemainingMs(state, TwinBoss::Veklor, nowMs),
                    GetThreatHoldRemainingMs(state, TwinBoss::Veknilash, nowMs));
}

bool MarkPickupEstablished(TwinEncounterState& state, TwinBoss boss, ObjectGuid ownerGuid, uint32 nowMs)
{
    if (ownerGuid.IsEmpty())
        return false;

    TwinBossRecoveryState& recovery = GetRecoveryState(state, boss);
    uint32 const now = ResolveNow(nowMs);
    bool const ownerChanged = recovery.pickupOwner != ownerGuid;
    bool const newlyEstablished = !recovery.pickupEstablished;
    bool changed = false;

    if (newlyEstablished)
    {
        recovery.pickupEstablished = true;
        changed = true;
    }

    if (ownerChanged)
    {
        recovery.pickupOwner = ownerGuid;
        changed = true;
    }

    if (newlyEstablished || ownerChanged || !recovery.pickupEstablishedAtMs)
    {
        recovery.pickupEstablishedAtMs = now;
        changed = true;
    }

    if (recovery.pickupLostAtMs)
    {
        recovery.pickupLostAtMs = 0;
        changed = true;
    }

    return changed;
}

void ClearPickupEstablished(TwinEncounterState& state, TwinBoss boss, uint32 nowMs)
{
    TwinBossRecoveryState& recovery = GetRecoveryState(state, boss);
    if (!recovery.pickupEstablished && recovery.pickupOwner.IsEmpty() && !recovery.pickupEstablishedAtMs)
        return;

    recovery.pickupEstablished = false;
    recovery.pickupOwner = ObjectGuid::Empty;
    recovery.pickupEstablishedAtMs = 0;
    recovery.pickupLostAtMs = ResolveNow(nowMs);
}

bool IsPickupEstablished(TwinEncounterState const& state, TwinBoss boss)
{
    return GetRecoveryState(state, boss).pickupEstablished;
}

ObjectGuid GetPickupOwner(TwinEncounterState const& state, TwinBoss boss)
{
    return GetRecoveryState(state, boss).pickupOwner;
}

uint32 GetPickupEstablishedAgeMs(TwinEncounterState const& state, TwinBoss boss, uint32 nowMs)
{
    TwinBossRecoveryState const& recovery = GetRecoveryState(state, boss);
    if (!recovery.pickupEstablished)
        return 0;

    return GetElapsedSince(recovery.pickupEstablishedAtMs, ResolveNow(nowMs));
}

bool SetSplitBand(TwinEncounterState& state, TwinSplitBand band, uint32 nowMs)
{
    uint32 const now = ResolveNow(nowMs);
    if (state.recovery.splitBand == band)
    {
        if (!state.recovery.splitBandEnteredAtMs)
        {
            state.recovery.splitBandEnteredAtMs = now;
            return true;
        }

        return false;
    }

    state.recovery.splitBand = band;
    state.recovery.splitBandEnteredAtMs = now;
    return true;
}

uint32 GetSplitBandAgeMs(TwinEncounterState const& state, uint32 nowMs)
{
    return GetElapsedSince(state.recovery.splitBandEnteredAtMs, ResolveNow(nowMs));
}

void EnterDualPullWindow(TwinEncounterState& state, uint32 nowMs)
{
    SetPhase(state, TwinEncounterPhase::DualPullWindow, nowMs);
}

void EnterStablePhase(TwinEncounterState& state, uint32 nowMs)
{
    SetPhase(state, TwinEncounterPhase::Stable, nowMs);
}

void EnterTeleportWindow(TwinEncounterState& state, uint32 threatHoldDurationMs, uint32 nowMs)
{
    uint32 const now = ResolveNow(nowMs);
    SetPhase(state, TwinEncounterPhase::TeleportWindow, now);

    for (TwinBoss boss : { TwinBoss::Veklor, TwinBoss::Veknilash })
    {
        ResetStableOwnership(state, boss, true);
        ArmThreatHoldWindow(state, boss, threatHoldDurationMs, now);
        ClearPickupEstablished(state, boss, now);
    }
}

void EnterPickupRecovery(TwinEncounterState& state, uint32 nowMs)
{
    SetPhase(state, TwinEncounterPhase::PickupRecovery, nowMs);
}

void EnterTerminalFailure(TwinEncounterState& state, uint32 nowMs)
{
    uint32 const now = ResolveNow(nowMs);
    SetSplitBand(state, TwinSplitBand::Terminal, now);
    SetPhase(state, TwinEncounterPhase::TerminalFailure, now);
}

void EnterDegradedPhase(TwinEncounterState& state, uint32 nowMs)
{
    uint32 const now = ResolveNow(nowMs);
    SetMode(state, TwinStrategyMode::Degraded, now);
    SetPhase(state, TwinEncounterPhase::Degraded, now);
}

uint32 GetInstanceId(Player const* bot)
{
    if (!bot || !bot->GetMap())
        return 0;

    return bot->GetMap()->GetInstanceId();
}

TwinEncounterState* GetEncounterState(Player* bot)
{
    uint32 const instanceId = GetInstanceId(bot);
    if (!instanceId)
        return nullptr;

    auto itr = sTwinStateByInstance.find(instanceId);
    return itr != sTwinStateByInstance.end() ? &itr->second : nullptr;
}

TwinEncounterState const* GetEncounterState(Player const* bot)
{
    uint32 const instanceId = GetInstanceId(bot);
    if (!instanceId)
        return nullptr;

    auto itr = sTwinStateByInstance.find(instanceId);
    return itr != sTwinStateByInstance.end() ? &itr->second : nullptr;
}

TwinEncounterState& EnsureEncounterState(Player* bot)
{
    uint32 const instanceId = GetInstanceId(bot);
    TwinEncounterState& state = sTwinStateByInstance[instanceId];
    if (state.instanceId != instanceId)
        ResetEncounterState(state, instanceId);

    return state;
}

TwinLockedPickupAnchor* GetLockedPickupAnchor(Player* bot)
{
    uint64 const key = GetBotKey(bot);
    if (!key)
        return nullptr;

    auto itr = sTwinPickupAnchorByBot.find(key);
    if (itr != sTwinPickupAnchorByBot.end() && IsLockedPickupAnchorExpired(itr->second))
    {
        sTwinPickupAnchorByBot.erase(itr);
        return nullptr;
    }

    return itr != sTwinPickupAnchorByBot.end() ? &itr->second : nullptr;
}

TwinLockedPickupAnchor const* GetLockedPickupAnchor(Player const* bot)
{
    uint64 const key = GetBotKey(bot);
    if (!key)
        return nullptr;

    auto itr = sTwinPickupAnchorByBot.find(key);
    if (itr != sTwinPickupAnchorByBot.end() && IsLockedPickupAnchorExpired(itr->second))
        return nullptr;

    return itr != sTwinPickupAnchorByBot.end() ? &itr->second : nullptr;
}

TwinLockedPickupAnchor& EnsureLockedPickupAnchor(Player* bot)
{
    uint64 const key = GetBotKey(bot);
    TwinLockedPickupAnchor& state = sTwinPickupAnchorByBot[key];
    if (state.instanceId != GetInstanceId(bot))
        ResetPickupAnchorState(state);

    state.instanceId = GetInstanceId(bot);
    return state;
}

bool IsLockedPickupAnchorExpired(TwinLockedPickupAnchor const& state, uint32 nowMs)
{
    return !state.instanceId || !state.expiresAtMs || !IsActiveUntil(state.expiresAtMs, ResolveNow(nowMs));
}

bool HasLockedPickupAnchor(Player const* bot, TwinBoss boss, uint32 nowMs)
{
    TwinLockedPickupAnchor const* state = GetLockedPickupAnchor(bot);
    return state && state->boss == boss && !IsLockedPickupAnchorExpired(*state, nowMs);
}

bool SetLockedPickupAnchor(Player* bot, TwinBoss boss, TwinSide side, TwinAnchor const& anchor, uint32 durationMs,
                           uint32 nowMs)
{
    uint32 const instanceId = GetInstanceId(bot);
    if (!bot || !instanceId)
        return false;

    TwinLockedPickupAnchor& state = EnsureLockedPickupAnchor(bot);
    uint32 const now = ResolveNow(nowMs);
    uint32 const expiresAtMs = durationMs ? now + durationMs : 0;
    bool const changed = state.boss != boss || state.side != side ||
                         state.anchor.position.GetPositionX() != anchor.position.GetPositionX() ||
                         state.anchor.position.GetPositionY() != anchor.position.GetPositionY() ||
                         state.anchor.position.GetPositionZ() != anchor.position.GetPositionZ() ||
                         state.anchor.preferredRange != anchor.preferredRange || state.anchor.facing != anchor.facing;

    state.instanceId = instanceId;
    state.boss = boss;
    state.side = side;
    state.lockedAtMs = now;
    state.expiresAtMs = expiresAtMs;
    state.anchor = anchor;
    return changed;
}

bool PruneExpiredLockedPickupAnchor(Player* bot, uint32 nowMs)
{
    uint64 const key = GetBotKey(bot);
    auto itr = sTwinPickupAnchorByBot.find(key);
    if (itr == sTwinPickupAnchorByBot.end() || !IsLockedPickupAnchorExpired(itr->second, nowMs))
        return false;

    sTwinPickupAnchorByBot.erase(itr);
    return true;
}

void ClearLockedPickupAnchor(Player* bot)
{
    uint64 const key = GetBotKey(bot);
    auto itr = sTwinPickupAnchorByBot.find(key);
    if (itr != sTwinPickupAnchorByBot.end())
        sTwinPickupAnchorByBot.erase(itr);
}

bool HasActiveLockedPickupAnchor(Player const* bot, uint32 nowMs)
{
    TwinLockedPickupAnchor const* state = GetLockedPickupAnchor(bot);
    return state && !IsLockedPickupAnchorExpired(*state, nowMs);
}

bool IsImmediateRepositionWindow(TwinEncounterState const& state, uint32 nowMs)
{
    return state.phase == TwinEncounterPhase::DualPullWindow || state.phase == TwinEncounterPhase::TeleportWindow ||
           state.phase == TwinEncounterPhase::PickupRecovery || IsAnyThreatHoldWindowActive(state, nowMs);
}

bool IsImmediateRepositionWindow(Player const* bot, uint32 nowMs)
{
    TwinEncounterState const* state = GetEncounterState(bot);
    return HasActiveLockedPickupAnchor(bot, nowMs) || (state && IsImmediateRepositionWindow(*state, nowMs));
}

bool RequestImmediateMovementInterrupt(Player* bot)
{
    if (!bot)
        return false;

    if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot))
    {
        botAI->RequestSpellInterrupt();
        return true;
    }

    return false;
}

void ResetEncounterState(TwinEncounterState& state, uint32 instanceId)
{
    state = TwinEncounterState();
    state.instanceId = instanceId;
}

void ResetPickupAnchorState(TwinLockedPickupAnchor& state)
{
    state = TwinLockedPickupAnchor();
}

bool ResetState(Player* bot)
{
    uint32 const instanceId = GetInstanceId(bot);
    if (!instanceId)
        return false;

    bool erased = sTwinStateByInstance.erase(instanceId) > 0;
    for (auto itr = sTwinPickupAnchorByBot.begin(); itr != sTwinPickupAnchorByBot.end();)
    {
        if (itr->second.instanceId == instanceId)
        {
            itr = sTwinPickupAnchorByBot.erase(itr);
            erased = true;
            continue;
        }

        ++itr;
    }

    return erased;
}

bool HasPersistentState(Player* bot)
{
    uint32 const instanceId = GetInstanceId(bot);
    if (!instanceId)
        return false;

    auto const stateItr = sTwinStateByInstance.find(instanceId);
    if (stateItr != sTwinStateByInstance.end() && HasMeaningfulEncounterState(stateItr->second))
        return true;

    for (auto const& entry : sTwinPickupAnchorByBot)
    {
        if (entry.second.instanceId == instanceId && !IsLockedPickupAnchorExpired(entry.second))
            return true;
    }

    return false;
}

char const* ToString(TwinBoss boss)
{
    switch (boss)
    {
        case TwinBoss::Veklor: return "veklor";
        case TwinBoss::Veknilash: return "veknilash";
    }

    return "unknown";
}

char const* ToString(TwinSide side)
{
    switch (side)
    {
        case TwinSide::Side0: return "side0";
        case TwinSide::Side1: return "side1";
        case TwinSide::Unknown: return "unknown";
    }

    return "unknown";
}

char const* ToString(TwinRoleCohort cohort)
{
    switch (cohort)
    {
        case TwinRoleCohort::None: return "none";
        case TwinRoleCohort::WarlockTank: return "warlock_tank";
        case TwinRoleCohort::MeleeTank: return "melee_tank";
        case TwinRoleCohort::SideHealer: return "side_healer";
        case TwinRoleCohort::RaidHealer: return "raid_healer";
        case TwinRoleCohort::RangedDps: return "ranged_dps";
        case TwinRoleCohort::Hunter: return "hunter";
        case TwinRoleCohort::MeleeDps: return "melee_dps";
    }

    return "unknown";
}

char const* ToString(TwinStrategyMode mode)
{
    switch (mode)
    {
        case TwinStrategyMode::Inactive: return "inactive";
        case TwinStrategyMode::StandardCompReady: return "standard_comp_ready";
        case TwinStrategyMode::Combat: return "combat";
        case TwinStrategyMode::Degraded: return "degraded";
    }

    return "unknown";
}

char const* ToString(TwinEncounterPhase phase)
{
    switch (phase)
    {
        case TwinEncounterPhase::PrePull: return "prepull";
        case TwinEncounterPhase::DualPullWindow: return "dual_pull_window";
        case TwinEncounterPhase::Stable: return "stable";
        case TwinEncounterPhase::TeleportWindow: return "teleport_window";
        case TwinEncounterPhase::PickupRecovery: return "pickup_recovery";
        case TwinEncounterPhase::TerminalFailure: return "terminal_failure";
        case TwinEncounterPhase::Degraded: return "degraded";
    }

    return "unknown";
}

char const* ToString(TwinScriptedEvent event)
{
    switch (event)
    {
        case TwinScriptedEvent::Teleport: return "teleport";
        case TwinScriptedEvent::Blizzard: return "blizzard";
        case TwinScriptedEvent::ArcaneBurst: return "arcane_burst";
        case TwinScriptedEvent::HealBrother: return "heal_brother";
        case TwinScriptedEvent::ExplodeBug: return "explode_bug";
        case TwinScriptedEvent::MutateBug: return "mutate_bug";
        case TwinScriptedEvent::Uppercut: return "uppercut";
        case TwinScriptedEvent::UnbalancingStrike: return "unbalancing_strike";
    }

    return "unknown";
}

char const* ToString(TwinSplitBand band)
{
    switch (band)
    {
        case TwinSplitBand::Stable: return "stable";
        case TwinSplitBand::Warning: return "warning";
        case TwinSplitBand::Urgent: return "urgent";
        case TwinSplitBand::Terminal: return "terminal";
    }

    return "unknown";
}
}    // namespace Aq40TwinEncounter