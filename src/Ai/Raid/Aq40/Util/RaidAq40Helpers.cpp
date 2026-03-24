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

struct TwinTeleportState
{
    Position veklorPosition;
    Position veknilashPosition;
    uint32 lastTeleportMs = 0;
    bool initialized = false;
};

TwinInstanceAssignments sTwinAssignments;
std::unordered_map<uint32, TwinTeleportState> sTwinTeleportStates;
std::unordered_map<uint32, uint32> sCthunPhase2StartByInstance;
std::unordered_map<uint32, bool> sCachedTwinSplitByX;  // Cached split axis per instance

Unit* FindTwinUnit(PlayerbotAI* botAI, GuidVector const& attackers, char const* name)
{
    return Aq40BossHelper::FindUnitByAnyName(botAI, attackers, { name });
}

uint32 GetGroupMemberOrder(Player* bot, Player* member)
{
    if (!bot || !member)
        return std::numeric_limits<uint32>::max();

    Group const* group = bot->GetGroup();
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

Player* FindTwinAssignedPlayerForSide(Player* bot, TwinRoleCohort cohort, uint32 sideIndex)
{
    if (!bot)
        return nullptr;

    Group const* group = bot->GetGroup();
    if (!group)
        return nullptr;

    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!IsTwinRoleMatch(cohort, member))
            continue;
        if (!Aq40BossHelper::IsEncounterParticipant(bot, member))
            continue;

        if (GetStableTwinRoleIndex(member, GET_PLAYERBOT_AI(member)) == sideIndex)
            return member;
    }

    return nullptr;
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

    // Clean stale state on wipe/re-pull: only when no encounter-local
    // cluster of combatants exists.  Perspective-independent so an isolated
    // bot cannot wipe shared assignments, but distant trash combat does not
    // preserve stale boss state after a wipe.
    if (!Aq40BossHelper::IsEncounterCombatActive(bot))
    {
        sTwinAssignments.erase(instanceId);
        sTwinTeleportStates.erase(instanceId);
        sCachedTwinSplitByX.erase(instanceId);
    }

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

    TwinRoleCohort const cohort = GetTwinRoleCohort(bot, botAI);

    // DPS follows their assigned boss across the room after teleport.
    //  - Ranged DPS → always Vek'lor  (caster, immune to physical)
    //  - Melee DPS  → always Vek'nilash (melee, immune to magic)
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
        UpdateTwinTeleportState(bot, result);
        return result;
    }

    // Tanks and healers are assigned a stable geographic room side.
    // Each side has one warlock tank and one melee tank.  When its boss
    // is the correct type the tank is "active" — otherwise it stands back.
    // Healers stay on their assigned side healing whoever is there.
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

    Group* group = bot->GetGroup();
    if (!group)
        return false;

    bool const needWarlockTank = assignment.veklor && assignment.sideEmperor == assignment.veklor;
    TwinRoleCohort const requiredCohort = needWarlockTank ? TwinRoleCohort::WarlockTank : TwinRoleCohort::MeleeTank;
    float const readyRange = needWarlockTank ? 34.0f : 8.0f;

    // Use stable side assignment to identify the tank for THIS side,
    // not proximity which can misclassify during teleport crossings.
    uint32 const botSideIndex = GetStableTwinRoleIndex(bot, botAI);

    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || !member->IsAlive() || !IsTwinRoleMatch(requiredCohort, member))
            continue;

        PlayerbotAI* memberAI = GET_PLAYERBOT_AI(member);

        if (GetStableTwinRoleIndex(member, memberAI) != botSideIndex)
            continue;

        if (assignment.sideEmperor->GetVictim() == member)
            return true;

        if (member->GetDistance2d(assignment.sideEmperor) <= readyRange &&
            member->GetTarget() == assignment.sideEmperor->GetGUID())
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
}  // namespace Aq40Helpers
