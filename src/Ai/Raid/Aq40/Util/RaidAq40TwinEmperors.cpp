#include "RaidAq40TwinEmperors.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <unordered_map>

#include "AiObjectContext.h"
#include "CharmInfo.h"
#include "Group.h"
#include "ObjectAccessor.h"
#include "Pet.h"
#include "Playerbots.h"
#include "RtiTargetValue.h"
#include "Spell.h"
#include "Timer.h"
#include "../RaidAq40SpellIds.h"
#include "../../RaidBossHelpers.h"

// ===========================================================================
// Twin Emperors encounter state implementation.
//
// VALIDATION: see TWIN_EMPERORS_VALIDATION.md for the full checklist.
//
// Phase model: PrePull → Stable → TeleportWindow → PickupRecovery → Stable
//              (or EmergencySplitRecovery / Degraded on failure)
//
// Split bands:  Stable ≥75y | Warning 65-75y | Urgent 60-65y | Terminal <60y
// Pickup ranges: Vek'lor 19-30y | Vek'nilash 1.5-8y
//
// All cached state maps below are keyed by instance context.
// ResetState() must clear every map entry for the bot's instance.
// ===========================================================================
namespace Aq40TwinEmperors
{
// Forward declarations for functions used by anonymous namespace helpers
bool HasBossPickupAggro(Player* member, Unit* boss);

namespace
{
float constexpr kTwinSplitWarningDistance = 75.0f;
float constexpr kTwinSplitUrgentDistance = 65.0f;
float constexpr kTwinSplitTerminalDistance = 60.0f;
uint32 constexpr kTwinTeleportWindowMs = 6000;
uint32 constexpr kTwinInitialPullHoldMs = 3500;
uint32 constexpr kTwinTeleportHoldMs = 4500;
uint32 constexpr kTwinPickupFailedHoldMs = 3500;
uint32 constexpr kTwinTeleportRecoveryMs = 3500;
uint32 constexpr kTwinUppercutRecoveryMs = 4500;
uint32 constexpr kTwinUnbalancingRecoveryMs = 4500;
uint32 constexpr kTwinPostSwapRecoveryMs = 1500;
uint32 constexpr kTwinPickupEstablishedMemoryMs = 12000;
uint32 constexpr kTwinPickupAnchorLockMs = 5000;

// Emergency split recovery constants (issue #3)
float constexpr kTwinEmergencySplitRecoveryMinSeparation = 80.0f;
// Room geometry for orientation and receiving anchors
float constexpr kTwinRoomCenterX = -8954.855f;
float constexpr kTwinRoomCenterY = 1235.7107f;
float constexpr kTwinRoomCenterZ = -112.62047f;
// Warlock receiving geometry: stand 22y from boss, inward toward center
float constexpr kTwinWarlockReceivingDesiredRange = 22.0f;
// Melee receiving geometry: stand 5y from boss inward
float constexpr kTwinMeleeReceivingDesiredRange = 5.0f;
// Arcane Burst safety: melee receiving must stay >16y from Vek'lor
float constexpr kTwinArcaneBurstSafetyRange = 16.0f;
// Wall-facing: tank stands between boss and wall at this range
float constexpr kTwinWallFacingTankRange = 3.5f;

struct TwinMeleeRecoveryState
{
    uint32 instanceId = 0;
    uint32 untilMs = 0;
    std::string reason;
};

struct TwinMarkerState
{
    ObjectGuid squareGuid = ObjectGuid::Empty;
    ObjectGuid diamondGuid = ObjectGuid::Empty;
    ObjectGuid skullGuid = ObjectGuid::Empty;
};

struct TwinTeleportState
{
    uint32 armedAtMs = 0;
    uint32 armedUntilMs = 0;
    ObjectGuid lastCasterGuid = ObjectGuid::Empty;
};

struct TwinPickupEstablishedState
{
    uint32 veklorEstablishedAtMs = 0;
    uint32 veknilashEstablishedAtMs = 0;
};

struct TwinPickupAnchorState
{
    uint32 instanceId = 0;
    uint32 sideIndex = 0;
    uint32 untilMs = 0;
    ObjectGuid bossGuid = ObjectGuid::Empty;
    Position anchor;
};

struct TwinThreatHoldWindowState
{
    uint32 initialPullOpenedAtMs = 0;
    uint32 initialPullUntilMs = 0;
    uint32 teleportOpenedAtMs = 0;
    uint32 teleportUntilMs = 0;
    uint32 pickupFailedOpenedAtMs = 0;
    uint32 pickupFailedUntilMs = 0;
    uint32 lastPickupFailureSourceAtMs = 0;
};

std::unordered_map<uint32, bool> sCachedTwinSplitByX;
std::unordered_map<uint32, bool> sTwinSideZeroIsLowSide;
std::unordered_map<uint32, bool> sCachedTwinVeklorIsLowSide;
std::unordered_map<uint32, uint32> sTwinLastVeklorSideByInstance;
std::unordered_map<uint32, uint32> sTwinLastVeklorSideChangedMsByInstance;
std::unordered_map<uint64, TwinMeleeRecoveryState> sTwinMeleeRecoveryStateByBot;
std::unordered_map<uint32, TwinMarkerState> sTwinMarkerStateByInstance;
std::unordered_map<uint32, TwinTeleportState> sTwinTeleportStateByInstance;
std::unordered_map<uint32, TwinPickupEstablishedState> sTwinPickupEstablishedStateByInstance;
std::unordered_map<uint32, TwinThreatHoldWindowState> sTwinThreatHoldStateByInstance;
std::unordered_map<uint64, TwinPickupAnchorState> sTwinPickupAnchorStateByBot;
std::unordered_map<uint32, TwinEncounterStateMachine> sTwinEncounterStateMachineByInstance;

// Emergency split recovery state (issue #3)
struct TwinEmergencySplitRecoveryState
{
    uint32 armedAtMs = 0;
    float armedAtSeparation = 0.0f;
    std::string reason;
};
std::unordered_map<uint32, TwinEmergencySplitRecoveryState> sTwinEmergencySplitRecoveryByInstance;

// Scripted spell event state (issue #6)
// Each event tracks the last cast time per instance so trigger/action code
// can react to explicit spell hooks instead of polling boss spell slots.
uint32 constexpr kTwinBlizzardEventWindowMs = 8000;
uint32 constexpr kTwinArcaneBurstEventWindowMs = 4000;
uint32 constexpr kTwinHealBrotherEventWindowMs = 10000;
uint32 constexpr kTwinUppercutEventWindowMs = 5000;
uint32 constexpr kTwinUnbalancingStrikeEventWindowMs = 5000;

struct TwinScriptedEventState
{
    uint32 lastBlizzardAtMs = 0;
    ObjectGuid lastBlizzardCasterGuid = ObjectGuid::Empty;
    uint32 lastArcaneBurstAtMs = 0;
    ObjectGuid lastArcaneBurstCasterGuid = ObjectGuid::Empty;
    uint32 lastHealBrotherAtMs = 0;
    ObjectGuid lastHealBrotherCasterGuid = ObjectGuid::Empty;
    uint32 lastUppercutAtMs = 0;
    ObjectGuid lastUppercutCasterGuid = ObjectGuid::Empty;
    uint32 lastUnbalancingStrikeAtMs = 0;
    ObjectGuid lastUnbalancingStrikeCasterGuid = ObjectGuid::Empty;
};
std::unordered_map<uint32, TwinScriptedEventState> sTwinScriptedEventStateByInstance;

bool IsScriptedEventActive(uint32 eventAtMs, uint32 windowMs, uint32 now, uint32* outElapsedMs)
{
    if (!eventAtMs)
        return false;

    uint32 const elapsed = now - eventAtMs;
    if (elapsed > windowMs)
        return false;

    if (outElapsedMs)
        *outElapsedMs = elapsed;

    return true;
}

uint32 constexpr kTwinOwnerStableRequiredMs = 2000;
float constexpr kTwinOwnerRangeVeklorMin = 19.0f;
float constexpr kTwinOwnerRangeVeklorMax = 30.0f;
float constexpr kTwinOwnerRangeVeknilashMin = 1.5f;
float constexpr kTwinOwnerRangeVeknilashMax = 8.0f;

uint32 GetTwinInstanceId(Player* bot)
{
    if (!bot || !bot->GetMap())
        return 0;

    return bot->GetMap()->GetInstanceId();
}

uint32 GetTwinInstanceId(Unit* unit)
{
    if (!unit || !unit->GetMap())
        return 0;

    return unit->GetMap()->GetInstanceId();
}

bool IsTwinTeleportStateActive(TwinTeleportState const& state, uint32 now)
{
    return state.armedAtMs != 0 && state.armedUntilMs > now;
}

bool HasActiveHoldWindow(uint32 untilMs, uint32 now)
{
    return untilMs != 0 && untilMs > now;
}

void ClearExpiredHoldWindows(TwinThreatHoldWindowState& state, uint32 now)
{
    if (!HasActiveHoldWindow(state.initialPullUntilMs, now))
        state.initialPullUntilMs = 0;
    if (!HasActiveHoldWindow(state.teleportUntilMs, now))
        state.teleportUntilMs = 0;
    if (!HasActiveHoldWindow(state.pickupFailedUntilMs, now))
        state.pickupFailedUntilMs = 0;
}

bool IsMarkerTargetValid(Player* bot, Unit* target)
{
    return bot && target && target->IsInWorld() && target->IsAlive() && target->GetMapId() == bot->GetMapId();
}

bool UpdateRaidMarker(Player* bot, uint8 iconIndex, Unit* target, ObjectGuid& stateGuid)
{
    Group* group = bot ? bot->GetGroup() : nullptr;
    if (!group)
        return false;

    ObjectGuid const desiredGuid = IsMarkerTargetValid(bot, target) ? target->GetGUID() : ObjectGuid::Empty;
    if (group->GetTargetIcon(iconIndex) == desiredGuid && stateGuid == desiredGuid)
        return false;

    group->SetTargetIcon(iconIndex, bot->GetGUID(), desiredGuid);
    stateGuid = desiredGuid;
    return true;
}

void NoteMeleeRecovery(Player* bot, uint32 durationMs, std::string const& reason)
{
    if (!bot)
        return;

    uint32 const now = getMSTime();
    TwinMeleeRecoveryState& state = sTwinMeleeRecoveryStateByBot[bot->GetGUID().GetRawValue()];
    state.instanceId = GetTwinInstanceId(bot);
    state.untilMs = std::max(state.untilMs, now + durationMs);
    state.reason = reason;
}

bool BossHasCurrentSpell(Unit* boss, uint32 spellId)
{
    if (!boss)
        return false;

    for (CurrentSpellTypes spellSlot : { CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL })
    {
        Spell* currentSpell = boss->GetCurrentSpell(spellSlot);
        if (!currentSpell || !currentSpell->GetSpellInfo())
            continue;

        if (currentSpell->GetSpellInfo()->Id == spellId)
            return true;
    }

    return false;
}

std::string ResolveLocalRti(Unit* target, Unit* veklor, Unit* veknilash, Unit* bugTarget)
{
    if (!target)
        return "";
    if (target == bugTarget || (target != veklor && target != veknilash))
        return "skull";
    if (target == veklor)
        return "square";
    if (target == veknilash)
        return "diamond";
    return "";
}

bool EvaluateOwnerStability(Player* owner, Unit* boss, bool isVeklor, Unit* sideEmperor, Unit* oppositeEmperor)
{
    if (!owner || !boss)
        return false;

    // 1. Target must be alive and in the world
    if (!owner->IsAlive() || !owner->IsInWorld())
        return false;

    // 2. Target must have aggro on the correct emperor
    if (!HasBossPickupAggro(owner, boss))
        return false;

    // 3. Range window is correct for the emperor type
    float const distance = owner->GetDistance2d(boss);
    if (isVeklor)
    {
        if (distance < kTwinOwnerRangeVeklorMin || distance > kTwinOwnerRangeVeklorMax)
            return false;
    }
    else
    {
        if (distance < kTwinOwnerRangeVeknilashMin || distance > kTwinOwnerRangeVeknilashMax)
            return false;
    }

    // 4. LOS is valid
    if (!owner->IsWithinLOSInMap(boss))
        return false;

    // 5. Target is on the correct side (closer to their emperor than the opposite)
    if (sideEmperor && oppositeEmperor)
    {
        float const distToSide = owner->GetDistance2d(sideEmperor);
        float const distToOpposite = owner->GetDistance2d(oppositeEmperor);
        if (distToOpposite < distToSide)
            return false;
    }

    return true;
}

void UpdateOwnershipTracking(TwinBossOwnership& ownership, Player* bot, Unit* boss, bool isVeklor,
                             Unit* sideEmperor, Unit* oppositeEmperor, uint32 now)
{
    // Find candidate: anyone who currently has aggro
    ObjectGuid candidateGuid = ObjectGuid::Empty;
    for (ObjectGuid const& guid : { ownership.expectedOwnerGuid, ownership.reserveOwnerGuid })
    {
        if (!guid)
            continue;

        Player* member = ObjectAccessor::FindConnectedPlayer(guid);
        if (!member || !member->IsAlive())
            continue;

        if (HasBossPickupAggro(member, boss))
        {
            candidateGuid = guid;
            break;
        }
    }
    ownership.candidateOwnerGuid = candidateGuid;

    // Evaluate stability of the candidate
    Player* candidate = candidateGuid ? ObjectAccessor::FindConnectedPlayer(candidateGuid) : nullptr;
    if (candidate && EvaluateOwnerStability(candidate, boss, isVeklor, sideEmperor, oppositeEmperor))
    {
        if (ownership.stableOwnerGuid != candidateGuid)
        {
            // New stable owner candidate — start fresh stability timer
            ownership.stableOwnerGuid = candidateGuid;
            ownership.stableSinceMs = now;
        }
        ownership.lastValidAtMs = now;
    }
    else
    {
        // Stability broken — clear stable tracking
        ownership.stableOwnerGuid = ObjectGuid::Empty;
        ownership.stableSinceMs = 0;
        ownership.lastValidAtMs = 0;
    }
}
}  // namespace

SplitBand GetSplitBand(float separation)
{
    if (separation < kTwinSplitTerminalDistance)
        return SplitBand::Terminal;
    if (separation < kTwinSplitUrgentDistance)
        return SplitBand::Urgent;
    if (separation < kTwinSplitWarningDistance)
        return SplitBand::Warning;
    return SplitBand::Stable;
}

void NoteTwinEncounterActive(Player* bot)
{
    if (!bot || !bot->GetMap())
        return;

    uint32 const instanceId = GetTwinInstanceId(bot);
    if (!instanceId)
        return;

    uint32 const now = getMSTime();
    TwinThreatHoldWindowState& state = sTwinThreatHoldStateByInstance[instanceId];
    ClearExpiredHoldWindows(state, now);
    if (state.initialPullOpenedAtMs != 0)
        return;

    state.initialPullOpenedAtMs = now;
    state.initialPullUntilMs = now + kTwinInitialPullHoldMs;
}

SideState ResolveSideState(Player* bot, Unit* veklor, Unit* veknilash)
{
    SideState state;
    state.separation = (veklor && veknilash) ? veklor->GetDistance2d(veknilash) : 0.0f;
    if (!bot || !veklor || !veknilash)
        return state;

    uint32 const instanceId = GetTwinInstanceId(bot);
    bool splitByX = std::abs(veklor->GetPositionX() - veknilash->GetPositionX()) >=
                    std::abs(veklor->GetPositionY() - veknilash->GetPositionY());
    auto axisItr = sCachedTwinSplitByX.find(instanceId);
    if (axisItr == sCachedTwinSplitByX.end() || state.separation > kTwinSplitWarningDistance)
    {
        sCachedTwinSplitByX[instanceId] = splitByX;
    }
    else
    {
        splitByX = axisItr->second;
    }

    float const veklorAxis = splitByX ? veklor->GetPositionX() : veklor->GetPositionY();
    float const veknilashAxis = splitByX ? veknilash->GetPositionX() : veknilash->GetPositionY();

    bool veklorIsLow = veklorAxis < veknilashAxis;
    auto lowSideItr = sCachedTwinVeklorIsLowSide.find(instanceId);
    if (lowSideItr == sCachedTwinVeklorIsLowSide.end() || state.separation > kTwinSplitWarningDistance)
    {
        sCachedTwinVeklorIsLowSide[instanceId] = veklorIsLow;
    }
    else
    {
        veklorIsLow = lowSideItr->second;
    }

    Unit* lowSideBoss = veklorIsLow ? veklor : veknilash;
    Unit* highSideBoss = veklorIsLow ? veknilash : veklor;

    bool sideZeroIsLowSide = (lowSideBoss == veknilash);
    auto sideMapItr = sTwinSideZeroIsLowSide.find(instanceId);
    if (sideMapItr == sTwinSideZeroIsLowSide.end())
    {
        sTwinSideZeroIsLowSide[instanceId] = sideZeroIsLowSide;
    }
    else
    {
        sideZeroIsLowSide = sideMapItr->second;
    }

    state.sideZeroBoss = sideZeroIsLowSide ? lowSideBoss : highSideBoss;
    state.sideOneBoss = sideZeroIsLowSide ? highSideBoss : lowSideBoss;
    state.veklorSideIndex = state.sideOneBoss == veklor ? 1u : 0u;
    state.veknilashSideIndex = state.sideOneBoss == veknilash ? 1u : 0u;

    uint32 const now = getMSTime();
    auto lastSideItr = sTwinLastVeklorSideByInstance.find(instanceId);
    if (lastSideItr == sTwinLastVeklorSideByInstance.end())
    {
        sTwinLastVeklorSideByInstance[instanceId] = state.veklorSideIndex;
        sTwinLastVeklorSideChangedMsByInstance[instanceId] = now;
    }
    else if (lastSideItr->second != state.veklorSideIndex)
    {
        lastSideItr->second = state.veklorSideIndex;
        sTwinLastVeklorSideChangedMsByInstance[instanceId] = now;
    }

    return state;
}

void NoteTwinTeleportCast(Unit* caster)
{
    if (!caster || !caster->GetMap())
        return;

    uint32 const instanceId = GetTwinInstanceId(caster);
    if (!instanceId)
        return;

    uint32 const now = getMSTime();
    TwinTeleportState& teleportState = sTwinTeleportStateByInstance[instanceId];
    teleportState.armedAtMs = now;
    teleportState.armedUntilMs = now + kTwinTeleportWindowMs;
    teleportState.lastCasterGuid = caster->GetGUID();

    sTwinPickupEstablishedStateByInstance[instanceId] = {};

    TwinThreatHoldWindowState& holdState = sTwinThreatHoldStateByInstance[instanceId];
    holdState.teleportOpenedAtMs = now;
    holdState.teleportUntilMs = now + kTwinTeleportHoldMs;
    holdState.pickupFailedOpenedAtMs = 0;
    holdState.pickupFailedUntilMs = 0;

    // Reset ownership stability on teleport — bosses swap sides
    auto smItr = sTwinEncounterStateMachineByInstance.find(instanceId);
    if (smItr != sTwinEncounterStateMachineByInstance.end())
    {
        smItr->second.veklor.stableOwnerGuid = ObjectGuid::Empty;
        smItr->second.veklor.stableSinceMs = 0;
        smItr->second.veklor.lastValidAtMs = 0;
        smItr->second.veklor.candidateOwnerGuid = ObjectGuid::Empty;
        smItr->second.veknilash.stableOwnerGuid = ObjectGuid::Empty;
        smItr->second.veknilash.stableSinceMs = 0;
        smItr->second.veknilash.lastValidAtMs = 0;
        smItr->second.veknilash.candidateOwnerGuid = ObjectGuid::Empty;
        smItr->second.reservePromotionUsedVeklor = false;
        smItr->second.reservePromotionUsedVeknilash = false;
    }

    for (auto itr = sTwinPickupAnchorStateByBot.begin(); itr != sTwinPickupAnchorStateByBot.end();)
    {
        if (itr->second.instanceId != instanceId)
        {
            ++itr;
            continue;
        }

        itr = sTwinPickupAnchorStateByBot.erase(itr);
    }
}

bool IsTwinTeleportWindowActive(Player* bot, uint32* outElapsedMs)
{
    if (outElapsedMs)
        *outElapsedMs = std::numeric_limits<uint32>::max();

    if (!bot || !bot->GetMap())
        return false;

    uint32 const instanceId = GetTwinInstanceId(bot);
    auto const itr = sTwinTeleportStateByInstance.find(instanceId);
    if (itr == sTwinTeleportStateByInstance.end())
        return false;

    uint32 const now = getMSTime();
    if (!IsTwinTeleportStateActive(itr->second, now))
        return false;

    if (outElapsedMs)
        *outElapsedMs = now - itr->second.armedAtMs;

    return true;
}

bool GetActiveThreatHold(Player* bot, ThreatHoldState& outState)
{
    outState = {};
    if (!bot || !bot->GetMap())
        return false;

    auto itr = sTwinThreatHoldStateByInstance.find(GetTwinInstanceId(bot));
    if (itr == sTwinThreatHoldStateByInstance.end())
        return false;

    uint32 const now = getMSTime();
    TwinThreatHoldWindowState& state = itr->second;
    ClearExpiredHoldWindows(state, now);

    if (HasActiveHoldWindow(state.teleportUntilMs, now))
    {
        outState.type = ThreatHoldType::Teleport;
        outState.openedAtMs = state.teleportOpenedAtMs;
        outState.untilMs = state.teleportUntilMs;
        return true;
    }

    if (HasActiveHoldWindow(state.pickupFailedUntilMs, now))
    {
        outState.type = ThreatHoldType::PickupFailed;
        outState.openedAtMs = state.pickupFailedOpenedAtMs;
        outState.untilMs = state.pickupFailedUntilMs;
        return true;
    }

    if (HasActiveHoldWindow(state.initialPullUntilMs, now))
    {
        outState.type = ThreatHoldType::InitialPull;
        outState.openedAtMs = state.initialPullOpenedAtMs;
        outState.untilMs = state.initialPullUntilMs;
        return true;
    }

    return false;
}

uint32 GetLatestPickupWindowOpenedAtMs(Player* bot)
{
    if (!bot || !bot->GetMap())
        return 0;

    auto const itr = sTwinThreatHoldStateByInstance.find(GetTwinInstanceId(bot));
    if (itr == sTwinThreatHoldStateByInstance.end())
        return 0;

    return std::max(itr->second.initialPullOpenedAtMs, itr->second.teleportOpenedAtMs);
}

bool ArmPickupFailedHold(Player* bot, uint32 sourceOpenedAtMs)
{
    if (!bot || !bot->GetMap() || !sourceOpenedAtMs)
        return false;

    TwinThreatHoldWindowState& state = sTwinThreatHoldStateByInstance[GetTwinInstanceId(bot)];
    if (state.lastPickupFailureSourceAtMs == sourceOpenedAtMs)
        return false;

    uint32 const now = getMSTime();
    state.lastPickupFailureSourceAtMs = sourceOpenedAtMs;
    state.pickupFailedOpenedAtMs = now;
    state.pickupFailedUntilMs = now + kTwinPickupFailedHoldMs;
    return true;
}

bool HasTwinMovementOwnershipState(Player* bot)
{
    if (!bot)
        return false;

    if (HasLockedPickupAnchor(bot))
        return true;

    ThreatHoldState holdState;
    return GetActiveThreatHold(bot, holdState);
}

void NoteTwinPickupEstablished(Player* bot, bool isVeklor)
{
    if (!bot || !bot->GetMap())
        return;

    uint32 const instanceId = GetTwinInstanceId(bot);
    TwinPickupEstablishedState& state = sTwinPickupEstablishedStateByInstance[instanceId];
    if (isVeklor)
    {
        state.veklorEstablishedAtMs = getMSTime();
        TwinThreatHoldWindowState& holdState = sTwinThreatHoldStateByInstance[instanceId];
        holdState.initialPullUntilMs = 0;
        holdState.teleportUntilMs = 0;
        holdState.pickupFailedUntilMs = 0;
    }
    else
        state.veknilashEstablishedAtMs = getMSTime();
}

bool HasTwinPickupEstablished(Player* bot, bool isVeklor)
{
    if (!bot || !bot->GetMap())
        return false;

    auto const itr = sTwinPickupEstablishedStateByInstance.find(GetTwinInstanceId(bot));
    if (itr == sTwinPickupEstablishedStateByInstance.end())
        return false;

    uint32 const establishedAtMs = isVeklor ? itr->second.veklorEstablishedAtMs : itr->second.veknilashEstablishedAtMs;
    if (!establishedAtMs)
        return false;

    return getMSTimeDiff(establishedAtMs, getMSTime()) < kTwinPickupEstablishedMemoryMs;
}

void RememberTwinPickupAnchor(Player* bot, Unit* boss, uint32 sideIndex, Position const& anchor)
{
    if (!bot || !bot->GetMap())
        return;

    uint32 lockUntilMs = getMSTime() + kTwinPickupAnchorLockMs;
    auto const teleportItr = sTwinTeleportStateByInstance.find(GetTwinInstanceId(bot));
    if (teleportItr != sTwinTeleportStateByInstance.end() &&
        IsTwinTeleportStateActive(teleportItr->second, getMSTime()))
    {
        lockUntilMs = std::min(lockUntilMs, teleportItr->second.armedUntilMs);
    }

    TwinPickupAnchorState& state = sTwinPickupAnchorStateByBot[bot->GetGUID().GetRawValue()];
    state.instanceId = GetTwinInstanceId(bot);
    state.sideIndex = sideIndex;
    state.untilMs = lockUntilMs;
    state.bossGuid = boss ? boss->GetGUID() : ObjectGuid::Empty;
    state.anchor = anchor;
}

bool GetTwinLockedPickupAnchor(Player* bot, Unit* boss, uint32 sideIndex, Position& outAnchor)
{
    if (!bot || !bot->GetMap())
        return false;

    auto itr = sTwinPickupAnchorStateByBot.find(bot->GetGUID().GetRawValue());
    if (itr == sTwinPickupAnchorStateByBot.end())
        return false;

    TwinPickupAnchorState const& state = itr->second;
    if (state.instanceId != GetTwinInstanceId(bot) || state.sideIndex != sideIndex || state.untilMs <= getMSTime())
    {
        sTwinPickupAnchorStateByBot.erase(itr);
        return false;
    }

    if (boss && state.bossGuid && state.bossGuid != boss->GetGUID())
        return false;

    outAnchor = state.anchor;
    return true;
}

bool HasLockedPickupAnchor(Player* bot)
{
    if (!bot || !bot->GetMap())
        return false;

    auto itr = sTwinPickupAnchorStateByBot.find(bot->GetGUID().GetRawValue());
    if (itr == sTwinPickupAnchorStateByBot.end())
        return false;

    if (itr->second.instanceId != GetTwinInstanceId(bot) || itr->second.untilMs <= getMSTime())
    {
        sTwinPickupAnchorStateByBot.erase(itr);
        return false;
    }

    return true;
}

void ClearTwinPickupState(Player* bot)
{
    if (!bot)
        return;

    sTwinPickupAnchorStateByBot.erase(bot->GetGUID().GetRawValue());
}

uint32 GetPostSwapElapsedMs(Player* bot, uint32 veklorSideIndex)
{
    if (!bot || !bot->GetMap())
        return std::numeric_limits<uint32>::max();

    uint32 const instanceId = GetTwinInstanceId(bot);
    uint32 const now = getMSTime();
    auto lastSideItr = sTwinLastVeklorSideByInstance.find(instanceId);
    auto changedItr = sTwinLastVeklorSideChangedMsByInstance.find(instanceId);
    auto teleportItr = sTwinTeleportStateByInstance.find(instanceId);
    if (teleportItr != sTwinTeleportStateByInstance.end() && IsTwinTeleportStateActive(teleportItr->second, now))
    {
        if (lastSideItr == sTwinLastVeklorSideByInstance.end() || changedItr == sTwinLastVeklorSideChangedMsByInstance.end() ||
            changedItr->second < teleportItr->second.armedAtMs)
            return now - teleportItr->second.armedAtMs;
    }

    if (lastSideItr == sTwinLastVeklorSideByInstance.end())
        return std::numeric_limits<uint32>::max();

    if (lastSideItr->second != veklorSideIndex)
        return 0;

    if (changedItr == sTwinLastVeklorSideChangedMsByInstance.end())
        return std::numeric_limits<uint32>::max();

    return now - changedItr->second;
}

bool HasBossPickupAggro(Player* member, Unit* boss)
{
    if (!member || !boss)
        return false;

    ObjectGuid const memberGuid = member->GetGUID();
    Pet* pet = member->GetPet();
    ObjectGuid const petGuid = pet ? pet->GetGUID() : ObjectGuid::Empty;

    return boss->GetVictim() == member ||
           boss->GetTarget() == memberGuid ||
           (petGuid && boss->GetTarget() == petGuid) ||
           (pet && boss->GetVictim() == pet);
}

bool IsPickupWindowSatisfied(Player* member, Unit* boss, bool isVeklor)
{
    if (!member || !boss || !member->IsAlive() || !boss->IsAlive() || !member->IsWithinLOSInMap(boss))
        return false;

    float const distance = member->GetDistance2d(boss);
    if (isVeklor)
        return distance >= 19.0f && distance <= 30.0f;

    return distance >= 1.5f && distance <= 8.0f;
}

bool RefreshMeleeRecoveryState(Player* bot, PlayerbotAI* botAI, Unit* veknilash, uint32 veklorSideIndex,
                               bool pickupEstablished, std::string* outReason)
{
    if (!bot || !botAI || !veknilash)
        return false;

    if (BossHasCurrentSpell(veknilash, Aq40SpellIds::TwinTeleport))
        NoteMeleeRecovery(bot, kTwinTeleportRecoveryMs, "teleport");
    if (BossHasCurrentSpell(veknilash, Aq40SpellIds::TwinUppercut))
        NoteMeleeRecovery(bot, kTwinUppercutRecoveryMs, "uppercut");
    if (BossHasCurrentSpell(veknilash, Aq40SpellIds::TwinUnbalancingStrike))
        NoteMeleeRecovery(bot, kTwinUnbalancingRecoveryMs, "unbalancing_strike");
    if (botAI->HasAura(Aq40SpellIds::TwinUnbalancingStrike, bot))
        NoteMeleeRecovery(bot, kTwinUnbalancingRecoveryMs, "unbalancing_strike");

    uint32 const elapsed = GetPostSwapElapsedMs(bot, veklorSideIndex);
    if (!pickupEstablished && elapsed != std::numeric_limits<uint32>::max() && elapsed <= 5000)
        NoteMeleeRecovery(bot, kTwinPostSwapRecoveryMs, "post_swap");

    auto stateItr = sTwinMeleeRecoveryStateByBot.find(bot->GetGUID().GetRawValue());
    if (stateItr == sTwinMeleeRecoveryStateByBot.end())
        return false;

    uint32 const now = getMSTime();
    if (stateItr->second.instanceId != GetTwinInstanceId(bot) || stateItr->second.untilMs <= now)
    {
        sTwinMeleeRecoveryStateByBot.erase(stateItr);
        return false;
    }

    if (outReason)
        *outReason = stateItr->second.reason;

    return true;
}

bool PublishRaidMarkers(Player* bot, PlayerbotAI* botAI, Unit* veklor, Unit* veknilash, Unit* bugTarget)
{
    if (!bot || !botAI || !bot->GetMap() || !bot->GetGroup())
        return false;
    if (!IsMechanicTrackerBot(botAI, bot, bot->GetMapId(), nullptr))
        return false;

    TwinMarkerState& markerState = sTwinMarkerStateByInstance[GetTwinInstanceId(bot)];
    bool changed = false;
    changed = UpdateRaidMarker(bot, RtiTargetValue::squareIndex, veklor, markerState.squareGuid) || changed;
    changed = UpdateRaidMarker(bot, RtiTargetValue::diamondIndex, veknilash, markerState.diamondGuid) || changed;
    changed = UpdateRaidMarker(bot, RtiTargetValue::skullIndex, bugTarget, markerState.skullGuid) || changed;
    return changed;
}

void ClearLocalRti(PlayerbotAI* botAI)
{
    if (!botAI || !botAI->GetAiObjectContext())
        return;

    botAI->GetAiObjectContext()->GetValue<std::string>("rti")->Set("");
    botAI->GetAiObjectContext()->GetValue<Unit*>("rti target")->Set(nullptr);
}

void SyncLocalRti(PlayerbotAI* botAI, Unit* target, Unit* veklor, Unit* veknilash, Unit* bugTarget)
{
    if (!botAI || !botAI->GetAiObjectContext() || !target)
    {
        ClearLocalRti(botAI);
        return;
    }

    std::string const desiredRti = ResolveLocalRti(target, veklor, veknilash, bugTarget);
    if (desiredRti.empty())
    {
        ClearLocalRti(botAI);
        return;
    }

    botAI->GetAiObjectContext()->GetValue<std::string>("rti")->Set(desiredRti);
    botAI->GetAiObjectContext()->GetValue<Unit*>("rti target")->Set(target);
}

std::string GetPhaseToken(TwinEncounterPhase phase)
{
    switch (phase)
    {
        case TwinEncounterPhase::PrePull:                return "pre_pull";
        case TwinEncounterPhase::Stable:                 return "stable";
        case TwinEncounterPhase::TeleportWindow:         return "teleport_window";
        case TwinEncounterPhase::PickupRecovery:         return "pickup_recovery";
        case TwinEncounterPhase::EmergencySplitRecovery: return "emergency_split_recovery";
        case TwinEncounterPhase::Degraded:               return "degraded";
        default:                                         return "unknown";
    }
}

TwinEncounterPhase GetEncounterPhase(Player* bot)
{
    if (!bot || !bot->GetMap())
        return TwinEncounterPhase::PrePull;

    uint32 const instanceId = GetTwinInstanceId(bot);
    auto const itr = sTwinEncounterStateMachineByInstance.find(instanceId);
    if (itr == sTwinEncounterStateMachineByInstance.end())
        return TwinEncounterPhase::PrePull;

    return itr->second.phase;
}

TwinEncounterStateMachine const* GetEncounterStateMachine(Player* bot)
{
    if (!bot || !bot->GetMap())
        return nullptr;

    uint32 const instanceId = GetTwinInstanceId(bot);
    auto const itr = sTwinEncounterStateMachineByInstance.find(instanceId);
    if (itr == sTwinEncounterStateMachineByInstance.end())
        return nullptr;

    return &itr->second;
}

bool IsBossOwnerStable(Player* bot, bool isVeklor)
{
    if (!bot || !bot->GetMap())
        return false;

    uint32 const instanceId = GetTwinInstanceId(bot);
    auto const itr = sTwinEncounterStateMachineByInstance.find(instanceId);
    if (itr == sTwinEncounterStateMachineByInstance.end())
        return false;

    TwinBossOwnership const& ownership = isVeklor ? itr->second.veklor : itr->second.veknilash;
    if (!ownership.stableOwnerGuid || !ownership.stableSinceMs)
        return false;

    uint32 const now = getMSTime();
    return (now - ownership.stableSinceMs) >= kTwinOwnerStableRequiredMs;
}

bool AreBothOwnersStable(Player* bot)
{
    return IsBossOwnerStable(bot, true) && IsBossOwnerStable(bot, false);
}

bool ShouldReleaseThreatHold(Player* bot, float separation, bool healBrotherActive)
{
    if (!bot || !bot->GetMap())
        return false;

    // All three criteria must be met for hold release:
    // 1. Both emperors have stable owners
    if (!AreBothOwnersStable(bot))
        return false;

    // 2. Current split is above the urgent band
    SplitBand const band = GetSplitBand(separation);
    if (band == SplitBand::Urgent || band == SplitBand::Terminal)
        return false;

    // 3. No active Heal Brother emergency
    if (healBrotherActive)
        return false;

    return true;
}

void UpdateBossOwnership(Player* bot, bool isVeklor, ObjectGuid const& expectedGuid,
                         ObjectGuid const& reserveGuid, ObjectGuid const& candidateGuid,
                         Unit* boss)
{
    if (!bot || !bot->GetMap())
        return;

    uint32 const instanceId = GetTwinInstanceId(bot);
    TwinEncounterStateMachine& state = sTwinEncounterStateMachineByInstance[instanceId];
    TwinBossOwnership& ownership = isVeklor ? state.veklor : state.veknilash;

    ownership.expectedOwnerGuid = expectedGuid;
    ownership.reserveOwnerGuid = reserveGuid;
    ownership.candidateOwnerGuid = candidateGuid;
}

void ProcessReserveTakeover(Player* bot, bool isVeklor)
{
    if (!bot || !bot->GetMap())
        return;

    uint32 const instanceId = GetTwinInstanceId(bot);
    auto itr = sTwinEncounterStateMachineByInstance.find(instanceId);
    if (itr == sTwinEncounterStateMachineByInstance.end())
        return;

    TwinEncounterStateMachine& state = itr->second;
    TwinBossOwnership& ownership = isVeklor ? state.veklor : state.veknilash;
    bool& promotionUsed = isVeklor ? state.reservePromotionUsedVeklor : state.reservePromotionUsedVeknilash;

    if (promotionUsed)
        return;

    // Check if primary owner (expected) is dead or invalid
    Player* expectedOwner = ownership.expectedOwnerGuid
        ? ObjectAccessor::FindConnectedPlayer(ownership.expectedOwnerGuid)
        : nullptr;

    bool const expectedInvalid = !expectedOwner || !expectedOwner->IsAlive() || !expectedOwner->IsInWorld();
    if (!expectedInvalid)
        return;

    if (!ownership.reserveOwnerGuid)
        return;

    // Promote reserve to expected
    ownership.expectedOwnerGuid = ownership.reserveOwnerGuid;
    ownership.reserveOwnerGuid = ObjectGuid::Empty;
    ownership.stableOwnerGuid = ObjectGuid::Empty;
    ownership.stableSinceMs = 0;
    ownership.lastValidAtMs = 0;
    promotionUsed = true;
}

void UpdateEncounterPhase(Player* bot, Unit* veklor, Unit* veknilash, float separation,
                          bool encounterLive, bool healBrotherActive)
{
    if (!bot || !bot->GetMap())
        return;

    uint32 const instanceId = GetTwinInstanceId(bot);
    uint32 const now = getMSTime();
    TwinEncounterStateMachine& state = sTwinEncounterStateMachineByInstance[instanceId];

    auto setPhase = [&](TwinEncounterPhase newPhase)
    {
        if (state.phase != newPhase)
        {
            state.phase = newPhase;
            state.phaseChangedAtMs = now;
        }
    };

    // Update ownership tracking for both bosses
    if (veklor && veknilash)
    {
        UpdateOwnershipTracking(state.veklor, bot, veklor, true, veklor, veknilash, now);
        UpdateOwnershipTracking(state.veknilash, bot, veknilash, false, veknilash, veklor, now);

        // Check for reserve takeover if encounter is live
        if (encounterLive)
        {
            ProcessReserveTakeover(bot, true);
            ProcessReserveTakeover(bot, false);
        }
    }

    // Phase transition logic
    if (!encounterLive)
    {
        setPhase(TwinEncounterPhase::PrePull);
        return;
    }

    // Check for teleport window
    if (IsTwinTeleportWindowActive(bot, nullptr))
    {
        setPhase(TwinEncounterPhase::TeleportWindow);
        return;
    }

    // Check for degraded mode: missing expected owners
    bool const veklorHasExpected = state.veklor.expectedOwnerGuid.IsEmpty() == false;
    bool const veknilashHasExpected = state.veknilash.expectedOwnerGuid.IsEmpty() == false;
    if (!veklorHasExpected || !veknilashHasExpected)
    {
        setPhase(TwinEncounterPhase::Degraded);
        return;
    }

    // Check for emergency split recovery
    SplitBand const band = GetSplitBand(separation);
    if (band == SplitBand::Terminal)
    {
        setPhase(TwinEncounterPhase::EmergencySplitRecovery);
        return;
    }

    // Check if we need pickup recovery
    ThreatHoldState holdState;
    bool const holdActive = GetActiveThreatHold(bot, holdState);
    bool const bothStable = AreBothOwnersStable(bot);

    if (holdActive && !ShouldReleaseThreatHold(bot, separation, healBrotherActive))
    {
        setPhase(TwinEncounterPhase::PickupRecovery);
        return;
    }

    if (!bothStable && (holdActive || !state.veklor.stableOwnerGuid || !state.veknilash.stableOwnerGuid))
    {
        setPhase(TwinEncounterPhase::PickupRecovery);
        return;
    }

    // Everything is stable
    setPhase(TwinEncounterPhase::Stable);
}

bool ResetState(Player* bot)
{
    if (!bot || !bot->GetMap())
        return false;

    uint32 const instanceId = GetTwinInstanceId(bot);
    bool erased = false;

    erased = sCachedTwinSplitByX.erase(instanceId) > 0 || erased;
    erased = sTwinSideZeroIsLowSide.erase(instanceId) > 0 || erased;
    erased = sCachedTwinVeklorIsLowSide.erase(instanceId) > 0 || erased;
    erased = sTwinLastVeklorSideByInstance.erase(instanceId) > 0 || erased;
    erased = sTwinLastVeklorSideChangedMsByInstance.erase(instanceId) > 0 || erased;
    erased = sTwinTeleportStateByInstance.erase(instanceId) > 0 || erased;
    erased = sTwinPickupEstablishedStateByInstance.erase(instanceId) > 0 || erased;
    erased = sTwinThreatHoldStateByInstance.erase(instanceId) > 0 || erased;
    erased = sTwinEncounterStateMachineByInstance.erase(instanceId) > 0 || erased;
    erased = sTwinEmergencySplitRecoveryByInstance.erase(instanceId) > 0 || erased;
    erased = sTwinScriptedEventStateByInstance.erase(instanceId) > 0 || erased;

    auto markerItr = sTwinMarkerStateByInstance.find(instanceId);
    if (markerItr != sTwinMarkerStateByInstance.end())
    {
        if (Group* group = bot->GetGroup())
        {
            group->SetTargetIcon(RtiTargetValue::squareIndex, bot->GetGUID(), ObjectGuid::Empty);
            group->SetTargetIcon(RtiTargetValue::diamondIndex, bot->GetGUID(), ObjectGuid::Empty);
            group->SetTargetIcon(RtiTargetValue::skullIndex, bot->GetGUID(), ObjectGuid::Empty);
        }

        sTwinMarkerStateByInstance.erase(markerItr);
        erased = true;
    }

    for (auto itr = sTwinMeleeRecoveryStateByBot.begin(); itr != sTwinMeleeRecoveryStateByBot.end();)
    {
        if (itr->second.instanceId != instanceId)
        {
            ++itr;
            continue;
        }

        itr = sTwinMeleeRecoveryStateByBot.erase(itr);
        erased = true;
    }

    for (auto itr = sTwinPickupAnchorStateByBot.begin(); itr != sTwinPickupAnchorStateByBot.end();)
    {
        if (itr->second.instanceId != instanceId)
        {
            ++itr;
            continue;
        }

        itr = sTwinPickupAnchorStateByBot.erase(itr);
        erased = true;
    }

    return erased;
}

bool HasPersistentState(Player* bot)
{
    if (!bot || !bot->GetMap())
        return false;

    uint32 const instanceId = GetTwinInstanceId(bot);
    if (sCachedTwinSplitByX.find(instanceId) != sCachedTwinSplitByX.end() ||
        sTwinSideZeroIsLowSide.find(instanceId) != sTwinSideZeroIsLowSide.end() ||
        sCachedTwinVeklorIsLowSide.find(instanceId) != sCachedTwinVeklorIsLowSide.end() ||
        sTwinLastVeklorSideByInstance.find(instanceId) != sTwinLastVeklorSideByInstance.end() ||
        sTwinLastVeklorSideChangedMsByInstance.find(instanceId) != sTwinLastVeklorSideChangedMsByInstance.end() ||
        sTwinMarkerStateByInstance.find(instanceId) != sTwinMarkerStateByInstance.end() ||
        sTwinTeleportStateByInstance.find(instanceId) != sTwinTeleportStateByInstance.end() ||
        sTwinPickupEstablishedStateByInstance.find(instanceId) != sTwinPickupEstablishedStateByInstance.end() ||
        sTwinThreatHoldStateByInstance.find(instanceId) != sTwinThreatHoldStateByInstance.end() ||
        sTwinEncounterStateMachineByInstance.find(instanceId) != sTwinEncounterStateMachineByInstance.end() ||
        sTwinScriptedEventStateByInstance.find(instanceId) != sTwinScriptedEventStateByInstance.end())
    {
        return true;
    }

    for (auto const& [_, state] : sTwinMeleeRecoveryStateByBot)
    {
        if (state.instanceId == instanceId)
            return true;
    }

    for (auto const& [_, state] : sTwinPickupAnchorStateByBot)
    {
        if (state.instanceId == instanceId)
            return true;
    }

    return false;
}

// --- Emergency split recovery (issue #3) ---

bool IsEmergencySplitRecoveryActive(Player* bot)
{
    if (!bot || !bot->GetMap())
        return false;

    uint32 const instanceId = GetTwinInstanceId(bot);
    auto const itr = sTwinEmergencySplitRecoveryByInstance.find(instanceId);
    return itr != sTwinEmergencySplitRecoveryByInstance.end() && itr->second.armedAtMs != 0;
}

void ArmEmergencySplitRecovery(Player* bot, float separation, std::string const& reason)
{
    if (!bot || !bot->GetMap())
        return;

    uint32 const instanceId = GetTwinInstanceId(bot);
    TwinEmergencySplitRecoveryState& state = sTwinEmergencySplitRecoveryByInstance[instanceId];
    if (state.armedAtMs != 0)
        return;  // Already armed

    state.armedAtMs = getMSTime();
    state.armedAtSeparation = separation;
    state.reason = reason;
}

void ClearEmergencySplitRecovery(Player* bot)
{
    if (!bot || !bot->GetMap())
        return;

    sTwinEmergencySplitRecoveryByInstance.erase(GetTwinInstanceId(bot));
}

bool GetEmergencySplitRecoveryAnchor(Player* bot, Unit* boss, bool isMeleeOwner, Position& outAnchor)
{
    if (!bot || !boss || !bot->GetMap())
        return false;

    // Push the boss toward its side anchor: tank stands on the far side from
    // room center so the boss faces center and gets pulled outward.
    float const dirX = boss->GetPositionX() - kTwinRoomCenterX;
    float const dirY = boss->GetPositionY() - kTwinRoomCenterY;
    float const length = std::sqrt(dirX * dirX + dirY * dirY);
    if (length < 0.1f)
        return false;

    float const desiredRange = isMeleeOwner ? 3.0f : 22.0f;
    float targetX = boss->GetPositionX() + (dirX / length) * desiredRange;
    float targetY = boss->GetPositionY() + (dirY / length) * desiredRange;
    float targetZ = boss->GetPositionZ();

    if (!bot->GetMap()->CheckCollisionAndGetValidCoords(bot, bot->GetPositionX(), bot->GetPositionY(),
            bot->GetPositionZ(), targetX, targetY, targetZ))
        return false;

    outAnchor.Relocate(targetX, targetY, targetZ);
    return true;
}

// --- Teleport-prep receiving geometry (issue #3) ---

bool GetWarlockReceivingAnchor(Player* bot, Unit* veklor, Unit* veknilash,
                               uint32 receivingSideIndex, Position& outAnchor)
{
    if (!bot || !bot->GetMap())
        return false;

    // Use the veknilash position as reference for the "receiving" side center,
    // since after teleport Vek'lor will arrive where Vek'nilash currently is.
    Unit* receivingSideRef = veknilash;  // Boss currently on the receiving side
    if (!receivingSideRef)
        return false;

    // Stand inward toward center at desired range from where the boss will appear
    float const dirX = kTwinRoomCenterX - receivingSideRef->GetPositionX();
    float const dirY = kTwinRoomCenterY - receivingSideRef->GetPositionY();
    float const length = std::sqrt(dirX * dirX + dirY * dirY);
    if (length < 0.1f)
        return false;

    float targetX = receivingSideRef->GetPositionX() + (dirX / length) * kTwinWarlockReceivingDesiredRange;
    float targetY = receivingSideRef->GetPositionY() + (dirY / length) * kTwinWarlockReceivingDesiredRange;
    float targetZ = receivingSideRef->GetPositionZ();

    if (!bot->GetMap()->CheckCollisionAndGetValidCoords(bot, bot->GetPositionX(), bot->GetPositionY(),
            bot->GetPositionZ(), targetX, targetY, targetZ))
        return false;

    outAnchor.Relocate(targetX, targetY, targetZ);
    return true;
}

bool GetMeleeReceivingAnchor(Player* bot, Unit* veklor, Unit* veknilash,
                             uint32 receivingSideIndex, Position& outAnchor)
{
    if (!bot || !bot->GetMap())
        return false;

    // After teleport, Vek'nilash will arrive where Vek'lor currently is.
    Unit* receivingSideRef = veklor;  // Boss currently on the receiving side
    if (!receivingSideRef)
        return false;

    // Stand inward toward center at melee range from where the boss will appear
    float const dirX = kTwinRoomCenterX - receivingSideRef->GetPositionX();
    float const dirY = kTwinRoomCenterY - receivingSideRef->GetPositionY();
    float const length = std::sqrt(dirX * dirX + dirY * dirY);
    if (length < 0.1f)
        return false;

    float targetX = receivingSideRef->GetPositionX() + (dirX / length) * kTwinMeleeReceivingDesiredRange;
    float targetY = receivingSideRef->GetPositionY() + (dirY / length) * kTwinMeleeReceivingDesiredRange;
    float targetZ = receivingSideRef->GetPositionZ();

    // Validate Arcane Burst safety: must stay >16y from Vek'lor
    if (veklor)
    {
        float const distToVeklor = veklor->GetDistance2d(targetX, targetY);
        if (distToVeklor < kTwinArcaneBurstSafetyRange)
        {
            // Adjust position outward along the direction from veklor
            float const pushDirX = targetX - veklor->GetPositionX();
            float const pushDirY = targetY - veklor->GetPositionY();
            float const pushLength = std::sqrt(pushDirX * pushDirX + pushDirY * pushDirY);
            if (pushLength > 0.1f)
            {
                float const pushDist = kTwinArcaneBurstSafetyRange - distToVeklor + 2.0f;
                targetX += (pushDirX / pushLength) * pushDist;
                targetY += (pushDirY / pushLength) * pushDist;
            }
        }
    }

    if (!bot->GetMap()->CheckCollisionAndGetValidCoords(bot, bot->GetPositionX(), bot->GetPositionY(),
            bot->GetPositionZ(), targetX, targetY, targetZ))
        return false;

    outAnchor.Relocate(targetX, targetY, targetZ);
    return true;
}

// --- Vek'nilash wall-facing orientation (issue #3) ---

bool GetVeknilashWallFacingAnchor(Player* bot, Unit* veknilash, uint32 sideIndex,
                                  Position& outAnchor)
{
    if (!bot || !veknilash || !bot->GetMap())
        return false;

    // Tank should stand between the boss and the nearest wall so that
    // the boss faces the wall.  Uppercut knockback then sends the tank
    // toward room center (safe) instead of into wall geometry.
    // Direction from center to boss = toward wall; tank stands past the boss
    // in that direction.
    float const dirX = veknilash->GetPositionX() - kTwinRoomCenterX;
    float const dirY = veknilash->GetPositionY() - kTwinRoomCenterY;
    float const length = std::sqrt(dirX * dirX + dirY * dirY);
    if (length < 0.1f)
        return false;

    // Stand beyond the boss toward the wall at melee range
    float targetX = veknilash->GetPositionX() + (dirX / length) * kTwinWallFacingTankRange;
    float targetY = veknilash->GetPositionY() + (dirY / length) * kTwinWallFacingTankRange;
    float targetZ = veknilash->GetPositionZ();

    if (!bot->GetMap()->CheckCollisionAndGetValidCoords(bot, bot->GetPositionX(), bot->GetPositionY(),
            bot->GetPositionZ(), targetX, targetY, targetZ))
        return false;

    outAnchor.Relocate(targetX, targetY, targetZ);
    return true;
}

// --- Stop all raid damage on a boss (issue #3) ---

void StopAllRaidDamageOnBoss(Player* bot, Unit* boss)
{
    if (!bot || !boss || !bot->GetGroup())
        return;

    Group const* group = bot->GetGroup();
    uint32 const instanceId = GetTwinInstanceId(bot);

    for (GroupReference const* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || !member->IsAlive())
            continue;
        if (member->GetMapId() != bot->GetMapId())
            continue;
        if (member->GetMap() && member->GetMap()->GetInstanceId() != instanceId)
            continue;

        // Stop melee attack
        if (member->GetVictim() == boss)
            member->AttackStop();
        if (member->GetTarget() == boss->GetGUID())
            member->SetTarget(ObjectGuid::Empty);

        // Stop pet
        if (Pet* pet = member->GetPet())
        {
            if (pet->GetVictim() == boss || pet->GetTarget() == boss->GetGUID())
            {
                pet->AttackStop();
                pet->SetTarget(ObjectGuid::Empty);
                if (CharmInfo* charmInfo = pet->GetCharmInfo())
                    charmInfo->SetIsCommandAttack(false);
            }
        }

        // Clear pinned target if it points to this boss
        PlayerbotAI* memberAI = GET_PLAYERBOT_AI(member);
        if (memberAI && memberAI->GetAiObjectContext())
        {
            Unit* currentTarget = memberAI->GetAiObjectContext()->GetValue<Unit*>("current target")->Get();
            if (currentTarget == boss)
            {
                memberAI->GetAiObjectContext()->GetValue<Unit*>("current target")->Set(nullptr);
                GuidVector emptyTargets;
                memberAI->GetAiObjectContext()->GetValue<GuidVector>("prioritized targets")->Set(emptyTargets);
                memberAI->GetAiObjectContext()->GetValue<ObjectGuid>("pull target")->Set(ObjectGuid::Empty);
            }
            memberAI->RequestSpellInterrupt();
        }
    }
}

// --- Spell-driven event notifications (issue #6) ---

void NoteTwinBlizzardCast(Unit* caster)
{
    if (!caster || !caster->GetMap())
        return;

    uint32 const instanceId = GetTwinInstanceId(caster);
    if (!instanceId)
        return;

    uint32 const now = getMSTime();
    TwinScriptedEventState& state = sTwinScriptedEventStateByInstance[instanceId];
    state.lastBlizzardAtMs = now;
    state.lastBlizzardCasterGuid = caster->GetGUID();
}

void NoteTwinArcaneBurstCast(Unit* caster)
{
    if (!caster || !caster->GetMap())
        return;

    uint32 const instanceId = GetTwinInstanceId(caster);
    if (!instanceId)
        return;

    uint32 const now = getMSTime();
    TwinScriptedEventState& state = sTwinScriptedEventStateByInstance[instanceId];
    state.lastArcaneBurstAtMs = now;
    state.lastArcaneBurstCasterGuid = caster->GetGUID();
}

void NoteTwinHealBrotherCast(Unit* caster)
{
    if (!caster || !caster->GetMap())
        return;

    uint32 const instanceId = GetTwinInstanceId(caster);
    if (!instanceId)
        return;

    uint32 const now = getMSTime();
    TwinScriptedEventState& state = sTwinScriptedEventStateByInstance[instanceId];
    state.lastHealBrotherAtMs = now;
    state.lastHealBrotherCasterGuid = caster->GetGUID();

    // Heal Brother is a terminal event — arm emergency split recovery immediately.
    // This fires from the script hook so recovery is armed before the next
    // action-engine tick, not after the damage has already landed.
    auto smItr = sTwinEncounterStateMachineByInstance.find(instanceId);
    if (smItr != sTwinEncounterStateMachineByInstance.end())
    {
        auto& emergencyState = sTwinEmergencySplitRecoveryByInstance[instanceId];
        if (emergencyState.armedAtMs == 0)
        {
            emergencyState.armedAtMs = now;
            emergencyState.armedAtSeparation = 0.0f;  // Unknown from script context
            emergencyState.reason = "heal_brother_script";
        }
    }
}

void NoteTwinUppercutCast(Unit* caster)
{
    if (!caster || !caster->GetMap())
        return;

    uint32 const instanceId = GetTwinInstanceId(caster);
    if (!instanceId)
        return;

    uint32 const now = getMSTime();
    TwinScriptedEventState& state = sTwinScriptedEventStateByInstance[instanceId];
    state.lastUppercutAtMs = now;
    state.lastUppercutCasterGuid = caster->GetGUID();

    // Arm melee recovery for all bots near Vek'nilash that are
    // currently in melee range.  This ensures recovery fires from the
    // script hook instead of waiting for the next action-engine poll.
    if (!caster->GetMap())
        return;

    Map::PlayerList const& players = caster->GetMap()->GetPlayers();
    for (Map::PlayerList::const_iterator it = players.begin(); it != players.end(); ++it)
    {
        Player* player = it->GetSource();
        if (!player || !player->IsAlive())
            continue;

        if (caster->GetExactDist2d(player) > 15.0f)
            continue;

        PlayerbotAI* botAI = GET_PLAYERBOT_AI(player);
        if (!botAI || !botAI->HasStrategy("aq40", BOT_STATE_COMBAT))
            continue;

        NoteMeleeRecovery(player, kTwinUppercutRecoveryMs, "uppercut_script");
    }
}

void NoteTwinUnbalancingStrikeCast(Unit* caster)
{
    if (!caster || !caster->GetMap())
        return;

    uint32 const instanceId = GetTwinInstanceId(caster);
    if (!instanceId)
        return;

    uint32 const now = getMSTime();
    TwinScriptedEventState& state = sTwinScriptedEventStateByInstance[instanceId];
    state.lastUnbalancingStrikeAtMs = now;
    state.lastUnbalancingStrikeCasterGuid = caster->GetGUID();

    // Arm melee recovery for the tank that Vek'nilash is targeting.
    if (!caster->GetVictim())
        return;

    Player* victim = caster->GetVictim()->ToPlayer();
    if (!victim)
        return;

    PlayerbotAI* botAI = GET_PLAYERBOT_AI(victim);
    if (botAI && botAI->HasStrategy("aq40", BOT_STATE_COMBAT))
        NoteMeleeRecovery(victim, kTwinUnbalancingRecoveryMs, "unbalancing_strike_script");
}

// --- Scripted event state queries (issue #6) ---

bool IsScriptedBlizzardActive(Player* bot, uint32* outElapsedMs)
{
    if (outElapsedMs)
        *outElapsedMs = 0;

    if (!bot || !bot->GetMap())
        return false;

    uint32 const instanceId = GetTwinInstanceId(bot);
    auto const itr = sTwinScriptedEventStateByInstance.find(instanceId);
    if (itr == sTwinScriptedEventStateByInstance.end())
        return false;

    return IsScriptedEventActive(itr->second.lastBlizzardAtMs, kTwinBlizzardEventWindowMs,
                                 getMSTime(), outElapsedMs);
}

bool IsScriptedArcaneBurstActive(Player* bot, uint32* outElapsedMs)
{
    if (outElapsedMs)
        *outElapsedMs = 0;

    if (!bot || !bot->GetMap())
        return false;

    uint32 const instanceId = GetTwinInstanceId(bot);
    auto const itr = sTwinScriptedEventStateByInstance.find(instanceId);
    if (itr == sTwinScriptedEventStateByInstance.end())
        return false;

    return IsScriptedEventActive(itr->second.lastArcaneBurstAtMs, kTwinArcaneBurstEventWindowMs,
                                 getMSTime(), outElapsedMs);
}

bool IsScriptedHealBrotherActive(Player* bot, uint32* outElapsedMs)
{
    if (outElapsedMs)
        *outElapsedMs = 0;

    if (!bot || !bot->GetMap())
        return false;

    uint32 const instanceId = GetTwinInstanceId(bot);
    auto const itr = sTwinScriptedEventStateByInstance.find(instanceId);
    if (itr == sTwinScriptedEventStateByInstance.end())
        return false;

    return IsScriptedEventActive(itr->second.lastHealBrotherAtMs, kTwinHealBrotherEventWindowMs,
                                 getMSTime(), outElapsedMs);
}

bool IsScriptedUppercutActive(Player* bot, uint32* outElapsedMs)
{
    if (outElapsedMs)
        *outElapsedMs = 0;

    if (!bot || !bot->GetMap())
        return false;

    uint32 const instanceId = GetTwinInstanceId(bot);
    auto const itr = sTwinScriptedEventStateByInstance.find(instanceId);
    if (itr == sTwinScriptedEventStateByInstance.end())
        return false;

    return IsScriptedEventActive(itr->second.lastUppercutAtMs, kTwinUppercutEventWindowMs,
                                 getMSTime(), outElapsedMs);
}

bool IsScriptedUnbalancingStrikeActive(Player* bot, uint32* outElapsedMs)
{
    if (outElapsedMs)
        *outElapsedMs = 0;

    if (!bot || !bot->GetMap())
        return false;

    uint32 const instanceId = GetTwinInstanceId(bot);
    auto const itr = sTwinScriptedEventStateByInstance.find(instanceId);
    if (itr == sTwinScriptedEventStateByInstance.end())
        return false;

    return IsScriptedEventActive(itr->second.lastUnbalancingStrikeAtMs, kTwinUnbalancingStrikeEventWindowMs,
                                 getMSTime(), outElapsedMs);
}

bool IsScriptedTankRecoveryActive(Player* bot)
{
    return IsScriptedUppercutActive(bot) || IsScriptedUnbalancingStrikeActive(bot);
}
}  // namespace Aq40TwinEmperors
