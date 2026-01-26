/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "LootRollAction.h"

#include <string>
#include <vector>

#include "AiObjectContext.h"
#include "Event.h"
#include "Group.h"
#include "ItemUsageValue.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "SharedDefines.h"

// Encodes the "random enchant" component of an item into a single int32.
//
// WotLK loot can specify a random enchant as either:
//  - a RandomPropertyId (from ItemRandomProperties.dbc), or
//  - a RandomSuffixId   (from ItemRandomSuffix.dbc).
//
// We store both in one signed integer:
//  - > 0 : RandomPropertyId
//  - < 0 : RandomSuffixId (stored as negative)
//  - = 0 : no random enchant
//
// This convention is relied upon by downstream code, notably:
//  - StatsWeightCalculator::CalculateRandomProperty(), which interprets
//    negative values as suffix IDs and looks them up via LookupEntry(-id).
static inline int32 EncodeRandomEnchantParam(uint32 randomPropertyId, uint32 randomSuffixId)
{
    if (randomPropertyId)
        return static_cast<int32>(randomPropertyId);

    if (randomSuffixId)
        return -static_cast<int32>(randomSuffixId);

    return 0;
}

bool LootRollAction::Execute(Event event)
{
    Group* group = bot->GetGroup();
    if (!group)
        return false;

    std::vector<Roll*> const& rolls = group->GetRolls();
    for (Roll* const roll : rolls)
    {
        if (!roll)
            continue;

        // Avoid server crash, key may not exit for the bot on login
        auto it = roll->playerVote.find(bot->GetGUID());
        if (it != roll->playerVote.end() && it->second != NOT_EMITED_YET)
            continue;

        ObjectGuid guid = roll->itemGUID;
        uint32 itemId = roll->itemid;
        int32 randomProperty = EncodeRandomEnchantParam(roll->itemRandomPropId, roll->itemRandomSuffix);

        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
        if (!proto)
            continue;

        LOG_DEBUG("playerbots",
                  "[LootRollDBG] start bot={} item={} \"{}\" class={} q={} lootMethod={} enchSkill={} rp={}",
                  bot->GetName(), itemId, proto->Name1, proto->Class, proto->Quality, (int)group->GetLootMethod(),
                  bot->HasSkill(SKILL_ENCHANTING), randomProperty);

        std::string const itemUsageParam = ItemUsageValue::BuildItemUsageParam(itemId, randomProperty);
        ItemUsage usage = AI_VALUE2(ItemUsage, "loot usage", itemUsageParam);

        LOG_DEBUG("playerbots", "[LootRollDBG] usage={} (EQUIP=1 REPLACE=2 BAD_EQUIP=8 DISENCHANT=9)", (int)usage);
        RollVote vote = CalculateLootRollVote(bot, proto, randomProperty, usage, group, "[LootRollDBG]");
        // Announce + send the roll vote (if ML/FFA => PASS)
        RollVote sent = vote;
        if (group->GetLootMethod() == MASTER_LOOT || group->GetLootMethod() == FREE_FOR_ALL)
            sent = PASS;

        LOG_DEBUG("playerbots", "[LootPaternDBG] send vote={} (lootMethod={} Lvl={}) -> guid={} itemId={}",
                  RollVoteText(sent), (int)group->GetLootMethod(), sPlayerbotAIConfig->lootRollLevel,
                  guid.ToString(),
                  itemId);

        group->CountRollVote(bot->GetGUID(), guid, sent);
        // One item at a time
        return true;
    }

    return false;
}

bool MasterLootRollAction::isUseful() { return !botAI->HasActivePlayerMaster(); }

bool MasterLootRollAction::Execute(Event event)
{
    Player* bot = QueryItemUsageAction::botAI->GetBot();

    WorldPacket p(event.getPacket());  // WorldPacket packet for CMSG_LOOT_ROLL, (8+4+1)
    ObjectGuid creatureGuid;
    uint32 mapId;
    uint32 itemSlot;
    uint32 itemId;
    uint32 randomSuffix;
    uint32 randomPropertyId;
    uint32 count;
    uint32 timeout;

    p.rpos(0);              // reset packet pointer
    p >> creatureGuid;      // creature guid what we're looting
    p >> mapId;             // 3.3.3 mapid
    p >> itemSlot;          // the itemEntryId for the item that shall be rolled for
    p >> itemId;            // the itemEntryId for the item that shall be rolled for
    p >> randomSuffix;      // randomSuffix
    p >> randomPropertyId;  // item random property ID
    p >> count;             // items in stack
    p >> timeout;           // the countdown time to choose "need" or "greed"

    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
    if (!proto)
        return false;

    Group* group = bot->GetGroup();
    if (!group)
        return false;

    LOG_DEBUG("playerbots",
              "[LootEnchantDBG][ML] start bot={} item={} \"{}\" class={} q={} lootMethod={} enchSkill={} rp={}",
              bot->GetName(), itemId, proto->Name1, proto->Class, proto->Quality, (int)group->GetLootMethod(),
              bot->HasSkill(SKILL_ENCHANTING), randomPropertyId);

    // Compute random property and usage, same pattern as LootRollAction::Execute
    int32 randomProperty = EncodeRandomEnchantParam(randomPropertyId, randomSuffix);

    std::string const itemUsageParam = ItemUsageValue::BuildItemUsageParam(itemId, randomProperty);
    ItemUsage usage = AI_VALUE2(ItemUsage, "loot usage", itemUsageParam);

    // 1) Token heuristic: ONLY NEED if the target slot is a likely upgrade
    RollVote vote = CalculateLootRollVote(bot, proto, randomProperty, usage, group, "[LootEnchantDBG][ML]");

    RollVote sent = vote;
    if (group->GetLootMethod() == MASTER_LOOT || group->GetLootMethod() == FREE_FOR_ALL)
        sent = PASS;

    LOG_DEBUG("playerbots", "[LootEnchantDBG][ML] vote={} -> sent={} lootMethod={} enchSkill={} deOK={}",
              RollVoteText(vote), RollVoteText(sent), (int)group->GetLootMethod(),
              bot->HasSkill(SKILL_ENCHANTING), usage == ITEM_USAGE_DISENCHANT ? 1 : 0);

    group->CountRollVote(bot->GetGUID(), creatureGuid, sent);

    return true;
}

bool RollAction::Execute(Event event)
{
    std::string link = event.getParam();

    if (link.empty())
    {
        bot->DoRandomRoll(0, 100);
        return false;
    }
    ItemIds itemIds = chat->parseItems(link);
    if (itemIds.empty())
        return false;
    uint32 itemId = *itemIds.begin();
    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
    if (!proto)
        return false;

    std::string itemUsageParam;
    itemUsageParam = std::to_string(itemId);

    ItemUsage usage = AI_VALUE2(ItemUsage, "item usage", itemUsageParam);
    switch (proto->Class)
    {
        case ITEM_CLASS_WEAPON:
        case ITEM_CLASS_ARMOR:
            if (usage == ITEM_USAGE_EQUIP || usage == ITEM_USAGE_REPLACE)
                bot->DoRandomRoll(0, 100);
    }

    return true;
}