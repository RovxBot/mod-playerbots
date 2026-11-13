/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "ProfessionActions.h"

#include <algorithm>

#include "AuctionHouseMgr.h"
#include "Bag.h"
#include "BudgetValues.h"
#include "Containers.h"
#include "Event.h"
#include "GameObject.h"
#include "GameTime.h"
#include "ItemUsageValue.h"
#include "LootMgr.h"
#include "LootObjectStack.h"
#include "Mail.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "QuestDef.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "WorldPacket.h"
#include "WorldSession.h"

namespace
{
constexpr uint32 MAX_PROFESSION_FAILURES = 3;
constexpr uint32 MAX_LOOT_REFINEMENTS = 4;
constexpr uint32 REFINEMENT_STACK_SIZE = 5;
constexpr uint32 SPELL_MILLING = 51005;
constexpr uint32 SPELL_PROSPECTING = 31252;
constexpr uint32 AUCTION_DURATION_MINUTES = 12 * HOUR / MINUTE;
constexpr uint32 SERVICE_RESULT_DELAY_MS = 3 * IN_MILLISECONDS;
constexpr uint32 MAX_AUCTION_MAIL = 20;
constexpr uint32 MAX_AUCTION_MAIL_DURATION = 5 * MINUTE * IN_MILLISECONDS;
constexpr float SERVICE_DISTANCE = 3.0f;

bool HasReagents(Player* bot, ProfessionRecipe const& recipe)
{
    for (auto const& [itemId, count] : recipe.Reagents)
        if (!bot->HasItemCount(itemId, count))
            return false;
    return true;
}

uint32 GetAuctionAmount(PlayerbotAI* botAI, Item const* item)
{
    if (!ProfessionMgr::CanAuction(item))
        return 0;

    Player* bot = botAI->GetBot();
    ItemTemplate const* proto = item->GetTemplate();
    uint32 const itemId = item->GetEntry();
    if (proto->Class == ITEM_CLASS_RECIPE && proto->RequiredSkill && bot->HasSkill(proto->RequiredSkill) &&
        !bot->HasSpell(proto->Spells[2].SpellId))
        return 0;

    ItemUsageValue usage(botAI);
    usage.Qualify(std::to_string(itemId) + "," + std::to_string(item->GetItemRandomPropertyId()));
    ItemUsage const itemUsage = usage.Calculate();
    if (itemUsage != ITEM_USAGE_NONE && itemUsage != ITEM_USAGE_VENDOR && itemUsage != ITEM_USAGE_AH)
        return 0;

    uint32 keepCount = 0;
    for (auto const& questPair : botAI->GetCurrentQuestsRequiringItemId(itemId))
    {
        Quest const* quest = questPair.first;
        if (!quest)
            continue;
        for (uint8 index = 0; index < QUEST_ITEM_OBJECTIVES_COUNT; ++index)
            if (quest->RequiredItemId[index] == itemId)
                keepCount += quest->RequiredItemCount[index];
    }

    if (proto->Class == ITEM_CLASS_TRADE_GOODS || proto->Class == ITEM_CLASS_MISC || proto->Class == ITEM_CLASS_REAGENT)
    {
        bool const lowBagSpace = botAI->GetAiObjectContext()->GetValue<uint8>("bag space")->Get() > 50;
        if (usage.IsItemNeededForUsefullSpell(proto, lowBagSpace))
            keepCount = std::max(keepCount, 2u * proto->GetMaxStackSize());
    }

    uint32 const carried = bot->GetItemCount(itemId);
    return carried > keepCount ? std::min(item->GetCount(), carried - keepCount) : 0;
}

bool CanCollectAuctionMail(Mail const* mail, time_t now)
{
    return mail && mail->messageType == MAIL_AUCTION && mail->state != MAIL_STATE_DELETED && !mail->COD &&
           mail->deliver_time <= now && mail->expire_time > now;
}
}  // namespace

bool ProfessionAction::isUseful()
{
    ProfessionWork const& work = botAI->rpgInfo.Profession;
    return botAI->rpgInfo.GetStatus() == RPG_DO_PROFESSIONS && work.Phase == _phase && !work.Waiting &&
           bot->IsAlive() && !bot->IsInCombat() && !bot->IsNonMeleeSpellCast(false);
}

bool ProfessionAction::isPossible()
{
    return ProfessionMgr::CanRun(botAI) && !bot->GetLootGUID() && !bot->IsBeingTeleported() && !bot->IsInFlight() &&
           !bot->HasUnitState(UNIT_STATE_LOST_CONTROL);
}

void ProfessionAction::ChangePhase(ProfessionPhase phase)
{
    ProfessionWork& work = botAI->rpgInfo.Profession;
    work.Phase = phase;
    work.Destination.reset();
    work.Attempts = 0;
    work.InputsChecked = false;
    work.AuctionItemChecked = false;
}

void ProfessionAction::WaitForResult()
{
    ProfessionWork& work = botAI->rpgInfo.Profession;
    work.Waiting = true;
    ObjectGuid const botGuid = bot->GetGUID();
    uint64 const generation = work.Generation;
    botAI->AddTimedEvent(
        [botGuid, generation]()
        {
            Player* bot = ObjectAccessor::FindPlayer(botGuid);
            PlayerbotAI* ai = bot ? GET_PLAYERBOT_AI(bot) : nullptr;
            if (ai && ai->rpgInfo.Profession.Generation == generation)
                ai->rpgInfo.Profession.Waiting = false;
        },
        SERVICE_RESULT_DELAY_MS);
}

bool ProfessionAction::Fail()
{
    ProfessionWork& work = botAI->rpgInfo.Profession;
    if (++work.Attempts >= MAX_PROFESSION_FAILURES)
        ProfessionMgr::Finish(botAI);
    else
        botAI->SetNextCheckDelay(sPlayerbotAIConfig.RepeatDelay);
    return true;
}

bool ProfessionAction::Travel(bool& arrived)
{
    arrived = false;
    ProfessionWork& work = botAI->rpgInfo.Profession;
    if (!work.Destination || work.Destination->Position.GetMapId() != bot->GetMapId())
        return Fail();
    if (bot->GetExactDist(work.Destination->Position) > SERVICE_DISTANCE)
        return MoveFarTo(work.Destination->Position);
    if (bot->isMoving())
    {
        bot->StopMoving();
        return true;
    }
    arrived = true;
    return true;
}

Creature* ProfessionAction::GetDestinationCreature() const
{
    ProfessionWork const& work = botAI->rpgInfo.Profession;
    if (!work.Destination)
        return nullptr;
    auto const bounds = bot->GetMap()->GetCreatureBySpawnIdStore().equal_range(work.Destination->SpawnId);
    for (auto itr = bounds.first; itr != bounds.second; ++itr)
        if (itr->second && itr->second->IsAlive() && itr->second->GetEntry() == work.Destination->Entry)
            return itr->second;
    return nullptr;
}

bool ProfessionAction::PrepareSpell(ProfessionRecipe const& recipe, bool& ready)
{
    ready = false;
    if (!bot->HasSpell(recipe.SpellId) || bot->GetPureSkillValue(recipe.SkillId) < recipe.RequiredSkill)
        return Fail();
    if (recipe.Focus)
    {
        ProfessionWork& work = botAI->rpgInfo.Profession;
        if (!work.Destination)
            work.Destination = ProfessionMgr::Instance().FindFocus(bot, recipe.Focus);
        bool arrived;
        bool const result = Travel(arrived);
        if (!arrived)
            return result;
    }
    if (bot->isMoving())
    {
        bot->StopMoving();
        return true;
    }
    SpellInfo const* spell = sSpellMgr->GetSpellInfo(recipe.SpellId);
    if (!spell)
        return Fail();
    if (spell->CheckShapeshift(bot->GetShapeshiftForm()) != SPELL_CAST_OK)
    {
        bot->RemoveAurasByType(SPELL_AURA_MOD_SHAPESHIFT);
        return true;
    }
    if (bot->IsMounted())
    {
        bot->RemoveAurasByType(SPELL_AURA_MOUNTED);
        return true;
    }
    ItemPosCountVec destination;
    if (bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, destination, recipe.ItemId, recipe.OutputCount) != EQUIP_ERR_OK)
        return Fail();
    ready = true;
    return true;
}

bool ProfessionAction::CastStep(ProfessionStep& step, bool product)
{
    ProfessionWork& work = botAI->rpgInfo.Profession;
    if (work.PendingOutput)
    {
        uint32 const count = bot->GetItemCount(work.PendingOutput);
        uint32 const made = count > work.BeforeCount ? count - work.BeforeCount : 0;
        work.PendingOutput = 0;
        if (!made)
            return Fail();
        if (product)
            work.Products[step.ItemId] += made;
        --step.Count;
        work.Attempts = 0;
        work.InputsChecked = false;
        return true;
    }
    ProfessionRecipe const* recipe = ProfessionMgr::Instance().GetRecipe(step.SpellId);
    if (!recipe)
        return Fail();
    if (!work.InputsChecked && !HasReagents(bot, *recipe))
    {
        if (!work.Supplies.empty())
        {
            ChangePhase(ProfessionPhase::Supplies);
            return true;
        }
        return Fail();
    }
    work.InputsChecked = true;
    bool ready;
    bool const result = PrepareSpell(*recipe, ready);
    if (!ready)
        return result;
    uint32 const before = bot->GetItemCount(step.ItemId);
    if (!botAI->CastSpell(step.SpellId, bot))
        return Fail();
    work.PendingOutput = step.ItemId;
    work.BeforeCount = before;
    return true;
}

bool ProfessionMailAction::FinishMail()
{
    ProfessionWork& work = botAI->rpgInfo.Profession;
    work.AuctionMail.clear();
    work.MailRequest = ProfessionMailRequest::None;
    ChangePhase(ProfessionPhase::Plan);
    return true;
}

bool ProfessionMailAction::Execute(Event /*event*/)
{
    ProfessionWork& work = botAI->rpgInfo.Profession;
    if (botAI->rpgInfo.HasStatusPersisted(MAX_AUCTION_MAIL_DURATION))
        return FinishMail();
    time_t const now = GameTime::GetGameTime().count();
    if (!work.MailPrepared)
    {
        for (Mail const* mail : bot->GetMails())
        {
            if (CanCollectAuctionMail(mail, now))
                work.AuctionMail.push_back(mail->messageID);
            if (work.AuctionMail.size() >= MAX_AUCTION_MAIL)
                break;
        }
        work.MailPrepared = true;
    }
    if (work.AuctionMail.empty())
        return FinishMail();

    auto nextMail = [&]()
    {
        work.AuctionMail.pop_back();
        work.MailRequest = ProfessionMailRequest::None;
        work.Attempts = 0;
        return true;
    };
    Mail const* mail = bot->GetMail(work.AuctionMail.back());
    if (!CanCollectAuctionMail(mail, now))
        return nextMail();

    if (work.MailRequest != ProfessionMailRequest::None)
    {
        bool const completed =
            work.MailRequest == ProfessionMailRequest::Money
                ? !mail->money
                : work.MailRequest == ProfessionMailRequest::Item &&
                      std::none_of(mail->items.begin(), mail->items.end(),
                                   [&](MailItemInfo const& item) { return item.item_guid == work.MailItem; });
        if (completed)
            work.Attempts = 0;
        else if (++work.Attempts >= MAX_PROFESSION_FAILURES)
            return nextMail();
        work.MailRequest = ProfessionMailRequest::None;
        return true;
    }

    if (!work.Destination)
        work.Destination = ProfessionMgr::Instance().FindMailbox(bot);
    if (!work.Destination || work.Destination->Position.GetMapId() != bot->GetMapId())
        return FinishMail();
    bool arrived;
    bool const result = Travel(arrived);
    if (!arrived)
        return result;

    GameObject* mailbox = nullptr;
    auto const bounds = bot->GetMap()->GetGameObjectBySpawnIdStore().equal_range(work.Destination->SpawnId);
    for (auto itr = bounds.first; itr != bounds.second; ++itr)
        if (itr->second && itr->second->GetEntry() == work.Destination->Entry)
            if (GameObject* object =
                    bot->GetGameObjectIfCanInteractWith(itr->second->GetGUID(), GAMEOBJECT_TYPE_MAILBOX))
            {
                mailbox = object;
                break;
            }
    if (!mailbox)
    {
        if (++work.Attempts >= MAX_PROFESSION_FAILURES)
            return FinishMail();
        botAI->SetNextCheckDelay(sPlayerbotAIConfig.RepeatDelay);
        return true;
    }

    WorldPacket* packet = nullptr;
    if (mail->money)
    {
        if (uint64(bot->GetMoney()) + mail->money > MAX_MONEY_AMOUNT)
            return nextMail();
        work.MailRequest = ProfessionMailRequest::Money;
        packet = new WorldPacket(CMSG_MAIL_TAKE_MONEY);
        *packet << mailbox->GetGUID() << mail->messageID;
    }
    else if (!mail->items.empty())
    {
        work.MailItem = mail->items.front().item_guid;
        Item* item = bot->GetMItem(work.MailItem);
        ItemPosCountVec destination;
        if (!item || bot->CanStoreItem(NULL_BAG, NULL_SLOT, destination, item, false) != EQUIP_ERR_OK)
            return nextMail();
        work.MailRequest = ProfessionMailRequest::Item;
        packet = new WorldPacket(CMSG_MAIL_TAKE_ITEM);
        *packet << mailbox->GetGUID() << mail->messageID << work.MailItem;
    }
    else
    {
        work.MailRequest = ProfessionMailRequest::Delete;
        packet = new WorldPacket(CMSG_MAIL_DELETE);
        *packet << mailbox->GetGUID() << mail->messageID << uint32(0);
    }
    // Mail opcodes are PROCESS_THREADUNSAFE; normal session dispatch owns the writes.
    bot->GetSession()->QueuePacket(packet);
    WaitForResult();
    return true;
}

bool ProfessionPlanAction::Execute(Event /*event*/)
{
    if (bot->isMoving())
    {
        bot->StopMoving();
        return true;
    }
    ProfessionWork& work = botAI->rpgInfo.Profession;
    if (ProfessionMgr::Instance().BuildPlan(botAI))
        return true;

    if (work.Conversions < MAX_LOOT_REFINEMENTS)
    {
        auto tryItem = [&](Item* item)
        {
            if (!item || item->GetCount() < REFINEMENT_STACK_SIZE)
                return false;
            ItemTemplate const* proto = item->GetTemplate();
            uint32 spellId = 0;
            if (proto->HasFlag(ITEM_FLAG_IS_MILLABLE) && bot->HasSpell(SPELL_MILLING) &&
                LootTemplates_Milling.HaveLootFor(item->GetEntry()))
                spellId = SPELL_MILLING;
            else if (proto->HasFlag(ITEM_FLAG_IS_PROSPECTABLE) && bot->HasSpell(SPELL_PROSPECTING) &&
                     LootTemplates_Prospecting.HaveLootFor(item->GetEntry()))
                spellId = SPELL_PROSPECTING;
            uint32 const skillId = spellId == SPELL_MILLING ? SKILL_INSCRIPTION : SKILL_JEWELCRAFTING;
            if (!spellId || bot->GetSkillValue(skillId) < proto->RequiredSkillRank || bot->HasSpellCooldown(spellId))
                return false;
            if (!botAI->GetCurrentQuestsRequiringItemId(item->GetEntry()).empty())
                return false;
            work.PendingItem = item->GetGUID();
            work.PendingInput = item->GetEntry();
            work.BeforeCount = 0;
            work.Craft = {spellId, 0, 1};
            work.Reserved[item->GetEntry()] = REFINEMENT_STACK_SIZE;
            ChangePhase(ProfessionPhase::Refine);
            return true;
        };
        for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
            if (tryItem(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot)))
                return true;
        for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
            if (Bag* bag = bot->GetBagByPos(bagSlot))
                for (uint32 slot = 0; slot < bag->GetBagSize(); ++slot)
                    if (tryItem(bag->GetItemByPos(slot)))
                        return true;
    }
    ChangePhase(ProfessionPhase::Auction);
    return true;
}

bool ProfessionRefineAction::Execute(Event /*event*/)
{
    ProfessionWork& work = botAI->rpgInfo.Profession;
    if (work.PendingInput)
    {
        if (work.BeforeCount)
        {
            if (bot->GetItemCount(work.PendingInput) >= work.BeforeCount)
                return Fail();
            work.PendingInput = 0;
            work.PendingItem.Clear();
            work.Reserved.clear();
            ++work.Conversions;
            ChangePhase(ProfessionPhase::Plan);
            return true;
        }
        Item* item = bot->GetItemByGuid(work.PendingItem);
        if (!item || bot->isMoving())
        {
            bot->StopMoving();
            return item ? true : Fail();
        }
        uint32 const before = bot->GetItemCount(work.PendingInput);
        SpellInfo const* spell = sSpellMgr->GetSpellInfo(work.Craft.SpellId);
        if (!spell)
            return Fail();
        if (bot->IsMounted())
        {
            bot->RemoveAurasByType(SPELL_AURA_MOUNTED);
            return true;
        }
        if (spell->CheckShapeshift(bot->GetShapeshiftForm()) != SPELL_CAST_OK)
        {
            bot->RemoveAurasByType(SPELL_AURA_MOD_SHAPESHIFT);
            return true;
        }
        LootObject loot;
        loot.guid = work.PendingItem;
        context->GetValue<LootObject>("loot target")->Set(loot);
        if (!botAI->CastSpell(work.Craft.SpellId, bot, item))
            return Fail();
        work.BeforeCount = before;
        return true;
    }
    if (!work.Refinements.empty())
    {
        if (work.Refinements.front().Count)
            return CastStep(work.Refinements.front(), false);
        work.Refinements.erase(work.Refinements.begin());
        work.Destination.reset();
        return true;
    }
    ChangePhase(work.Supplies.empty() ? ProfessionPhase::Craft : ProfessionPhase::Supplies);
    return true;
}

bool ProfessionSupplyAction::Execute(Event /*event*/)
{
    ProfessionWork& work = botAI->rpgInfo.Profession;
    if (work.Supplies.empty())
    {
        ChangePhase(work.Refinements.empty() ? ProfessionPhase::Craft : ProfessionPhase::Refine);
        return true;
    }
    auto supply = work.Supplies.begin();
    uint32 const itemId = supply->first;
    if (!work.Destination)
        work.Destination = ProfessionMgr::Instance().FindVendor(bot, itemId);
    bool arrived;
    bool const result = Travel(arrived);
    if (!arrived)
        return result;
    Creature* vendor = GetDestinationCreature();
    if (!vendor || !bot->GetNPCIfCanInteractWith(vendor->GetGUID(), UNIT_NPC_FLAG_VENDOR))
        return Fail();
    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
    VendorItemData const* items = vendor->GetVendorItems();
    if (!proto || !items || proto->BuyPrice <= 0)
        return Fail();
    for (uint32 slot = 0; slot < items->GetItemCount(); ++slot)
    {
        VendorItem const* item = items->GetItem(slot);
        if (!item || item->item != itemId || item->maxcount || item->ExtendedCost)
            continue;
        FreeMoneyForValue availableBudget(botAI);
        availableBudget.Qualify(std::to_string(static_cast<uint32>(NeedMoneyFor::tradeskill)));
        uint32 const availableMoney = availableBudget.Calculate();
        uint32 const price = uint32(proto->BuyPrice * bot->GetReputationPriceDiscount(vendor));
        if (price > availableMoney || price > bot->GetMoney())
            return Fail();
        uint32 const before = bot->GetItemCount(itemId);
        // The core's return value reports limited stock, so inventory confirms a successful purchase.
        bot->BuyItemFromVendorSlot(vendor->GetGUID(), slot, itemId, 1, NULL_BAG, NULL_SLOT);
        uint32 const after = bot->GetItemCount(itemId);
        if (after <= before)
            return Fail();
        supply->second -= std::min(supply->second, after - before);
        work.Attempts = 0;
        if (!supply->second)
        {
            work.Supplies.erase(supply);
            work.Destination.reset();
        }
        return true;
    }
    return Fail();
}

bool ProfessionCraftAction::Execute(Event /*event*/)
{
    ProfessionWork& work = botAI->rpgInfo.Profession;
    if (work.Craft.Count)
        return CastStep(work.Craft, true);
    work.Reserved.clear();
    ChangePhase(ProfessionPhase::Auction);
    return true;
}

bool ProfessionAuctionAction::Execute(Event /*event*/)
{
    ProfessionWork& work = botAI->rpgInfo.Profession;
    if (work.PendingItem)
    {
        ObjectGuid const itemGuid = work.PendingItem;
        Item* item = bot->GetItemByGuid(work.PendingItem);
        uint32 const remaining = item ? item->GetCount() : 0;
        work.PendingItem.Clear();
        if (remaining >= work.BeforeCount)
            return Fail();
        work.AuctionItems.pop_back();
        work.AuctionSales.erase(itemGuid);
        work.Attempts = 0;
        work.AuctionItemChecked = false;
        return true;
    }
    if (ProfessionMgr::Instance().GetActiveAuctionCount(bot->GetGUID()) >= sPlayerbotAIConfig.ProfessionMaxAuctions)
    {
        ProfessionMgr::Finish(botAI);
        return true;
    }
    if (!work.AuctionsPrepared)
    {
        work.Products.clear();
        work.Reserved.clear();
        for (Item* item : botAI->GetInventoryItems())
            if (ProfessionMgr::CanAuction(item))
                work.AuctionItems.push_back(item->GetGUID());
        Acore::Containers::RandomShuffle(work.AuctionItems);
        work.AuctionsPrepared = true;
    }
    if (work.AuctionItems.empty())
    {
        ProfessionMgr::Finish(botAI);
        return true;
    }

    auto skipItem = [&]()
    {
        ProfessionMgr::AuctionAttempted(botAI, work.AuctionItems.back());
        work.AuctionItems.pop_back();
        work.AuctionItemChecked = false;
        return true;
    };
    Item* item = bot->GetItemByGuid(work.AuctionItems.back());
    if (!work.AuctionItemChecked)
    {
        if (!GetAuctionAmount(botAI, item))
            return skipItem();
        work.AuctionItemChecked = true;
    }
    if (!work.Destination)
        work.Destination = ProfessionMgr::Instance().FindAuctioneer(bot);
    bool arrived;
    bool const result = Travel(arrived);
    if (!arrived)
        return result;
    uint32 const amount = GetAuctionAmount(botAI, item);
    if (!amount)
        return skipItem();
    Creature* auctioneer = GetDestinationCreature();
    if (!auctioneer || !bot->GetNPCIfCanInteractWith(auctioneer->GetGUID(), UNIT_NPC_FLAG_AUCTIONEER))
        return Fail();
    AuctionHouseEntry const* house = AuctionHouseMgr::GetAuctionHouseEntryFromFactionTemplate(auctioneer->GetFaction());
    if (!house)
        return Fail();
    FreeMoneyForValue availableBudget(botAI);
    availableBudget.Qualify(std::to_string(static_cast<uint32>(NeedMoneyFor::tradeskill)));
    if (AuctionHouseMgr::GetAuctionDeposit(house, AUCTION_DURATION_MINUTES * MINUTE, item, amount) >
        std::min(availableBudget.Calculate(), bot->GetMoney()))
        return skipItem();
    uint64 const value = uint64(std::max(1u, item->GetTemplate()->SellPrice)) * amount;
    uint32 const buyout = uint32(std::clamp<double>(
        static_cast<double>(value) * sPlayerbotAIConfig.ProfessionAuctionPriceMultiplier, 1, MAX_MONEY_AMOUNT));
    WorldPacket* packet = new WorldPacket(CMSG_AUCTION_SELL_ITEM);
    *packet << auctioneer->GetGUID() << uint32(1) << item->GetGUID() << amount;
    *packet << std::max(1u, uint32(uint64(buyout) * 4 / 5)) << buyout << AUCTION_DURATION_MINUTES;
    work.PendingItem = item->GetGUID();
    ProfessionMgr::AuctionAttempted(botAI, work.PendingItem);
    work.BeforeCount = item->GetCount();
    // This opcode is PROCESS_THREADUNSAFE: normal session dispatch runs it on the world thread.
    bot->GetSession()->QueuePacket(packet);
    WaitForResult();
    return true;
}
