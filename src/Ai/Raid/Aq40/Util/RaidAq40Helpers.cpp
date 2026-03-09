#include "RaidAq40Helpers.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <vector>

#include "GameObject.h"
#include "../RaidAq40BossHelper.h"
#include "../RaidAq40SpellIds.h"
#include "Timer.h"

namespace Aq40Helpers
{
namespace
{
using TwinRoleAssignmentMap = std::unordered_map<uint64, uint32>;
using TwinInstanceAssignments = std::unordered_map<int, TwinRoleAssignmentMap>;

struct TwinTeleportState
{
    Position veklorPosition;
    Position veknilashPosition;
    uint32 lastTeleportMs = 0;
    bool initialized = false;
};

TwinInstanceAssignments sTwinAssignments;
std::unordered_map<uint32, TwinTeleportState> sTwinTeleportStates;
std::unordered_map<uint64, uint32> sCthunPhase2StartByBossGuid;

Unit* FindTwinUnit(PlayerbotAI* botAI, GuidVector const& attackers, char const* name)
{
    return Aq40BossHelper::FindUnitByAnyName(botAI, attackers, { name });
}

uint32 GetGroupMemberOrder(Player* bot, Player* member)
{
    if (!bot || !member)
        return std::numeric_limits<uint32>::max();

    Group* group = bot->GetGroup();
    if (!group)
        return std::numeric_limits<uint32>::max();

    uint32 order = 0;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next(), ++order)
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

void UpdateTwinTeleportState(Player* bot, TwinAssignments const& assignments)
{
    if (!bot || !bot->GetMap() || !assignments.veklor || !assignments.veknilash)
        return;

    TwinTeleportState& state = sTwinTeleportStates[bot->GetMap()->GetInstanceId()];
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

Player* FindTwinAssignedPlayerForSide(Player* bot, TwinRoleCohort cohort, uint32 sideIndex)
{
    if (!bot)
        return nullptr;

    Group* group = bot->GetGroup();
    if (!group)
        return nullptr;

    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!IsTwinRoleMatch(cohort, member))
            continue;

        if (GetStableTwinRoleIndex(member, GET_PLAYERBOT_AI(member)) == sideIndex)
            return member;
    }

    return nullptr;
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

    Group* group = bot->GetGroup();
    if (!group)
        return static_cast<uint32>(bot->GetGUID().GetCounter() % 2);

    uint32 const instanceId = bot->GetMap() ? bot->GetMap()->GetInstanceId() : 0;
    TwinRoleCohort const cohort = GetTwinRoleCohort(bot, botAI);
    TwinRoleAssignmentMap& assignments = sTwinAssignments[instanceId][static_cast<int>(cohort)];
    uint64 const botGuid = bot->GetGUID().GetRawValue();

    if (assignments.find(botGuid) != assignments.end())
        return assignments[botGuid];

    std::vector<Player*> members;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!IsTwinRoleMatch(cohort, member))
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

        assignments[memberGuid] = assignedCount % 2;
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

    bool splitByX = std::abs(result.veklor->GetPositionX() - result.veknilash->GetPositionX()) >=
                    std::abs(result.veklor->GetPositionY() - result.veknilash->GetPositionY());
    float veklorAxis = splitByX ? result.veklor->GetPositionX() : result.veklor->GetPositionY();
    float veknilashAxis = splitByX ? result.veknilash->GetPositionX() : result.veknilash->GetPositionY();
    Unit* lowSide = veklorAxis < veknilashAxis ? result.veklor : result.veknilash;
    Unit* highSide = lowSide == result.veklor ? result.veknilash : result.veklor;

    uint32 const sideIndex = GetStableTwinRoleIndex(bot, botAI);
    result.sideEmperor = sideIndex == 0 ? lowSide : highSide;
    result.oppositeEmperor = result.sideEmperor == lowSide ? highSide : lowSide;
    UpdateTwinTeleportState(bot, result);
    return result;
}

bool IsLikelyOnSameTwinSide(Unit* unit, Unit* sideEmperor, Unit* oppositeEmperor)
{
    if (!unit || !sideEmperor)
        return false;

    if (!oppositeEmperor)
        return true;

    return unit->GetDistance2d(sideEmperor) <= unit->GetDistance2d(oppositeEmperor);
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

bool IsTwinAssignedTankReady(Player* bot, PlayerbotAI* botAI, TwinAssignments const& assignment)
{
    if (!bot || !assignment.sideEmperor)
        return false;

    uint32 const sideIndex = GetStableTwinRoleIndex(bot, botAI);
    bool const needWarlockTank = assignment.veklor && assignment.sideEmperor == assignment.veklor;
    TwinRoleCohort const requiredCohort = needWarlockTank ? TwinRoleCohort::WarlockTank : TwinRoleCohort::MeleeTank;
    Player* assignedTank = FindTwinAssignedPlayerForSide(bot, requiredCohort, sideIndex);
    if (!assignedTank)
        return false;

    float const readyRange = needWarlockTank ? 34.0f : 8.0f;
    if (assignment.sideEmperor->GetVictim() == assignedTank)
        return true;

    if (assignedTank->GetDistance2d(assignment.sideEmperor) > readyRange)
        return false;

    return assignedTank->GetTarget() == assignment.sideEmperor->GetGUID();
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
    if (!cthunBody)
        return 0;

    bool inPhase2 = Aq40BossHelper::HasAnyNamedUnit(botAI, attackers,
                                                    { "giant eye tentacle", "giant claw tentacle", "flesh tentacle" });
    uint64 const bodyGuid = cthunBody->GetGUID().GetRawValue();
    if (!inPhase2)
    {
        sCthunPhase2StartByBossGuid.erase(bodyGuid);
        return 0;
    }

    uint32 const now = getMSTime();
    auto itr = sCthunPhase2StartByBossGuid.find(bodyGuid);
    if (itr == sCthunPhase2StartByBossGuid.end())
    {
        sCthunPhase2StartByBossGuid[bodyGuid] = now;
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

GameObject* FindLikelyStomachExitPortal(Player* bot, PlayerbotAI* botAI)
{
    GuidVector nearbyGameObjects = AI_VALUE(GuidVector, "nearest game objects");
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
}  // namespace Aq40Helpers
