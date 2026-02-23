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

class Aq40FankrissActiveTrigger : public Trigger
{
public:
    Aq40FankrissActiveTrigger(PlayerbotAI* botAI) : Trigger(botAI, "aq40 fankriss active") {}
    bool IsActive() override;
};

class Aq40FankrissSpawnedTrigger : public Trigger
{
public:
    Aq40FankrissSpawnedTrigger(PlayerbotAI* botAI) : Trigger(botAI, "aq40 fankriss spawn active") {}
    bool IsActive() override;
};

class Aq40HuhuranActiveTrigger : public Trigger
{
public:
    Aq40HuhuranActiveTrigger(PlayerbotAI* botAI) : Trigger(botAI, "aq40 huhuran active") {}
    bool IsActive() override;
};

class Aq40HuhuranPoisonPhaseTrigger : public Trigger
{
public:
    Aq40HuhuranPoisonPhaseTrigger(PlayerbotAI* botAI) : Trigger(botAI, "aq40 huhuran poison phase") {}
    bool IsActive() override;
};

class Aq40TwinEmperorsActiveTrigger : public Trigger
{
public:
    Aq40TwinEmperorsActiveTrigger(PlayerbotAI* botAI) : Trigger(botAI, "aq40 twin emperors active") {}
    bool IsActive() override;
};

class Aq40TwinEmperorsRoleMismatchTrigger : public Trigger
{
public:
    Aq40TwinEmperorsRoleMismatchTrigger(PlayerbotAI* botAI)
        : Trigger(botAI, "aq40 twin emperors role mismatch")
    {
    }
    bool IsActive() override;
};

#endif
