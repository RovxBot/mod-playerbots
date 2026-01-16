/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "LootRollAction.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <sstream>
#include <string>
#include <vector>

#include "AiObjectContext.h"
#include "Event.h"
#include "Group.h"
#include "ItemUsageValue.h"
#include "Log.h"
#include "LootAction.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "SharedDefines.h"
#include "SkillDiscovery.h"
#include "SpellMgr.h"
#include "Util.h"

static std::string BuildItemUsageParam(uint32 itemId, int32 randomProperty)
{
    return ItemUsageValue::BuildItemUsageParam(itemId, randomProperty);
}

// Encode "random enchant" parameter for CalculateRollVote / ItemUsage.
// >0 => randomPropertyId, <0 => randomSuffixId, 0 => none
static inline int32 EncodeRandomEnchantParam(uint32 randomPropertyId, uint32 randomSuffix)
{
    if (randomPropertyId)
        return static_cast<int32>(randomPropertyId);

    if (randomSuffix)
        return -static_cast<int32>(randomSuffix);

    return 0;
}

// Profession helpers: true if the item is a recipe/pattern/book (ITEM_CLASS_RECIPE).
static inline bool IsRecipeItem(ItemTemplate const* proto) { return proto && proto->Class == ITEM_CLASS_RECIPE; }

// Special-case: Book of Glyph Mastery (can own several; do not downgrade NEED on duplicates).
static bool IsGlyphMasteryBook(ItemTemplate const* proto)
{
    if (!proto)
        return false;

    if (proto->Class != ITEM_CLASS_RECIPE || proto->SubClass != ITEM_SUBCLASS_BOOK)
        return false;

    constexpr uint32 SPELL_BOOK_OF_GLYPH_MASTERY = 64323;
    for (int i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
    {
        if (proto->Spells[i].SpellId == SPELL_BOOK_OF_GLYPH_MASTERY)
            return true;
    }

    if (proto->RequiredSkill == SKILL_INSCRIPTION)
    {
        std::string n = ToLowerUtf8(proto->Name1);
        if (n.find("glyph mastery") != std::string::npos || n.find("book of glyph mastery") != std::string::npos)
            return true;
    }

    return false;
}

// Value object for collectible cosmetics (mounts/pets) used in loot rules.
struct CollectibleInfo
{
    bool isCosmetic = false;   // true if this is a cosmetic collectible (mount/pet)
    bool alreadyOwned = false; // true if the bot already knows/owns it in a meaningful way
};

static CollectibleInfo BuildCollectibleInfo(Player* bot, ItemTemplate const* proto)
{
    CollectibleInfo info;

    if (!bot || !proto)
        return info;

    if (proto->Class != ITEM_CLASS_MISC)
        return info;

#if defined(ITEM_SUBCLASS_MISC_MOUNT) || defined(ITEM_SUBCLASS_MISC_PET)
    bool const isMount =
#  if defined(ITEM_SUBCLASS_MISC_MOUNT)
        proto->SubClass == ITEM_SUBCLASS_MISC_MOUNT
#  else
        false
#  endif
        ;
    bool const isPet =
#  if defined(ITEM_SUBCLASS_MISC_PET)
        proto->SubClass == ITEM_SUBCLASS_MISC_PET
#  else
        false
#  endif
        ;
    if (!isMount && !isPet)
        return info;
#else
    if (proto->SubClass != 2 && proto->SubClass != 5)
        return info;
#endif

    info.isCosmetic = true;

    for (int i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
    {
        uint32 const spellId = proto->Spells[i].SpellId;
        if (!spellId)
            continue;

        if (bot->HasSpell(spellId))
        {
            info.alreadyOwned = true;
            return info;
        }
    }

    if (bot->GetItemCount(proto->ItemId, true) > 0)
        info.alreadyOwned = true;

    return info;
}

static inline char const* VoteTxt(RollVote v)
{
    switch (v)
    {
        case NEED:
            return "NEED";
        case GREED:
            return "GREED";
        case PASS:
            return "PASS";
        case DISENCHANT:
            return "DISENCHANT";
        default:
            return "UNKNOWN";
    }
}

static void DebugRecipeRoll(Player* bot, ItemTemplate const* proto, ItemUsage usage, bool recipeChecked,
                            bool recipeUseful, bool recipeKnown, uint32 reqSkill, uint32 reqRank, uint32 botRank,
                            RollVote before, RollVote after)
{
    LOG_DEBUG("playerbots",
              "[LootPaternDBG] {} JC:{} item:{} \"{}\" class={} sub={} bond={} usage={} "
              "recipeChecked={} useful={} known={} reqSkill={} reqRank={} botRank={} vote:{} -> {} dupCount={}",
              bot->GetName(), bot->GetSkillValue(SKILL_JEWELCRAFTING), proto->ItemId, proto->Name1, proto->Class,
              proto->SubClass, proto->Bonding, (int)usage, recipeChecked, recipeUseful, recipeKnown, reqSkill, reqRank,
              botRank, VoteTxt(before), VoteTxt(after), bot->GetItemCount(proto->ItemId, true));
}

struct RecipeInfo
{
    uint32 requiredSkill = 0;
    uint32 requiredRank = 0;
    uint32 botRank = 0;
    bool botHasProfession = false;
    bool known = false;
};

static RecipeInfo BuildRecipeInfo(Player* bot, ItemTemplate const* proto)
{
    RecipeInfo info;

    if (!bot || !IsRecipeItem(proto))
        return info;

    info.requiredSkill = GetRecipeSkill(proto);
    info.requiredRank = proto->RequiredSkillRank;

    if (!info.requiredSkill)
        return info;

    info.botHasProfession = bot->HasSkill(info.requiredSkill);
    if (info.botHasProfession)
        info.botRank = bot->GetSkillValue(info.requiredSkill);

    if (bot)
    {
        for (int i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
        {
            uint32 teach = proto->Spells[i].SpellId;
            if (!teach)
                continue;

            SpellInfo const* si = sSpellMgr->GetSpellInfo(teach);
            if (!si)
                continue;

            for (int eff = 0; eff < MAX_SPELL_EFFECTS; ++eff)
            {
                if (si->Effects[eff].Effect != SPELL_EFFECT_LEARN_SPELL)
                    continue;

                uint32 learned = si->Effects[eff].TriggerSpell;
                if (learned && bot->HasSpell(learned))
                {
                    info.known = true;
                    break;
                }
            }

            if (info.known)
                break;
        }
    }

    return info;
}

static bool IsProfessionRecipeUsefulForBot(RecipeInfo const& recipe)
{
    if (!recipe.requiredSkill)
        return false;

    if (!recipe.botHasProfession)
        return false;

    if (!sPlayerbotAIConfig->recipesIgnoreSkillRank && recipe.requiredRank && recipe.botRank < recipe.requiredRank)
        return false;

    if (recipe.known)
        return false;

    return true;
}

static bool IsClassAllowedByItemTemplate(uint8 cls, ItemTemplate const* proto)
{
    if (!proto)
        return true;

    int32 const allowable = proto->AllowableClass;
    if (allowable <= 0)
        return true;

    if (!cls)
        return false;

    uint32 const classMask = static_cast<uint32>(allowable);
    uint32 const thisClassBit = 1u << (cls - 1u);
    return (classMask & thisClassBit) != 0;
}

static bool CanBotUseToken(ItemTemplate const* proto, Player* bot);
static bool RollUniqueCheck(ItemTemplate const* proto, Player* bot);

static inline bool IsLikelyDisenchantable(ItemTemplate const* proto)
{
    if (!proto)
        return false;

    if (proto->DisenchantID > 0)
        return true;

    if (proto->DisenchantID < 0)
        return false;

    if (proto->Class != ITEM_CLASS_ARMOR && proto->Class != ITEM_CLASS_WEAPON)
        return false;

    return proto->Quality >= ITEM_QUALITY_UNCOMMON && proto->Quality <= ITEM_QUALITY_EPIC;
}

static int8 TokenSlotFromName(ItemTemplate const* proto)
{
    if (!proto)
        return -1;
    std::string n = std::string(proto->Name1);
    std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (n.find("helm") != std::string::npos || n.find("head") != std::string::npos)
        return INVTYPE_HEAD;

    if (n.find("shoulder") != std::string::npos || n.find("mantle") != std::string::npos ||
        n.find("spauld") != std::string::npos)
        return INVTYPE_SHOULDERS;

    if (n.find("chest") != std::string::npos || n.find("tunic") != std::string::npos ||
        n.find("robe") != std::string::npos || n.find("breastplate") != std::string::npos ||
        n.find("chestguard") != std::string::npos)
        return INVTYPE_CHEST;

    if (n.find("glove") != std::string::npos || n.find("handguard") != std::string::npos ||
        n.find("gauntlet") != std::string::npos)
        return INVTYPE_HANDS;

    if (n.find("leg") != std::string::npos || n.find("pant") != std::string::npos ||
        n.find("trouser") != std::string::npos)
        return INVTYPE_LEGS;

    return -1;
}

static std::array<uint32, 6> const& GetSanctificationTokenIds()
{
    static std::array<uint32, 6> const sanctificationTokenIds = {
        52025,
        52026,
        52027,
        52028,
        52029,
        52030
    };

    return sanctificationTokenIds;
}

static bool IsSanctificationToken(ItemTemplate const* proto)
{
    if (!proto)
        return false;

    for (uint32 const entry : GetSanctificationTokenIds())
    {
        if (proto->ItemId == entry)
            return true;
    }

    return false;
}

static uint32 GetOwnedSanctificationTokenCount(Player* bot)
{
    if (!bot)
        return 0;

    uint32 total = 0;
    for (uint32 const entry : GetSanctificationTokenIds())
        total += bot->GetItemCount(entry, true);

    return total;
}

static uint8 EquipmentSlotByInvTypeSafe(uint8 invType)
{
    switch (invType)
    {
        case INVTYPE_HEAD:
            return EQUIPMENT_SLOT_HEAD;
        case INVTYPE_SHOULDERS:
            return EQUIPMENT_SLOT_SHOULDERS;
        case INVTYPE_CHEST:
        case INVTYPE_ROBE:
            return EQUIPMENT_SLOT_CHEST;
        case INVTYPE_HANDS:
            return EQUIPMENT_SLOT_HANDS;
        case INVTYPE_LEGS:
            return EQUIPMENT_SLOT_LEGS;
        default:
            return EQUIPMENT_SLOT_END;
    }
}

static bool IsTokenLikelyUpgrade(ItemTemplate const* token, uint8 invTypeSlot, Player* bot)
{
    if (!token || !bot)
        return false;
    uint8 eq = EquipmentSlotByInvTypeSafe(invTypeSlot);
    if (eq >= EQUIPMENT_SLOT_END)
        return true;

    Item* oldItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, eq);
    if (!oldItem)
        return true;

    ItemTemplate const* oldProto = oldItem->GetTemplate();
    if (!oldProto)
        return true;

    float margin = sPlayerbotAIConfig->tokenILevelMargin;
    return (float)token->ItemLevel >= (float)oldProto->ItemLevel + margin;
}

struct TokenInfo
{
    bool isToken = false;
    bool classCanUse = false;
    int8 invTypeSlot = -1;
    bool likelyUpgrade = false;
};

static TokenInfo BuildTokenInfo(ItemTemplate const* proto, Player* bot)
{
    TokenInfo info;

    if (!proto || !bot)
        return info;

    info.isToken = (proto->Class == ITEM_CLASS_MISC &&
                    proto->SubClass == ITEM_SUBCLASS_JUNK &&
                    proto->Quality == ITEM_QUALITY_EPIC) ||
                   IsSanctificationToken(proto);
    if (!info.isToken)
        return info;

    info.classCanUse = CanBotUseToken(proto, bot);
    info.invTypeSlot = TokenSlotFromName(proto);

    if (info.classCanUse && info.invTypeSlot >= 0)
        info.likelyUpgrade = IsTokenLikelyUpgrade(proto, static_cast<uint8>(info.invTypeSlot), bot);

    if (IsSanctificationToken(proto))
    {
        bool const slotKnown = info.invTypeSlot >= 0;
        LOG_DEBUG("playerbots",
                  "[LootRollDBG][Token] bot={} itemId={} \"{}\" classId={} isToken={} classCanUse={} invTypeSlot={} slotKnown={} likelyUpgrade={}",
                  bot->GetName(), proto->ItemId, proto->Name1, bot->getClass(), info.isToken ? 1 : 0,
                  info.classCanUse ? 1 : 0, static_cast<int32>(info.invTypeSlot), slotKnown ? 1 : 0,
                  info.likelyUpgrade ? 1 : 0);
    }

    return info;
}

static bool TryTokenRollVote(ItemTemplate const* proto, Player* bot, RollVote& outVote)
{
    TokenInfo const token = BuildTokenInfo(proto, bot);
    constexpr uint32 SANCTIFICATION_TOKEN_MAX_COUNT = 5u;

    // Not a token → let other rules decide.
    if (!token.isToken)
        return false;

    if (token.classCanUse)
    {
        if (token.invTypeSlot >= 0)
        {
            // Known slot: NEED only if it looks like an upgrade, otherwise GREED.
            outVote = token.likelyUpgrade ? NEED : GREED;
        }
        else
        {
            // Unknown slot (e.g. T10 sanctification tokens).
            if (IsSanctificationToken(proto) && sPlayerbotAIConfig->sanctificationTokenRollMode == 1u)
            {
                uint32 const ownedTokens = GetOwnedSanctificationTokenCount(bot);
                outVote = (ownedTokens < SANCTIFICATION_TOKEN_MAX_COUNT) ? NEED : GREED;
                LOG_DEBUG("playerbots",
                          "[LootRollDBG][Token] bot={} itemId={} \"{}\" mode={} ownedTokens={} maxTokens={} vote={}",
                          bot->GetName(), proto->ItemId, proto->Name1,
                          static_cast<uint32>(sPlayerbotAIConfig->sanctificationTokenRollMode),
                          ownedTokens, SANCTIFICATION_TOKEN_MAX_COUNT, VoteTxt(outVote));
            }
            else
                outVote = GREED;
        }
    }
    else
    {
        // Not eligible, so GREED.
        outVote = GREED;
    }

    return true;
}

static RollVote ApplyDisenchantPreference(RollVote currentVote, ItemTemplate const* proto, ItemUsage usage,
                                          Group* group, Player* bot, char const* logTag)
{
    std::string const tag = logTag ? logTag : "[LootRollDBG]";

    bool const isDeCandidate = IsLikelyDisenchantable(proto);
    bool const hasEnchantSkill = bot && bot->HasSkill(SKILL_ENCHANTING);
    int32 const lootMethod = group ? static_cast<int32>(group->GetLootMethod()) : -1;

    uint8 const deMode = sPlayerbotAIConfig->deButtonMode;
    // Mode 0 = no DE button; 1 = enchanters only; 2 = all bots can DE.
    bool const deAllowedForBot = (deMode == 2u) || (deMode == 1u && hasEnchantSkill);

    if (currentVote != NEED &&
        deAllowedForBot &&
        group &&
        (group->GetLootMethod() == NEED_BEFORE_GREED || group->GetLootMethod() == GROUP_LOOT) &&
        isDeCandidate &&
        usage == ITEM_USAGE_DISENCHANT)
    {
        LOG_DEBUG("playerbots",
                  "{} DE switch: {} -> DISENCHANT (lootMethod={}, mode={}, enchSkill={}, deOK=1, usage=DISENCHANT)",
                  tag, VoteTxt(currentVote), lootMethod, static_cast<uint32>(deMode), hasEnchantSkill ? 1 : 0);
        return DISENCHANT;
    }

    LOG_DEBUG("playerbots",
              "{} no DE: vote={} lootMethod={} mode={} enchSkill={} deOK={} usage={}",
              tag, VoteTxt(currentVote), lootMethod, static_cast<uint32>(deMode),
              hasEnchantSkill ? 1 : 0, isDeCandidate ? 1 : 0, static_cast<uint32>(usage));

    return currentVote;
}

static RollVote FinalizeRollVote(RollVote vote, ItemTemplate const* proto, ItemUsage usage, Group* group, Player* bot,
                                 char const* logTag)
{
    std::string const tag = logTag ? logTag : "[LootRollDBG]";

    vote = ApplyDisenchantPreference(vote, proto, usage, group, bot, tag.c_str());

    if (sPlayerbotAIConfig->lootRollLevel == 0)
    {
        LOG_DEBUG("playerbots", "{} LootRollLevel=0 forcing PASS (was {})", tag, VoteTxt(vote));
        return PASS;
    }

    if (sPlayerbotAIConfig->lootRollLevel == 1)
    {
        if (vote == NEED)
            vote = RollUniqueCheck(proto, bot) ? PASS : GREED;

        else if (vote == GREED)
            vote = PASS;

        LOG_DEBUG("playerbots", "{} LootRollLevel=1 adjusted vote={}", tag, VoteTxt(vote));
    }

    return vote;
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

        RollVote vote = PASS;

        std::string const itemUsageParam = BuildItemUsageParam(itemId, randomProperty);
        ItemUsage usage = AI_VALUE2(ItemUsage, "loot usage", itemUsageParam);

        LOG_DEBUG("playerbots", "[LootRollDBG] usage={} (EQUIP=1 REPLACE=2 BAD_EQUIP=8 DISENCHANT=9)", (int)usage);
        if (!TryTokenRollVote(proto, bot, vote))
        {
<<<<<<< HEAD
            if (CanBotUseToken(proto, bot))
            {
                vote = NEED; // Eligible for "Need"
            }
            else
            {
                vote = GREED; // Not eligible, so "Greed"
            }
        }
        else
        {
            switch (proto->Class)
            {
                case ITEM_CLASS_WEAPON:
                case ITEM_CLASS_ARMOR:
                    if (usage == ITEM_USAGE_EQUIP || usage == ITEM_USAGE_REPLACE || usage == ITEM_USAGE_BAD_EQUIP)
                    {
                        vote = NEED;
                    }
                    else if (usage != ITEM_USAGE_NONE)
                    {
                        vote = GREED;
                    }
                    break;
                default:
                    if (StoreLootAction::IsLootAllowed(itemId, botAI))
                        vote = CalculateRollVote(proto); // Ensure correct Need/Greed behavior
                    break;
            }
        }
        if (sPlayerbotAIConfig.lootRollLevel == 0)
        {
            vote = PASS;
        }
        else if (sPlayerbotAIConfig.lootRollLevel == 1)
        {
            // Level 1 = "greed" mode: bots greed on useful items but never need
            // Only downgrade NEED to GREED, preserve GREED votes as-is
            if (vote == NEED)
            {
                if (RollUniqueCheck(proto, bot))
                    {
                        vote = PASS;
                    }
                else
                    {
                        vote = GREED;
                    }
            }
        }
        switch (group->GetLootMethod())
        {
            case MASTER_LOOT:
            case FREE_FOR_ALL:
                group->CountRollVote(bot->GetGUID(), guid, PASS);
                break;
            default:
                group->CountRollVote(bot->GetGUID(), guid, vote);
                break;
=======
            // Let CalculateRollVote decide using loot-aware item usage.
            vote = CalculateRollVote(proto, randomProperty);
            LOG_DEBUG("playerbots", "[LootRollDBG] after CalculateRollVote: vote={}", VoteTxt(vote));
>>>>>>> 9d8d8df9 (First release)
        }

        vote = FinalizeRollVote(vote, proto, usage, group, bot, "[LootRollDBG]");
        // Announce + send the roll vote (if ML/FFA => PASS)
        RollVote sent = vote;
        if (group->GetLootMethod() == MASTER_LOOT || group->GetLootMethod() == FREE_FOR_ALL)
            sent = PASS;

        LOG_DEBUG("playerbots", "[LootPaternDBG] send vote={} (lootMethod={} Lvl={}) -> guid={} itemId={}",
                  VoteTxt(sent), (int)group->GetLootMethod(), sPlayerbotAIConfig->lootRollLevel, guid.ToString(),
                  itemId);

        group->CountRollVote(bot->GetGUID(), guid, sent);
        // One item at a time
        return true;
    }

    return false;
}

RollVote LootRollAction::CalculateRollVote(ItemTemplate const* proto, int32 randomProperty)
{
    // Player mimic: upgrade => NEED; useful => GREED; otherwise => PASS
    std::string const itemUsageParam = BuildItemUsageParam(proto->ItemId, randomProperty);
    ItemUsage usage = AI_VALUE2(ItemUsage, "loot usage", itemUsageParam);

    RollVote vote = PASS;

    CollectibleInfo const collectible = BuildCollectibleInfo(bot, proto);

    bool const isCollectibleCosmetic = collectible.isCosmetic;
    bool const alreadyHasCollectible = collectible.alreadyOwned;

    if (isCollectibleCosmetic)
        vote = alreadyHasCollectible ? GREED : NEED;

    bool recipeChecked = false;
    bool recipeNeed = false;
    bool recipeUseful = false;
    bool recipeKnown = false;
    uint32 reqSkillDbg = 0, reqRankDbg = 0, botRankDbg = 0;

    // Professions: NEED on useful recipes/patterns/books when enabled.
    if (sPlayerbotAIConfig->needOnProfessionRecipes && IsRecipeItem(proto))
    {
        RecipeInfo const recipe = BuildRecipeInfo(bot, proto);

        recipeChecked = true;
        // Collect debug data (what the helper va décider)
        reqSkillDbg = recipe.requiredSkill;
        reqRankDbg = recipe.requiredRank;
        botRankDbg = recipe.botRank;
        recipeKnown = recipe.known;

        recipeUseful = IsProfessionRecipeUsefulForBot(recipe);
        if (recipeUseful)
        {
            vote = NEED;
            recipeNeed = true;
        }
        else
            vote = GREED;  // recipe not for the bot -> GREED
    }

    // Do not overwrite the choice if already decided by recipe or cosmetic logic.
    if (!recipeChecked && !isCollectibleCosmetic)
    {
        switch (usage)
        {
            case ITEM_USAGE_EQUIP:
            case ITEM_USAGE_REPLACE:
                vote = NEED;
                break;
            case ITEM_USAGE_BAD_EQUIP:
            case ITEM_USAGE_GUILD_TASK:
            case ITEM_USAGE_SKILL:
            case ITEM_USAGE_USE:
            case ITEM_USAGE_DISENCHANT:
            case ITEM_USAGE_AH:
            case ITEM_USAGE_VENDOR:
            case ITEM_USAGE_KEEP:
            case ITEM_USAGE_AMMO:
                vote = GREED;
                break;
            default:
                vote = PASS;
                break;
        }
    }

    // Lockboxes: if the item is a lockbox and the bot is a Rogue with Lockpicking, prefer NEED (ignored by BoE/BoU).
    const SpecTraits traits = GetSpecTraits(bot);
    const bool isLockbox = ItemUsageValue::IsLockboxItem(proto);
    if (isLockbox && traits.isRogue && bot->HasSkill(SKILL_LOCKPICKING))
        vote = NEED;

    // BoE/BoU rule: by default, avoid NEED on Bind-on-Equip / Bind-on-Use (raid etiquette)
    // BoE/BoU etiquette: avoid NEED on BoE/BoU, except useful profession recipes.
    constexpr uint32 BIND_WHEN_EQUIPPED = 2;  // BoE
    constexpr uint32 BIND_WHEN_USE = 3;       // BoU

    if (vote == NEED && !recipeNeed && !isLockbox && !isCollectibleCosmetic &&
        proto->Bonding == BIND_WHEN_EQUIPPED &&
        !sPlayerbotAIConfig->allowBoENeedIfUpgrade)

        vote = GREED;

    if (vote == NEED && !recipeNeed && !isLockbox && !isCollectibleCosmetic &&
        proto->Bonding == BIND_WHEN_USE &&
        !sPlayerbotAIConfig->allowBoUNeedIfUpgrade)

        vote = GREED;

    // Non-unique soft rule: NEED -> GREED on duplicates, except Book of Glyph Mastery.
    if (vote == NEED)
    {
        if (!IsGlyphMasteryBook(proto))
        {
            // includeBank=true to catch banked duplicates as well.
            if (bot->GetItemCount(proto->ItemId, true) > 0)
                vote = GREED;
        }
    }

    // Unique-equip: never NEED a duplicate (already equipped/owned)
    if (vote == NEED && RollUniqueCheck(proto, bot))
        vote = PASS;

    // Final decision (with allow/deny from loot strategy).
    RollVote finalVote = StoreLootAction::IsLootAllowed(proto->ItemId, GET_PLAYERBOT_AI(bot)) ? vote : PASS;

    // DEBUG: dump for recipes
    if (IsRecipeItem(proto))
    {
        DebugRecipeRoll(bot, proto, usage, recipeChecked, recipeUseful, recipeKnown, reqSkillDbg, reqRankDbg,
                        botRankDbg,
                        /*before*/ (recipeNeed ? NEED : PASS),
                        /*after*/ finalVote);
    }

    return finalVote;
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

    std::string const itemUsageParam = BuildItemUsageParam(itemId, randomProperty);
    ItemUsage usage = AI_VALUE2(ItemUsage, "loot usage", itemUsageParam);

    // 1) Token heuristic: ONLY NEED if the target slot is a likely upgrade
    RollVote vote = PASS;

    if (!TryTokenRollVote(proto, bot, vote))
        vote = CalculateRollVote(proto, randomProperty);

    vote = FinalizeRollVote(vote, proto, usage, group, bot, "[LootEnchantDBG][ML]");

    RollVote sent = vote;
    if (group->GetLootMethod() == MASTER_LOOT || group->GetLootMethod() == FREE_FOR_ALL)
        sent = PASS;

    LOG_DEBUG("playerbots", "[LootEnchantDBG][ML] vote={} -> sent={} lootMethod={} enchSkill={} deOK={}", VoteTxt(vote),
              VoteTxt(sent), (int)group->GetLootMethod(), bot->HasSkill(SKILL_ENCHANTING),
              IsLikelyDisenchantable(proto));

    group->CountRollVote(bot->GetGUID(), creatureGuid, sent);

    return true;
}

static bool CanBotUseToken(ItemTemplate const* proto, Player* bot)
{
    if (!proto || !bot)
        return false;

    return IsClassAllowedByItemTemplate(bot->getClass(), proto);
}

static bool RollUniqueCheck(ItemTemplate const* proto, Player* bot)
{
    // Count the total number of the item (equipped + in bags)
    uint32 totalItemCount = bot->GetItemCount(proto->ItemId, true);

    // Count the number of the item in bags only
    uint32 bagItemCount = bot->GetItemCount(proto->ItemId, false);

    // Determine if the unique item is already equipped
    bool isEquipped = (totalItemCount > bagItemCount);
    if (isEquipped && proto->HasFlag(ITEM_FLAG_UNIQUE_EQUIPPABLE))
        return true;  // Unique Item is already equipped

    else if (proto->HasFlag(ITEM_FLAG_UNIQUE_EQUIPPABLE) && (bagItemCount > 1))
        return true;  // Unique item already in bag, don't roll for it

    return false;  // Item is not equipped or in bags, roll for it
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