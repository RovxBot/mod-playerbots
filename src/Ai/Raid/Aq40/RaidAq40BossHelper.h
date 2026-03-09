#ifndef _PLAYERBOT_RAIDAQ40BOSSHELPER_H
#define _PLAYERBOT_RAIDAQ40BOSSHELPER_H

#include <initializer_list>
#include <limits>
#include <vector>

#include "ObjectGuid.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "SharedDefines.h"

namespace Aq40BossHelper
{
static constexpr uint32 MAP_ID = 531;  // Temple of Ahn'Qiraj

inline bool IsInAq40(Player const* player)
{
    return player && player->GetMapId() == MAP_ID;
}

inline uint32 GetAliveWarlockOrdinal(Player* player)
{
    if (!player || player->getClass() != CLASS_WARLOCK || !player->IsAlive())
        return std::numeric_limits<uint32>::max();

    Group* group = player->GetGroup();
    if (!group)
        return 0;

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

    for (uint32 ordinal = 0; ordinal < warlocks.size(); ++ordinal)
    {
        if (warlocks[ordinal]->GetGUID() == player->GetGUID())
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
