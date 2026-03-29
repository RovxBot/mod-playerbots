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
std::unordered_map<uint32, bool> sTwinSideZeroIsLowSide;
std::unordered_map<uint32, uint32> sSkeramPostBlinkHoldUntilByInstance;

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

    for (GroupReference const* ref = group->GetFirstMember(); ref; ref = ref->next())
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
        sTwinSideZeroIsLowSide.erase(instanceId);
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

    // Tanks and healers are assigned stable room sides. Side 0 is locked to
    // the initial Vek'nilash pull side and side 1 to the initial Vek'lor side
    // so the main tank starts on melee, while the off-tank and active warlock
    // start on caster. After teleport, tanks remain on their room side and the
    // correct side-local pickup becomes active.
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

    uint32 const sideIndex = GetStableTwinRoleIndex(bot, botAI);
    result.sideEmperor = sideIndex == 0 ? sideZeroBoss : sideOneBoss;
    result.oppositeEmperor = sideIndex == 0 ? sideOneBoss : sideZeroBoss;
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

bool IsTwinPreTeleportWindow(Player* bot, PlayerbotAI* botAI, GuidVector const& attackers)
{
    TwinAssignments const assignments = GetTwinAssignments(bot, botAI, attackers);
    if (!bot || !bot->GetMap() || !assignments.veklor || !assignments.veknilash)
        return false;

    auto const itr = sTwinTeleportStates.find(bot->GetMap()->GetInstanceId());
    if (itr == sTwinTeleportStates.end())
        return false;

    if (!itr->second.lastTeleportMs)
        return false;

    uint32 const elapsed = getMSTime() - itr->second.lastTeleportMs;
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

    // Use stable side assignment to identify the tank for THIS side,
    // not proximity which can misclassify during teleport crossings.
    uint32 const botSideIndex = GetStableTwinRoleIndex(bot, botAI);

    for (GroupReference const* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || !member->IsAlive() || !IsTwinRoleMatch(requiredCohort, member))
            continue;

        PlayerbotAI* memberAI = GET_PLAYERBOT_AI(member);

        if (GetStableTwinRoleIndex(member, memberAI) != botSideIndex)
            continue;

        if (assignment.sideEmperor->GetVictim() == member)
            return true;

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
