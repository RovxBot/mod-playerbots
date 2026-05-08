#ifndef _PLAYERBOT_RAIDAQ40TWINEMPERORS_H_
#define _PLAYERBOT_RAIDAQ40TWINEMPERORS_H_

#include <string>

#include "Position.h"
#include "Player.h"
#include "PlayerbotAI.h"

// ===========================================================================
// Twin Emperors encounter-local state module.
//
// VALIDATION: see TWIN_EMPERORS_VALIDATION.md for the full checklist.
//
// Key invariants this module must preserve:
//   - All state is instance-scoped; ResetState() must clear everything.
//   - Boss ownership requires LOS + range verification, not just GetVictim().
//   - DPS holds remain active until AreBothOwnersStable() is true AND
//     separation is in the Stable band.
//   - Emergency split recovery suppresses all non-tank behavior.
//   - Spell-driven events (Blizzard, Arcane Burst, Heal Brother, Uppercut,
//     Unbalancing Strike) are first-class inputs via script hooks.
//
// Failure-log targets (zero tolerance in supported runs):
//   pickup_failed, movement_failure, split_risk, heal_brother_terminal
// ===========================================================================
namespace Aq40TwinEmperors
{
enum class ThreatHoldType : uint8
{
    None = 0,
    InitialPull = 1,
    Teleport = 2,
    PickupFailed = 3,
};

enum class SplitBand : uint8
{
    Stable = 0,
    Warning = 1,
    Urgent = 2,
    Terminal = 3,
};

enum class TwinEncounterPhase : uint8
{
    PrePull = 0,
    Stable = 1,
    TeleportWindow = 2,
    PickupRecovery = 3,
    EmergencySplitRecovery = 4,
    Degraded = 5,
};

struct TwinBossOwnership
{
    ObjectGuid expectedOwnerGuid = ObjectGuid::Empty;
    ObjectGuid reserveOwnerGuid = ObjectGuid::Empty;
    ObjectGuid candidateOwnerGuid = ObjectGuid::Empty;
    ObjectGuid stableOwnerGuid = ObjectGuid::Empty;
    uint32 stableSinceMs = 0;
    uint32 lastValidAtMs = 0;
};

struct TwinEncounterStateMachine
{
    TwinEncounterPhase phase = TwinEncounterPhase::PrePull;
    TwinBossOwnership veklor;
    TwinBossOwnership veknilash;
    uint32 phaseChangedAtMs = 0;
    bool reservePromotionUsedVeklor = false;
    bool reservePromotionUsedVeknilash = false;
};

struct SideState
{
    Unit* sideZeroBoss = nullptr;
    Unit* sideOneBoss = nullptr;
    uint32 veklorSideIndex = 0;
    uint32 veknilashSideIndex = 0;
    float separation = 0.0f;
};

struct ThreatHoldState
{
    ThreatHoldType type = ThreatHoldType::None;
    uint32 openedAtMs = 0;
    uint32 untilMs = 0;
};

SplitBand GetSplitBand(float separation);
SideState ResolveSideState(Player* bot, Unit* veklor, Unit* veknilash);
uint32 GetPostSwapElapsedMs(Player* bot, uint32 veklorSideIndex);
void NoteTwinEncounterActive(Player* bot);
void NoteTwinTeleportCast(Unit* caster);
bool IsTwinTeleportWindowActive(Player* bot, uint32* outElapsedMs = nullptr);
bool GetActiveThreatHold(Player* bot, ThreatHoldState& outState);
uint32 GetLatestPickupWindowOpenedAtMs(Player* bot);
bool ArmPickupFailedHold(Player* bot, uint32 sourceOpenedAtMs);
bool HasTwinMovementOwnershipState(Player* bot);
void NoteTwinPickupEstablished(Player* bot, bool isVeklor);
bool HasTwinPickupEstablished(Player* bot, bool isVeklor);
void RememberTwinPickupAnchor(Player* bot, Unit* boss, uint32 sideIndex, Position const& anchor);
bool GetTwinLockedPickupAnchor(Player* bot, Unit* boss, uint32 sideIndex, Position& outAnchor);
bool HasLockedPickupAnchor(Player* bot);
void ClearTwinPickupState(Player* bot);

bool HasBossPickupAggro(Player* member, Unit* boss);
bool IsPickupWindowSatisfied(Player* member, Unit* boss, bool isVeklor);

bool RefreshMeleeRecoveryState(Player* bot, PlayerbotAI* botAI, Unit* veknilash, uint32 veklorSideIndex,
                               bool pickupEstablished, std::string* outReason = nullptr);

bool PublishRaidMarkers(Player* bot, PlayerbotAI* botAI, Unit* veklor, Unit* veknilash, Unit* bugTarget);
void ClearLocalRti(PlayerbotAI* botAI);
void SyncLocalRti(PlayerbotAI* botAI, Unit* target, Unit* veklor, Unit* veknilash, Unit* bugTarget);

bool ResetState(Player* bot);
bool HasPersistentState(Player* bot);

TwinEncounterPhase GetEncounterPhase(Player* bot);
TwinEncounterStateMachine const* GetEncounterStateMachine(Player* bot);
void UpdateEncounterPhase(Player* bot, Unit* veklor, Unit* veknilash, float separation,
                          bool encounterLive, bool healBrotherActive);
bool IsBossOwnerStable(Player* bot, bool isVeklor);
bool AreBothOwnersStable(Player* bot);
bool ShouldReleaseThreatHold(Player* bot, float separation, bool healBrotherActive);
void UpdateBossOwnership(Player* bot, bool isVeklor, ObjectGuid const& expectedGuid,
                         ObjectGuid const& reserveGuid, ObjectGuid const& candidateGuid,
                         Unit* boss);
void ProcessReserveTakeover(Player* bot, bool isVeklor);
std::string GetPhaseToken(TwinEncounterPhase phase);

// --- Spell-driven event notifications (issue #6) ---
// Called by Aq40TwinEmperorsListenerScript when a boss casts these spells.
void NoteTwinBlizzardCast(Unit* caster);
void NoteTwinArcaneBurstCast(Unit* caster);
void NoteTwinHealBrotherCast(Unit* caster);
void NoteTwinUppercutCast(Unit* caster);
void NoteTwinUnbalancingStrikeCast(Unit* caster);

// --- Scripted event state queries (issue #6) ---
// Returns true if a scripted Blizzard event was recorded recently.
bool IsScriptedBlizzardActive(Player* bot, uint32* outElapsedMs = nullptr);
// Returns true if a scripted Arcane Burst event was recorded recently.
bool IsScriptedArcaneBurstActive(Player* bot, uint32* outElapsedMs = nullptr);
// Returns true if a scripted Heal Brother event was recorded recently.
bool IsScriptedHealBrotherActive(Player* bot, uint32* outElapsedMs = nullptr);
// Returns true if a scripted Uppercut event was recorded recently.
bool IsScriptedUppercutActive(Player* bot, uint32* outElapsedMs = nullptr);
// Returns true if a scripted Unbalancing Strike event was recorded recently.
bool IsScriptedUnbalancingStrikeActive(Player* bot, uint32* outElapsedMs = nullptr);
// Returns true if any scripted tank-recovery event is active (Uppercut or Unbalancing Strike).
bool IsScriptedTankRecoveryActive(Player* bot);

// --- Emergency split recovery (issue #3) ---
bool IsEmergencySplitRecoveryActive(Player* bot);
void ArmEmergencySplitRecovery(Player* bot, float separation, std::string const& reason);
void ClearEmergencySplitRecovery(Player* bot);
bool GetEmergencySplitRecoveryAnchor(Player* bot, Unit* boss, bool isMeleeOwner, Position& outAnchor);

// --- Teleport-prep receiving geometry (issue #3) ---
// Warlock receiving side: move inward before Vek'lor arrives so warlock is
// second-closest valid unit and can immediately cast Searing Pain.
bool GetWarlockReceivingAnchor(Player* bot, Unit* veklor, Unit* veknilash,
                               uint32 receivingSideIndex, Position& outAnchor);
// Melee receiving side: move inward so melee tank is closest valid unit when
// Vek'nilash arrives, while respecting Vek'lor Arcane Burst safety.
bool GetMeleeReceivingAnchor(Player* bot, Unit* veklor, Unit* veknilash,
                             uint32 receivingSideIndex, Position& outAnchor);

// --- Vek'nilash wall-facing orientation (issue #3) ---
// Returns a position the Vek'nilash tank should stand at so the boss faces
// toward the wall, making Uppercut knockback center-safe.
bool GetVeknilashWallFacingAnchor(Player* bot, Unit* veknilash, uint32 sideIndex,
                                  Position& outAnchor);

// --- Stop all raid damage on both bosses (issue #3) ---
void StopAllRaidDamageOnBoss(Player* bot, Unit* boss);
}  // namespace Aq40TwinEmperors

#endif
