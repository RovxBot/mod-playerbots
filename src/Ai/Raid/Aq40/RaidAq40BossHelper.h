#ifndef _PLAYERBOT_RAIDAQ40BOSSHELPER_H
#define _PLAYERBOT_RAIDAQ40BOSSHELPER_H

#include <algorithm>
#include <array>
#include <initializer_list>
#include <limits>
#include <unordered_map>
#include <vector>

#include "Group.h"
#include "ObjectGuid.h"
#include "Player.h"
#include "Playerbots.h"
#include "PlayerbotAI.h"
#include "SharedDefines.h"

namespace Aq40BossHelper
{
static constexpr uint32 MAP_ID = 531;  // Temple of Ahn'Qiraj

inline bool IsInAq40(Player const* player)
{
    return player && player->GetMapId() == MAP_ID;
}

inline Player* GetEncounterPrimaryTank(Player* player)
{
    if (!player)
        return nullptr;

    Group* group = player->GetGroup();
    if (!group)
        return PlayerbotAI::IsTank(player) && player->IsAlive() ? player : nullptr;

    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || !member->IsAlive() || !PlayerbotAI::IsTank(member))
            continue;

        if (PlayerbotAI::IsMainTank(member))
            return member;
    }

    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (member && member->IsAlive() && PlayerbotAI::IsTank(member))
            return member;
    }

    return nullptr;
}

inline Player* GetEncounterBackupTank(Player* player, uint8 index = 0)
{
    if (!player)
        return nullptr;

    Group* group = player->GetGroup();
    if (!group)
        return nullptr;

    Player* primaryTank = GetEncounterPrimaryTank(player);
    std::vector<Player*> backups;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || !member->IsAlive() || !PlayerbotAI::IsTank(member) || member == primaryTank)
            continue;

        backups.push_back(member);
    }

    std::sort(backups.begin(), backups.end(), [](Player* left, Player* right)
    {
        auto getPriority = [](Player* member) -> uint32
        {
            if (PlayerbotAI::IsAssistTankOfIndex(member, 0, true))
                return 0;
            if (PlayerbotAI::IsAssistTankOfIndex(member, 1, true))
                return 1;
            if (PlayerbotAI::IsAssistTank(member))
                return 2;
            return 10;
        };

        uint32 const leftPriority = getPriority(left);
        uint32 const rightPriority = getPriority(right);
        if (leftPriority != rightPriority)
            return leftPriority < rightPriority;

        return left->GetGUID().GetRawValue() < right->GetGUID().GetRawValue();
    });

    return index < backups.size() ? backups[index] : nullptr;
}

inline bool IsEncounterPrimaryTank(Player* referencePlayer, Player* player)
{
    return player && player == GetEncounterPrimaryTank(referencePlayer);
}

inline bool IsEncounterBackupTank(Player* referencePlayer, Player* player, uint8 index = 0)
{
    return player && player == GetEncounterBackupTank(referencePlayer, index);
}

inline bool IsEncounterTank(Player* referencePlayer, Player* player)
{
    return IsEncounterPrimaryTank(referencePlayer, player) ||
           IsEncounterBackupTank(referencePlayer, player, 0) ||
           IsEncounterBackupTank(referencePlayer, player, 1);
}

inline uint32 GetAliveWarlockOrdinal(Player* player)
{
    if (!player || player->getClass() != CLASS_WARLOCK || !player->IsAlive())
        return std::numeric_limits<uint32>::max();

    Group* group = player->GetGroup();
    if (!group)
        return 0;

    uint32 const instanceId = player->GetMap() ? player->GetMap()->GetInstanceId() : 0;
    static std::unordered_map<uint32, std::array<uint64, 2>> sTwinWarlockAssignmentsByInstance;
    if (!player->IsInCombat())
        sTwinWarlockAssignmentsByInstance.erase(instanceId);

    std::vector<Player*> warlocks;

    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || !member->IsAlive() || member->getClass() != CLASS_WARLOCK)
            continue;

        warlocks.push_back(member);
    }

    if (warlocks.empty())
        return std::numeric_limits<uint32>::max();

    std::sort(warlocks.begin(), warlocks.end(), [](Player* left, Player* right)
    {
        int32 const leftShadowRes = left->GetResistance(SPELL_SCHOOL_SHADOW);
        int32 const rightShadowRes = right->GetResistance(SPELL_SCHOOL_SHADOW);
        if (leftShadowRes != rightShadowRes)
            return leftShadowRes > rightShadowRes;

        return left->GetGUID().GetRawValue() < right->GetGUID().GetRawValue();
    });

    auto& assignedWarlocks = sTwinWarlockAssignmentsByInstance[instanceId];
    auto findAliveAssignedWarlock = [&](uint64 guid) -> Player*
    {
        if (!guid)
            return nullptr;

        for (Player* warlock : warlocks)
        {
            if (warlock->GetGUID().GetRawValue() == guid)
                return warlock;
        }

        return nullptr;
    };

    for (uint32 slot = 0; slot < assignedWarlocks.size(); ++slot)
    {
        if (!findAliveAssignedWarlock(assignedWarlocks[slot]))
            assignedWarlocks[slot] = 0;
    }

    for (Player* warlock : warlocks)
    {
        uint64 const guid = warlock->GetGUID().GetRawValue();
        if (assignedWarlocks[0] == guid || assignedWarlocks[1] == guid)
            continue;

        for (uint32 slot = 0; slot < assignedWarlocks.size(); ++slot)
        {
            if (!assignedWarlocks[slot])
            {
                assignedWarlocks[slot] = guid;
                break;
            }
        }
    }

    for (uint32 ordinal = 0; ordinal < assignedWarlocks.size(); ++ordinal)
    {
        if (assignedWarlocks[ordinal] == player->GetGUID().GetRawValue())
            return ordinal;
    }

    return std::numeric_limits<uint32>::max();
}

inline bool IsDesignatedTwinWarlockTank(Player* player)
{
    uint32 ordinal = GetAliveWarlockOrdinal(player);
    return ordinal != std::numeric_limits<uint32>::max() && ordinal < 2;
}

inline bool IsUnitNamedAny(PlayerbotAI* botAI, Unit* unit, std::initializer_list<char const*> names)
{
    if (!botAI || !unit)
        return false;

    for (char const* name : names)
    {
        if (botAI->EqualLowercaseName(unit->GetName(), name))
            return true;
    }

    return false;
}

inline Unit* FindUnitByAnyName(PlayerbotAI* botAI, GuidVector const& units, std::initializer_list<char const*> names)
{
    if (!botAI)
        return nullptr;

    for (ObjectGuid const guid : units)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (IsUnitNamedAny(botAI, unit, names))
            return unit;
    }

    return nullptr;
}

inline std::vector<Unit*> FindUnitsByAnyName(PlayerbotAI* botAI, GuidVector const& units,
                                             std::initializer_list<char const*> names)
{
    std::vector<Unit*> found;
    if (!botAI)
        return found;

    for (ObjectGuid const guid : units)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (IsUnitNamedAny(botAI, unit, names))
            found.push_back(unit);
    }

    return found;
}

inline bool IsNearbyEncounterUnit(Player* bot, PlayerbotAI* botAI, Unit* candidate, GuidVector const& attackers)
{
    if (!bot || !botAI || !candidate || !candidate->IsInWorld() || !candidate->IsAlive() ||
        candidate->GetMapId() != bot->GetMapId() || candidate->IsFriendlyTo(bot))
        return false;

    float const encounterRange = sPlayerbotAIConfig.sightDistance;

    if (candidate->GetDistance2d(bot) <= encounterRange)
        return true;

    for (ObjectGuid const attackerGuid : attackers)
    {
        Unit* attacker = botAI->GetUnit(attackerGuid);
        if (!attacker || !attacker->IsInWorld() || attacker->GetMapId() != bot->GetMapId())
            continue;

        if (candidate->GetDistance2d(attacker) <= encounterRange)
            return true;
    }

    return false;
}

inline GuidVector GetEncounterUnits(PlayerbotAI* botAI, GuidVector const& attackers)
{
    GuidVector units = attackers;
    if (!botAI)
        return units;

    Player* bot = botAI->GetBot();
    GuidVector const& possibleTargetsNoLos =
        botAI->GetAiObjectContext()->GetValue<GuidVector>("possible targets no los")->Get();
    for (ObjectGuid const guid : possibleTargetsNoLos)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!IsNearbyEncounterUnit(bot, botAI, unit, attackers))
            continue;

        if (std::find(units.begin(), units.end(), guid) == units.end())
            units.push_back(guid);
    }

    return units;
}

inline bool HasAnyNamedUnit(PlayerbotAI* botAI, GuidVector const& units, std::initializer_list<char const*> names)
{
    return FindUnitByAnyName(botAI, units, names) != nullptr;
}

inline Unit* FindLowestHealthUnitByAnyName(PlayerbotAI* botAI, GuidVector const& units,
                                           std::initializer_list<char const*> names)
{
    std::vector<Unit*> matches = FindUnitsByAnyName(botAI, units, names);
    Unit* chosen = nullptr;
    for (Unit* unit : matches)
    {
        if (!unit)
            continue;

        if (!chosen || unit->GetHealthPct() < chosen->GetHealthPct())
            chosen = unit;
    }

    return chosen;
}

inline bool IsBossEncounterActive(PlayerbotAI* botAI, GuidVector const& attackers)
{
    return HasAnyNamedUnit(botAI, attackers,
                           { "the prophet skeram", "battleguard sartura", "sartura's royal guard",
                             "lord kri", "princess yauj", "vem", "yauj brood",
                             "fankriss the unyielding", "spawn of fankriss", "princess huhuran",
                             "emperor vek'nilash", "emperor vek'lor", "ouro", "dirt mound",
                             "viscidus", "glob of viscidus", "toxic slime", "c'thun",
                             "eye of c'thun", "eye tentacle", "claw tentacle",
                             "giant eye tentacle", "giant claw tentacle", "flesh tentacle" });
}

inline bool IsTrashEncounterActive(PlayerbotAI* botAI, GuidVector const& attackers)
{
    return HasAnyNamedUnit(botAI, attackers,
                           { "anubisath warder", "anubisath defender", "obsidian eradicator", "obsidian nullifier",
                             "vekniss stinger", "qiraji slayer", "qiraji champion", "qiraji mindslayer",
                             "qiraji brainwasher", "qiraji battleguard", "anubisath sentinel", "qiraji lasher",
                             "vekniss warrior", "vekniss guardian", "vekniss drone", "vekniss soldier",
                             "vekniss wasp", "scarab", "qiraji scarab", "spitting scarab", "scorpion" });
}
}  // namespace Aq40BossHelper

#endif
