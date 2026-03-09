#ifndef _PLAYERBOT_RAIDAQ40MULTIPLIERS_H
#define _PLAYERBOT_RAIDAQ40MULTIPLIERS_H

#include "Multiplier.h"

class Aq40GenericMultiplier : public Multiplier
{
public:
    Aq40GenericMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "aq40 generic") {}
    float GetValue(Action* action) override;
};

class Aq40BugTrioMultiplier : public Multiplier
{
public:
    Aq40BugTrioMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "aq40 bug trio") {}
    float GetValue(Action* action) override;
};

class Aq40OuroMultiplier : public Multiplier
{
public:
    Aq40OuroMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "aq40 ouro") {}
    float GetValue(Action* action) override;
};

class Aq40ViscidusMultiplier : public Multiplier
{
public:
    Aq40ViscidusMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "aq40 viscidus") {}
    float GetValue(Action* action) override;
};

#endif
