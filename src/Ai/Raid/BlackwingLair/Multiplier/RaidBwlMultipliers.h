#ifndef _PLAYERBOT_RAIDBWLMULTIPLIERS_H
#define _PLAYERBOT_RAIDBWLMULTIPLIERS_H

#include "Multiplier.h"
#include "RaidBwlBossHelper.h"

class BwlGenericMultiplier : public Multiplier
{
public:
    BwlGenericMultiplier(PlayerbotAI* ai) : Multiplier(ai, "bwl generic"), helper(ai) {}
    float GetValue(Action* action) override;

private:
    BwlBossHelper helper;
};

#endif
