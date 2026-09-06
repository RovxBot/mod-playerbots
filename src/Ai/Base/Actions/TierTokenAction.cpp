/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "TierTokenAction.h"

#include "DBCStores.h"
#include "DatabaseEnv.h"
#include "Event.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "LootMgr.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "StatsWeightCalculator.h"
#include "WorldPacket.h"
#include <algorithm>
#include <limits>
#include <unordered_map>
#include <vector>

namespace
{
using TierTokenRewards = std::unordered_map<uint32, std::vector<uint32>>;

bool IsTierToken(ItemTemplate const* item)
{
    return item && item->Class == ITEM_CLASS_MISC && item->SubClass == ITEM_SUBCLASS_JUNK &&
           item->Quality == ITEM_QUALITY_EPIC && item->GetMaxStackSize() == 1;
}

TierTokenRewards LoadTierTokenRewards()
{
    TierTokenRewards rewards;
    QueryResult result = WorldDatabase.Query("SELECT item, ExtendedCost FROM npc_vendor WHERE item > 0 AND ExtendedCost > 0");
    if (!result)
    {
        LOG_WARN("playerbots", "No vendor items with extended costs found; tier tokens will not be converted");
        return rewards;
    }

    do
    {
        Field* fields = result->Fetch();
        uint32 rewardId = fields[0].Get<uint32>();
        ItemExtendedCostEntry const* cost = sItemExtendedCostStore.LookupEntry(fields[1].Get<uint32>());
        ItemTemplate const* reward = sObjectMgr->GetItemTemplate(rewardId);

        // A raid-tier reward is an armour item belonging to an item set. This prevents currencies and
        // non-tier vendor items from becoming conversion targets.
        if (!cost || !reward || reward->Class != ITEM_CLASS_ARMOR || reward->ItemSet == 0)
            continue;

        for (uint8 requirement = 0; requirement < MAX_ITEM_EXTENDED_COST_REQUIREMENTS; ++requirement)
        {
            uint32 tokenId = cost->reqitem[requirement];
            if (cost->reqitemcount[requirement] != 1 || !IsTierToken(sObjectMgr->GetItemTemplate(tokenId)))
                continue;

            rewards[tokenId].push_back(rewardId);
        }
    } while (result->NextRow());

    uint32 rewardCount = 0;
    for (auto& [tokenId, tokenRewards] : rewards)
    {
        std::sort(tokenRewards.begin(), tokenRewards.end());
        tokenRewards.erase(std::unique(tokenRewards.begin(), tokenRewards.end()), tokenRewards.end());
        rewardCount += tokenRewards.size();
    }

    LOG_INFO("playerbots", "Loaded {} tier-token rewards for {} tokens", rewardCount, rewards.size());
    return rewards;
}

TierTokenRewards const& GetTierTokenRewards()
{
    static TierTokenRewards const rewards = LoadTierTokenRewards();
    return rewards;
}

uint32 SelectTierReward(Player* bot, uint32 tokenId)
{
    TierTokenRewards const& rewards = GetTierTokenRewards();
    auto const tokenRewards = rewards.find(tokenId);
    if (tokenRewards == rewards.end())
        return 0;

    uint32 const classMask = 1u << (bot->getClass() - 1);
    float bestScore = std::numeric_limits<float>::lowest();
    uint32 bestRewardId = 0;
    StatsWeightCalculator calculator(bot, true);

    for (uint32 rewardId : tokenRewards->second)
    {
        ItemTemplate const* reward = sObjectMgr->GetItemTemplate(rewardId);
        if (!reward || (reward->AllowableClass && !(reward->AllowableClass & classMask)) ||
            bot->CanUseItem(reward) != EQUIP_ERR_OK)
        {
            continue;
        }

        float score = calculator.CalculateItem(rewardId);
        if (score > bestScore)
        {
            bestScore = score;
            bestRewardId = rewardId;
        }
    }

    return bestRewardId;
}

void RestoreToken(Player* bot, uint32 tokenId)
{
    ItemPosCountVec dest;
    if (bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, tokenId, 1) != EQUIP_ERR_OK)
        return;

    if (Item* token = bot->StoreNewItem(dest, tokenId, true))
        bot->SendNewItem(token, 1, false, false);
}

bool ConvertTierToken(Player* bot, Item* token)
{
    uint32 const tokenId = token->GetEntry();
    uint32 rewardId = SelectTierReward(bot, tokenId);
    if (!rewardId || bot->CanTakeMoreSimilarItems(rewardId, 1) != EQUIP_ERR_OK)
        return false;

    // Removing the non-stackable token first frees a bag slot. CanStoreNewItem below must therefore
    // succeed unless an unexpected inventory rule rejects the selected reward.
    bot->DestroyItem(token->GetBagSlot(), token->GetSlot(), true);

    ItemPosCountVec dest;
    if (bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, rewardId, 1) != EQUIP_ERR_OK)
    {
        LOG_ERROR("playerbots", "Could not store tier reward {} for bot {}; restoring token {}", rewardId,
                  bot->GetName(), tokenId);
        RestoreToken(bot, tokenId);
        return false;
    }

    Item* reward = bot->StoreNewItem(dest, rewardId, true);
    if (!reward)
    {
        LOG_ERROR("playerbots", "Could not create tier reward {} for bot {}; restoring token {}", rewardId,
                  bot->GetName(), tokenId);
        RestoreToken(bot, tokenId);
        return false;
    }

    bot->SendNewItem(reward, 1, false, false);
    LOG_DEBUG("playerbots", "Converted tier token {} into reward {} for bot {}", tokenId, rewardId, bot->GetName());
    return true;
}
}  // namespace

bool ConvertTierTokenAction::Execute(Event event)
{
    if (!sPlayerbotAIConfig.autoConvertTierTokens)
        return false;

    WorldPacket packet(event.getPacket());
    packet.rpos(0);

    Item* token = nullptr;
    uint32 tokenId = 0;
    if (event.GetSource() == "item push result")
    {
        ObjectGuid owner;
        uint32 received;
        uint32 created;
        uint32 sendChatMessage;
        uint8 bag;
        uint32 slot;

        packet >> owner >> received >> created >> sendChatMessage >> bag >> slot >> tokenId;

        // Loot awards are sent with received == 0 and created == 0. Limiting conversion to this case
        // prevents a traded, vendor-bought, crafted, or GM-created token from being consumed.
        if (owner != bot->GetGUID() || received != 0 || created != 0 || slot == uint32(-1))
            return false;

        token = bot->GetItemByPos(bag, uint8(slot));
    }
    else if (event.GetSource() == "loot roll won")
    {
        ObjectGuid source;
        uint32 lootSlot;
        uint32 itemSuffix;
        uint32 itemProperty;
        ObjectGuid winner;
        uint8 rollNumber;
        uint8 rollType;

        packet >> source >> lootSlot >> tokenId >> itemSuffix >> itemProperty >> winner >> rollNumber >> rollType;
        if (winner != bot->GetGUID() || (rollType != ROLL_NEED && rollType != ROLL_GREED))
            return false;

        // Group-roll rewards do not send SMSG_ITEM_PUSH_RESULT. The bot AI handles this packet on its
        // next update, after the reward has been stored, so locate the newly awarded non-stackable token.
        token = bot->GetItemByEntry(tokenId);
    }
    else
        return false;

    if (!token || token->GetEntry() != tokenId || !IsTierToken(token->GetTemplate()))
        return false;

    return ConvertTierToken(bot, token);
}
