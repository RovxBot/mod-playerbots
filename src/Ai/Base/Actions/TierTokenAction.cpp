/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "TierTokenAction.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <unordered_map>
#include <vector>

#include "DBCStores.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "RandomItemMgr.h"
#include "StatsWeightCalculator.h"
#include "WorldPacket.h"

namespace
{
constexpr uint32 TIER_TOKEN_RETRY_DELAY_MS = 30 * IN_MILLISECONDS;

struct TierReward
{
    uint32 ItemId;
    uint32 ExtendedCost;
};

// Populated on startup, before map updates begin; readers never modify this cache.
std::unordered_map<uint32, std::vector<TierReward>> tierTokenRewards;

bool IsTierToken(ItemTemplate const* item)
{
    return item && item->Class == ITEM_CLASS_MISC && item->SubClass == ITEM_SUBCLASS_JUNK &&
           item->Quality == ITEM_QUALITY_EPIC && item->GetMaxStackSize() == 1;
}

bool CanAffordTierReward(Player* bot, TierReward const& reward, uint32 pendingTokenId)
{
    ItemExtendedCostEntry const* cost = sItemExtendedCostStore.LookupEntry(reward.ExtendedCost);
    if (!cost || cost->reqhonorpoints || cost->reqarenapoints || cost->reqpersonalarenarating)
        return false;

    for (uint8 requirement = 0; requirement < MAX_ITEM_EXTENDED_COST_REQUIREMENTS; ++requirement)
        if (cost->reqitem[requirement] && cost->reqitem[requirement] != pendingTokenId &&
            !bot->HasItemCount(cost->reqitem[requirement], cost->reqitemcount[requirement]))
            return false;

    return bot->CanTakeMoreSimilarItems(reward.ItemId, 1) == EQUIP_ERR_OK;
}

TierReward const* SelectTierReward(Player* bot, uint32 tokenId, bool tokenAwardPending = false)
{
    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI)
        return nullptr;

    auto const tokenRewards = tierTokenRewards.find(tokenId);
    if (tokenRewards == tierTokenRewards.end())
        return nullptr;

    float bestScore = std::numeric_limits<float>::lowest();
    TierReward const* bestReward = nullptr;
    StatsWeightCalculator calculator(bot, true);
    calculator.SetItemSetBonus(false);
    calculator.SetOverflowPenalty(false);

    for (TierReward const& candidate : tokenRewards->second)
    {
        ItemTemplate const* reward = sObjectMgr->GetItemTemplate(candidate.ItemId);
        if (!reward || bot->CanUseItem(reward) != EQUIP_ERR_OK ||
            !sRandomItemMgr.CanEquipArmor(reward, bot->getClass(), bot->GetLevel()) ||
            !CanAffordTierReward(bot, candidate, tokenAwardPending ? tokenId : 0))
            continue;

        uint8 const slot = botAI->FindEquipSlot(reward, NULL_SLOT, true);
        if (slot == NULL_SLOT)
            continue;

        float const score = calculator.CalculateItem(candidate.ItemId);
        if (score <= 0.0f)
            continue;

        if (Item* equipped = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
        {
            float const equippedScore =
                calculator.CalculateItem(equipped->GetEntry(), equipped->GetItemRandomPropertyId());
            if (equipped->GetEntry() == candidate.ItemId || score <= equippedScore ||
                score <= equippedScore * sPlayerbotAIConfig.equipUpgradeThreshold)
                continue;
        }

        if (score > bestScore)
        {
            bestScore = score;
            bestReward = &candidate;
        }
    }

    return bestReward;
}

bool ConvertTierToken(Player* bot, Item* token)
{
    uint32 const tokenId = token->GetEntry();
    TierReward const* selected = SelectTierReward(bot, tokenId);
    if (!selected)
        return false;

    std::unique_ptr<Item> reward(Item::CreateItem(selected->ItemId, 1, bot));
    if (!reward)
        return false;

    // Validate the replacement before consuming anything, including when the bags are full.
    ItemPosCountVec dest;
    if (bot->CanStoreItem(token->GetBagSlot(), token->GetSlot(), dest, reward.get(), true) != EQUIP_ERR_OK)
        return false;

    ItemExtendedCostEntry const* cost = sItemExtendedCostStore.LookupEntry(selected->ExtendedCost);
    bot->DestroyItem(token->GetBagSlot(), token->GetSlot(), true);
    for (uint8 requirement = 0; requirement < MAX_ITEM_EXTENDED_COST_REQUIREMENTS; ++requirement)
        if (cost->reqitem[requirement] && cost->reqitem[requirement] != tokenId)
            bot->DestroyItemCount(cost->reqitem[requirement], cost->reqitemcount[requirement], true);

    Item* storedReward = bot->StoreItem(dest, reward.release(), true);
    bot->AdditionalSavingAddMask(ADDITIONAL_SAVING_INVENTORY_AND_GOLD);
    bot->ItemAddedQuestCheck(selected->ItemId, 1);
    bot->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_RECEIVE_EPIC_ITEM, selected->ItemId, 1);
    bot->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_OWN_ITEM, selected->ItemId, 1);
    bot->SendNewItem(storedReward, 1, true, false);
    LOG_DEBUG("playerbots", "Converted tier token {} into reward {} for bot {}", tokenId, selected->ItemId,
              bot->GetName());
    return true;
}

void AttemptTierTokenConversion(ObjectGuid botGuid, ObjectGuid tokenGuid)
{
    Player* bot = ObjectAccessor::FindPlayer(botGuid);
    PlayerbotAI* botAI = bot ? GET_PLAYERBOT_AI(bot) : nullptr;
    if (!botAI || !sPlayerbotAIConfig.autoConvertTierTokens)
        return;

    Item* token = bot->GetItemByGuid(tokenGuid);
    if (!token || !Player::IsInventoryPos(token->GetBagSlot(), token->GetSlot()))
        return;

    if (bot->IsAlive() && bot->IsInWorld() && !bot->IsBeingTeleported() && !bot->IsInCombat() && !bot->GetTradeData() &&
        ConvertTierToken(bot, token))
    {
        botAI->DoSpecificAction("equip upgrades packet action", Event(), true);
        return;
    }

    botAI->AddTimedEvent([botGuid, tokenGuid]() { AttemptTierTokenConversion(botGuid, tokenGuid); },
                         TIER_TOKEN_RETRY_DELAY_MS);
}
}  // namespace

void InitializeTierTokenRewards()
{
    for (auto const& [entry, creature] : *sObjectMgr->GetCreatureTemplates())
    {
        VendorItemData const* vendor = sObjectMgr->GetNpcVendorItemList(entry);
        if (!vendor)
            continue;

        for (uint32 slot = 0; slot < vendor->GetItemCount(); ++slot)
        {
            VendorItem const* vendorItem = vendor->GetItem(slot);
            ItemTemplate const* reward = sObjectMgr->GetItemTemplate(vendorItem->item);
            ItemExtendedCostEntry const* cost = sItemExtendedCostStore.LookupEntry(vendorItem->ExtendedCost);
            if (!cost || !reward || reward->Class != ITEM_CLASS_ARMOR || !reward->ItemSet ||
                (vendorItem->IsGoldRequired(reward) && reward->BuyPrice) || cost->reqhonorpoints ||
                cost->reqarenapoints || cost->reqpersonalarenarating)
                continue;

            for (uint8 requirement = 0; requirement < MAX_ITEM_EXTENDED_COST_REQUIREMENTS; ++requirement)
            {
                uint32 const tokenId = cost->reqitem[requirement];
                if (cost->reqitemcount[requirement] != 1 || !IsTierToken(sObjectMgr->GetItemTemplate(tokenId)))
                    continue;

                auto& rewards = tierTokenRewards[tokenId];
                if (std::none_of(rewards.begin(), rewards.end(),
                                 [vendorItem](TierReward const& candidate)
                                 {
                                     return candidate.ItemId == vendorItem->item &&
                                            candidate.ExtendedCost == vendorItem->ExtendedCost;
                                 }))
                    rewards.push_back({vendorItem->item, vendorItem->ExtendedCost});
            }
        }
    }

    for (auto& [tokenId, rewards] : tierTokenRewards)
        std::sort(rewards.begin(), rewards.end(),
                  [](TierReward const& left, TierReward const& right)
                  {
                      return left.ItemId < right.ItemId ||
                             (left.ItemId == right.ItemId && left.ExtendedCost < right.ExtendedCost);
                  });

    LOG_INFO("playerbots", "Loaded tier-token rewards for {} tokens", tierTokenRewards.size());
}

bool CanBotUseTierToken(Player* bot, ItemTemplate const* token)
{
    // The token being rolled on is not in the bot's inventory yet.
    return bot && token && bot->CanUseItem(token) == EQUIP_ERR_OK && SelectTierReward(bot, token->ItemId, true);
}

void ScheduleTierTokenConversion(Player* bot, Item* token)
{
    if (!bot || !token || !sPlayerbotAIConfig.autoConvertTierTokens ||
        tierTokenRewards.find(token->GetEntry()) == tierTokenRewards.end())
        return;

    if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot))
    {
        ObjectGuid const botGuid = bot->GetGUID();
        ObjectGuid const tokenGuid = token->GetGUID();
        // Loot hooks run while the core still uses the awarded Item*. Defer its destruction until the next update.
        botAI->AddTimedEvent([botGuid, tokenGuid]() { AttemptTierTokenConversion(botGuid, tokenGuid); }, 1);
    }
}

void ScheduleTierTokenConversionFromPacket(Player* bot, WorldPacket const& packet)
{
    if (!sPlayerbotAIConfig.autoConvertTierTokens || packet.GetOpcode() != SMSG_ITEM_PUSH_RESULT)
        return;

    WorldPacket award(packet);
    award.rpos(0);
    ObjectGuid owner;
    uint32 received;
    uint32 created;
    uint32 sendChatMessage;
    uint8 bag;
    uint32 slot;
    uint32 tokenId;
    award >> owner >> received >> created >> sendChatMessage >> bag >> slot >> tokenId;
    if (owner != bot->GetGUID() || received || created || slot == uint32(-1))
        return;

    // Capture the GUID while the packet's slot still refers to the awarded item, before the AI queue runs.
    Item* token = bot->GetItemByPos(bag, uint8(slot));
    if (token && token->GetEntry() == tokenId)
        ScheduleTierTokenConversion(bot, token);
}
