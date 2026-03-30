#ifndef _PLAYERBOT_RAIDAQ40HELPERS_H_
#define _PLAYERBOT_RAIDAQ40HELPERS_H_

#include <unordered_map>

#include "ObjectGuid.h"
#include "Player.h"
#include "PlayerbotAI.h"

class GameObject;

namespace Aq40Helpers
{
enum class TwinRoleCohort : uint8
{
    WarlockTank = 0,
    MeleeTank = 1,
    Healer = 2,
    Other = 3,
};

struct TwinAssignments
{
    Unit* sideEmperor = nullptr;
    Unit* oppositeEmperor = nullptr;
    Unit* veklor = nullptr;
    Unit* veknilash = nullptr;
    uint32 sideIndex = 0;
};

TwinRoleCohort GetTwinRoleCohort(Player* bot, PlayerbotAI* botAI);
uint32 GetStableTwinRoleIndex(Player* bot, PlayerbotAI* botAI);
TwinAssignments GetTwinAssignments(Player* bot, PlayerbotAI* botAI, GuidVector const& attackers);
GuidVector GetTwinPrePullUnits(Player* bot, PlayerbotAI* botAI);
GuidVector GetTwinEncounterUnits(Player* bot, PlayerbotAI* botAI, GuidVector const& attackers);
bool IsInTwinEmperorRoom(Player* bot);
bool IsTwinRaidCombatActive(Player* bot);
bool IsTwinPrePullReady(Player* bot, PlayerbotAI* botAI);
bool IsLikelyOnSameTwinSide(Unit* unit, Unit* sideEmperor, Unit* oppositeEmperor);
bool IsTwinMutateBug(PlayerbotAI* botAI, Unit* unit);
bool IsTwinExplodeBug(PlayerbotAI* botAI, Unit* unit);
bool IsTwinCriticalSideBug(Player* bot, PlayerbotAI* botAI, TwinAssignments const& assignment, Unit* bug);
bool IsTwinDpsWaitWindow(Player* bot, PlayerbotAI* botAI, GuidVector const& attackers);
bool IsTwinTeleportRecoveryWindow(Player* bot, PlayerbotAI* botAI, GuidVector const& attackers);
bool IsTwinPreTeleportWindow(Player* bot, PlayerbotAI* botAI, GuidVector const& attackers);
bool IsTwinAssignedTankReady(Player* bot, PlayerbotAI* botAI, TwinAssignments const& assignment);

bool IsCthunInStomach(Player* bot, PlayerbotAI* botAI);
uint32 GetCthunPhase2ElapsedMs(PlayerbotAI* botAI, GuidVector const& attackers);
bool IsCthunVulnerableNow(PlayerbotAI* botAI, GuidVector const& attackers);
GameObject* FindLikelyStomachExitPortal(Player* bot, PlayerbotAI* botAI);
bool IsSkeramPostBlinkHoldActive(Player* bot, PlayerbotAI* botAI, GuidVector const& attackers);
bool ResetEncounterState(Player* bot);
}    // namespace Aq40Helpers

#endif
