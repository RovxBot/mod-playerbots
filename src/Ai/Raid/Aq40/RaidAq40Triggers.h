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

class Aq40SkeramActiveTrigger : public Trigger
{
public:
    Aq40SkeramActiveTrigger(PlayerbotAI* botAI) : Trigger(botAI, "aq40 skeram active") {}
    bool IsActive() override;
};

class Aq40SkeramBlinkTrigger : public Trigger
{
public:
    Aq40SkeramBlinkTrigger(PlayerbotAI* botAI) : Trigger(botAI, "aq40 skeram blinked") {}
    bool IsActive() override;
};

class Aq40SkeramArcaneExplosionTrigger : public Trigger
{
public:
    Aq40SkeramArcaneExplosionTrigger(PlayerbotAI* botAI) : Trigger(botAI, "aq40 skeram interrupt cast") {}
    bool IsActive() override;
};

class Aq40SkeramMindControlTrigger : public Trigger
{
public:
    Aq40SkeramMindControlTrigger(PlayerbotAI* botAI) : Trigger(botAI, "aq40 skeram mc detected") {}
    bool IsActive() override;
};

class Aq40SkeramSplitTrigger : public Trigger
{
public:
    Aq40SkeramSplitTrigger(PlayerbotAI* botAI) : Trigger(botAI, "aq40 skeram split active") {}
    bool IsActive() override;
};

class Aq40SkeramExecutePhaseTrigger : public Trigger
{
public:
    Aq40SkeramExecutePhaseTrigger(PlayerbotAI* botAI) : Trigger(botAI, "aq40 skeram execute phase") {}
    bool IsActive() override;
};

class Aq40SarturaActiveTrigger : public Trigger
{
public:
    Aq40SarturaActiveTrigger(PlayerbotAI* botAI) : Trigger(botAI, "aq40 sartura active") {}
    bool IsActive() override;
};

class Aq40SarturaWhirlwindTrigger : public Trigger
{
public:
    Aq40SarturaWhirlwindTrigger(PlayerbotAI* botAI) : Trigger(botAI, "aq40 sartura whirlwind") {}
    bool IsActive() override;
};

#endif
