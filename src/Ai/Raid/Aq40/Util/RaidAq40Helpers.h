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
    uint32 veklorSideIndex = 0;
    uint32 veknilashSideIndex = 0;
    uint32 tankStageSide = 0;
    bool isTankBackup = false;
};

TwinRoleCohort GetTwinRoleCohort(Player* bot, PlayerbotAI* botAI);
uint32 GetStableTwinRoleIndex(Player* bot, PlayerbotAI* botAI);
TwinAssignments GetTwinAssignments(Player* bot, PlayerbotAI* botAI, GuidVector const& attackers);
GuidVector GetTwinEncounterUnits(Player* bot, PlayerbotAI* botAI, GuidVector const& attackers);
bool IsInTwinEmperorRoom(Player* bot);
bool IsTwinRaidCombatActive(Player* bot);
bool IsTwinPlayerPullAuthorized(Player* bot, PlayerbotAI* botAI, GuidVector const& attackers);
bool IsTwinCombatInProgress(Player* bot, PlayerbotAI* botAI, GuidVector const& attackers);
bool IsTwinPrePullReady(Player* bot, PlayerbotAI* botAI);
bool IsLikelyOnSameTwinSide(Unit* unit, Unit* sideEmperor, Unit* oppositeEmperor);
bool IsTwinWarlockPickupEstablished(Player* bot, PlayerbotAI* botAI, TwinAssignments const& assignment);
bool IsTwinMeleePickupEstablished(Player* bot, PlayerbotAI* botAI, TwinAssignments const& assignment);
bool HasTwinBossAggro(Player* member, Unit* boss);
bool IsTwinPrimaryTankOnActiveBoss(Player* bot, TwinAssignments const& assignment);
bool HasTwinBossesResolved(Player* bot, PlayerbotAI* botAI, GuidVector const& attackers);

bool IsCthunInStomach(Player* bot, PlayerbotAI* botAI);
uint32 GetCthunPhase2ElapsedMs(PlayerbotAI* botAI, GuidVector const& attackers);
bool IsCthunVulnerableNow(PlayerbotAI* botAI, GuidVector const& attackers);
GameObject* FindLikelyStomachExitPortal(Player* bot, PlayerbotAI* botAI);
GuidVector GetObservedSkeramEncounterUnits(Player* bot, PlayerbotAI* botAI, GuidVector const& attackers);
bool HasObservedSkeramEncounter(Player* bot, PlayerbotAI* botAI, GuidVector const& attackers);
bool IsSkeramPostBlinkHoldActive(Player* bot, PlayerbotAI* botAI, GuidVector const& attackers);
bool HasManagedResistanceStrategy(Player* bot, PlayerbotAI* botAI);
bool IsResistanceManagementNeeded(Player* bot, PlayerbotAI* botAI, GuidVector const& attackers);
bool ShouldRunOutOfCombatMaintenance(Player* bot, PlayerbotAI* botAI);
bool HasPersistentEncounterState(Player* bot);
bool ResetEncounterState(Player* bot);
bool IsAnyGroupMemberInTwinRoom(Player* bot);
}    // namespace Aq40Helpers

#endif
