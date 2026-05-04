#ifndef _PLAYERBOT_RAIDAQ40TWINEMPERORS_H_
#define _PLAYERBOT_RAIDAQ40TWINEMPERORS_H_

#include <string>

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
