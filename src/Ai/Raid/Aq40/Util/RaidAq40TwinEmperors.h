#ifndef _PLAYERBOT_RAIDAQ40TWINEMPERORS_H_
#define _PLAYERBOT_RAIDAQ40TWINEMPERORS_H_

#include <string>

#include "Position.h"
#include "Player.h"
#include "PlayerbotAI.h"

namespace Aq40TwinEmperors
{
enum class SplitBand : uint8
{
    Stable = 0,
    Warning = 1,
    Urgent = 2,
    Terminal = 3,
};

struct SideState
{
    Unit* sideZeroBoss = nullptr;
    Unit* sideOneBoss = nullptr;
    uint32 veklorSideIndex = 0;
    uint32 veknilashSideIndex = 0;
    float separation = 0.0f;
};

SplitBand GetSplitBand(float separation);
SideState ResolveSideState(Player* bot, Unit* veklor, Unit* veknilash);
uint32 GetPostSwapElapsedMs(Player* bot, uint32 veklorSideIndex);
void NoteTwinTeleportCast(Unit* caster);
bool IsTwinTeleportWindowActive(Player* bot, uint32* outElapsedMs = nullptr);
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
}  // namespace Aq40TwinEmperors

#endif
