#ifndef _PLAYERBOT_RAIDAQ40TRIGGERS_H
#define _PLAYERBOT_RAIDAQ40TRIGGERS_H

#include "RaidAq40BossHelper.h"
#include "Trigger.h"

class Aq40EngageTrigger : public Trigger
{
public:
    Aq40EngageTrigger(PlayerbotAI* botAI) : Trigger(botAI, "aq40 engage") {}
    bool IsActive() override;
};

#endif
