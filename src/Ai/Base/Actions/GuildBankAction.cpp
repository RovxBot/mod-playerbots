/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "GuildBankAction.h"

#include "AiObjectContext.h"
#include "GameObject.h"
#include "Guild.h"
#include "PlayerbotAI.h"

bool GuildBankAction::Execute(Event event)
{
    std::string const text = event.getParam();
    if (text.empty())
        return false;

    if (!bot->GetGuildId() || (GetMaster() && GetMaster()->GetGuildId() != bot->GetGuildId()))
    {
        botAI->TellMaster("I'm not in your guild!");
        return false;
    }

    if (GameObject* bank = GetNearbyGuildBank())
        return Execute(text, bank);

    botAI->TellMaster("Cannot find the guild bank nearby");
    return false;
}

GameObject* GuildBankAction::GetNearbyGuildBank() const
{
    GuidVector const gos = *botAI->GetAiObjectContext()->GetValue<GuidVector>("nearest game objects");
    for (ObjectGuid const& guid : gos)
    {
        GameObject* go = botAI->GetGameObject(guid);
        if (!go)
            continue;

        if (GameObject* bank = bot->GetGameObjectIfCanInteractWith(go->GetGUID(), GAMEOBJECT_TYPE_GUILD_BANK))
            return bank;
    }

    return nullptr;
}

bool GuildBankAction::CanDepositToFirstTab() const
{
    Guild* guild = bot->GetGuild();
    return guild && guild->MemberHasTabRights(bot->GetGUID(), 0, GUILD_BANK_RIGHT_DEPOSIT_ITEM);
}

bool GuildBankAction::Execute(std::string const text, GameObject* bank)
{
    bool result = true;

    std::vector<Item*> found = parseItems(text);
    if (found.empty())
        return false;

    for (std::vector<Item*>::iterator i = found.begin(); i != found.end(); i++)
    {
        Item* item = *i;
        if (item)
            result &= MoveFromCharToBank(item, bank);
    }

    return result;
}

bool GuildBankAction::MoveFromCharToBank(Item* item, GameObject* bank, bool report)
{
    if (!item || !bank || !bot->GetGameObjectIfCanInteractWith(bank->GetGUID(), GAMEOBJECT_TYPE_GUILD_BANK))
        return false;

    uint32 playerSlot = item->GetSlot();
    uint32 playerBag = item->GetBagSlot();
    ObjectGuid const itemGuid = item->GetGUID();
    std::string const itemText = chat->FormatItem(item->GetTemplate());

    std::ostringstream out;

    Guild* guild = bot->GetGuild();
    // guild->SwapItems(bot, 0, playerSlot, 0, INVENTORY_SLOT_BAG_0, 0);

    // check source pos rights (item moved to bank)
    if (!guild || !guild->MemberHasTabRights(bot->GetGUID(), 0, GUILD_BANK_RIGHT_DEPOSIT_ITEM))
    {
        out << "I can't put " << itemText
            << " to guild bank. I have no rights to put items in the first guild bank tab";
        if (report)
            botAI->TellMaster(out);
        return false;
    }

    guild->SwapItemsWithInventory(bot, false, 0, NULL_SLOT, playerBag, playerSlot, 0);

    Item* sourceItem = bot->GetItemByPos(playerBag, playerSlot);
    bool const moved = !sourceItem || sourceItem->GetGUID() != itemGuid;
    if (report)
    {
        if (moved)
            out << itemText << " put to guild bank";
        else
            out << "I can't put " << itemText << " to guild bank";

        botAI->TellMaster(out);
    }

    return moved;
}
