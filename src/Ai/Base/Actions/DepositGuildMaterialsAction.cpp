/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "DepositGuildMaterialsAction.h"

#include "AiObjectContext.h"
#include "ItemUsageValue.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QuestDef.h"

bool DepositGuildMaterialsAction::Execute(Event /*event*/)
{
    if (!bot->IsAlive() || !bot->IsInWorld() || bot->IsBeingTeleported() || bot->IsInCombat() ||
        !CanDepositToFirstTab())
        return false;

    GameObject* bank = GetNearbyGuildBank();
    if (!bank)
        return false;

    CollectItemsVisitor visitor;
    IterateItems(&visitor, ITERATE_ITEMS_IN_BAGS);

    bool deposited = false;
    for (Item* item : visitor.items)
    {
        if (!IsDepositCandidate(item))
            continue;

        // Stop after the first failed transfer. This includes an unpurchased or full first tab,
        // and avoids sending an inventory error for every remaining stack.
        if (!MoveFromCharToBank(item, bank, false))
            break;

        deposited = true;
    }

    return deposited;
}

bool DepositGuildMaterialsAction::isPossible()
{
    // This intentionally does not require a nearby guild bank. RandomPlayerbotMgr uses it as a
    // location-independent preflight before selecting a city guild-bank visit.
    return CanDepositToFirstTab() && HasDepositCandidate();
}

bool DepositGuildMaterialsAction::HasDepositCandidate()
{
    CollectItemsVisitor visitor;
    IterateItems(&visitor, ITERATE_ITEMS_IN_BAGS);

    for (Item* item : visitor.items)
        if (IsDepositCandidate(item))
            return true;

    return false;
}

bool DepositGuildMaterialsAction::IsDepositCandidate(Item* item)
{
    if (!item || item->IsSoulBound() || !item->CanBeTraded())
        return false;

    ItemTemplate const* proto = item->GetTemplate();
    if (!proto || proto->Class != ITEM_CLASS_TRADE_GOODS || proto->Duration > 0 ||
        proto->Bonding == BIND_WHEN_PICKED_UP || proto->Bonding == BIND_QUEST_ITEM ||
        proto->Bonding == BIND_QUEST_ITEM1 || proto->HasFlag(ITEM_FLAG_HAS_QUEST_GLOW))
        return false;

    if (IsQuestItem(proto))
        return false;

    // ItemUsage already knows which reagents and reserve stacks the bot should retain for its
    // own professions, as well as active guild-task items. Only donate items the existing usage
    // rules consider disposable.
    ItemUsage const usage = context->GetValue<ItemUsage>("item usage", item->GetEntry())->Get();
    return usage == ITEM_USAGE_NONE || usage == ITEM_USAGE_AH || usage == ITEM_USAGE_VENDOR;
}

bool DepositGuildMaterialsAction::IsQuestItem(ItemTemplate const* proto)
{
    auto isRequiredForActiveQuest = [proto](Player* player)
    {
        if (!player)
            return false;

        for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
        {
            Quest const* quest = sObjectMgr->GetQuestTemplate(player->GetQuestSlotQuestId(slot));
            if (!quest)
                continue;

            for (uint8 itemSlot = 0; itemSlot < 4; ++itemSlot)
                if (quest->RequiredItemId[itemSlot] == proto->ItemId)
                    return true;
        }

        return false;
    };

    return isRequiredForActiveQuest(bot) || isRequiredForActiveQuest(GetMaster());
}
