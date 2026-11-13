/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_PROFESSIONMGR_H
#define PLAYERBOTS_PROFESSIONMGR_H

#include <array>
#include <ctime>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ObjectGuid.h"
#include "TravelMgr.h"

class Player;
class PlayerbotAI;
class Item;
struct ItemTemplate;
struct AuctionEntry;
struct AuctionHouseEntry;

// Each phase has its own action; triggers only inspect this state.
enum class ProfessionPhase : uint8
{
    Mail,
    Sell,
    Plan,
    Refine,
    Supplies,
    Craft,
    Auction,
    Done
};

struct ProfessionDestination
{
    WorldPosition Position;
    ObjectGuid::LowType SpawnId = 0;
    uint32 Entry = 0;
    uint32 Faction = 0;
};

struct ProfessionStep
{
    uint32 SpellId = 0;
    uint32 ItemId = 0;
    uint32 Count = 0;
};

struct AuctionSaleOpportunity
{
    uint32 FirstSeen = 0;
    bool Attempted = false;
};

enum class ProfessionMailRequest : uint8
{
    None,
    Money,
    Item,
    Delete
};

struct ProfessionWork
{
    ProfessionPhase Phase = ProfessionPhase::Done;
    std::vector<ProfessionStep> Refinements;
    ProfessionStep Craft;
    std::map<uint32, uint32> Supplies;
    std::map<uint32, uint32> Reserved;
    std::map<uint32, uint32> Products;
    std::vector<ObjectGuid> AuctionItems;
    std::vector<uint32> AuctionMail;
    std::unordered_map<ObjectGuid, AuctionSaleOpportunity> AuctionSales;
    std::optional<ProfessionDestination> Destination;
    ObjectGuid PendingItem;
    uint32 PendingInput = 0;
    uint32 PendingOutput = 0;
    uint32 BeforeCount = 0;
    uint32 Attempts = 0;
    uint32 Conversions = 0;
    uint32 RecipeCursor = 0;
    uint32 MailItem = 0;
    ProfessionMailRequest MailRequest = ProfessionMailRequest::None;
    uint64 Generation = 0;
    bool Ready = true;
    bool Waiting = false;
    bool InputsChecked = false;
    bool AuctionsPrepared = false;
    bool AuctionItemChecked = false;
    bool MailPrepared = false;
};

struct ProfessionRecipe
{
    uint32 SpellId = 0;
    uint32 SkillId = 0;
    uint32 ItemId = 0;
    uint32 OutputCount = 1;
    uint32 RequiredSkill = 0;
    uint32 Focus = 0;
    std::vector<std::pair<uint32, uint32>> Reagents;
};

// Recipe and destination indexes are immutable after startup; auction stock and prices have their own lock.
class ProfessionMgr
{
public:
    static ProfessionMgr& Instance()
    {
        static ProfessionMgr instance;
        return instance;
    }

    void Load();
    ProfessionRecipe const* GetRecipe(uint32 spellId) const;
    bool CanStart(PlayerbotAI* botAI) const;
    void Start(PlayerbotAI* botAI) const;
    bool BuildPlan(PlayerbotAI* botAI) const;
    static void Finish(PlayerbotAI* botAI);
    static bool IsReserved(PlayerbotAI* botAI, uint32 itemId);
    static bool CanRun(PlayerbotAI* botAI);
    static bool CanAuction(Item const* item);
    static bool ShouldDeferSale(PlayerbotAI* botAI, Item const* item);
    static void AuctionAttempted(PlayerbotAI* botAI, ObjectGuid item);
    void InitializeAuctionCounts();
    void AuctionAdded(AuctionEntry const& auction);
    void AuctionRemoved(AuctionEntry const& auction);
    void AuctionSold(AuctionEntry const& auction);
    uint32 GetActiveAuctionCount(ObjectGuid owner) const;
    uint32 GetActiveAuctionItemCount(ObjectGuid owner, uint32 itemId) const;
    static uint32 GetAuctionMinimumUnitPrice(ItemTemplate const* item, AuctionHouseEntry const* house);
    static uint32 GetAuctionStartingBid(ItemTemplate const* item, AuctionHouseEntry const* house, uint32 count,
                                        uint32 buyout);
    uint32 GetAuctionUnitPrice(ItemTemplate const* item, AuctionHouseEntry const* house,
                               int32 randomProperty = 0) const;

    std::optional<ProfessionDestination> FindVendor(Player* bot, uint32 itemId = 0) const;
    std::optional<ProfessionDestination> FindAuctioneer(Player* bot) const;
    std::optional<ProfessionDestination> FindMailbox(Player* bot) const;
    std::optional<ProfessionDestination> FindFocus(Player* bot, uint32 focus) const;

private:
    static constexpr std::size_t AUCTION_PRICE_SAMPLES = 8;

    struct AuctionStock
    {
        uint32 Listings = 0;
        std::unordered_map<uint32, uint32> Units;
    };

    struct AuctionPriceSample
    {
        uint32 UnitPrice = 0;
        time_t SoldAt = 0;
    };

    struct AuctionPriceHistory
    {
        std::array<AuctionPriceSample, AUCTION_PRICE_SAMPLES> Samples{};
        uint8 Cursor = 0;
    };

    using DestinationIndex = std::unordered_map<uint32, std::vector<ProfessionDestination>>;
    std::optional<ProfessionDestination> FindNearest(Player* bot,
                                                     std::vector<ProfessionDestination> const& candidates) const;
    std::unordered_map<uint32, ProfessionRecipe> _recipes;
    std::unordered_map<uint32, std::vector<uint32>> _recipesByOutput;
    std::unordered_map<uint32, std::vector<uint32>> _vendorsByItem;
    DestinationIndex _creaturesByEntry;
    DestinationIndex _vendorsByMap;
    DestinationIndex _auctioneersByMap;
    DestinationIndex _mailboxesByMap;
    DestinationIndex _focusesByType;
    mutable std::mutex _auctionMutex;
    std::unordered_map<ObjectGuid, AuctionStock> _auctionStock;
    std::unordered_map<uint32, int32> _auctionProperties;
    std::unordered_map<uint32, std::unordered_map<uint64, AuctionPriceHistory>> _auctionPrices;
    std::deque<std::pair<uint32, uint64>> _auctionPriceOrder;
};

#endif
