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

// Ground Twin Emps room geometry in repo travel-node data:
// - The Master's Eye = mid-room anchor
// - Emperor Vek'lor / Vek'nilash = initial boss-side references
float constexpr kTwinRoomCenterX = -8954.855f;
float constexpr kTwinRoomCenterY = 1235.7107f;
float constexpr kTwinRoomCenterZ = -112.62047f;
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
std::unordered_map<uint32, TwinKnownBossGuids> sTwinKnownTwinBosses;
std::unordered_map<uint32, uint32> sCthunPhase2StartByInstance;
std::unordered_map<uint32, bool> sCachedTwinSplitByX;  // Cached split axis per instance
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

void SeedTwinCombatTimer(Player* bot)
{
    if (!bot || !bot->GetMap() || !IsTwinRaidCombatActiveInternal(bot))
        return;

    TwinTeleportState& state = sTwinTeleportStates[bot->GetMap()->GetInstanceId()];
    if (!state.encounterStartMs)
        state.encounterStartMs = getMSTime();
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

bool IsTwinOuterAnchorPosition(Player* member, Unit* boss, Unit* oppositeBoss)
{
    if (!member || !boss)
        return false;

    if (oppositeBoss && !IsLikelyOnSameTwinSide(member, boss, oppositeBoss))
        return false;

    if (!oppositeBoss)
        return true;

    float const offsetX = member->GetPositionX() - boss->GetPositionX();
    float const offsetY = member->GetPositionY() - boss->GetPositionY();
    float const awayX = boss->GetPositionX() - oppositeBoss->GetPositionX();
    float const awayY = boss->GetPositionY() - oppositeBoss->GetPositionY();
    return (offsetX * awayX + offsetY * awayY) >= 0.0f;
}

bool IsTwinPickupMemberEstablished(Player* member, TwinAssignments const& assignment, Unit* boss)
{
    if (!member || !member->IsAlive() || !boss || !boss->IsAlive())
        return false;

    Unit* oppositeBoss = boss == assignment.veklor ? assignment.veknilash : assignment.veklor;
    if (!member->IsWithinLOSInMap(boss) || !HasTwinBossAggroInternal(member, boss))
        return false;

    float const distance = member->GetDistance2d(boss);
    float const minRange = boss == assignment.veklor ? 18.0f : 1.5f;
    // Vek'nilash max range raised from 10y to 15y so that Uppercut knockback
    // does not immediately flip the pickup flag false and freeze all DPS.
    float const maxRange = boss == assignment.veklor ? 36.0f : 15.0f;
    if (distance < minRange || distance > maxRange)
        return false;

    // During teleport recovery the tank may approach from any direction
    // (running across the room).  Requiring the outer-anchor dot-product
    // check would fail if the tank arrives on the "inner" side (between the
    // two bosses).  Once the tank has LOS + aggro + correct range the
    // pickup is established; outer positioning is a steady-state maintenance
    // concern that enforce-separation will handle.
    if (distance <= (boss == assignment.veklor ? 30.0f : 8.0f))
        return true;

    return IsTwinOuterAnchorPosition(member, boss, oppositeBoss);
}

bool IsTwinHealBrotherCastActive(TwinAssignments const& assignment)
{
    if (!assignment.veklor || !assignment.veknilash)
        return false;

    Spell* veklorSpell = assignment.veklor->GetCurrentSpell(CURRENT_GENERIC_SPELL);
    Spell* veknilashSpell = assignment.veknilash->GetCurrentSpell(CURRENT_GENERIC_SPELL);
    return (veklorSpell &&
            Aq40SpellIds::MatchesAnySpellId(veklorSpell->GetSpellInfo(), { Aq40SpellIds::TwinHealBrother })) ||
           (veknilashSpell &&
            Aq40SpellIds::MatchesAnySpellId(veknilashSpell->GetSpellInfo(), { Aq40SpellIds::TwinHealBrother }));
}

}  // namespace

bool HasTwinBossAggro(Player* member, Unit* boss)
{
    return HasTwinBossAggroInternal(member, boss);
}

bool IsTwinDpsDraggingMeleeBoss(Player* bot, PlayerbotAI* botAI, TwinAssignments const& assignment)
{
    if (!bot || !botAI || PlayerbotAI::IsTank(bot) || botAI->IsHeal(bot) || !assignment.veknilash)
        return false;

    return HasTwinBossAggroInternal(bot, assignment.veknilash);
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
        // Preserve side assignments while the raid is staged inside the Twin room
        // so the pre-pull tank split survives until the actual pull.
        sTwinTeleportStates.erase(instanceId);

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

    // Purge stale entries for players who are no longer in this cohort
    // (e.g. warlocks that died and are no longer designated tanks).
    // Without this, dead warlock GUIDs stay cached and prevent the
    // replacement warlock from getting the correct side assignment.
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

    // Determine which axis separates the two bosses.  The axis is cached at
    // encounter start and only recalculated when the bosses are very far apart
    // (safely separated).  Recalculating at shorter distances caused the axis
    // to flip when boss positions drifted, which reversed the side mapping and
    // broke all tank assignments mid-fight.
    float separation = result.veklor->GetDistance2d(result.veknilash);
    bool splitByX;
    auto axisIt = sCachedTwinSplitByX.find(instanceId);
    if (axisIt != sCachedTwinSplitByX.end())
    {
        // Use cached axis.  Only recalculate when bosses are very far apart
        // and clearly on their correct sides (post-teleport settled).
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

    result.veklorSideIndex = getBossSideIndex(result.veklor);
    result.veknilashSideIndex = getBossSideIndex(result.veknilash);

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

    // Use identity-based boss assignment for tanks so that sideEmperor is
    // always the boss they are supposed to engage, regardless of which
    // physical room-side that boss is currently on.  The previous room-side
    // mapping inverted after every teleport swap (bosses exchange positions
    // but sTwinSideZeroIsLowSide never updates), causing tanks to position
    // near the wrong boss while ChooseTarget sent them toward the correct
    // one — producing the back-and-forth oscillation.
    //
    // Healers keep the stable-index room-side mapping because they need to
    // spread across both sides independently of boss identity.
    if (cohort == TwinRoleCohort::WarlockTank)
    {
        result.sideEmperor = result.veklor;
        result.oppositeEmperor = result.veknilash;
        result.sideIndex = result.veklorSideIndex;

        // Determine if this is the backup warlock tank.  Backup tanks
        // (slot 1) stage on the opposite room-side so they are ready to
        // pick up Vek'lor immediately after a teleport swap.
        uint32 const stableIdx = GetStableTwinRoleIndex(bot, botAI);
        uint32 const veklorRoomSide = result.veklorSideIndex;
        result.tankStageSide = stableIdx;
        result.isTankBackup = (stableIdx != veklorRoomSide);

        UpdateTwinTeleportState(bot, result);
        return result;
    }
    if (cohort == TwinRoleCohort::MeleeTank)
    {
        result.sideEmperor = result.veknilash;
        result.oppositeEmperor = result.veklor;
        result.sideIndex = result.veknilashSideIndex;

        // Determine if this is the off-tank.  Off-tanks (slot 1) stage
        // on the opposite room-side so they are ready to pick up
        // Vek'nilash immediately after a teleport swap.
        uint32 const stableIdx = GetStableTwinRoleIndex(bot, botAI);
        uint32 const veknilashRoomSide = result.veknilashSideIndex;
        result.tankStageSide = stableIdx;
        result.isTankBackup = (stableIdx != veknilashRoomSide);

        UpdateTwinTeleportState(bot, result);
        return result;
    }

    // Healers: use stable room-side index so they spread across both sides.
    uint32 const sideIndex = GetStableTwinRoleIndex(bot, botAI);
    result.sideIndex = sideIndex;
    result.sideEmperor = sideIndex == 0 ? sideZeroBoss : sideOneBoss;
    result.oppositeEmperor = sideIndex == 0 ? sideOneBoss : sideZeroBoss;
    UpdateTwinTeleportState(bot, result);
    return result;
}

GuidVector GetTwinPrePullUnits(Player* bot, PlayerbotAI* botAI)
{
    GuidVector units = CollectTwinVisibleUnits(bot, botAI);
    UpdateTwinBossCache(bot, botAI, units);
    return units;
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
            if (victimPlayer == bot || Aq40BossHelper::IsEncounterParticipant(bot, victimPlayer))
                return true;
        }
    }

    return false;
}

TwinEncounterState GetTwinEncounterState(Player* bot, PlayerbotAI* botAI, GuidVector const& attackers)
{
    if (!bot || !botAI)
        return TwinEncounterState::SteadySplit;

    if (IsTwinPrePullReady(bot, botAI))
        return TwinEncounterState::PrePullStage;

    TwinAssignments const assignments = GetTwinAssignments(bot, botAI, attackers);
    if (!bot->GetMap() || !assignments.veklor || !assignments.veknilash)
        return TwinEncounterState::SteadySplit;

    SeedTwinCombatTimer(bot);

    auto const itr = sTwinTeleportStates.find(bot->GetMap()->GetInstanceId());
    if (itr == sTwinTeleportStates.end())
        return TwinEncounterState::SteadySplit;

    bool const warlockPickupEstablished = IsTwinWarlockPickupEstablished(bot, botAI, assignments);
    bool const meleePickupEstablished = IsTwinMeleePickupEstablished(bot, botAI, assignments);
    bool const healthySeparation = assignments.veklor->GetDistance2d(assignments.veknilash) >= 60.0f;
    bool const stableEncounter =
        warlockPickupEstablished && meleePickupEstablished && healthySeparation &&
        !IsTwinHealBrotherCastActive(assignments);
    uint32 const now = getMSTime();

    if (itr->second.lastTeleportMs &&
        (now - itr->second.lastTeleportMs) <= 10000 &&
        !stableEncounter)
        return TwinEncounterState::TeleportRecovery;

    if (!itr->second.lastTeleportMs &&
        itr->second.encounterStartMs &&
        (now - itr->second.encounterStartMs) <= 10000 &&
        !stableEncounter)
        return TwinEncounterState::OpenerHold;

    if (IsTwinPreTeleportWindow(bot, botAI, attackers) && stableEncounter)
        return TwinEncounterState::PreTeleportStage;

    return TwinEncounterState::SteadySplit;
}

bool IsTwinTeleportRecoveryWindow(Player* bot, PlayerbotAI* botAI, GuidVector const& attackers)
{
    return GetTwinEncounterState(bot, botAI, attackers) == TwinEncounterState::TeleportRecovery;
}

bool IsTwinDpsWaitWindow(Player* bot, PlayerbotAI* botAI, GuidVector const& attackers)
{
    TwinEncounterState const state = GetTwinEncounterState(bot, botAI, attackers);
    return state == TwinEncounterState::OpenerHold || state == TwinEncounterState::TeleportRecovery;
}

bool HasTwinBossesResolved(Player* bot, PlayerbotAI* botAI, GuidVector const& attackers)
{
    TwinAssignments const assignments = GetTwinAssignments(bot, botAI, attackers);
    return assignments.veklor && assignments.veknilash;
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

bool IsTwinReadyForPreTeleportStage(Player* bot, PlayerbotAI* botAI, GuidVector const& attackers)
{
    if (!bot || !botAI)
        return false;

    TwinAssignments const assignments = GetTwinAssignments(bot, botAI, attackers);
    if (!assignments.veklor || !assignments.veknilash)
        return false;

    if (!IsTwinPreTeleportWindow(bot, botAI, attackers))
        return false;

    TwinEncounterState const state = GetTwinEncounterState(bot, botAI, attackers);
    if (state == TwinEncounterState::TeleportRecovery ||
        state == TwinEncounterState::OpenerHold)
        return false;

    return IsTwinWarlockPickupEstablished(bot, botAI, assignments) &&
           IsTwinMeleePickupEstablished(bot, botAI, assignments) &&
           assignments.veklor->GetDistance2d(assignments.veknilash) >= 60.0f &&
           !IsTwinHealBrotherCastActive(assignments);
}

bool IsTwinWarlockPickupEstablished(Player* bot, PlayerbotAI* botAI, TwinAssignments const& assignment)
{
    if (!bot || !botAI || !assignment.veklor)
        return false;

    Group const* group = bot->GetGroup();
    if (!group)
        return false;

    // Check ALL designated warlock tanks against Vek'lor directly rather than
    // filtering by side index.  After a teleport the cached side mapping can
    // invert (Vek'lor now sits on the formerly-Vek'nilash side), which makes
    // GetTwinBossSideIndex return the wrong value and causes this function to
    // skip the warlock that actually has aggro.  Boss identity is stable;
    // room-side mapping is transient.
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

    // Check ALL melee tanks against Vek'nilash directly, same rationale as
    // IsTwinWarlockPickupEstablished — boss identity is stable across
    // teleports; room-side mapping is not.
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

bool IsTwinAssignedTankReady(Player* bot, PlayerbotAI* botAI, TwinAssignments const& assignment, Unit* boss)
{
    if (!bot || !botAI || !boss)
        return false;

    if (boss == assignment.veklor)
        return IsTwinWarlockPickupEstablished(bot, botAI, assignment);

    if (boss == assignment.veknilash)
        return IsTwinMeleePickupEstablished(bot, botAI, assignment);

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
           sTwinTeleportStates.find(instanceId) != sTwinTeleportStates.end() ||
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

    // Do not run cleanup while any group member is staged inside the Twin
    // Emperors room.  Individual bots outside the room must not wipe the
    // shared instance caches that bots inside the room are actively using
    // for pre-pull positioning.
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
