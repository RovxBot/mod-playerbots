#include "RaidAq40Helpers.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <list>
#include <mutex>
#include <set>
#include <sstream>
#include <unordered_map>
#include <vector>

#include "GameObject.h"
#include "ObjectAccessor.h"
#include "Playerbots.h"
#include "../RaidAq40BossHelper.h"
#include "../RaidAq40SpellIds.h"
#include "RaidAq40TwinEmperors.h"
#include "Timer.h"

namespace Aq40Helpers
{
uint32 GetGroupMemberOrder(Player* bot, Player* member);
uint32 GetTwinRoleSideForSlot(TwinRoleCohort cohort, uint32 slot);

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
float constexpr kTwinInitialVeklorX = -8868.31f;
float constexpr kTwinInitialVeklorY = 1205.97f;
float constexpr kTwinInitialVeklorZ = -104.231f;
float constexpr kTwinInitialVeknilashX = -9023.67f;
float constexpr kTwinInitialVeknilashY = 1176.24f;
float constexpr kTwinInitialVeknilashZ = -104.226f;
float constexpr kTwinSideHealerAnchorDistance = 120.0f;
float constexpr kTwinSideTankCornerDistance = 150.0f;
float constexpr kTwinRoomStageRadius = 150.0f;
float constexpr kTwinRoomStageZTolerance = 18.0f;
float constexpr kTwinPrePullDetectionRange = 320.0f;
uint32 constexpr kTwinTankHealersPerSide = 2;

TwinInstanceAssignments sTwinAssignments;
std::unordered_map<uint32, TwinRoleLock> sTwinRoleLocksByInstance;
std::unordered_map<uint32, TwinKnownBossGuids> sTwinKnownTwinBosses;
std::unordered_map<uint32, uint32> sCthunPhase2StartByInstance;
std::unordered_map<uint32, uint32> sSkeramPostBlinkHoldUntilByInstance;
struct TwinHealerFocusState
{
    bool hadFocusStrategy = false;
    std::list<ObjectGuid> previousTargets;
};
std::unordered_map<uint64, TwinHealerFocusState> sTwinHealerFocusStateByBot;

struct TwinTemporaryStrategyState
{
    bool hadAvoidAoeStrategy = false;
};
std::unordered_map<uint64, TwinTemporaryStrategyState> sTwinTemporaryStrategyStateByBot;
std::unordered_map<uint64, Aq40TwinEmperors::ThreatHoldState> sTwinLastThreatHoldByBot;
std::unordered_map<std::string, uint32> sAq40LogLastMsByKey;
std::mutex sAq40LogMutex;

bool IsTwinRaidCombatActiveInternal(Player* bot);
bool HasTwinPrePullVisibility(Player* bot, PlayerbotAI* botAI, GuidVector* outUnits = nullptr);
void UpdateTwinBossCache(Player* bot, PlayerbotAI* botAI, GuidVector const& units);
void AppendKnownTwinBosses(Player* bot, GuidVector& units);
bool IsTwinWipeCleanupEligible(Player* bot, PlayerbotAI* botAI, GuidVector const& attackers,
                               std::string* outReason = nullptr, bool logBlocked = false);

std::list<ObjectGuid> GetFocusHealTargets(PlayerbotAI* botAI)
{
    if (!botAI || !botAI->GetAiObjectContext())
        return {};

    return botAI->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("focus heal targets")->Get();
}

void SetFocusHealTargets(PlayerbotAI* botAI, std::list<ObjectGuid> const& focusTargets)
{
    if (!botAI || !botAI->GetAiObjectContext())
        return;

    botAI->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("focus heal targets")->Set(focusTargets);
}

bool HasTwinHealerFocusState(Player* bot)
{
    return bot && sTwinHealerFocusStateByBot.find(bot->GetGUID().GetRawValue()) != sTwinHealerFocusStateByBot.end();
}

bool HasTwinTemporaryStrategyState(Player* bot)
{
    return bot && sTwinTemporaryStrategyStateByBot.find(bot->GetGUID().GetRawValue()) !=
                      sTwinTemporaryStrategyStateByBot.end();
}

void GetTwinInitialBossPosition(uint32 sideIndex, float& bossX, float& bossY, float& bossZ)
{
    if (sideIndex == 1u)
    {
        bossX = kTwinInitialVeklorX;
        bossY = kTwinInitialVeklorY;
        bossZ = kTwinInitialVeklorZ;
        return;
    }

    bossX = kTwinInitialVeknilashX;
    bossY = kTwinInitialVeknilashY;
    bossZ = kTwinInitialVeknilashZ;
}

Position GetTwinInitialRadialAnchor(uint32 sideIndex, float distance)
{
    float bossX, bossY, bossZ;
    GetTwinInitialBossPosition(sideIndex, bossX, bossY, bossZ);

    float const dirX = kTwinRoomCenterX - bossX;
    float const dirY = kTwinRoomCenterY - bossY;
    float const length = std::sqrt(dirX * dirX + dirY * dirY);

    Position anchor;
    if (length < 0.1f)
    {
        anchor.Relocate(bossX, bossY, bossZ);
        return anchor;
    }

    float const zRatio = std::min(distance / length, 1.0f);
    anchor.Relocate(bossX + (dirX / length) * distance,
                    bossY + (dirY / length) * distance,
                    bossZ + (kTwinRoomCenterZ - bossZ) * zRatio);
    return anchor;
}

void AppendUniqueFocusTarget(std::list<ObjectGuid>& focusTargets, Player* target)
{
    if (!target)
        return;

    ObjectGuid const guid = target->GetGUID();
    if (std::find(focusTargets.begin(), focusTargets.end(), guid) == focusTargets.end())
        focusTargets.push_back(guid);
}

void AppendUniqueFocusTargetCapped(std::list<ObjectGuid>& focusTargets, Player* target, size_t maxTargets)
{
    if (focusTargets.size() >= maxTargets)
        return;

    AppendUniqueFocusTarget(focusTargets, target);
}

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

std::string ToAq40LogToken(std::string value)
{
    std::string token;
    bool lastWasSeparator = false;
    for (char ch : value)
    {
        unsigned char const uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch))
        {
            token.push_back(static_cast<char>(std::tolower(uch)));
            lastWasSeparator = false;
        }
        else if (!lastWasSeparator && !token.empty())
        {
            token.push_back('_');
            lastWasSeparator = true;
        }
    }

    while (!token.empty() && token.back() == '_')
        token.pop_back();

    return token.empty() ? "unknown" : token;
}

uint32 GetAq40LogInstanceId(Player* bot)
{
    if (!bot)
        return 0;

    if (bot->GetMap())
        return bot->GetMap()->GetInstanceId();

    return bot->GetMapId();
}

void LogAq40(Player* bot, std::string const& eventKey, std::string const& stateKey,
             std::string const& fields, uint32 throttleMs, bool warn)
{
    if (!sPlayerbotAIConfig.aq40StrategyLog || !bot)
        return;

    uint32 const instanceId = GetAq40LogInstanceId(bot);
    uint64 const botGuid = bot->GetGUID().GetRawValue();
    uint32 const effectiveThrottleMs = throttleMs ? throttleMs : sPlayerbotAIConfig.aq40StrategyLogThrottleMs;

    std::ostringstream key;
    key << instanceId << ":" << botGuid << ":" << ToAq40LogToken(eventKey) << ":" << stateKey;
    std::string const logKey = key.str();

    uint32 const now = getMSTime();
    {
        std::lock_guard<std::mutex> guard(sAq40LogMutex);
        auto const itr = sAq40LogLastMsByKey.find(logKey);
        if (effectiveThrottleMs > 0 && itr != sAq40LogLastMsByKey.end() && now - itr->second < effectiveThrottleMs)
            return;

        sAq40LogLastMsByKey[logKey] = now;
    }

    std::ostringstream line;
    line << "event=" << ToAq40LogToken(eventKey)
         << " bot=" << ToAq40LogToken(bot->GetName())
         << " role=" << GetAq40LogRole(bot, GET_PLAYERBOT_AI(bot))
         << " instance=" << instanceId;
    if (!fields.empty())
        line << " " << fields;

    if (warn)
        LOG_WARN("playerbots_aq40", "AQ40 {}", line.str());
    else
        LOG_INFO("playerbots_aq40", "AQ40 {}", line.str());
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
            return (slot % 2 == 0) ? 1u : 0u;
        case TwinRoleCohort::Other:
            return slot % 2;
    }

    return slot % 2;
}

bool GetCurrentTwinRoleLock(Player* bot, TwinRoleLock& outLock)
{
    if (!bot || !bot->GetMap())
        return false;

    auto const itr = sTwinRoleLocksByInstance.find(bot->GetMap()->GetInstanceId());
    if (itr == sTwinRoleLocksByInstance.end())
        return false;

    outLock = itr->second;
    return true;
}

std::string BuildTwinRoleLockMembers(Player* bot, GuidVector const& members)
{
    if (!bot || members.empty())
        return "none";

    std::ostringstream out;
    bool first = true;
    for (ObjectGuid const& guid : members)
    {
        Player* member = ObjectAccessor::FindConnectedPlayer(guid);
        if (!member || !Aq40BossHelper::IsEncounterParticipant(bot, member))
            continue;

        if (!first)
            out << ",";

        out << GetAq40LogToken(member->GetName()) << ":" << member->GetGUID().GetCounter();
        first = false;
    }

    return first ? "none" : out.str();
}

TwinRoleLock BuildTwinRoleLockSnapshot(Player* bot)
{
    TwinRoleLock roleLock;
    if (!bot || !bot->GetGroup())
        return roleLock;

    std::vector<Player*> botWarlocks;
    std::vector<Player*> botMeleeTanks;
    std::vector<Player*> botHealers;
    std::vector<Player*> remainingBots;
    std::set<uint64> lockedSpecialRoleGuids;

    auto const sortByGroupOrder = [bot](Player const* left, Player const* right)
    {
        uint32 const leftOrder = GetGroupMemberOrder(bot, const_cast<Player*>(left));
        uint32 const rightOrder = GetGroupMemberOrder(bot, const_cast<Player*>(right));
        if (leftOrder != rightOrder)
            return leftOrder < rightOrder;

        return left->GetGUID().GetRawValue() < right->GetGUID().GetRawValue();
    };

    for (GroupReference const* ref = bot->GetGroup()->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || !member->IsAlive() || member->IsGameMaster())
            continue;
        if (!Aq40BossHelper::IsEncounterParticipant(bot, member))
            continue;

        PlayerbotAI* memberAI = GET_PLAYERBOT_AI(member);
        if (!memberAI || memberAI->IsRealPlayer())
            continue;

        if (member->getClass() == CLASS_WARLOCK)
            botWarlocks.push_back(member);
        if (PlayerbotAI::IsTank(member) && !PlayerbotAI::IsRanged(member))
            botMeleeTanks.push_back(member);
        if (PlayerbotAI::IsHeal(member))
            botHealers.push_back(member);
        remainingBots.push_back(member);
    }

    std::sort(botWarlocks.begin(), botWarlocks.end(), [](Player* left, Player* right)
    {
        int32 const leftShadowRes = left->GetResistance(SPELL_SCHOOL_SHADOW);
        int32 const rightShadowRes = right->GetResistance(SPELL_SCHOOL_SHADOW);
        if (leftShadowRes != rightShadowRes)
            return leftShadowRes > rightShadowRes;

        return left->GetGUID().GetRawValue() < right->GetGUID().GetRawValue();
    });

    std::sort(botMeleeTanks.begin(), botMeleeTanks.end(), [bot](Player* left, Player* right)
    {
        uint32 const leftPriority = GetTwinRolePriority(bot, left, TwinRoleCohort::MeleeTank);
        uint32 const rightPriority = GetTwinRolePriority(bot, right, TwinRoleCohort::MeleeTank);
        if (leftPriority != rightPriority)
            return leftPriority < rightPriority;

        return left->GetGUID().GetRawValue() < right->GetGUID().GetRawValue();
    });

    std::sort(botHealers.begin(), botHealers.end(), sortByGroupOrder);
    std::sort(remainingBots.begin(), remainingBots.end(), sortByGroupOrder);

    for (uint32 slot = 0; slot < botWarlocks.size() && slot < 2; ++slot)
    {
        Player* member = botWarlocks[slot];
        ObjectGuid const guid = member->GetGUID();
        roleLock.lockedWarlocks.push_back(guid);
        roleLock.stagedSidesByGuid[guid.GetRawValue()] = (slot == 0u) ? 1u : 0u;
        lockedSpecialRoleGuids.insert(guid.GetRawValue());
    }

    for (uint32 slot = 0; slot < botMeleeTanks.size() && slot < 2; ++slot)
    {
        Player* member = botMeleeTanks[slot];
        ObjectGuid const guid = member->GetGUID();
        roleLock.lockedMeleeTanks.push_back(guid);
        roleLock.stagedSidesByGuid[guid.GetRawValue()] = slot % 2u;
        lockedSpecialRoleGuids.insert(guid.GetRawValue());
    }

    uint32 healerCounts[2] = { 0, 0 };
    uint32 healerSlot = 0;
    for (Player* healer : botHealers)
    {
        uint64 const healerGuid = healer->GetGUID().GetRawValue();
        if (lockedSpecialRoleGuids.find(healerGuid) != lockedSpecialRoleGuids.end())
            continue;

        uint32 assignedSide = GetTwinRoleSideForSlot(TwinRoleCohort::Healer, healerSlot++);
        if (healerCounts[assignedSide] >= kTwinTankHealersPerSide)
            assignedSide = 1u - assignedSide;
        if (healerCounts[assignedSide] >= kTwinTankHealersPerSide)
            continue;

        ++healerCounts[assignedSide];
        roleLock.healerSidesByGuid[healerGuid] = assignedSide;
        roleLock.stagedSidesByGuid[healerGuid] = assignedSide;
    }

    uint32 otherSlot = 0;
    for (Player* member : remainingBots)
    {
        uint64 const memberGuid = member->GetGUID().GetRawValue();
        if (roleLock.stagedSidesByGuid.find(memberGuid) != roleLock.stagedSidesByGuid.end())
            continue;

        roleLock.stagedSidesByGuid[memberGuid] = GetTwinRoleSideForSlot(TwinRoleCohort::Other, otherSlot++);
    }

    bool const hasWarlocks = roleLock.lockedWarlocks.size() == 2;
    bool const hasMeleeTanks = roleLock.lockedMeleeTanks.size() == 2;
    roleLock.supported = hasWarlocks && hasMeleeTanks;
    if (!roleLock.supported)
    {
        if (!hasWarlocks && !hasMeleeTanks)
            roleLock.unsupportedReason = "missing_bot_warlocks_and_melee_tanks";
        else if (!hasWarlocks)
            roleLock.unsupportedReason = "missing_bot_warlocks";
        else
            roleLock.unsupportedReason = "missing_bot_melee_tanks";
    }

    return roleLock;
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

bool IsTwinPickupMemberEstablished(Player* member, TwinAssignments const& assignment, Unit* boss)
{
    if (!member || !member->IsAlive() || !boss || !boss->IsAlive())
        return false;

    bool const isVeklor = (boss == assignment.veklor);
    if (!Aq40TwinEmperors::HasBossPickupAggro(member, boss) ||
        !Aq40TwinEmperors::IsPickupWindowSatisfied(member, boss, isVeklor))
        return false;

    return true;
}

bool IsTwinWipeCleanupEligible(Player* bot, PlayerbotAI* botAI, GuidVector const& attackers,
                               std::string* outReason, bool logBlocked)
{
    if (!bot || !botAI || !bot->GetMap() || !IsInTwinRoomBounds(bot))
        return false;

    // Fresh pre-pull staging only creates stable-side assignments; only treat
    // in-room cleanup as a wipe reset when encounter-owned Twin combat residue
    // is still hanging around.
    if (!Aq40TwinEmperors::HasPersistentState(bot))
        return false;

    GuidVector visibleTwins;
    if (!HasTwinPrePullVisibility(bot, botAI, &visibleTwins))
        return false;

    GuidVector observedUnits = BuildTwinObservedUnits(bot, botAI, attackers);
    std::set<uint64> twinBossGuids;
    for (ObjectGuid const guid : observedUnits)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!IsTwinBossUnit(botAI, unit))
            continue;

        twinBossGuids.insert(unit->GetGUID().GetRawValue());
    }

    auto hasAliveTwinTargeting = [&]() -> bool
    {
        if (twinBossGuids.empty())
            return false;

        auto isTwinBossGuid = [&twinBossGuids](ObjectGuid const& guid) -> bool
        {
            return guid && twinBossGuids.find(guid.GetRawValue()) != twinBossGuids.end();
        };

        Group const* group = bot->GetGroup();
        if (!group)
        {
            if ((bot->IsInCombat() && isTwinBossGuid(bot->GetTarget())) ||
                (bot->GetVictim() && isTwinBossGuid(bot->GetVictim()->GetGUID())))
            {
                return true;
            }

            Pet* pet = bot->GetPet();
            return pet && pet->IsAlive() &&
                   ((pet->IsInCombat() && isTwinBossGuid(pet->GetTarget())) ||
                    (pet->GetVictim() && isTwinBossGuid(pet->GetVictim()->GetGUID())));
        }

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

            if ((member->IsInCombat() && isTwinBossGuid(member->GetTarget())) ||
                (member->GetVictim() && isTwinBossGuid(member->GetVictim()->GetGUID())))
            {
                return true;
            }

            Pet* pet = member->GetPet();
            if (!pet || !pet->IsAlive())
                continue;

            if ((pet->IsInCombat() && isTwinBossGuid(pet->GetTarget())) ||
                (pet->GetVictim() && isTwinBossGuid(pet->GetVictim()->GetGUID())))
            {
                return true;
            }
        }

        return false;
    }();

    bool const twinCombatLive = IsTwinCombatInProgress(bot, botAI, attackers);
    bool const encounterCombatActive = Aq40BossHelper::IsEncounterCombatActive(bot) || IsTwinRaidCombatActiveInternal(bot);
    if (encounterCombatActive || twinCombatLive || hasAliveTwinTargeting)
    {
        if (logBlocked)
        {
            std::string blocker = encounterCombatActive ? "combat_active" :
                (twinCombatLive ? "combat_live" : "active_targeting");
            LogAq40Info(bot, "encounter_reset", "twins:blocked:" + blocker,
                "boss=twins state=blocked reason=room_visibility_blocked blocker=" + blocker, 10000);
        }
        return false;
    }

    if (outReason)
        *outReason = "wipe_cleanup";

    return true;
}

}  // namespace

std::string GetAq40LogToken(std::string value)
{
    return ToAq40LogToken(value);
}

std::string GetAq40LogUnit(Unit* unit)
{
    if (!unit)
        return "none";

    std::ostringstream out;
    out << ToAq40LogToken(unit->GetName()) << ":" << unit->GetGUID().GetCounter();
    return out.str();
}

std::string GetAq40LogRole(Player* bot, PlayerbotAI* botAI)
{
    if (!bot)
        return "unknown";

    switch (GetTwinRoleCohort(bot, botAI))
    {
        case TwinRoleCohort::WarlockTank:
            return "warlock_tank";
        case TwinRoleCohort::MeleeTank:
            return "melee_tank";
        case TwinRoleCohort::Healer:
            return "healer";
        case TwinRoleCohort::Other:
        default:
            break;
    }

    if (botAI && botAI->IsRanged(bot))
        return "ranged";
    return "melee";
}

void LogAq40Info(Player* bot, std::string const& eventKey, std::string const& stateKey,
                 std::string const& fields, uint32 throttleMs)
{
    LogAq40(bot, eventKey, stateKey, fields, throttleMs, false);
}

void LogAq40Warn(Player* bot, std::string const& eventKey, std::string const& stateKey,
                 std::string const& fields, uint32 throttleMs)
{
    LogAq40(bot, eventKey, stateKey, fields, throttleMs, true);
}

void LogAq40Target(Player* bot, std::string const& boss, std::string const& reason, Unit* target,
                   uint32 throttleMs)
{
    std::ostringstream fields;
    fields << "boss=" << ToAq40LogToken(boss)
           << " target=" << GetAq40LogUnit(target)
           << " reason=" << ToAq40LogToken(reason);
    LogAq40Info(bot, "target_change", boss + ":" + reason + ":" + GetAq40LogUnit(target),
                fields.str(), throttleMs);
}

std::list<ObjectGuid> GetTwinHealerFocusTargets(Player* bot, PlayerbotAI* botAI, TwinAssignments const& assignment)
{
    std::list<ObjectGuid> focusTargets;
    if (!bot || !botAI || !bot->GetGroup())
        return focusTargets;

    std::vector<Player*> primaryTargets;
    std::vector<Player*> reserveTargets;
    std::vector<Player*> sideHealers;
    constexpr size_t kMaxTwinHealerFocusTargets = 4;
    for (GroupReference const* ref = bot->GetGroup()->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || !member->IsAlive() || member->IsGameMaster())
            continue;
        if (member->GetMapId() != bot->GetMapId())
            continue;
        if (!Aq40BossHelper::IsEncounterParticipant(bot, member))
            continue;

        PlayerbotAI* memberAI = GET_PLAYERBOT_AI(member);
        TwinRoleCohort const cohort = GetTwinRoleCohort(member, memberAI ? memberAI : botAI);
        bool const isWarlockTank = cohort == TwinRoleCohort::WarlockTank;
        bool const isMeleeTank = cohort == TwinRoleCohort::MeleeTank;
        if (isWarlockTank || isMeleeTank)
        {
            if (GetStableTwinRoleIndex(member, memberAI ? memberAI : botAI) != assignment.sideIndex)
                continue;

            bool const isPrimaryForCurrentBoss =
                (isWarlockTank && assignment.veklor && assignment.veklorSideIndex == assignment.sideIndex) ||
                (isMeleeTank && assignment.veknilash && assignment.veknilashSideIndex == assignment.sideIndex);

            if (isPrimaryForCurrentBoss)
                primaryTargets.push_back(member);
            else
                reserveTargets.push_back(member);

            continue;
        }

        if (cohort != TwinRoleCohort::Healer)
            continue;

        uint32 healerSide = 0;
        if (!GetTwinDedicatedTankHealerSide(member, memberAI ? memberAI : botAI, healerSide) ||
            healerSide != assignment.sideIndex)
            continue;

        sideHealers.push_back(member);
    }

    for (Player* target : primaryTargets)
        AppendUniqueFocusTargetCapped(focusTargets, target, kMaxTwinHealerFocusTargets);
    for (Player* target : reserveTargets)
        AppendUniqueFocusTargetCapped(focusTargets, target, kMaxTwinHealerFocusTargets);

    // Keep the local tank-healer in the side-local focus set so the base
    // healer AI can still self-heal without falling back to a broad raid list.
    AppendUniqueFocusTargetCapped(focusTargets, bot, kMaxTwinHealerFocusTargets);

    for (Player* target : sideHealers)
    {
        if (target != bot)
        {
            AppendUniqueFocusTargetCapped(focusTargets, target, kMaxTwinHealerFocusTargets);
            break;
        }
    }

    return focusTargets;
}

Unit* FindTwinMarkedBug(Player* bot, PlayerbotAI* botAI, GuidVector const& encounterUnits, uint32 auraSpellId)
{
    if (!bot || !botAI)
        return nullptr;

    Unit* closestMarked = nullptr;
    float closestMarkedDistance = 0.0f;
    for (ObjectGuid const guid : encounterUnits)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit || !unit->IsInWorld() || !unit->IsAlive() || unit->GetMapId() != bot->GetMapId() ||
            unit->IsFriendlyTo(bot))
            continue;

        bool const isQirajiBug = Aq40BossHelper::IsUnitNamedAny(botAI, unit, { "qiraji scarab", "qiraji scorpion" });
        if (isQirajiBug && Aq40SpellIds::HasAnyAura(botAI, unit, { auraSpellId }))
        {
            float const distance = bot->GetDistance2d(unit);
            if (!closestMarked || distance < closestMarkedDistance)
            {
                closestMarked = unit;
                closestMarkedDistance = distance;
            }
            continue;
        }
    }

    return closestMarked;
}

Unit* FindTwinHostileBug(Player* bot, PlayerbotAI* botAI, GuidVector const& encounterUnits)
{
    if (!bot || !botAI)
        return nullptr;

    auto isThreateningRaid = [bot](Unit* unit) -> bool
    {
        if (!unit)
            return false;

        Group const* group = bot->GetGroup();
        if (!group)
            return unit->GetVictim() == bot || unit->GetTarget() == bot->GetGUID();

        for (GroupReference const* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || !member->IsAlive() || member->GetMapId() != bot->GetMapId())
                continue;

            if (unit->GetVictim() == member || unit->GetTarget() == member->GetGUID())
                return true;

            Pet* pet = member->GetPet();
            if (pet && (unit->GetVictim() == pet || unit->GetTarget() == pet->GetGUID()))
                return true;
        }

        return false;
    };

    Unit* closestBug = nullptr;
    float closestDistance = 0.0f;
    for (ObjectGuid const guid : encounterUnits)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit || !unit->IsInWorld() || !unit->IsAlive() || unit->GetMapId() != bot->GetMapId() ||
            unit->IsFriendlyTo(bot))
            continue;

        if (!Aq40BossHelper::IsUnitNamedAny(botAI, unit, { "qiraji scarab", "qiraji scorpion" }))
            continue;
        if (!Aq40SpellIds::HasAnyAura(botAI, unit, { Aq40SpellIds::TwinExplodeBug, Aq40SpellIds::TwinMutateBug }) &&
            !isThreateningRaid(unit))
            continue;

        float const distance = bot->GetDistance2d(unit);
        if (!closestBug || distance < closestDistance)
        {
            closestBug = unit;
            closestDistance = distance;
        }
    }

    return closestBug;
}

Position GetTwinRoomCenterPosition()
{
    Position center;
    center.Relocate(kTwinRoomCenterX, kTwinRoomCenterY, kTwinRoomCenterZ);
    return center;
}

Position GetTwinRoomSideAnchor(uint32 sideIndex)
{
    return GetTwinInitialRadialAnchor(sideIndex, kTwinSideTankCornerDistance);
}

Position GetTwinRoomSideHealerAnchor(uint32 sideIndex)
{
    return GetTwinInitialRadialAnchor(sideIndex, kTwinSideHealerAnchorDistance);
}

bool ApplyTwinHealerFocusTargets(Player* bot, PlayerbotAI* botAI, std::list<ObjectGuid> const& focusTargets)
{
    if (!bot || !botAI)
        return false;
    if (focusTargets.empty())
        return ClearTwinHealerFocusTargets(bot, botAI);

    uint64 const botGuid = bot->GetGUID().GetRawValue();
    if (sTwinHealerFocusStateByBot.find(botGuid) == sTwinHealerFocusStateByBot.end())
    {
        sTwinHealerFocusStateByBot[botGuid] =
        {
            botAI->HasStrategy("focus heal targets", BOT_STATE_COMBAT),
            GetFocusHealTargets(botAI)
        };
    }

    bool changed = false;
    std::list<ObjectGuid> const currentTargets = GetFocusHealTargets(botAI);
    if (currentTargets != focusTargets)
    {
        SetFocusHealTargets(botAI, focusTargets);
        changed = true;
    }

    if (!botAI->HasStrategy("focus heal targets", BOT_STATE_COMBAT))
    {
        botAI->ChangeStrategy("+focus heal targets", BOT_STATE_COMBAT);
        changed = true;
    }

    return changed;
}

bool ClearTwinHealerFocusTargets(Player* bot, PlayerbotAI* botAI)
{
    if (!bot || !botAI)
        return false;

    uint64 const botGuid = bot->GetGUID().GetRawValue();
    auto stateItr = sTwinHealerFocusStateByBot.find(botGuid);
    if (stateItr == sTwinHealerFocusStateByBot.end())
        return false;

    if (stateItr->second.hadFocusStrategy)
    {
        SetFocusHealTargets(botAI, stateItr->second.previousTargets);
        if (!botAI->HasStrategy("focus heal targets", BOT_STATE_COMBAT))
            botAI->ChangeStrategy("+focus heal targets", BOT_STATE_COMBAT);
    }
    else
    {
        std::list<ObjectGuid> emptyTargets;
        SetFocusHealTargets(botAI, emptyTargets);
        if (botAI->HasStrategy("focus heal targets", BOT_STATE_COMBAT))
            botAI->ChangeStrategy("-focus heal targets", BOT_STATE_COMBAT);
    }

    sTwinHealerFocusStateByBot.erase(stateItr);
    return true;
}

bool IsTwinHealerOutsideSideLeash(Player* bot, TwinAssignments const& assignment)
{
    if (!bot)
        return false;

    float constexpr kSideLeashDistance = 55.0f;
    float constexpr kWrongSideMargin = 8.0f;
    Position const sideAnchor = GetTwinRoomSideHealerAnchor(assignment.sideIndex);
    float const sideAnchorDistance = bot->GetExactDist2d(sideAnchor.GetPositionX(), sideAnchor.GetPositionY());
    if (sideAnchorDistance > kSideLeashDistance)
        return true;

    Position const oppositeAnchor = GetTwinRoomSideHealerAnchor(1u - assignment.sideIndex);
    float const oppositeAnchorDistance =
        bot->GetExactDist2d(oppositeAnchor.GetPositionX(), oppositeAnchor.GetPositionY());
    if (oppositeAnchorDistance + kWrongSideMargin < sideAnchorDistance)
        return true;

    if (!assignment.sideEmperor)
        return false;

    float const sideDistance = bot->GetDistance2d(assignment.sideEmperor);
    if (sideDistance > kSideLeashDistance)
        return true;

    if (!assignment.oppositeEmperor)
        return false;

    float const oppositeDistance = bot->GetDistance2d(assignment.oppositeEmperor);
    return oppositeDistance + (kWrongSideMargin + 2.0f) < sideDistance;
}

bool ApplyTwinTemporaryCombatStrategies(Player* bot, PlayerbotAI* botAI)
{
    if (!bot || !botAI)
        return false;

    uint64 const botGuid = bot->GetGUID().GetRawValue();
    if (sTwinTemporaryStrategyStateByBot.find(botGuid) == sTwinTemporaryStrategyStateByBot.end())
    {
        sTwinTemporaryStrategyStateByBot[botGuid] =
        {
            botAI->HasStrategy("avoid aoe", BOT_STATE_COMBAT)
        };
    }

    if (botAI->HasStrategy("avoid aoe", BOT_STATE_COMBAT))
        return false;

    botAI->ChangeStrategy("+avoid aoe", BOT_STATE_COMBAT);
    return true;
}

bool ClearTwinTemporaryCombatStrategies(Player* bot, PlayerbotAI* botAI)
{
    if (!bot || !botAI)
        return false;

    uint64 const botGuid = bot->GetGUID().GetRawValue();
    auto stateItr = sTwinTemporaryStrategyStateByBot.find(botGuid);
    if (stateItr == sTwinTemporaryStrategyStateByBot.end())
        return false;

    if (stateItr->second.hadAvoidAoeStrategy)
    {
        if (!botAI->HasStrategy("avoid aoe", BOT_STATE_COMBAT))
            botAI->ChangeStrategy("+avoid aoe", BOT_STATE_COMBAT);
    }
    else if (botAI->HasStrategy("avoid aoe", BOT_STATE_COMBAT))
    {
        botAI->ChangeStrategy("-avoid aoe", BOT_STATE_COMBAT);
    }

    sTwinTemporaryStrategyStateByBot.erase(stateItr);
    return true;
}

bool HasTwinVisibleEmperors(Player* bot, PlayerbotAI* botAI, GuidVector* outUnits)
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

bool HasTwinBossAggro(Player* member, Unit* boss)
{
    return Aq40TwinEmperors::HasBossPickupAggro(member, boss);
}

bool IsTwinPrimaryTankOnActiveBoss(Player* bot, TwinAssignments const& assignment)
{
    if (!bot)
        return false;

    TwinRoleCohort const cohort = GetTwinRoleCohort(bot, GET_PLAYERBOT_AI(bot));

    // Each side has one warlock tank + one melee tank permanently assigned.
    // Tanks never cross sides — the backup on the other side picks up after
    // teleport.  Primary = side-matched to the boss's current position.
    if (cohort == TwinRoleCohort::WarlockTank)
        return assignment.veklor && !assignment.isTankBackup;

    if (cohort == TwinRoleCohort::MeleeTank)
        return assignment.veknilash && !assignment.isTankBackup;

    return false;
}

bool GetTwinRoleLock(Player* bot, PlayerbotAI* botAI, TwinRoleLock& outLock)
{
    outLock = {};
    if (!bot)
        return false;

    PlayerbotAI* roleAI = botAI ? botAI : GET_PLAYERBOT_AI(bot);
    if (!roleAI || !bot->GetMap() || !bot->GetGroup())
        return false;

    if (GetCurrentTwinRoleLock(bot, outLock))
        return true;

    GuidVector attackers;
    if (roleAI->GetAiObjectContext())
        attackers = roleAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get();

    bool const shouldLock =
        HasTwinVisibleEmperors(bot, roleAI) ||
        IsTwinPlayerPullAuthorized(bot, roleAI, attackers) ||
        IsTwinCombatInProgress(bot, roleAI, attackers) ||
        Aq40TwinEmperors::HasPersistentState(bot);
    if (!shouldLock)
        return false;

    TwinRoleLock roleLock = BuildTwinRoleLockSnapshot(bot);
    uint32 const instanceId = bot->GetMap()->GetInstanceId();
    sTwinRoleLocksByInstance[instanceId] = roleLock;
    outLock = roleLock;

    std::ostringstream lockFields;
    lockFields << "boss=twins supported=" << (roleLock.supported ? 1 : 0)
               << " warlocks=" << roleLock.lockedWarlocks.size()
               << " melee_tanks=" << roleLock.lockedMeleeTanks.size()
               << " healers=" << roleLock.healerSidesByGuid.size()
               << " warlock_members=" << BuildTwinRoleLockMembers(bot, roleLock.lockedWarlocks)
               << " melee_members=" << BuildTwinRoleLockMembers(bot, roleLock.lockedMeleeTanks);
    LogAq40Info(bot, "twin_role_lock", "twins:role_lock:" + std::to_string(instanceId), lockFields.str(), 30000);

    if (!roleLock.supported)
    {
        std::ostringstream unsupportedFields;
        unsupportedFields << "boss=twins supported=0 reason=" << roleLock.unsupportedReason
                          << " warlocks=" << roleLock.lockedWarlocks.size()
                          << " melee_tanks=" << roleLock.lockedMeleeTanks.size();
        LogAq40Warn(bot, "unsupported_setup",
            "twins:unsupported:" + roleLock.unsupportedReason + ":" + std::to_string(instanceId),
            unsupportedFields.str(), 30000);
    }

    return true;
}

bool GetTwinDedicatedTankHealerSide(Player* bot, PlayerbotAI* botAI, uint32& sideIndex)
{
    sideIndex = 0;
    if (!bot || !PlayerbotAI::IsHeal(bot))
        return false;

    if (PlayerbotAI* roleAI = botAI ? botAI : GET_PLAYERBOT_AI(bot))
    {
        TwinRoleLock roleLock;
        if (GetTwinRoleLock(bot, roleAI, roleLock))
        {
            auto const healerItr = roleLock.healerSidesByGuid.find(bot->GetGUID().GetRawValue());
            if (healerItr == roleLock.healerSidesByGuid.end())
                return false;

            sideIndex = healerItr->second;
            return true;
        }
    }

    Group const* group = bot->GetGroup();
    if (!group)
        return false;

    std::vector<Player*> healers;
    for (GroupReference const* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || !member->IsAlive() || member->IsGameMaster())
            continue;
        if (member->GetMapId() != bot->GetMapId())
            continue;
        if (!PlayerbotAI::IsHeal(member))
            continue;
        if (!Aq40BossHelper::IsEncounterParticipant(bot, member))
            continue;

        healers.push_back(member);
    }

    std::sort(healers.begin(), healers.end(), [bot](Player const* left, Player const* right)
    {
        uint32 const leftOrder = GetGroupMemberOrder(bot, const_cast<Player*>(left));
        uint32 const rightOrder = GetGroupMemberOrder(bot, const_cast<Player*>(right));
        if (leftOrder != rightOrder)
            return leftOrder < rightOrder;

        return left->GetGUID().GetRawValue() < right->GetGUID().GetRawValue();
    });

    uint32 sideCounts[2] = { 0, 0 };
    uint32 slot = 0;
    for (Player* healer : healers)
    {
        uint32 assignedSide = GetTwinRoleSideForSlot(TwinRoleCohort::Healer, slot++);
        if (sideCounts[assignedSide] >= kTwinTankHealersPerSide)
            assignedSide = 1u - assignedSide;
        if (sideCounts[assignedSide] >= kTwinTankHealersPerSide)
            continue;

        ++sideCounts[assignedSide];
        if (healer == bot)
        {
            sideIndex = assignedSide;
            return true;
        }
    }

    return false;
}

TwinRoleCohort GetTwinRoleCohort(Player* bot, PlayerbotAI* botAI)
{
    if (!bot)
        return TwinRoleCohort::Other;

    PlayerbotAI* roleAI = botAI ? botAI : GET_PLAYERBOT_AI(bot);
    TwinRoleLock roleLock;
    if (roleAI && GetTwinRoleLock(bot, roleAI, roleLock))
    {
        uint64 const botGuid = bot->GetGUID().GetRawValue();
        auto const guidMatches = [botGuid](GuidVector const& members)
        {
            return std::find_if(members.begin(), members.end(), [botGuid](ObjectGuid const& guid)
            {
                return guid.GetRawValue() == botGuid;
            }) != members.end();
        };

        if (guidMatches(roleLock.lockedWarlocks))
            return TwinRoleCohort::WarlockTank;
        if (guidMatches(roleLock.lockedMeleeTanks))
            return TwinRoleCohort::MeleeTank;
        if (PlayerbotAI::IsHeal(bot))
            return TwinRoleCohort::Healer;
        return TwinRoleCohort::Other;
    }

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

    PlayerbotAI* roleAI = botAI ? botAI : GET_PLAYERBOT_AI(bot);

    Group const* group = bot->GetGroup();
    if (!group)
        return static_cast<uint32>(bot->GetGUID().GetCounter() % 2);

    uint32 const instanceId = bot->GetMap() ? bot->GetMap()->GetInstanceId() : 0;

    bool const encounterActive = Aq40BossHelper::IsEncounterCombatActive(bot) || IsTwinRaidCombatActiveInternal(bot);
    if (!encounterActive)
    {
        PlayerbotAI* cleanupAI = (roleAI && roleAI->GetBot() == bot) ? roleAI : GET_PLAYERBOT_AI(bot);
        GuidVector attackers;
        if (cleanupAI && cleanupAI->GetAiObjectContext())
            attackers = cleanupAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get();

        if (cleanupAI && IsTwinWipeCleanupEligible(bot, cleanupAI, attackers, nullptr, true))
        {
            ClearTwinHealerFocusTargets(bot, cleanupAI);
            ClearTwinTemporaryCombatStrategies(bot, cleanupAI);
            ResetEncounterState(bot);
        }
        else
        {
            bool const twinVisiblePrePull = cleanupAI && HasTwinPrePullVisibility(bot, cleanupAI);
            if (!IsInTwinRoomBounds(bot) && !twinVisiblePrePull && !IsAnyGroupMemberInTwinRoomInternal(bot))
            {
                if (cleanupAI)
                {
                    ClearTwinHealerFocusTargets(bot, cleanupAI);
                    ClearTwinTemporaryCombatStrategies(bot, cleanupAI);
                }

                ResetEncounterState(bot);
            }
        }
    }

    TwinRoleLock roleLock;
    if (roleAI && GetTwinRoleLock(bot, roleAI, roleLock))
    {
        auto const itr = roleLock.stagedSidesByGuid.find(bot->GetGUID().GetRawValue());
        if (itr != roleLock.stagedSidesByGuid.end())
            return itr->second;
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

    // Collect which sides are already occupied by existing assignments.
    // This prevents the bug where a warlock returning from death gets
    // assigned the same side as the remaining warlock (both on same side).
    std::set<uint32> occupiedSides;
    for (auto const& [guid, side] : assignments)
        occupiedSides.insert(side);

    uint32 assignedSlot = static_cast<uint32>(assignments.size());
    for (Player* member : members)
    {
        uint64 const memberGuid = member->GetGUID().GetRawValue();
        if (assignments.find(memberGuid) != assignments.end())
            continue;

        // For cohorts with only 2 members per side (warlock/melee tanks),
        // prefer the first unoccupied side to guarantee one per side.
        uint32 assignedSide;
        if ((cohort == TwinRoleCohort::WarlockTank || cohort == TwinRoleCohort::MeleeTank) &&
            occupiedSides.size() < 2)
        {
            // For warlocks: prefer side 1 (Vek'lor's initial side) first.
            // For melee tanks: prefer side 0 (Vek'nilash's initial side) first.
            uint32 const preferredSide = (cohort == TwinRoleCohort::WarlockTank) ? 1u : 0u;
            uint32 const otherSide = 1u - preferredSide;
            if (occupiedSides.find(preferredSide) == occupiedSides.end())
                assignedSide = preferredSide;
            else
                assignedSide = otherSide;
        }
        else
        {
            assignedSide = GetTwinRoleSideForSlot(cohort, assignedSlot);
        }

        assignments[memberGuid] = assignedSide;
        occupiedSides.insert(assignedSide);
        ++assignedSlot;
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
    Aq40TwinEmperors::SideState const sideState =
        Aq40TwinEmperors::ResolveSideState(bot, result.veklor, result.veknilash);
    Unit* sideZeroBoss = sideState.sideZeroBoss;
    Unit* sideOneBoss = sideState.sideOneBoss;
    auto getBossSideIndex = [sideZeroBoss, sideOneBoss](Unit* boss) -> uint32
    {
        if (boss == sideOneBoss)
            return 1u;
        return 0u;
    };

    result.veklorSideIndex = sideState.veklorSideIndex;
    result.veknilashSideIndex = sideState.veknilashSideIndex;

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

    // Dedicated tank healers bind to their assigned side; raid healers stay
    // central and only keep a stable side index for callers that need one.
    uint32 dedicatedHealerSide = 0;
    uint32 const sideIndex = GetTwinDedicatedTankHealerSide(bot, botAI, dedicatedHealerSide) ?
        dedicatedHealerSide : GetStableTwinRoleIndex(bot, botAI);
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

    Aq40TwinEmperors::NoteTwinEncounterActive(bot);
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

    if (Aq40TwinEmperors::HasTwinPickupEstablished(bot, true))
        return true;

    Group const* group = bot->GetGroup();
    if (!group)
        return false;

    for (GroupReference const* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || !member->IsAlive())
            continue;

        PlayerbotAI* memberAI = GET_PLAYERBOT_AI(member);
        if (GetTwinRoleCohort(member, memberAI ? memberAI : botAI) != TwinRoleCohort::WarlockTank)
            continue;

        if (GetStableTwinRoleIndex(member, memberAI ? memberAI : botAI) != assignment.veklorSideIndex)
            continue;

        if (IsTwinPickupMemberEstablished(member, assignment, assignment.veklor))
        {
            Aq40TwinEmperors::NoteTwinPickupEstablished(bot, true);
            return true;
        }
    }

    return false;
}

uint32 GetTwinPostSwapElapsedMs(Player* bot, TwinAssignments const& assignment)
{
    if (!assignment.veklor)
        return std::numeric_limits<uint32>::max();

    return Aq40TwinEmperors::GetPostSwapElapsedMs(bot, assignment.veklorSideIndex);
}

bool IsTwinPostSwapThreatHoldActive(Player* bot, PlayerbotAI* botAI, TwinAssignments const& assignment)
{
    if (!bot || !botAI || !assignment.veklor)
        return false;

    uint64 const botGuid = bot->GetGUID().GetRawValue();
    Aq40TwinEmperors::ThreatHoldState holdState;
    bool const holdActive = Aq40TwinEmperors::GetActiveThreatHold(bot, holdState);

    auto const lastHoldItr = sTwinLastThreatHoldByBot.find(botGuid);
    if (holdActive)
    {
        bool const holdChanged = lastHoldItr == sTwinLastThreatHoldByBot.end() ||
            lastHoldItr->second.type != holdState.type ||
            lastHoldItr->second.openedAtMs != holdState.openedAtMs ||
            lastHoldItr->second.untilMs != holdState.untilMs;
        if (holdChanged)
        {
            std::string holdType = "initial_pull";
            if (holdState.type == Aq40TwinEmperors::ThreatHoldType::Teleport)
                holdType = "teleport";
            else if (holdState.type == Aq40TwinEmperors::ThreatHoldType::PickupFailed)
                holdType = "pickup_failed";

            std::ostringstream fields;
            fields << "boss=twins hold=" << holdType
                   << " opened_ms=" << holdState.openedAtMs
                   << " until_ms=" << holdState.untilMs;
            LogAq40Info(bot, "pickup_window_open",
                "twins:" + holdType + ":" + std::to_string(holdState.openedAtMs), fields.str(), 3000);
        }

        sTwinLastThreatHoldByBot[botGuid] = holdState;
        return true;
    }

    if (lastHoldItr != sTwinLastThreatHoldByBot.end())
    {
        std::ostringstream fields;
        fields << "boss=twins opened_ms=" << lastHoldItr->second.openedAtMs;
        LogAq40Info(bot, "pickup_window_expired",
            "twins:" + std::to_string(lastHoldItr->second.openedAtMs), fields.str(), 3000);
        sTwinLastThreatHoldByBot.erase(lastHoldItr);
    }

    if (Aq40TwinEmperors::HasTwinPickupEstablished(bot, true) ||
        IsTwinWarlockPickupEstablished(bot, botAI, assignment))
    {
        return false;
    }

    uint32 const sourceOpenedAtMs = Aq40TwinEmperors::GetLatestPickupWindowOpenedAtMs(bot);
    if (Aq40TwinEmperors::ArmPickupFailedHold(bot, sourceOpenedAtMs))
    {
        std::ostringstream fields;
        fields << "boss=twins source_opened_ms=" << sourceOpenedAtMs
               << " target=" << GetAq40LogUnit(assignment.veklor);
        LogAq40Warn(bot, "pickup_failed",
            "twins:" + std::to_string(sourceOpenedAtMs) + ":" + GetAq40LogUnit(assignment.veklor),
            fields.str(), 3000);
    }

    if (Aq40TwinEmperors::GetActiveThreatHold(bot, holdState))
    {
        sTwinLastThreatHoldByBot[botGuid] = holdState;
        return true;
    }

    return false;
}

bool IsTwinMeleePickupEstablished(Player* bot, PlayerbotAI* botAI, TwinAssignments const& assignment)
{
    if (!bot || !botAI || !assignment.veknilash)
        return false;

    if (Aq40TwinEmperors::HasTwinPickupEstablished(bot, false))
        return true;

    Group const* group = bot->GetGroup();
    if (!group)
        return false;

    for (GroupReference const* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || !member->IsAlive())
            continue;

        PlayerbotAI* memberAI = GET_PLAYERBOT_AI(member);
        if (GetTwinRoleCohort(member, memberAI ? memberAI : botAI) != TwinRoleCohort::MeleeTank)
            continue;

        if (GetStableTwinRoleIndex(member, memberAI ? memberAI : botAI) != assignment.veknilashSideIndex)
            continue;

        if (IsTwinPickupMemberEstablished(member, assignment, assignment.veknilash))
        {
            Aq40TwinEmperors::NoteTwinPickupEstablished(bot, false);
            return true;
        }
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
        LogAq40Info(bot, "encounter_phase", "cthun:phase2", "boss=cthun phase=phase2", 30000);
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

static GuidVector GetRawObservedSkeramEncounterUnits(Player* bot, PlayerbotAI* botAI, GuidVector const& attackers)
{
    GuidVector observedUnits;
    if (!bot || !botAI || !Aq40BossHelper::IsInAq40(bot))
        return observedUnits;

    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, attackers);
    for (ObjectGuid const guid : encounterUnits)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit || !unit->IsInWorld() || !unit->IsAlive() || unit->GetMapId() != bot->GetMapId() || unit->IsFriendlyTo(bot))
            continue;

        if (Aq40BossHelper::IsUnitNamedAny(botAI, unit, { "the prophet skeram" }))
            observedUnits.push_back(guid);
    }

    return observedUnits;
}

bool IsSkeramEncounterLive(Player* bot, PlayerbotAI* botAI, GuidVector const& attackers)
{
    if (!bot || !botAI || !Aq40BossHelper::IsInAq40(bot))
        return false;

    GuidVector const activeUnits = Aq40BossHelper::GetActiveCombatUnits(botAI, attackers);
    return Aq40BossHelper::HasAnyNamedUnit(botAI, activeUnits, { "the prophet skeram" });
}

GuidVector GetObservedSkeramEncounterUnits(Player* bot, PlayerbotAI* botAI, GuidVector const& attackers)
{
    if (!IsSkeramEncounterLive(bot, botAI, attackers))
        return {};

    return GetRawObservedSkeramEncounterUnits(bot, botAI, attackers);
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

    GuidVector skeramUnits = GetObservedSkeramEncounterUnits(bot, botAI, attackers);
    std::vector<Unit*> skerams = Aq40BossHelper::FindUnitsByAnyName(botAI, skeramUnits, { "the prophet skeram" });
    if (skerams.empty())
    {
        sSkeramPostBlinkHoldUntilByInstance.erase(instanceId);
        return false;
    }

    Player* primaryTank = Aq40BossHelper::GetEncounterPrimaryTank(bot);
    if (!primaryTank || !primaryTank->IsAlive())
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
        if (itr == sSkeramPostBlinkHoldUntilByInstance.end())
        {
            sSkeramPostBlinkHoldUntilByInstance[instanceId] = now + 2500;
            return true;
        }

        if (now < itr->second)
            return true;

        sSkeramPostBlinkHoldUntilByInstance.erase(itr);
        return false;
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
    std::string resetReason = "shared_cleanup";
    if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot))
    {
        GuidVector const attackers = botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get();
        std::string twinResetReason;
        if (IsTwinWipeCleanupEligible(bot, botAI, attackers, &twinResetReason, false))
            resetReason = twinResetReason;
    }

    erased = sTwinAssignments.erase(instanceId) > 0 || erased;
    erased = sTwinRoleLocksByInstance.erase(instanceId) > 0 || erased;
    erased = sTwinKnownTwinBosses.erase(instanceId) > 0 || erased;
    erased = sCthunPhase2StartByInstance.erase(instanceId) > 0 || erased;
    erased = sSkeramPostBlinkHoldUntilByInstance.erase(instanceId) > 0 || erased;
    erased = Aq40TwinEmperors::ResetState(bot) || erased;

    std::set<uint64> instanceMemberGuids;
    if (Group const* group = bot->GetGroup())
    {
        for (GroupReference const* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || !Aq40BossHelper::IsEncounterParticipant(bot, member))
                continue;

            instanceMemberGuids.insert(member->GetGUID().GetRawValue());
        }
    }
    else
    {
        instanceMemberGuids.insert(bot->GetGUID().GetRawValue());
    }

    for (auto itr = sTwinLastThreatHoldByBot.begin(); itr != sTwinLastThreatHoldByBot.end();)
    {
        if (instanceMemberGuids.find(itr->first) == instanceMemberGuids.end())
        {
            ++itr;
            continue;
        }

        itr = sTwinLastThreatHoldByBot.erase(itr);
        erased = true;
    }

    if (erased)
    {
        if (resetReason == "wipe_cleanup")
        {
            LogAq40Info(bot, "encounter_reset", "twins:reset:" + std::to_string(instanceId),
                "boss=twins state=reset reason=wipe_cleanup instance=" + std::to_string(instanceId), 30000);
        }
        else
        {
            LogAq40Info(bot, "encounter_reset", "shared:" + std::to_string(instanceId),
                "boss=shared state=reset instance=" + std::to_string(instanceId), 30000);
        }
    }

    return erased;
}

bool HasPersistentEncounterState(Player* bot)
{
    if (!bot || !bot->GetMap())
        return false;

    uint32 const instanceId = bot->GetMap()->GetInstanceId();
    return sTwinAssignments.find(instanceId) != sTwinAssignments.end() ||
           sTwinRoleLocksByInstance.find(instanceId) != sTwinRoleLocksByInstance.end() ||
           sTwinKnownTwinBosses.find(instanceId) != sTwinKnownTwinBosses.end() ||
           sCthunPhase2StartByInstance.find(instanceId) != sCthunPhase2StartByInstance.end() ||
           Aq40TwinEmperors::HasPersistentState(bot) ||
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
    bool const hasTwinHealerFocusState = HasTwinHealerFocusState(bot);
    bool const hasTwinTemporaryStrategyState = HasTwinTemporaryStrategyState(bot);
    bool const hasPersistentEncounterState = HasPersistentEncounterState(bot);

    if (hasManagedResistanceStrategy || hasTwinHealerFocusState || hasTwinTemporaryStrategyState)
        return true;

    if (!hasPersistentEncounterState)
        return false;

    GuidVector const attackers = botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get();
    if (IsTwinWipeCleanupEligible(bot, botAI, attackers, nullptr, true))
        return true;

    if (IsAnyGroupMemberInTwinRoomInternal(bot))
        return false;

    if (IsTwinPrePullReady(bot, botAI))
        return false;

    if (IsInTwinRoomBounds(bot) && HasTwinVisibleEmperors(bot, botAI))
        return false;

    if (IsTwinRaidCombatActiveInternal(bot))
        return false;

    if (!Aq40BossHelper::GetActiveCombatUnits(botAI, attackers).empty())
        return false;

    if (IsSkeramEncounterLive(bot, botAI, attackers))
        return false;

    if (bot->GetMap())
    {
        uint32 const instanceId = bot->GetMap()->GetInstanceId();
        auto itr = sSkeramPostBlinkHoldUntilByInstance.find(instanceId);
        if (itr != sSkeramPostBlinkHoldUntilByInstance.end())
            sSkeramPostBlinkHoldUntilByInstance.erase(itr);
    }

    return true;
}

bool IsAnyGroupMemberInTwinRoom(Player* bot)
{
    return IsAnyGroupMemberInTwinRoomInternal(bot);
}
}    // namespace Aq40Helpers
