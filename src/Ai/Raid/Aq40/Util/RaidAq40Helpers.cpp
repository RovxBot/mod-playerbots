#include "RaidAq40Helpers.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <vector>

#include "GameObject.h"
#include "ObjectAccessor.h"
#include "Playerbots.h"
#include "../RaidAq40BossHelper.h"
#include "../RaidAq40SpellIds.h"
#include "Timer.h"

namespace Aq40Helpers
{
namespace
{
using TwinRoleAssignmentMap = std::unordered_map<uint64, uint32>;
using TwinCohortAssignments = std::unordered_map<int, TwinRoleAssignmentMap>;
using TwinInstanceAssignments = std::unordered_map<uint32, TwinCohortAssignments>;
struct TwinKnownBossGuids
{
    ObjectGuid veklorGuid = ObjectGuid::Empty;
    ObjectGuid veknilashGuid = ObjectGuid::Empty;
};

// Room geometry constants.
float constexpr kTwinRoomCenterX = -8954.855f;
float constexpr kTwinRoomCenterY = 1235.7107f;
float constexpr kTwinRoomCenterZ = -112.62047f;
float constexpr kTwinRoomStageRadius = 150.0f;
float constexpr kTwinRoomStageZTolerance = 18.0f;
float constexpr kTwinPrePullDetectionRange = 320.0f;

TwinInstanceAssignments sTwinAssignments;
std::unordered_map<uint32, TwinKnownBossGuids> sTwinKnownTwinBosses;
std::unordered_map<uint32, uint32> sCthunPhase2StartByInstance;
std::unordered_map<uint32, bool> sCachedTwinSplitByX;
std::unordered_map<uint32, bool> sTwinSideZeroIsLowSide;
std::unordered_map<uint32, uint32> sSkeramPostBlinkHoldUntilByInstance;

bool IsTwinRaidCombatActiveInternal(Player* bot);
bool HasTwinPrePullVisibility(Player* bot, PlayerbotAI* botAI, GuidVector* outUnits = nullptr);
void UpdateTwinBossCache(Player* bot, PlayerbotAI* botAI, GuidVector const& units);
void AppendKnownTwinBosses(Player* bot, GuidVector& units);

bool IsTwinBossUnit(PlayerbotAI* botAI, Unit* unit)
{
    return botAI && unit && Aq40BossHelper::IsUnitNamedAny(botAI, unit,
        { "emperor vek'nilash", "emperor vek'lor" });
}

GuidVector BuildTwinObservedUnits(Player* bot, PlayerbotAI* botAI, GuidVector const& attackers,
                                  GuidVector* outVisibleTwins = nullptr)
{
    GuidVector units = Aq40BossHelper::GetEncounterUnits(botAI, attackers);
    if (!bot || !botAI)
        return units;

    GuidVector visibleTwins;
    HasTwinPrePullVisibility(bot, botAI, &visibleTwins);
    UpdateTwinBossCache(bot, botAI, units);
    UpdateTwinBossCache(bot, botAI, visibleTwins);
    AppendKnownTwinBosses(bot, units);

    for (ObjectGuid const guid : visibleTwins)
    {
        if (std::find(units.begin(), units.end(), guid) == units.end())
            units.push_back(guid);
    }

    if (outVisibleTwins)
        *outVisibleTwins = visibleTwins;

    return units;
}

bool IsTwinRealPlayer(Player* player)
{
    if (!player)
        return false;

    PlayerbotAI* playerAI = GET_PLAYERBOT_AI(player);
    return !playerAI || playerAI->IsRealPlayer();
}

Unit* FindTwinUnit(PlayerbotAI* botAI, GuidVector const& attackers, char const* name)
{
    return Aq40BossHelper::FindUnitByAnyName(botAI, attackers, { name });
}

Unit* ResolveTwinUnitFromGuid(Player* bot, ObjectGuid const& guid)
{
    if (!bot || !guid)
        return nullptr;

    Unit* unit = ObjectAccessor::GetUnit(*bot, guid);
    if (!unit || !unit->IsInWorld() || !unit->IsAlive() || unit->GetMapId() != bot->GetMapId())
        return nullptr;

    return unit;
}

void UpdateTwinBossCache(Player* bot, PlayerbotAI* botAI, GuidVector const& units)
{
    if (!bot || !botAI || !bot->GetMap())
        return;

    TwinKnownBossGuids& knownBosses = sTwinKnownTwinBosses[bot->GetMap()->GetInstanceId()];
    for (ObjectGuid const guid : units)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit || !unit->IsInWorld() || !unit->IsAlive() || unit->GetMapId() != bot->GetMapId())
            continue;

        if (Aq40BossHelper::IsUnitNamedAny(botAI, unit, { "emperor vek'lor" }))
            knownBosses.veklorGuid = guid;
        else if (Aq40BossHelper::IsUnitNamedAny(botAI, unit, { "emperor vek'nilash" }))
            knownBosses.veknilashGuid = guid;
    }
}

void AppendKnownTwinBosses(Player* bot, GuidVector& units)
{
    if (!bot || !bot->GetMap())
        return;

    auto const itr = sTwinKnownTwinBosses.find(bot->GetMap()->GetInstanceId());
    if (itr == sTwinKnownTwinBosses.end())
        return;

    for (ObjectGuid const guid : { itr->second.veklorGuid, itr->second.veknilashGuid })
    {
        if (!guid || std::find(units.begin(), units.end(), guid) != units.end())
            continue;

        if (ResolveTwinUnitFromGuid(bot, guid))
            units.push_back(guid);
    }
}

bool IsInTwinRoomBounds(Player* bot)
{
    if (!bot || !Aq40BossHelper::IsInAq40(bot))
        return false;

    return bot->GetDistance(kTwinRoomCenterX, kTwinRoomCenterY, kTwinRoomCenterZ) <= kTwinRoomStageRadius &&
           std::abs(bot->GetPositionZ() - kTwinRoomCenterZ) <= kTwinRoomStageZTolerance;
}

GuidVector CollectTwinVisibleUnits(Player* bot, PlayerbotAI* botAI)
{
    GuidVector units;
    if (!bot || !botAI || !Aq40BossHelper::IsInAq40(bot))
        return units;

    GuidVector const& possibleTargetsNoLos =
        botAI->GetAiObjectContext()->GetValue<GuidVector>("possible targets no los")->Get();
    for (ObjectGuid const guid : possibleTargetsNoLos)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit || !unit->IsInWorld() || !unit->IsAlive() || unit->GetMapId() != bot->GetMapId())
            continue;

        if (!Aq40BossHelper::IsUnitNamedAny(botAI, unit, { "emperor vek'nilash", "emperor vek'lor" }))
            continue;

        if (bot->GetDistance2d(unit) > kTwinPrePullDetectionRange)
            continue;

        units.push_back(guid);
    }

    return units;
}

bool HasTwinPrePullVisibility(Player* bot, PlayerbotAI* botAI, GuidVector* outUnits)
{
    GuidVector units = CollectTwinVisibleUnits(bot, botAI);
    if (outUnits)
        *outUnits = units;

    return botAI && !units.empty();
}

bool HasTwinVisibleEmperors(Player* bot, PlayerbotAI* botAI, GuidVector* outUnits = nullptr)
{
    GuidVector units;
    if (!HasTwinPrePullVisibility(bot, botAI, &units))
    {
        if (outUnits)
            *outUnits = units;
        return false;
    }

    if (outUnits)
        *outUnits = units;

    return Aq40BossHelper::HasAnyNamedUnit(botAI, units, { "emperor vek'nilash" }) &&
           Aq40BossHelper::HasAnyNamedUnit(botAI, units, { "emperor vek'lor" });
}

bool IsTwinRaidCombatActiveInternal(Player* bot)
{
    if (!bot || !bot->GetMap())
        return false;

    Group const* group = bot->GetGroup();
    if (!group)
        return false;

    uint32 const instanceId = bot->GetMap()->GetInstanceId();
    for (GroupReference const* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || !member->IsAlive() || !member->IsInCombat())
            continue;
        if (member->GetMapId() != bot->GetMapId())
            continue;
        if (member->GetMap() && member->GetMap()->GetInstanceId() != instanceId)
            continue;
        if (!IsInTwinRoomBounds(member))
            continue;

        return true;
    }

    return false;
}

bool IsAnyGroupMemberInTwinRoomInternal(Player* bot)
{
    if (!bot || !bot->GetMap())
        return false;

    Group const* group = bot->GetGroup();
    if (!group)
        return IsInTwinRoomBounds(bot);

    uint32 const instanceId = bot->GetMap()->GetInstanceId();
    for (GroupReference const* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || !member->IsAlive())
            continue;
        if (member->GetMapId() != bot->GetMapId())
            continue;
        if (member->GetMap() && member->GetMap()->GetInstanceId() != instanceId)
            continue;
        if (IsInTwinRoomBounds(member))
            return true;
    }

    return false;
}

uint32 GetGroupMemberOrder(Player* bot, Player* member)
{
    if (!bot || !member)
        return std::numeric_limits<uint32>::max();

    Group const* group = bot->GetGroup();
    if (!group)
        return std::numeric_limits<uint32>::max();

    uint32 order = 0;
    for (GroupReference const* ref = group->GetFirstMember(); ref; ref = ref->next(), ++order)
    {
        if (ref->GetSource() == member)
            return order;
    }

    return std::numeric_limits<uint32>::max();
}

uint32 GetTwinRolePriority(Player* bot, Player* member, TwinRoleCohort cohort)
{
    switch (cohort)
    {
        case TwinRoleCohort::WarlockTank:
            return Aq40BossHelper::GetAliveWarlockOrdinal(member);
        case TwinRoleCohort::MeleeTank:
            if (PlayerbotAI::IsMainTank(member))
                return 0;
            if (PlayerbotAI::IsAssistTankOfIndex(member, 0, true))
                return 1;
            if (PlayerbotAI::IsAssistTankOfIndex(member, 1, true))
                return 2;
            return 10 + GetGroupMemberOrder(bot, member);
        case TwinRoleCohort::Healer:
        case TwinRoleCohort::Other:
            return GetGroupMemberOrder(bot, member);
    }

    return std::numeric_limits<uint32>::max();
}

uint32 GetTwinRoleSideForSlot(TwinRoleCohort cohort, uint32 slot)
{
    switch (cohort)
    {
        case TwinRoleCohort::WarlockTank:
            return (slot % 2 == 0) ? 1u : 0u;
        case TwinRoleCohort::MeleeTank:
            return slot % 2;
        case TwinRoleCohort::Healer:
        case TwinRoleCohort::Other:
            return slot % 2;
    }

    return slot % 2;
}

bool IsTwinRoleMatch(TwinRoleCohort cohort, Player* member)
{
    if (!member || !member->IsAlive())
        return false;

    switch (cohort)
    {
        case TwinRoleCohort::WarlockTank:
            return Aq40BossHelper::IsDesignatedTwinWarlockTank(member);
        case TwinRoleCohort::MeleeTank:
            return PlayerbotAI::IsTank(member) && !PlayerbotAI::IsRanged(member);
        case TwinRoleCohort::Healer:
            return PlayerbotAI::IsHeal(member);
        case TwinRoleCohort::Other:
            return true;
    }

    return false;
}

bool HasTwinBossAggroInternal(Player* member, Unit* boss)
{
    if (!member || !boss)
        return false;

    ObjectGuid const memberGuid = member->GetGUID();
    ObjectGuid petGuid = ObjectGuid::Empty;
    if (Pet* pet = member->GetPet())
        petGuid = pet->GetGUID();

    return boss->GetVictim() == member ||
           boss->GetTarget() == memberGuid ||
           (petGuid && boss->GetTarget() == petGuid) ||
           member->GetVictim() == boss;
}

bool IsTwinPickupMemberEstablished(Player* member, TwinAssignments const& assignment, Unit* boss)
{
    if (!member || !member->IsAlive() || !boss || !boss->IsAlive())
        return false;

    if (!member->IsWithinLOSInMap(boss) || !HasTwinBossAggroInternal(member, boss))
        return false;

    float const distance = member->GetDistance2d(boss);
    float const minRange = boss == assignment.veklor ? 18.0f : 1.5f;
    float const maxRange = boss == assignment.veklor ? 36.0f : 15.0f;
    if (distance < minRange || distance > maxRange)
        return false;

    if (distance <= (boss == assignment.veklor ? 30.0f : 8.0f))
        return true;

    return true;
}

}  // namespace

bool HasTwinBossAggro(Player* member, Unit* boss)
{
    return HasTwinBossAggroInternal(member, boss);
}

bool IsTwinPrimaryTankOnActiveBoss(Player* bot, TwinAssignments const& assignment)
{
    if (!bot)
        return false;

    if (Aq40BossHelper::IsDesignatedTwinWarlockTank(bot))
        return assignment.veklor && !assignment.isTankBackup;

    if (PlayerbotAI::IsTank(bot) && !PlayerbotAI::IsRanged(bot))
        return assignment.veknilash && !assignment.isTankBackup;

    return false;
}

TwinRoleCohort GetTwinRoleCohort(Player* bot, PlayerbotAI* botAI)
{
    if (Aq40BossHelper::IsDesignatedTwinWarlockTank(bot))
        return TwinRoleCohort::WarlockTank;
    if (PlayerbotAI::IsTank(bot) && !PlayerbotAI::IsRanged(bot))
        return TwinRoleCohort::MeleeTank;
    if (PlayerbotAI::IsHeal(bot))
        return TwinRoleCohort::Healer;
    return TwinRoleCohort::Other;
}

uint32 GetStableTwinRoleIndex(Player* bot, PlayerbotAI* botAI)
{
    if (!bot)
        return 0;

    Group const* group = bot->GetGroup();
    if (!group)
        return static_cast<uint32>(bot->GetGUID().GetCounter() % 2);

    uint32 const instanceId = bot->GetMap() ? bot->GetMap()->GetInstanceId() : 0;

    bool const encounterActive = Aq40BossHelper::IsEncounterCombatActive(bot) || IsTwinRaidCombatActiveInternal(bot);
    bool const twinVisiblePrePull = botAI && HasTwinPrePullVisibility(bot, botAI);
    if (!encounterActive)
    {
        if (!IsInTwinRoomBounds(bot) && !twinVisiblePrePull && !IsAnyGroupMemberInTwinRoomInternal(bot))
        {
            sTwinAssignments.erase(instanceId);
            sCachedTwinSplitByX.erase(instanceId);
            sTwinSideZeroIsLowSide.erase(instanceId);
            sTwinKnownTwinBosses.erase(instanceId);
        }
    }

    TwinRoleCohort const cohort = GetTwinRoleCohort(bot, botAI);
    TwinRoleAssignmentMap& assignments = sTwinAssignments[instanceId][static_cast<int>(cohort)];
    uint64 const botGuid = bot->GetGUID().GetRawValue();

    {
        std::vector<uint64> staleGuids;
        for (auto const& [guid, _] : assignments)
        {
            bool found = false;
            for (GroupReference const* ref = group->GetFirstMember(); ref; ref = ref->next())
            {
                Player* member = ref->GetSource();
                if (member && member->GetGUID().GetRawValue() == guid &&
                    IsTwinRoleMatch(cohort, member) &&
                    Aq40BossHelper::IsEncounterParticipant(bot, member))
                {
                    found = true;
                    break;
                }
            }
            if (!found)
                staleGuids.push_back(guid);
        }
        for (uint64 guid : staleGuids)
            assignments.erase(guid);
    }

    if (assignments.find(botGuid) != assignments.end())
        return assignments[botGuid];

    std::vector<Player*> members;
    for (GroupReference const* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!IsTwinRoleMatch(cohort, member))
            continue;
        if (!Aq40BossHelper::IsEncounterParticipant(bot, member))
            continue;

        members.push_back(member);
    }

    std::sort(members.begin(), members.end(), [bot, cohort](Player const* left, Player const* right)
    {
        uint32 const leftPriority = GetTwinRolePriority(bot, const_cast<Player*>(left), cohort);
        uint32 const rightPriority = GetTwinRolePriority(bot, const_cast<Player*>(right), cohort);
        if (leftPriority != rightPriority)
            return leftPriority < rightPriority;

        return left->GetGUID().GetRawValue() < right->GetGUID().GetRawValue();
    });

    uint32 assignedCount = 0;
    for (Player* member : members)
    {
        if (assignments.find(member->GetGUID().GetRawValue()) != assignments.end())
            ++assignedCount;
    }

    for (Player* member : members)
    {
        uint64 const memberGuid = member->GetGUID().GetRawValue();
        if (assignments.find(memberGuid) != assignments.end())
            continue;

        assignments[memberGuid] = GetTwinRoleSideForSlot(cohort, assignedCount);
        ++assignedCount;
    }

    auto itr = assignments.find(botGuid);
    if (itr != assignments.end())
        return itr->second;

    return static_cast<uint32>(bot->GetGUID().GetCounter() % 2);
}

TwinAssignments GetTwinAssignments(Player* bot, PlayerbotAI* botAI, GuidVector const& attackers)
{
    TwinAssignments result;
    if (bot && botAI)
        UpdateTwinBossCache(bot, botAI, attackers);

    result.veklor = FindTwinUnit(botAI, attackers, "emperor vek'lor");
    result.veknilash = FindTwinUnit(botAI, attackers, "emperor vek'nilash");
    if ((!result.veklor || !result.veknilash) && bot && botAI)
    {
        GuidVector const visibleTwins = CollectTwinVisibleUnits(bot, botAI);
        UpdateTwinBossCache(bot, botAI, visibleTwins);
    }

    if ((!result.veklor || !result.veknilash) && bot && bot->GetMap())
    {
        auto const knownBosses = sTwinKnownTwinBosses.find(bot->GetMap()->GetInstanceId());
        if (knownBosses != sTwinKnownTwinBosses.end())
        {
            if (!result.veklor)
                result.veklor = ResolveTwinUnitFromGuid(bot, knownBosses->second.veklorGuid);
            if (!result.veknilash)
                result.veknilash = ResolveTwinUnitFromGuid(bot, knownBosses->second.veknilashGuid);
        }
    }

    if (!result.veklor || !result.veknilash)
        return result;

    TwinRoleCohort const cohort = GetTwinRoleCohort(bot, botAI);
    uint32 const instanceId = bot->GetMap() ? bot->GetMap()->GetInstanceId() : 0;

    float separation = result.veklor->GetDistance2d(result.veknilash);
    bool splitByX;
    auto axisIt = sCachedTwinSplitByX.find(instanceId);
    if (axisIt != sCachedTwinSplitByX.end())
    {
        if (separation > 60.0f)
        {
            splitByX = std::abs(result.veklor->GetPositionX() - result.veknilash->GetPositionX()) >=
                       std::abs(result.veklor->GetPositionY() - result.veknilash->GetPositionY());
            sCachedTwinSplitByX[instanceId] = splitByX;
        }
        else
        {
            splitByX = axisIt->second;
        }
    }
    else
    {
        splitByX = std::abs(result.veklor->GetPositionX() - result.veknilash->GetPositionX()) >=
                   std::abs(result.veklor->GetPositionY() - result.veknilash->GetPositionY());
        sCachedTwinSplitByX[instanceId] = splitByX;
    }

    float veklorAxis = splitByX ? result.veklor->GetPositionX() : result.veklor->GetPositionY();
    float veknilashAxis = splitByX ? result.veknilash->GetPositionX() : result.veknilash->GetPositionY();
    Unit* lowSide = veklorAxis < veknilashAxis ? result.veklor : result.veknilash;
    Unit* highSide = lowSide == result.veklor ? result.veknilash : result.veklor;

    bool sideZeroIsLowSide = true;
    auto sideMappingIt = sTwinSideZeroIsLowSide.find(instanceId);
    if (sideMappingIt == sTwinSideZeroIsLowSide.end())
    {
        sideZeroIsLowSide = (lowSide == result.veknilash);
        sTwinSideZeroIsLowSide[instanceId] = sideZeroIsLowSide;
    }
    else
    {
        sideZeroIsLowSide = sideMappingIt->second;
    }

    Unit* sideZeroBoss = sideZeroIsLowSide ? lowSide : highSide;
    Unit* sideOneBoss = sideZeroIsLowSide ? highSide : lowSide;
    auto getBossSideIndex = [sideZeroBoss, sideOneBoss](Unit* boss) -> uint32
    {
        if (boss == sideOneBoss)
            return 1u;
        return 0u;
    };

    result.veklorSideIndex = getBossSideIndex(result.veklor);
    result.veknilashSideIndex = getBossSideIndex(result.veknilash);

    if (cohort == TwinRoleCohort::Other)
    {
        if (PlayerbotAI::IsRanged(bot))
        {
            result.sideEmperor = result.veklor;
            result.oppositeEmperor = result.veknilash;
        }
        else
        {
            result.sideEmperor = result.veknilash;
            result.oppositeEmperor = result.veklor;
        }
        result.sideIndex = getBossSideIndex(result.sideEmperor);
        return result;
    }

    if (cohort == TwinRoleCohort::WarlockTank)
    {
        result.sideEmperor = result.veklor;
        result.oppositeEmperor = result.veknilash;
        result.sideIndex = result.veklorSideIndex;

        uint32 const stableIdx = GetStableTwinRoleIndex(bot, botAI);
        uint32 const veklorRoomSide = result.veklorSideIndex;
        result.tankStageSide = stableIdx;
        result.isTankBackup = (stableIdx != veklorRoomSide);
        return result;
    }
    if (cohort == TwinRoleCohort::MeleeTank)
    {
        result.sideEmperor = result.veknilash;
        result.oppositeEmperor = result.veklor;
        result.sideIndex = result.veknilashSideIndex;

        uint32 const stableIdx = GetStableTwinRoleIndex(bot, botAI);
        uint32 const veknilashRoomSide = result.veknilashSideIndex;
        result.tankStageSide = stableIdx;
        result.isTankBackup = (stableIdx != veknilashRoomSide);
        return result;
    }

    // Healers: use stable room-side index.
    uint32 const sideIndex = GetStableTwinRoleIndex(bot, botAI);
    result.sideIndex = sideIndex;
    result.sideEmperor = sideIndex == 0 ? sideZeroBoss : sideOneBoss;
    result.oppositeEmperor = sideIndex == 0 ? sideOneBoss : sideZeroBoss;
    return result;
}

GuidVector GetTwinEncounterUnits(Player* bot, PlayerbotAI* botAI, GuidVector const& attackers)
{
    GuidVector units = Aq40BossHelper::GetEncounterUnits(botAI, attackers);
    if (!bot || !botAI)
        return units;

    if (!IsTwinPlayerPullAuthorized(bot, botAI, attackers) &&
        !IsTwinCombatInProgress(bot, botAI, attackers))
        return units;

    return BuildTwinObservedUnits(bot, botAI, attackers);
}

bool IsInTwinEmperorRoom(Player* bot)
{
    return IsInTwinRoomBounds(bot);
}

bool IsTwinRaidCombatActive(Player* bot)
{
    return IsTwinRaidCombatActiveInternal(bot);
}

bool IsTwinPlayerPullAuthorized(Player* bot, PlayerbotAI* botAI, GuidVector const& attackers)
{
    if (!bot || !botAI || !bot->GetMap())
        return false;

    Group const* group = bot->GetGroup();
    if (!group)
        return false;

    GuidVector observedUnits = BuildTwinObservedUnits(bot, botAI, attackers);
    uint32 const instanceId = bot->GetMap()->GetInstanceId();
    for (GroupReference const* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || !member->IsAlive() || !IsTwinRealPlayer(member))
            continue;
        if (member->GetMapId() != bot->GetMapId())
            continue;
        if (member->GetMap() && member->GetMap()->GetInstanceId() != instanceId)
            continue;

        if (Unit* victim = member->GetVictim())
        {
            if (IsTwinBossUnit(botAI, victim))
                return true;
        }

        Pet* pet = member->GetPet();
        if (pet && pet->GetVictim() && IsTwinBossUnit(botAI, pet->GetVictim()))
            return true;

        ObjectGuid const memberGuid = member->GetGUID();
        ObjectGuid const memberTargetGuid = member->GetTarget();
        ObjectGuid petGuid = pet ? pet->GetGUID() : ObjectGuid::Empty;
        for (ObjectGuid const guid : observedUnits)
        {
            Unit* unit = botAI->GetUnit(guid);
            if (!IsTwinBossUnit(botAI, unit))
                continue;

            if (member->IsInCombat() && memberTargetGuid == unit->GetGUID())
                return true;

            if (unit->GetTarget() == memberGuid || (petGuid && unit->GetTarget() == petGuid) ||
                unit->GetVictim() == member || (pet && unit->GetVictim() == pet))
                return true;
        }
    }

    return false;
}

bool IsTwinCombatInProgress(Player* bot, PlayerbotAI* botAI, GuidVector const& attackers)
{
    if (!bot || !botAI)
        return false;

    GuidVector observedUnits = BuildTwinObservedUnits(bot, botAI, attackers);
    for (ObjectGuid const guid : observedUnits)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!IsTwinBossUnit(botAI, unit))
            continue;

        if (unit->IsInCombat() || unit->GetVictim() || unit->GetTarget())
            return true;
    }

    return false;
}

bool IsTwinPrePullReady(Player* bot, PlayerbotAI* botAI)
{
    if (!bot || !botAI || !bot->IsAlive())
        return false;

    if (!Aq40BossHelper::IsInAq40(bot) || !IsInTwinRoomBounds(bot))
        return false;

    if (bot->IsInCombat() || Aq40BossHelper::IsEncounterCombatActive(bot) || IsTwinRaidCombatActiveInternal(bot))
        return false;

    return HasTwinVisibleEmperors(bot, botAI);
}

bool IsLikelyOnSameTwinSide(Unit* unit, Unit* sideEmperor, Unit* oppositeEmperor)
{
    if (!unit || !sideEmperor)
        return false;

    if (!oppositeEmperor)
        return true;

    return unit->GetDistance2d(sideEmperor) <= unit->GetDistance2d(oppositeEmperor);
}

bool IsTwinWarlockPickupEstablished(Player* bot, PlayerbotAI* botAI, TwinAssignments const& assignment)
{
    if (!bot || !botAI || !assignment.veklor)
        return false;

    Group const* group = bot->GetGroup();
    if (!group)
        return false;

    for (GroupReference const* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || !member->IsAlive() || !Aq40BossHelper::IsDesignatedTwinWarlockTank(member))
            continue;

        PlayerbotAI* memberAI = GET_PLAYERBOT_AI(member);
        if (GetStableTwinRoleIndex(member, memberAI ? memberAI : botAI) != assignment.veklorSideIndex)
            continue;

        if (IsTwinPickupMemberEstablished(member, assignment, assignment.veklor))
            return true;
    }

    return false;
}

bool IsTwinMeleePickupEstablished(Player* bot, PlayerbotAI* botAI, TwinAssignments const& assignment)
{
    if (!bot || !botAI || !assignment.veknilash)
        return false;

    Group const* group = bot->GetGroup();
    if (!group)
        return false;

    for (GroupReference const* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || !member->IsAlive() || !PlayerbotAI::IsTank(member) || PlayerbotAI::IsRanged(member))
            continue;

        PlayerbotAI* memberAI = GET_PLAYERBOT_AI(member);
        if (GetStableTwinRoleIndex(member, memberAI ? memberAI : botAI) != assignment.veknilashSideIndex)
            continue;

        if (IsTwinPickupMemberEstablished(member, assignment, assignment.veknilash))
            return true;
    }

    return false;
}

bool HasTwinBossesResolved(Player* bot, PlayerbotAI* botAI, GuidVector const& attackers)
{
    TwinAssignments const assignments = GetTwinAssignments(bot, botAI, attackers);
    return assignments.veklor && assignments.veknilash;
}

// ---------------------------------------------------------------------------
// Non-Twin-Emperors helpers (C'Thun, Skeram, resistance, cleanup)
// ---------------------------------------------------------------------------

bool IsCthunInStomach(Player* bot, PlayerbotAI* botAI)
{
    if (!bot || !botAI)
        return false;

    return Aq40SpellIds::GetAnyAura(bot, { Aq40SpellIds::CthunDigestiveAcid }) ||
           botAI->GetAura("digestive acid", bot, false, false) ||
           botAI->GetAura("digestive acid", bot, false, true, 1);
}

uint32 GetCthunPhase2ElapsedMs(PlayerbotAI* botAI, GuidVector const& attackers)
{
    Unit* cthunBody = Aq40BossHelper::FindUnitByAnyName(botAI, attackers, { "c'thun" });
    Player* bot = botAI ? botAI->GetBot() : nullptr;
    uint32 const instanceId = (bot && bot->GetMap()) ? bot->GetMap()->GetInstanceId() : 0;

    if (bot && !Aq40BossHelper::IsEncounterCombatActive(bot))
    {
        sCthunPhase2StartByInstance.erase(instanceId);
        return 0;
    }

    if (!cthunBody)
    {
        if (!Aq40BossHelper::HasAnyNamedUnit(botAI, attackers,
                { "eye of c'thun", "flesh tentacle", "eye tentacle",
                  "giant eye tentacle", "claw tentacle", "giant claw tentacle" }))
        {
            sCthunPhase2StartByInstance.erase(instanceId);
            return 0;
        }

        auto itr = sCthunPhase2StartByInstance.find(instanceId);
        if (itr != sCthunPhase2StartByInstance.end())
            return getMSTime() - itr->second;

        return 0;
    }

    bool inPhase2 = Aq40BossHelper::HasAnyNamedUnit(botAI, attackers,
                                                    { "giant eye tentacle", "giant claw tentacle", "flesh tentacle" });
    if (!inPhase2)
    {
        sCthunPhase2StartByInstance.erase(instanceId);
        return 0;
    }

    uint32 const now = getMSTime();
    auto itr = sCthunPhase2StartByInstance.find(instanceId);
    if (itr == sCthunPhase2StartByInstance.end())
    {
        sCthunPhase2StartByInstance[instanceId] = now;
        return 0;
    }

    return now - itr->second;
}

bool IsCthunVulnerableNow(PlayerbotAI* botAI, GuidVector const& attackers)
{
    Unit* cthunBody = Aq40BossHelper::FindUnitByAnyName(botAI, attackers, { "c'thun" });
    if (!cthunBody)
        return false;

    return botAI->HasAura("weakened", cthunBody);
}

bool IsSkeramPostBlinkHoldActive(Player* bot, PlayerbotAI* botAI, GuidVector const& attackers)
{
    if (!bot || !botAI || !bot->GetMap())
        return false;

    uint32 const instanceId = bot->GetMap()->GetInstanceId();
    if (!Aq40BossHelper::IsEncounterCombatActive(bot))
    {
        sSkeramPostBlinkHoldUntilByInstance.erase(instanceId);
        return false;
    }

    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, attackers);
    std::vector<Unit*> skerams = Aq40BossHelper::FindUnitsByAnyName(botAI, encounterUnits, { "the prophet skeram" });
    if (skerams.empty())
    {
        sSkeramPostBlinkHoldUntilByInstance.erase(instanceId);
        return false;
    }

    bool primaryTankOwnsSkeram = false;
    for (Unit* skeram : skerams)
    {
        if (Aq40BossHelper::IsUnitHeldByEncounterTank(bot, skeram, true))
        {
            primaryTankOwnsSkeram = true;
            break;
        }
    }

    uint32 const now = getMSTime();
    auto itr = sSkeramPostBlinkHoldUntilByInstance.find(instanceId);
    if (!primaryTankOwnsSkeram)
    {
        sSkeramPostBlinkHoldUntilByInstance[instanceId] = now + 2500;
        return true;
    }

    if (itr == sSkeramPostBlinkHoldUntilByInstance.end())
        return false;

    if (now >= itr->second)
    {
        sSkeramPostBlinkHoldUntilByInstance.erase(itr);
        return false;
    }

    return true;
}

GameObject* FindLikelyStomachExitPortal(Player* bot, PlayerbotAI* botAI)
{
    static constexpr uint32 CTHUN_EXIT_PORTAL_ENTRY = 180523;
    GameObject* portal = bot->FindNearestGameObject(CTHUN_EXIT_PORTAL_ENTRY, 150.0f);
    if (portal)
        return portal;

    GuidVector nearbyGameObjects = *botAI->GetAiObjectContext()->GetValue<GuidVector>("nearest game objects");
    GameObject* candidate = nullptr;
    float bestDistance = 999.0f;

    for (ObjectGuid const guid : nearbyGameObjects)
    {
        GameObject* go = botAI->GetGameObject(guid);
        if (!go)
            continue;

        std::string name = go->GetName();
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        bool looksLikeExit = name.find("portal") != std::string::npos ||
                             name.find("exit") != std::string::npos ||
                             name.find("teleport") != std::string::npos;
        if (!looksLikeExit)
            continue;

        float distance = bot->GetDistance2d(go);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            candidate = go;
        }
    }

    return candidate;
}

bool ResetEncounterState(Player* bot)
{
    if (!bot || !bot->GetMap())
        return false;

    uint32 const instanceId = bot->GetMap()->GetInstanceId();
    bool erased = false;

    erased = sTwinAssignments.erase(instanceId) > 0 || erased;
    erased = sTwinKnownTwinBosses.erase(instanceId) > 0 || erased;
    erased = sCthunPhase2StartByInstance.erase(instanceId) > 0 || erased;
    erased = sCachedTwinSplitByX.erase(instanceId) > 0 || erased;
    erased = sTwinSideZeroIsLowSide.erase(instanceId) > 0 || erased;
    erased = sSkeramPostBlinkHoldUntilByInstance.erase(instanceId) > 0 || erased;

    return erased;
}

bool HasPersistentEncounterState(Player* bot)
{
    if (!bot || !bot->GetMap())
        return false;

    uint32 const instanceId = bot->GetMap()->GetInstanceId();
    return sTwinAssignments.find(instanceId) != sTwinAssignments.end() ||
           sTwinKnownTwinBosses.find(instanceId) != sTwinKnownTwinBosses.end() ||
           sCthunPhase2StartByInstance.find(instanceId) != sCthunPhase2StartByInstance.end() ||
           sCachedTwinSplitByX.find(instanceId) != sCachedTwinSplitByX.end() ||
           sTwinSideZeroIsLowSide.find(instanceId) != sTwinSideZeroIsLowSide.end() ||
           sSkeramPostBlinkHoldUntilByInstance.find(instanceId) != sSkeramPostBlinkHoldUntilByInstance.end();
}

bool HasManagedResistanceStrategy(Player* bot, PlayerbotAI* botAI)
{
    if (!bot || !botAI)
        return false;

    switch (bot->getClass())
    {
        case CLASS_HUNTER:
            return botAI->HasStrategy("rnature", BotState::BOT_STATE_COMBAT) ||
                   botAI->HasStrategy("rnature", BotState::BOT_STATE_NON_COMBAT);
        case CLASS_SHAMAN:
            return botAI->HasStrategy("nature resistance", BotState::BOT_STATE_COMBAT);
        case CLASS_PRIEST:
        case CLASS_PALADIN:
            return botAI->HasStrategy("rshadow", BotState::BOT_STATE_COMBAT) ||
                   botAI->HasStrategy("rshadow", BotState::BOT_STATE_NON_COMBAT);
        case CLASS_WARLOCK:
            return bot->HasAura(Aq40SpellIds::TwinWarlockShadowResistBuff);
        default:
            return false;
    }
}

bool IsResistanceManagementNeeded(Player* bot, PlayerbotAI* botAI, GuidVector const& attackers)
{
    if (!bot || !botAI || !Aq40BossHelper::IsInAq40(bot))
        return false;

    GuidVector const activeUnits = Aq40BossHelper::GetActiveCombatUnits(botAI, attackers);
    bool const needNatureResistance =
        Aq40BossHelper::HasAnyNamedUnit(botAI, activeUnits,
            { "princess huhuran", "viscidus", "glob of viscidus", "toxic slime" });
    bool const needShadowResistance =
        Aq40BossHelper::HasAnyNamedUnit(botAI, activeUnits,
            { "emperor vek'nilash", "emperor vek'lor" });

    switch (bot->getClass())
    {
        case CLASS_HUNTER:
        case CLASS_SHAMAN:
            return needNatureResistance || HasManagedResistanceStrategy(bot, botAI);
        case CLASS_PRIEST:
        case CLASS_PALADIN:
            return needShadowResistance || HasManagedResistanceStrategy(bot, botAI);
        default:
            return false;
    }
}

bool ShouldRunOutOfCombatMaintenance(Player* bot, PlayerbotAI* botAI)
{
    if (!bot || !botAI)
        return false;

    bool const hasManagedResistanceStrategy = HasManagedResistanceStrategy(bot, botAI);
    bool const hasPersistentEncounterState = HasPersistentEncounterState(bot);

    if (hasManagedResistanceStrategy)
        return true;

    if (!hasPersistentEncounterState)
        return false;

    if (IsAnyGroupMemberInTwinRoomInternal(bot))
        return false;

    if (IsTwinPrePullReady(bot, botAI))
        return false;

    if (IsInTwinRoomBounds(bot) && HasTwinVisibleEmperors(bot, botAI))
        return false;

    if (IsTwinRaidCombatActiveInternal(bot))
        return false;

    return true;
}

bool IsAnyGroupMemberInTwinRoom(Player* bot)
{
    return IsAnyGroupMemberInTwinRoomInternal(bot);
}
}    // namespace Aq40Helpers
