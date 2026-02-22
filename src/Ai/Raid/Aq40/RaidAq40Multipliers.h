#ifndef _PLAYERBOT_RAIDAQ40MULTIPLIERS_H
#define _PLAYERBOT_RAIDAQ40MULTIPLIERS_H

#include "Multiplier.h"

class Aq40GenericMultiplier : public Multiplier
{
public:
    Aq40GenericMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "aq40 generic") {}
    float GetValue(Action* action) override;
};

#endif
