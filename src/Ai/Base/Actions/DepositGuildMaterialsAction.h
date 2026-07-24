/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_DEPOSITGUILDMATERIALSACTION_H
#define PLAYERBOTS_DEPOSITGUILDMATERIALSACTION_H

#include "GuildBankAction.h"

class Item;
struct ItemTemplate;

class DepositGuildMaterialsAction : public GuildBankAction
{
public:
    DepositGuildMaterialsAction(PlayerbotAI* botAI) : GuildBankAction(botAI, "deposit guild materials") {}

    bool Execute(Event event) override;
    bool isPossible() override;

private:
    bool HasDepositCandidate();
    bool IsDepositCandidate(Item* item);
    bool IsQuestItem(ItemTemplate const* proto) const;
};

#endif
