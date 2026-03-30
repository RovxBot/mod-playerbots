#include "RaidAq40Helpers.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <vector>

#include "GameObject.h"
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

// Ground Twin Emps room geometry in repo travel-node data:
// - The Master's Eye = mid-room anchor
// - Emperor Vek'lor / Vek'nilash = initial boss-side references
float constexpr kTwinRoomCenterX = -8953.3f;
float constexpr kTwinRoomCenterY = 1233.64f;
float constexpr kTwinRoomCenterZ = -99.718f;
float constexpr kTwinRoomStageRadius = 150.0f;
float constexpr kTwinRoomStageZTolerance = 18.0f;
float constexpr kTwinPrePullDetectionRange = 320.0f;

struct TwinTeleportState
{
    Position veklorPosition;
    Position veknilashPosition;
    uint32 encounterStartMs = 0;
    uint32 lastTeleportMs = 0;
    bool initialized = false;
};

TwinInstanceAssignments sTwinAssignments;
std::unordered_map<uint32, TwinTeleportState> sTwinTeleportStates;
std::unordered_map<uint32, uint32> sCthunPhase2StartByInstance;
std::unordered_map<uint32, bool> sCachedTwinSplitByX;  // Cached split axis per instance
std::unordered_map<uint32, bool> sTwinSideZeroIsLowSide;
std::unordered_map<uint32, uint32> sSkeramPostBlinkHoldUntilByInstance;

Unit* FindTwinUnit(PlayerbotAI* botAI, GuidVector const& attackers, char const* name)
{
    return Aq40BossHelper::FindUnitByAnyName(botAI, attackers, { name });
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

bool HasTwinPrePullVisibility(Player* bot, PlayerbotAI* botAI, GuidVector* outUnits = nullptr)
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

bool HasTwinDeadGroupMemberAwaitingRecovery(Player* bot)
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
        if (!member || member == bot || member->IsAlive())
            continue;
        if (member->GetMapId() != bot->GetMapId())
            continue;
        if (member->GetMap() && member->GetMap()->GetInstanceId() != instanceId)
            continue;
        if (!IsInTwinRoomBounds(member) && !Aq40BossHelper::IsNearEncounter(bot, member))
            continue;

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
            // Best shadow-resist warlock starts on the initial Vek'lor side
            // so the caster boss is picked up immediately on pull.
            return (slot % 2 == 0) ? 1u : 0u;
        case TwinRoleCohort::MeleeTank:
            // Main tank starts on the initial Vek'nilash side, off-tank on
            // the opposite side waiting for the first teleport pickup.
            return slot % 2;
        case TwinRoleCohort::Healer:
        case TwinRoleCohort::Other:
            return slot % 2;
    }

    return slot % 2;
}

void UpdateTwinTeleportState(Player* bot, TwinAssignments const& assignments)
{
    if (!bot || !bot->GetMap() || !assignments.veklor || !assignments.veknilash)
        return;

    TwinTeleportState& state = sTwinTeleportStates[bot->GetMap()->GetInstanceId()];
    if (!state.encounterStartMs && IsTwinRaidCombatActiveInternal(bot))
        state.encounterStartMs = getMSTime();

    Position const veklorPosition = assignments.veklor->GetPosition();
    Position const veknilashPosition = assignments.veknilash->GetPosition();
    if (!state.initialized)
    {
        state.veklorPosition = veklorPosition;
        state.veknilashPosition = veknilashPosition;
        state.initialized = true;
        return;
    }

    float const veklorMove = state.veklorPosition.GetExactDist2d(veklorPosition);
    float const veknilashMove = state.veknilashPosition.GetExactDist2d(veknilashPosition);
    if ((veklorMove > 18.0f && veknilashMove > 18.0f) || veklorMove > 35.0f || veknilashMove > 35.0f)
        state.lastTeleportMs = getMSTime();

    state.veklorPosition = veklorPosition;
    state.veknilashPosition = veknilashPosition;
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

}  // namespace

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
        // Preserve side assignments while the raid is staged inside the Twin room
        // so the pre-pull tank split survives until the actual pull.
        sTwinTeleportStates.erase(instanceId);

        if (!IsInTwinRoomBounds(bot) && !twinVisiblePrePull)
        {
            sTwinAssignments.erase(instanceId);
            sCachedTwinSplitByX.erase(instanceId);
            sTwinSideZeroIsLowSide.erase(instanceId);
        }
    }

    TwinRoleCohort const cohort = GetTwinRoleCohort(bot, botAI);
    TwinRoleAssignmentMap& assignments = sTwinAssignments[instanceId][static_cast<int>(cohort)];
    uint64 const botGuid = bot->GetGUID().GetRawValue();

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
    result.veklor = FindTwinUnit(botAI, attackers, "emperor vek'lor");
    result.veknilash = FindTwinUnit(botAI, attackers, "emperor vek'nilash");
    if (!result.veklor || !result.veknilash)
        return result;

    TwinRoleCohort const cohort = GetTwinRoleCohort(bot, botAI);
    uint32 const instanceId = bot->GetMap() ? bot->GetMap()->GetInstanceId() : 0;

    // Determine which axis separates the two bosses (cache during teleport
    // to avoid flicker as the bosses pass through the center).
    float separation = result.veklor->GetDistance2d(result.veknilash);
    bool splitByX;
    auto axisIt = sCachedTwinSplitByX.find(instanceId);
    if (separation > 15.0f)
    {
        splitByX = std::abs(result.veklor->GetPositionX() - result.veknilash->GetPositionX()) >=
                   std::abs(result.veklor->GetPositionY() - result.veknilash->GetPositionY());
        sCachedTwinSplitByX[instanceId] = splitByX;
    }
    else if (axisIt != sCachedTwinSplitByX.end())
    {
        splitByX = axisIt->second;
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

    // Side index 0 is the initial Vek'nilash pull side; side index 1 is the
    // initial Vek'lor side. This keeps the main tank on the melee-pull side
    // and the off-tank/active warlock staged on the caster side from pull.
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

    // DPS always attacks its assigned emperor, but readiness checks still need
    // the current room-side index for that emperor so they wait on the correct pickup tank.
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
        UpdateTwinTeleportState(bot, result);
        return result;
    }

    uint32 const sideIndex = GetStableTwinRoleIndex(bot, botAI);
    result.sideIndex = sideIndex;
    result.sideEmperor = sideIndex == 0 ? sideZeroBoss : sideOneBoss;
    result.oppositeEmperor = sideIndex == 0 ? sideOneBoss : sideZeroBoss;
    UpdateTwinTeleportState(bot, result);
    return result;
}

GuidVector GetTwinPrePullUnits(Player* bot, PlayerbotAI* botAI)
{
    return CollectTwinVisibleUnits(bot, botAI);
}

GuidVector GetTwinEncounterUnits(Player* bot, PlayerbotAI* botAI, GuidVector const& attackers)
{
    GuidVector units = Aq40BossHelper::GetEncounterUnits(botAI, attackers);
    if (!bot || !botAI)
        return units;

    GuidVector visibleTwins;
    if (!HasTwinPrePullVisibility(bot, botAI, &visibleTwins))
        return units;

    if (!bot->IsInCombat() && !Aq40BossHelper::IsEncounterCombatActive(bot) && !IsTwinRaidCombatActiveInternal(bot))
        return units;

    for (ObjectGuid const guid : visibleTwins)
    {
        if (std::find(units.begin(), units.end(), guid) == units.end())
            units.push_back(guid);
    }

    return units;
}

bool IsInTwinEmperorRoom(Player* bot)
{
    return IsInTwinRoomBounds(bot);
}

bool IsTwinRaidCombatActive(Player* bot)
{
    return IsTwinRaidCombatActiveInternal(bot);
}

bool IsTwinPrePullReady(Player* bot, PlayerbotAI* botAI)
{
    if (!bot || !botAI || !bot->IsAlive())
        return false;

    if (!Aq40BossHelper::IsInAq40(bot) || !IsInTwinRoomBounds(bot))
        return false;

    if (bot->IsInCombat() || Aq40BossHelper::IsEncounterCombatActive(bot) || IsTwinRaidCombatActiveInternal(bot))
        return false;

    if (HasTwinDeadGroupMemberAwaitingRecovery(bot))
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

bool IsTwinMutateBug(PlayerbotAI* botAI, Unit* unit)
{
    if (!botAI || !unit)
        return false;

    return Aq40SpellIds::HasAnyAura(botAI, unit, { Aq40SpellIds::TwinMutateBug }) ||
           botAI->EqualLowercaseName(unit->GetName(), "mutate bug");
}

bool IsTwinExplodeBug(PlayerbotAI* botAI, Unit* unit)
{
    return botAI && unit && Aq40SpellIds::HasAnyAura(botAI, unit, { Aq40SpellIds::TwinExplodeBug });
}

bool IsTwinCriticalSideBug(Player* bot, PlayerbotAI* botAI, TwinAssignments const& assignment, Unit* bug)
{
    if (!bot || !botAI || !bug || !bug->IsAlive() || !assignment.sideEmperor)
        return false;

    if (!IsLikelyOnSameTwinSide(bug, assignment.sideEmperor, assignment.oppositeEmperor))
        return false;

    if (IsTwinMutateBug(botAI, bug) || IsTwinExplodeBug(botAI, bug))
        return true;

    Unit* victim = bug->GetVictim();
    if (victim)
    {
        if (Player* victimPlayer = victim->ToPlayer())
        {
            PlayerbotAI* victimAI = GET_PLAYERBOT_AI(victimPlayer);
            if (victimPlayer == bot ||
                Aq40BossHelper::IsDesignatedTwinWarlockTank(victimPlayer) ||
                (victimAI && victimAI->IsHeal(victimPlayer)))
                return true;
        }
    }

    return bug->GetDistance2d(assignment.sideEmperor) <= 10.0f;
}

bool IsTwinTeleportRecoveryWindow(Player* bot, PlayerbotAI* botAI, GuidVector const& attackers)
{
    TwinAssignments const assignments = GetTwinAssignments(bot, botAI, attackers);
    if (!bot || !bot->GetMap() || !assignments.veklor || !assignments.veknilash)
        return false;

    auto const itr = sTwinTeleportStates.find(bot->GetMap()->GetInstanceId());
    if (itr == sTwinTeleportStates.end() || !itr->second.lastTeleportMs)
        return false;

    return (getMSTime() - itr->second.lastTeleportMs) <= 3500;
}

bool IsTwinDpsWaitWindow(Player* bot, PlayerbotAI* botAI, GuidVector const& attackers)
{
    TwinAssignments const assignments = GetTwinAssignments(bot, botAI, attackers);
    if (!bot || !botAI || !bot->GetMap() || !assignments.veklor || !assignments.veknilash)
        return false;

    auto const itr = sTwinTeleportStates.find(bot->GetMap()->GetInstanceId());
    if (itr == sTwinTeleportStates.end())
        return false;

    uint32 const now = getMSTime();
    if (!itr->second.lastTeleportMs && itr->second.encounterStartMs &&
        (now - itr->second.encounterStartMs) <= 5000)
        return true;

    if (itr->second.lastTeleportMs &&
        (now - itr->second.lastTeleportMs) <= 4000)
        return true;

    return false;
}

bool IsTwinPreTeleportWindow(Player* bot, PlayerbotAI* botAI, GuidVector const& attackers)
{
    TwinAssignments const assignments = GetTwinAssignments(bot, botAI, attackers);
    if (!bot || !bot->GetMap() || !assignments.veklor || !assignments.veknilash)
        return false;

    auto const itr = sTwinTeleportStates.find(bot->GetMap()->GetInstanceId());
    if (itr == sTwinTeleportStates.end())
        return false;

    uint32 const referenceMs = itr->second.lastTeleportMs ? itr->second.lastTeleportMs : itr->second.encounterStartMs;
    if (!referenceMs)
        return false;

    uint32 const elapsed = getMSTime() - referenceMs;
    return elapsed >= 25000 && elapsed <= 38000;
}

bool IsTwinAssignedTankReady(Player* bot, PlayerbotAI* botAI, TwinAssignments const& assignment)
{
    if (!bot || !assignment.sideEmperor)
        return false;

    Group const* group = bot->GetGroup();
    if (!group)
        return false;

    bool const needWarlockTank = assignment.veklor && assignment.sideEmperor == assignment.veklor;
    TwinRoleCohort const requiredCohort = needWarlockTank ? TwinRoleCohort::WarlockTank : TwinRoleCohort::MeleeTank;
    float const readyRange = needWarlockTank ? 34.0f : 8.0f;

    for (GroupReference const* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || !member->IsAlive() || !IsTwinRoleMatch(requiredCohort, member))
            continue;

        PlayerbotAI* memberAI = GET_PLAYERBOT_AI(member);

        if (GetStableTwinRoleIndex(member, memberAI) != assignment.sideIndex)
            continue;

        bool const bossTargetingAssignedTank =
            assignment.sideEmperor->GetVictim() == member ||
            assignment.sideEmperor->GetTarget() == member->GetGUID();
        if (bossTargetingAssignedTank)
            return true;

        if (needWarlockTank)
            continue;

        Unit* memberCurrentTarget = memberAI ? memberAI->GetAiObjectContext()->GetValue<Unit*>("current target")->Get() : nullptr;
        bool const targetingAssignedBoss =
            member->GetVictim() == assignment.sideEmperor ||
            memberCurrentTarget == assignment.sideEmperor ||
            member->GetTarget() == assignment.sideEmperor->GetGUID();
        if (targetingAssignedBoss &&
            member->IsWithinLOSInMap(assignment.sideEmperor) &&
            member->GetDistance2d(assignment.sideEmperor) <= (readyRange + 4.0f))
            return true;
    }

    return false;
}

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

    // Wipe/reset safety: if no encounter cluster is active near the caller,
    // clear the timer unconditionally so stale state cannot survive a repull.
    if (bot && !Aq40BossHelper::IsEncounterCombatActive(bot))
    {
        sCthunPhase2StartByInstance.erase(instanceId);
        return 0;
    }

    if (!cthunBody)
    {
    // Body not in this caller's view.  Only erase the timer if no
    // C'Thun-related unit is visible at all (encounter over).  A bot
    // that sees tentacles but not the body must not zero the shared
    // timer — other callers who CAN see the body will maintain it.
        if (!Aq40BossHelper::HasAnyNamedUnit(botAI, attackers,
                { "eye of c'thun", "flesh tentacle", "eye tentacle",
                  "giant eye tentacle", "claw tentacle", "giant claw tentacle" }))
        {
            sCthunPhase2StartByInstance.erase(instanceId);
            return 0;
        }

    // Tentacles visible but body not — return the stored elapsed time
    // so wave prediction stays consistent with bots that can see the body.
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
    // Primary: find by entry ID (AzerothCore C'Thun script portal entry).
    // Pattern lifted from Karazhan NPC_GREEN/BLUE/RED_PORTAL entry ID lookups.
    static constexpr uint32 CTHUN_EXIT_PORTAL_ENTRY = 180523;
    GameObject* portal = bot->FindNearestGameObject(CTHUN_EXIT_PORTAL_ENTRY, 150.0f);
    if (portal)
        return portal;

    // Fallback: string matching for non-standard server scripts.
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
    erased = sTwinTeleportStates.erase(instanceId) > 0 || erased;
    erased = sCthunPhase2StartByInstance.erase(instanceId) > 0 || erased;
    erased = sCachedTwinSplitByX.erase(instanceId) > 0 || erased;
    erased = sTwinSideZeroIsLowSide.erase(instanceId) > 0 || erased;
    erased = sSkeramPostBlinkHoldUntilByInstance.erase(instanceId) > 0 || erased;

    return erased;
}
}    // namespace Aq40Helpers
