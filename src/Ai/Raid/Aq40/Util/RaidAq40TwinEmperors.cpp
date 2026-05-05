#include "RaidAq40TwinEmperors.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <unordered_map>

#include "AiObjectContext.h"
#include "Group.h"
#include "Pet.h"
#include "RtiTargetValue.h"
#include "Spell.h"
#include "Timer.h"
#include "../RaidAq40SpellIds.h"
#include "../../RaidBossHelpers.h"

namespace Aq40TwinEmperors
{
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
        sTwinThreatHoldStateByInstance.find(instanceId) != sTwinThreatHoldStateByInstance.end())
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
}  // namespace Aq40TwinEmperors
