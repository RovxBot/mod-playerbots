#ifndef _PLAYERBOT_RAIDBWLBOSSHELPER_H
#define _PLAYERBOT_RAIDBWLBOSSHELPER_H

#include "AiObject.h"
#include "PlayerbotAI.h"
#include "RaidBwlSpellIds.h"

class BwlBossHelper : public AiObject
{
public:
    BwlBossHelper(PlayerbotAI* botAI) : AiObject(botAI) {}

    bool IsInBwl() const { return bot->GetMapId() == 469; }
    bool HasBronzeAffliction() const { return botAI->HasAura(BwlSpellIds::AfflictionBronze, bot); }
    bool HasHourglassSand() const { return bot->HasItemCount(BwlItems::HourglassSand, 1, false); }
};

#endif
