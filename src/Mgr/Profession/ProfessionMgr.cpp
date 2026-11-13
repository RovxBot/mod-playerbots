/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "ProfessionMgr.h"

#include <algorithm>
#include <atomic>
#include <functional>
#include <unordered_set>
#include <utility>

#include "AuctionHouseMgr.h"
#include "Bag.h"
#include "BudgetValues.h"
#include "DBCStores.h"
#include "ItemUsageValue.h"
#include "Log.h"
#include "LootObjectStack.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "QuestDef.h"
#include "Random.h"
#include "RandomPlayerbotMgr.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Timer.h"

namespace
{
constexpr uint32 MAX_RECIPE_CANDIDATES = 64;
constexpr uint32 MAX_REFINEMENT_STEPS = 16;
constexpr uint32 MAX_REFINEMENT_DEPTH = 3;
constexpr uint32 MAX_PRODUCER_CHOICES = 4;
constexpr uint32 MAX_RECIPE_EXPANSIONS = 128;
constexpr uint32 MAX_DESTINATIONS = 256;
constexpr float MAX_TRAVEL_DISTANCE = 15000.0f;
constexpr uint32 MAX_AUCTION_DEFERRAL = HOUR * IN_MILLISECONDS;
constexpr uint32 MIN_AUCTION_BALANCE = SILVER;
std::atomic<uint64> nextProfessionGeneration{1};

struct PlanningInventory
{
    std::unordered_map<uint32, uint32> Available;
    std::map<uint32, uint32> Supplies;
    std::map<uint32, uint32> Reserved;
    std::vector<ProfessionStep> Refinements;
    std::unordered_set<uint32> Path;
    uint64 PurchaseCost = 0;
    uint32 RefinementCasts = 0;
    bool UsesCollectedMaterial = false;
};
}  // namespace

void ProfessionMgr::Load()
{
    _recipes.clear();
    _recipesByOutput.clear();
    _vendorsByItem.clear();
    _creaturesByEntry.clear();
    _auctioneersByMap.clear();
    _mailboxesByMap.clear();
    _focusesByType.clear();

    for (SkillLineAbilityEntry const* ability : sSkillLineAbilityStore)
    {
        SkillLineEntry const* skill = sSkillLineStore.LookupEntry(ability->SkillLine);
        if (!skill || (skill->categoryId != SKILL_CATEGORY_PROFESSION && skill->categoryId != SKILL_CATEGORY_SECONDARY))
            continue;

        SpellInfo const* spell = sSpellMgr->GetSpellInfo(ability->Spell);
        if (!spell || spell->Effects[EFFECT_0].Effect != SPELL_EFFECT_CREATE_ITEM ||
            !sObjectMgr->GetItemTemplate(spell->Effects[EFFECT_0].ItemType) || _recipes.contains(spell->Id))
            continue;

        ProfessionRecipe recipe;
        recipe.SpellId = spell->Id;
        recipe.SkillId = ability->SkillLine;
        recipe.ItemId = spell->Effects[EFFECT_0].ItemType;
        recipe.OutputCount = std::max(1, spell->Effects[EFFECT_0].CalcValue());
        recipe.RequiredSkill = ability->MinSkillLineRank;
        recipe.Focus = spell->RequiresSpellFocus;
        for (uint8 index = 0; index < MAX_SPELL_REAGENTS; ++index)
            if (spell->Reagent[index] > 0 && spell->ReagentCount[index] > 0)
                recipe.Reagents.emplace_back(spell->Reagent[index], spell->ReagentCount[index]);

        if (recipe.Reagents.empty())
            continue;

        _recipesByOutput[recipe.ItemId].push_back(recipe.SpellId);
        _recipes.emplace(recipe.SpellId, std::move(recipe));
    }

    std::unordered_set<uint32> vendorEntries;
    for (auto const& [spawnId, data] : sObjectMgr->GetAllCreatureData())
    {
        MapEntry const* map = sMapStore.LookupEntry(data.mapid);
        CreatureTemplate const* creature = sObjectMgr->GetCreatureTemplate(data.id);
        if (!map || !map->IsContinent() || !creature)
            continue;

        uint32 const flags = data.npcflag ? data.npcflag : creature->npcflag;
        if (!(flags & (UNIT_NPC_FLAG_VENDOR | UNIT_NPC_FLAG_AUCTIONEER)))
            continue;

        ProfessionDestination destination{WorldPosition(data.mapid, data.posX, data.posY, data.posZ), spawnId, data.id,
                                          creature->faction};
        if (flags & UNIT_NPC_FLAG_AUCTIONEER)
            _auctioneersByMap[data.mapid].push_back(destination);

        if (!(flags & UNIT_NPC_FLAG_VENDOR))
            continue;

        _creaturesByEntry[data.id].push_back(destination);
        if (!vendorEntries.insert(data.id).second)
            continue;

        VendorItemData const* items = sObjectMgr->GetNpcVendorItemList(data.id);
        if (!items)
            continue;

        for (uint32 slot = 0; slot < items->GetItemCount(); ++slot)
        {
            VendorItem const* item = items->GetItem(slot);
            if (item && !item->maxcount && !item->ExtendedCost)
                _vendorsByItem[item->item].push_back(data.id);
        }
    }

    for (auto const& [spawnId, data] : sObjectMgr->GetAllGOData())
    {
        MapEntry const* map = sMapStore.LookupEntry(data.mapid);
        GameObjectTemplate const* object = sObjectMgr->GetGameObjectTemplate(data.id);
        if (!map || !map->IsContinent() || !object)
            continue;

        if (object->type == GAMEOBJECT_TYPE_MAILBOX)
            _mailboxesByMap[data.mapid].push_back(
                {WorldPosition(data.mapid, data.posX, data.posY, data.posZ), spawnId, data.id, object->faction});
        else if (object->type == GAMEOBJECT_TYPE_SPELL_FOCUS)
            _focusesByType[object->spellFocus.focusId].push_back(
                {WorldPosition(data.mapid, data.posX, data.posY, data.posZ), spawnId, data.id, 0});
    }

    LOG_INFO("playerbots", "Loaded {} profession recipes and {} vendor supply entries", _recipes.size(),
             _vendorsByItem.size());
}

ProfessionRecipe const* ProfessionMgr::GetRecipe(uint32 spellId) const
{
    auto const itr = _recipes.find(spellId);
    return itr == _recipes.end() ? nullptr : &itr->second;
}

void ProfessionMgr::InitializeAuctionCounts()
{
    std::lock_guard<std::mutex> lock(_auctionCountsMutex);
    _auctionCounts.clear();
    std::unordered_set<AuctionHouseObject*> visited;
    for (AuctionHouseId houseId : {AuctionHouseId::Alliance, AuctionHouseId::Horde, AuctionHouseId::Neutral})
    {
        AuctionHouseObject* house = sAuctionMgr->GetAuctionsMapByHouseId(houseId);
        if (!house || !visited.insert(house).second)
            continue;
        for (auto const& [auctionId, auction] : house->GetAuctions())
            if (auction && auction->owner)
                ++_auctionCounts[auction->owner];
    }
}

void ProfessionMgr::AuctionAdded(ObjectGuid owner)
{
    std::lock_guard<std::mutex> lock(_auctionCountsMutex);
    ++_auctionCounts[owner];
}

void ProfessionMgr::AuctionRemoved(ObjectGuid owner)
{
    std::lock_guard<std::mutex> lock(_auctionCountsMutex);
    auto const itr = _auctionCounts.find(owner);
    if (itr != _auctionCounts.end() && --itr->second == 0)
        _auctionCounts.erase(itr);
}

uint32 ProfessionMgr::GetActiveAuctionCount(ObjectGuid owner) const
{
    std::lock_guard<std::mutex> lock(_auctionCountsMutex);
    auto const itr = _auctionCounts.find(owner);
    return itr == _auctionCounts.end() ? 0 : itr->second;
}

bool ProfessionMgr::CanRun(PlayerbotAI* botAI)
{
    Player* bot = botAI->GetBot();
    Player* master = botAI->GetMaster();
    return sPlayerbotAIConfig.EnableProfessionLoop && sRandomPlayerbotMgr.IsRandomBot(bot) &&
           (!master || !IsRealPlayer(master)) && !bot->GetGroup() && bot->IsAlive() && bot->IsInWorld() &&
           !bot->IsInCombat() && !bot->InBattleground() && !bot->GetTradeData() &&
           bot->GetMap()->GetEntry()->IsContinent();
}

bool ProfessionMgr::CanStart(PlayerbotAI* botAI) const
{
    return botAI->rpgInfo.Profession.Ready && CanRun(botAI) && !botAI->GetBot()->GetLootGUID() &&
           !botAI->GetBot()->IsNonMeleeSpellCast(false) && !botAI->GetBot()->IsFlying();
}

void ProfessionMgr::Start(PlayerbotAI* botAI) const
{
    ProfessionWork& work = botAI->rpgInfo.Profession;
    uint64 const generation = nextProfessionGeneration.fetch_add(1, std::memory_order_relaxed);
    uint32 const cursor = work.RecipeCursor;
    auto sales = std::move(work.AuctionSales);
    work = ProfessionWork{};
    work.AuctionSales = std::move(sales);
    if (!work.AuctionSales.empty())
    {
        std::unordered_set<ObjectGuid> carried;
        for (Item* item : botAI->GetInventoryItems())
            carried.insert(item->GetGUID());
        std::erase_if(work.AuctionSales, [&](auto const& sale) { return !carried.contains(sale.first); });
    }
    work.Generation = generation;
    work.RecipeCursor = cursor;
    work.Ready = false;
    work.Phase = ProfessionPhase::Mail;
    botAI->GetAiObjectContext()->GetValue<LootObject>("loot target")->Set(LootObject());
    ObjectGuid const botGuid = botAI->GetBot()->GetGUID();
    uint32 const delay = sPlayerbotAIConfig.ProfessionLoopInterval * IN_MILLISECONDS;
    botAI->AddTimedEvent(
        [botGuid, generation]()
        {
            Player* bot = ObjectAccessor::FindPlayer(botGuid);
            PlayerbotAI* ai = bot ? GET_PLAYERBOT_AI(bot) : nullptr;
            if (ai && ai->rpgInfo.Profession.Generation == generation)
                ai->rpgInfo.Profession.Ready = true;
        },
        urand(delay, delay + delay / 4));
}

void ProfessionMgr::Finish(PlayerbotAI* botAI)
{
    ProfessionWork& work = botAI->rpgInfo.Profession;
    work.Phase = ProfessionPhase::Done;
    work.Reserved.clear();
    work.Supplies.clear();
    work.Destination.reset();
    work.Waiting = false;
    LootObject const loot = botAI->GetAiObjectContext()->GetValue<LootObject>("loot target")->Get();
    if (loot.guid == work.PendingItem)
        botAI->GetAiObjectContext()->GetValue<LootObject>("loot target")->Set(LootObject());
    botAI->rpgInfo.ChangeToIdle();
}

bool ProfessionMgr::IsReserved(PlayerbotAI* botAI, uint32 itemId)
{
    ProfessionWork const& work = botAI->rpgInfo.Profession;
    return work.Phase != ProfessionPhase::Done && (work.Reserved.contains(itemId) || work.Products.contains(itemId));
}

bool ProfessionMgr::CanAuction(Item const* item)
{
    if (!item || item->IsEquipped() || item->IsInTrade() || !item->CanBeTraded() || item->IsNotEmptyBag() ||
        item->GetUInt32Value(ITEM_FIELD_DURATION))
        return false;

    ItemTemplate const* proto = item->GetTemplate();
    return !proto->HasFlag(ITEM_FLAG_CONJURED) && !proto->HasFlag(ITEM_FLAG_IS_BOUND_TO_ACCOUNT);
}

bool ProfessionMgr::ShouldDeferSale(PlayerbotAI* botAI, Item const* item)
{
    if (!sPlayerbotAIConfig.EnableNewRpgStrategy || !CanRun(botAI) || !CanAuction(item) ||
        botAI->GetBot()->GetMoney() < MIN_AUCTION_BALANCE)
        return false;

    auto const sale =
        botAI->rpgInfo.Profession.AuctionSales.try_emplace(item->GetGUID(), AuctionSaleOpportunity{getMSTime(), false})
            .first;
    return !sale->second.Attempted && GetMSTimeDiffToNow(sale->second.FirstSeen) < MAX_AUCTION_DEFERRAL;
}

void ProfessionMgr::AuctionAttempted(PlayerbotAI* botAI, ObjectGuid item)
{
    botAI->rpgInfo.Profession.AuctionSales[item].Attempted = true;
}

std::optional<ProfessionDestination> ProfessionMgr::FindNearest(
    Player* bot, std::vector<ProfessionDestination> const& candidates) const
{
    std::optional<ProfessionDestination> result;
    float bestDistance = MAX_TRAVEL_DISTANCE;
    uint32 examined = 0;
    for (ProfessionDestination const& candidate : candidates)
    {
        if (candidate.Position.GetMapId() != bot->GetMapId())
            continue;

        if (++examined > MAX_DESTINATIONS)
            break;

        if (candidate.Faction)
        {
            FactionTemplateEntry const* faction = sFactionTemplateStore.LookupEntry(candidate.Faction);
            FactionTemplateEntry const* botFaction = bot->GetFactionTemplateEntry();
            if (!faction || !botFaction || faction->IsHostileTo(*botFaction) || botFaction->IsHostileTo(*faction))
                continue;
        }

        float const distance = bot->GetExactDist(candidate.Position);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            result = candidate;
        }
    }
    return result;
}

std::optional<ProfessionDestination> ProfessionMgr::FindVendor(Player* bot, uint32 itemId) const
{
    auto const vendors = _vendorsByItem.find(itemId);
    if (vendors == _vendorsByItem.end())
        return std::nullopt;

    std::vector<ProfessionDestination> candidates;
    for (uint32 entry : vendors->second)
    {
        auto const spawns = _creaturesByEntry.find(entry);
        if (spawns == _creaturesByEntry.end())
            continue;
        for (ProfessionDestination const& spawn : spawns->second)
        {
            if (spawn.Position.GetMapId() == bot->GetMapId())
                candidates.push_back(spawn);
            if (candidates.size() >= MAX_DESTINATIONS)
                return FindNearest(bot, candidates);
        }
    }
    return FindNearest(bot, candidates);
}

std::optional<ProfessionDestination> ProfessionMgr::FindAuctioneer(Player* bot) const
{
    auto const itr = _auctioneersByMap.find(bot->GetMapId());
    return itr == _auctioneersByMap.end() ? std::nullopt : FindNearest(bot, itr->second);
}

std::optional<ProfessionDestination> ProfessionMgr::FindMailbox(Player* bot) const
{
    auto const itr = _mailboxesByMap.find(bot->GetMapId());
    return itr == _mailboxesByMap.end() ? std::nullopt : FindNearest(bot, itr->second);
}

std::optional<ProfessionDestination> ProfessionMgr::FindFocus(Player* bot, uint32 focus) const
{
    auto const itr = _focusesByType.find(focus);
    return itr == _focusesByType.end() ? std::nullopt : FindNearest(bot, itr->second);
}

bool ProfessionMgr::BuildPlan(PlayerbotAI* botAI) const
{
    Player* bot = botAI->GetBot();
    ProfessionWork& work = botAI->rpgInfo.Profession;
    PlanningInventory inventory;
    std::unordered_set<uint32> collectedItems;
    auto collect = [&](Item* item)
    {
        ItemTemplate const* proto = item->GetTemplate();
        if (ItemUsageValue::IsProfessionTool(bot, proto))
            return;
        inventory.Available[item->GetEntry()] += item->GetCount();
        if (!_vendorsByItem.contains(item->GetEntry()))
            collectedItems.insert(item->GetEntry());
    };
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            collect(item);
    for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
        if (Bag* bag = bot->GetBagByPos(bagSlot))
            for (uint32 slot = 0; slot < bag->GetBagSize(); ++slot)
                if (Item* item = bag->GetItemByPos(slot))
                    collect(item);

    std::map<uint32, uint32> questItems;
    for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        Quest const* quest = sObjectMgr->GetQuestTemplate(bot->GetQuestSlotQuestId(slot));
        if (!quest)
            continue;
        for (uint8 index = 0; index < QUEST_ITEM_OBJECTIVES_COUNT; ++index)
            if (quest->RequiredItemId[index])
                questItems[quest->RequiredItemId[index]] += quest->RequiredItemCount[index];
    }
    for (auto const& [itemId, count] : questItems)
        inventory.Available[itemId] -= std::min(inventory.Available[itemId], count);

    std::vector<ProfessionRecipe const*> knownRecipes;
    for (auto const& [spellId, spell] : bot->GetSpellMap())
        if (spell->State != PLAYERSPELL_REMOVED && spell->Active)
            if (ProfessionRecipe const* recipe = GetRecipe(spellId))
                knownRecipes.push_back(recipe);
    if (knownRecipes.empty())
        return false;

    uint32 const budget =
        botAI->GetAiObjectContext()
            ->GetValue<uint32>("free money for", std::to_string(static_cast<uint32>(NeedMoneyFor::tradeskill)))
            ->Get();
    uint32 bestScore = 0;
    PlanningInventory bestPlan;
    ProfessionStep bestCraft;
    uint32 const candidateCount = std::min<uint32>(MAX_RECIPE_CANDIDATES, knownRecipes.size());
    std::unordered_map<uint32, bool> reachableFocuses;
    std::unordered_map<uint32, bool> reachableVendors;
    auto hasFocus = [&](uint32 focus)
    {
        if (!focus)
            return true;
        auto const [itr, inserted] = reachableFocuses.try_emplace(focus, false);
        if (inserted)
            itr->second = FindFocus(bot, focus).has_value();
        return itr->second;
    };
    for (uint32 index = 0; index < candidateCount; ++index)
    {
        ProfessionRecipe const& recipe = *knownRecipes[(work.RecipeCursor + index) % knownRecipes.size()];
        ItemTemplate const* output = sObjectMgr->GetItemTemplate(recipe.ItemId);
        if (!output || bot->GetPureSkillValue(recipe.SkillId) < recipe.RequiredSkill ||
            bot->HasSpellCooldown(recipe.SpellId) || !hasFocus(recipe.Focus) ||
            (ItemUsageValue::IsProfessionTool(bot, output) && bot->HasItemCount(recipe.ItemId, 1)))
            continue;

        bool const skillUp = bot->GetPureSkillValue(recipe.SkillId) < bot->GetPureMaxSkillValue(recipe.SkillId) &&
                             ItemUsageValue::SpellGivesSkillUp(recipe.SpellId, bot);
        bool const auctionable = output->SellPrice && output->Bonding != BIND_WHEN_PICKED_UP &&
                                 output->Bonding != BIND_QUEST_ITEM && !output->HasFlag(ITEM_FLAG_CONJURED);
        bool useful = false;
        if (!skillUp || !auctionable)
        {
            ItemUsage const usage =
                botAI->GetAiObjectContext()->GetValue<ItemUsage>("item usage", recipe.ItemId)->Get();
            useful = usage == ITEM_USAGE_EQUIP || usage == ITEM_USAGE_REPLACE || usage == ITEM_USAGE_USE ||
                     usage == ITEM_USAGE_SKILL || usage == ITEM_USAGE_QUEST;
        }
        uint32 const score = skillUp ? 3 : useful ? 2 : auctionable ? 1 : 0;
        if (!score || score < bestScore || (!useful && !auctionable))
            continue;

        uint32 expansionsLeft = MAX_RECIPE_EXPANSIONS;
        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(recipe.SpellId);
        bool const singleCast = (useful && !auctionable) || (spellInfo && spellInfo->GetRecoveryTime());
        uint32 maximumBatch = singleCast ? 1 : sPlayerbotAIConfig.ProfessionLoopBatchSize;
        if (output->MaxCount > 0)
        {
            uint32 const owned = bot->GetItemCount(recipe.ItemId, true);
            if (owned >= uint32(output->MaxCount))
                continue;
            maximumBatch = std::min(maximumBatch, (uint32(output->MaxCount) - owned) / recipe.OutputCount);
        }
        for (uint32 batch = maximumBatch; batch; --batch)
        {
            PlanningInventory trial = inventory;
            std::function<bool(uint32, uint32, uint32)> acquire;
            acquire = [&](uint32 itemId, uint32 count, uint32 depth)
            {
                if (!expansionsLeft || depth > MAX_REFINEMENT_DEPTH || trial.Refinements.size() >= MAX_REFINEMENT_STEPS)
                    return false;
                --expansionsLeft;
                uint32 const carried = std::min(trial.Available[itemId], count);
                trial.Available[itemId] -= carried;
                trial.Reserved[itemId] += count;
                if (carried && collectedItems.contains(itemId))
                    trial.UsesCollectedMaterial = true;
                count -= carried;
                if (!count)
                    return true;

                ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
                if (!proto)
                    return false;
                if (_vendorsByItem.contains(itemId) && proto->BuyPrice > 0)
                {
                    auto const [vendor, inserted] = reachableVendors.try_emplace(itemId, false);
                    if (inserted)
                        vendor->second = FindVendor(bot, itemId).has_value();
                    if (!vendor->second)
                        return false;
                    uint32 const buyCount = std::max(1u, proto->BuyCount);
                    uint32 const purchaseCount = (count + buyCount - 1) / buyCount;
                    trial.PurchaseCost += uint64(purchaseCount) * proto->BuyPrice;
                    trial.Supplies[itemId] += count;
                    return trial.PurchaseCost <= budget;
                }
                auto const producers = _recipesByOutput.find(itemId);
                if (producers == _recipesByOutput.end() || proto->Class != ITEM_CLASS_TRADE_GOODS ||
                    !trial.Path.insert(itemId).second)
                    return false;

                PlanningInventory const before = trial;
                uint32 choices = 0;
                for (uint32 spellId : producers->second)
                {
                    ProfessionRecipe const& producer = _recipes.at(spellId);
                    if (!bot->HasSpell(spellId) || bot->HasSpellCooldown(spellId) ||
                        bot->GetPureSkillValue(producer.SkillId) < producer.RequiredSkill || !hasFocus(producer.Focus))
                        continue;
                    if (++choices > MAX_PRODUCER_CHOICES)
                        break;
                    trial = before;
                    uint32 const casts = (count + producer.OutputCount - 1) / producer.OutputCount;
                    SpellInfo const* producerSpell = sSpellMgr->GetSpellInfo(spellId);
                    if (casts + trial.RefinementCasts > MAX_REFINEMENT_STEPS ||
                        (casts > 1 && producerSpell && producerSpell->GetRecoveryTime()))
                        continue;
                    bool possible = true;
                    for (auto const& [reagent, amount] : producer.Reagents)
                        if (!acquire(reagent, amount * casts, depth + 1))
                        {
                            possible = false;
                            break;
                        }
                    if (possible && trial.Refinements.size() < MAX_REFINEMENT_STEPS &&
                        trial.RefinementCasts + casts <= MAX_REFINEMENT_STEPS)
                    {
                        trial.Refinements.push_back({spellId, itemId, casts});
                        trial.RefinementCasts += casts;
                        trial.Available[itemId] += casts * producer.OutputCount - count;
                        trial.Path.erase(itemId);
                        return true;
                    }
                }
                return false;
            };
            bool possible = true;
            trial.Path.insert(recipe.ItemId);
            for (auto const& [itemId, count] : recipe.Reagents)
                if (!acquire(itemId, count * batch, 0))
                {
                    possible = false;
                    break;
                }
            if (!possible || !trial.UsesCollectedMaterial)
                continue;
            bestScore = score;
            bestPlan = std::move(trial);
            bestCraft = {recipe.SpellId, recipe.ItemId, batch};
            break;
        }
    }
    work.RecipeCursor = (work.RecipeCursor + candidateCount) % knownRecipes.size();
    if (!bestScore)
        return false;
    work.Refinements = std::move(bestPlan.Refinements);
    work.Supplies = std::move(bestPlan.Supplies);
    work.Reserved = std::move(bestPlan.Reserved);
    work.Craft = bestCraft;
    work.Phase = ProfessionPhase::Refine;
    work.Destination.reset();
    return true;
}
