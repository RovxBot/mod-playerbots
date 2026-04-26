#ifndef _PLAYERBOT_RAIDAQ40HELPERS_H_
#define _PLAYERBOT_RAIDAQ40HELPERS_H_

#include <list>
#include <string>
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
bool GetTwinDedicatedTankHealerSide(Player* bot, PlayerbotAI* botAI, uint32& sideIndex);
std::list<ObjectGuid> GetTwinHealerFocusTargets(Player* bot, PlayerbotAI* botAI, TwinAssignments const& assignment);
Unit* FindTwinMarkedBug(Player* bot, PlayerbotAI* botAI, GuidVector const& encounterUnits, uint32 auraSpellId);
bool ApplyTwinHealerFocusTargets(Player* bot, PlayerbotAI* botAI, std::list<ObjectGuid> const& focusTargets);
bool ClearTwinHealerFocusTargets(Player* bot, PlayerbotAI* botAI);
bool IsTwinHealerOutsideSideLeash(Player* bot, TwinAssignments const& assignment);
bool ApplyTwinTemporaryCombatStrategies(Player* bot, PlayerbotAI* botAI);
bool ClearTwinTemporaryCombatStrategies(Player* bot, PlayerbotAI* botAI);
bool HasTwinBossesResolved(Player* bot, PlayerbotAI* botAI, GuidVector const& attackers);
bool HasTwinVisibleEmperors(Player* bot, PlayerbotAI* botAI, GuidVector* outUnits = nullptr);

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
std::string GetAq40LogToken(std::string value);
std::string GetAq40LogUnit(Unit* unit);
std::string GetAq40LogRole(Player* bot, PlayerbotAI* botAI);
void LogAq40Info(Player* bot, std::string const& eventKey, std::string const& stateKey,
                 std::string const& fields = "", uint32 throttleMs = 0);
void LogAq40Warn(Player* bot, std::string const& eventKey, std::string const& stateKey,
                 std::string const& fields = "", uint32 throttleMs = 0);
void LogAq40Target(Player* bot, std::string const& boss, std::string const& reason, Unit* target,
                   uint32 throttleMs = 0);
}    // namespace Aq40Helpers

#endif
