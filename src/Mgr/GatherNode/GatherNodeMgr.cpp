/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "GatherNodeMgr.h"

#include <algorithm>
#include <cmath>

#include "DBCStores.h"
#include "DatabaseEnv.h"
#include "GameObject.h"
#include "Log.h"
#include "Map.h"
#include "MapMgr.h"
#include "Object.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "Random.h"
#include "SharedDefines.h"
#include "Timer.h"

namespace
{
constexpr float GATHER_CELL_SIZE = 128.0f;

int32 GatherCell(float coordinate) { return int32(std::floor(coordinate / GATHER_CELL_SIZE)); }

uint64 GatherCellKey(int32 x, int32 y) { return (uint64(uint32(x)) << 32) | uint32(y); }

bool GetGatherSkillFromTemplate(GameObjectTemplate const* goInfo, uint32& skillId, uint32& reqSkillValue)
{
    if (!goInfo || goInfo->type != GAMEOBJECT_TYPE_CHEST)
        return false;

    uint32 lockId = goInfo->GetLockId();
    if (!lockId)
        return false;

    LockEntry const* lockInfo = sLockStore.LookupEntry(lockId);
    if (!lockInfo)
        return false;

    for (uint8 j = 0; j < 8; ++j)
    {
        if (lockInfo->Type[j] != LOCK_KEY_SKILL)
            continue;

        uint32 skill = SkillByLockType(LockType(lockInfo->Index[j]));
        if (skill == SKILL_HERBALISM || skill == SKILL_MINING)
        {
            skillId = skill;
            reqSkillValue = std::max(1u, lockInfo->Skill[j]);
            return true;
        }
    }

    return false;
}
}

void GatherNodeMgr::Load()
{
    _nodeCells.clear();
    _nodes.clear();
    uint32 oldMSTime = getMSTime();
    uint32 count = 0;

    // Loot ids with at least one freely lootable row. Quest-gated
    // "gathering" chests (Cactus Apple, Serpentbloom, ...) carry a
    // herbalism/mining lock but ALL their loot rows are QuestRequired, so
    // bots without the quest can't harvest them - and they never despawn,
    // which makes them permanent live-node bait. Note that filtering on
    // gameobject_questitem instead would be wrong: standard herbs carry a
    // conditional quest item (Root Sample) on top of their normal loot.
    std::unordered_set<uint32> openLootIds;
    if (QueryResult result =
            WorldDatabase.Query("SELECT DISTINCT Entry FROM gameobject_loot_template WHERE QuestRequired = 0"))
    {
        do
        {
            openLootIds.insert((*result)[0].Get<uint32>());
        } while (result->NextRow());
    }

    std::unordered_map<ObjectGuid::LowType, uint32> dbZoneIds;
    if (QueryResult result = WorldDatabase.Query("SELECT guid, zoneId FROM gameobject WHERE zoneId <> 0"))
    {
        do
        {
            dbZoneIds[(*result)[0].Get<uint32>()] = (*result)[1].Get<uint32>();
        } while (result->NextRow());
    }

    for (auto const& [spawnId, goData] : sObjectMgr->GetAllGOData())
    {
        // Instances are excluded: the state targets outdoor zone roaming.
        MapEntry const* mapEntry = sMapStore.LookupEntry(goData.mapid);
        if (!mapEntry || !mapEntry->IsContinent())
            continue;

        GameObjectTemplate const* goInfo = sObjectMgr->GetGameObjectTemplate(goData.id);

        uint32 skillId = 0;
        uint32 reqSkillValue = 0;
        if (!GetGatherSkillFromTemplate(goInfo, skillId, reqSkillValue))
            continue;

        if (!openLootIds.contains(goInfo->GetLootId()))
            continue;

        GatherNodeSpawn node;
        node.spawnId = spawnId;
        node.pos = WorldPosition(goData.mapid, goData.posX, goData.posY, goData.posZ);
        node.skillId = skillId;
        node.reqSkillValue = reqSkillValue;
        auto zoneItr = dbZoneIds.find(spawnId);
        uint32 zoneId = zoneItr != dbZoneIds.end()
                            ? zoneItr->second
                            : sMapMgr->GetZoneId(PHASEMASK_NORMAL, goData.mapid, goData.posX, goData.posY, goData.posZ);
        _nodes[goData.mapid][zoneId].push_back(std::move(node));
        ++count;
    }

    // Zone vectors must stop growing before the spatial index stores node addresses.
    for (auto const& [mapId, zones] : _nodes)
        for (auto const& [zoneId, nodes] : zones)
            for (GatherNodeSpawn const& node : nodes)
            {
                uint64 const cell =
                    GatherCellKey(GatherCell(node.pos.GetPositionX()), GatherCell(node.pos.GetPositionY()));
                _nodeCells[mapId][cell].push_back(&node);
            }

    LOG_INFO("playerbots", ">> Loaded {} gather node spawns in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

bool GatherNodeMgr::IsUsable(PlayerbotAI* botAI, Player* bot, GatherNodeSpawn const& node)
{
    if (!botAI->HasSkill(SkillType(node.skillId)))
        return false;

    return bot->GetSkillValue(node.skillId) >= node.reqSkillValue;
}

std::vector<GatherNodeSpawn> const* GatherNodeMgr::GetZoneNodes(uint32 mapId, uint32 zoneId) const
{
    auto mapItr = _nodes.find(mapId);
    if (mapItr == _nodes.end())
        return nullptr;

    auto zoneItr = mapItr->second.find(zoneId);
    return zoneItr != mapItr->second.end() ? &zoneItr->second : nullptr;
}

bool GatherNodeMgr::HasUsableNodes(Player* bot)
{
    std::vector<GatherNodeSpawn> const* nodes = GetZoneNodes(bot->GetMapId(), bot->GetZoneId());
    if (!nodes)
        return false;

    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI)
        return false;

    for (GatherNodeSpawn const& node : *nodes)
        if (IsUsable(botAI, bot, node))
            return true;

    return false;
}

GatherNodeSpawn const* GatherNodeMgr::GetNextNode(Player* bot, std::unordered_set<ObjectGuid::LowType> const& visited)
{
    std::vector<GatherNodeSpawn> const* nodes = GetZoneNodes(bot->GetMapId(), bot->GetZoneId());
    if (!nodes)
        return nullptr;

    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI)
        return nullptr;

    Map* map = bot->GetMap();
    WorldPosition botPos(bot);

    std::vector<std::pair<float, GatherNodeSpawn const*>> candidates;
    for (GatherNodeSpawn const& node : *nodes)
    {
        if (visited.contains(node.spawnId) || !IsUsable(botAI, bot, node))
            continue;

        // Don't route to spawn points we can already see to be empty.
        // Evaluated fresh on every selection so respawns come back into
        // the pool naturally.
        if (IsVerifiablyDown(map, node.pos, node.spawnId))
            continue;

        candidates.push_back({botPos.sqDistance(node.pos), &node});
    }

    if (candidates.empty())
        return nullptr;

    uint32 nearestCount = std::min<uint32>(4, candidates.size());
    std::partial_sort(candidates.begin(), candidates.begin() + nearestCount, candidates.end(),
                      [](auto const& a, auto const& b) { return a.first < b.first; });

    return candidates[urand(0, nearestCount - 1)].second;
}

GameObject* GatherNodeMgr::FindLiveNode(Map* map, ObjectGuid::LowType spawnId)
{
    auto bounds = map->GetGameObjectBySpawnIdStore().equal_range(spawnId);
    for (auto it = bounds.first; it != bounds.second; ++it)
        if (it->second->isSpawned() && it->second->GetGoState() == GO_STATE_READY)
            return it->second;

    return nullptr;
}

bool GatherNodeMgr::IsVerifiablyDown(Map* map, WorldPosition const& pos, ObjectGuid::LowType spawnId)
{
    // An unloaded grid means "unknown", not "not up".
    if (!map->IsGridLoaded(pos.GetPositionX(), pos.GetPositionY()))
        return false;

    return !FindLiveNode(map, spawnId);
}

GatherNodeSpawn const* GatherNodeMgr::GetNearestLiveNode(Player* bot,
                                                         std::unordered_set<ObjectGuid::LowType> const& visited,
                                                         float radius)
{
    if (!std::isfinite(radius) || radius < 0.0f)
        return nullptr;
    auto mapItr = _nodeCells.find(bot->GetMapId());
    if (mapItr == _nodeCells.end())
        return nullptr;

    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI)
        return nullptr;

    Map* map = bot->GetMap();
    WorldPosition botPos(bot);
    GatherNodeSpawn const* nearest = nullptr;
    float nearestDistSq = radius * radius;
    int32 const minX = GatherCell(bot->GetPositionX() - radius);
    int32 const minY = GatherCell(bot->GetPositionY() - radius);
    int32 const maxX = GatherCell(bot->GetPositionX() + radius);
    int32 const maxY = GatherCell(bot->GetPositionY() + radius);
    uint32 const width = maxX - minX + 1;
    uint32 const height = maxY - minY + 1;
    for (uint32 index = 0; index < width * height; ++index)
    {
        auto const cell = mapItr->second.find(GatherCellKey(minX + int32(index % width), minY + int32(index / width)));
        if (cell == mapItr->second.end())
            continue;
        for (GatherNodeSpawn const* node : cell->second)
        {
            float distSq = botPos.sqDistance(node->pos);
            if (distSq > nearestDistSq)
                continue;

            if (visited.contains(node->spawnId) || !IsUsable(botAI, bot, *node))
                continue;

            if (!map->IsGridLoaded(node->pos.GetPositionX(), node->pos.GetPositionY()))
                continue;

            if (!FindLiveNode(map, node->spawnId))
                continue;

            nearest = node;
            nearestDistSq = distSq;
        }
    }

    return nearest;
}
