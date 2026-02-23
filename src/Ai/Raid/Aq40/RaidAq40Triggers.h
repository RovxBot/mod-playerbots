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

class Aq40TwinEmperorsArcaneBurstRiskTrigger : public Trigger
{
public:
    Aq40TwinEmperorsArcaneBurstRiskTrigger(PlayerbotAI* botAI)
        : Trigger(botAI, "aq40 twin emperors arcane burst risk")
    {
    }
    bool IsActive() override;
};

class Aq40CthunActiveTrigger : public Trigger
{
public:
    Aq40CthunActiveTrigger(PlayerbotAI* botAI) : Trigger(botAI, "aq40 cthun active") {}
    bool IsActive() override;
};

class Aq40CthunPhase2Trigger : public Trigger
{
public:
    Aq40CthunPhase2Trigger(PlayerbotAI* botAI) : Trigger(botAI, "aq40 cthun phase2") {}
    bool IsActive() override;
};

class Aq40CthunAddsPresentTrigger : public Trigger
{
public:
    Aq40CthunAddsPresentTrigger(PlayerbotAI* botAI) : Trigger(botAI, "aq40 cthun adds present") {}
    bool IsActive() override;
};

class Aq40CthunDarkGlareTrigger : public Trigger
{
public:
    Aq40CthunDarkGlareTrigger(PlayerbotAI* botAI) : Trigger(botAI, "aq40 cthun dark glare") {}
    bool IsActive() override;
};

class Aq40CthunInStomachTrigger : public Trigger
{
public:
    Aq40CthunInStomachTrigger(PlayerbotAI* botAI) : Trigger(botAI, "aq40 cthun in stomach") {}
    bool IsActive() override;
};

class Aq40CthunVulnerableTrigger : public Trigger
{
public:
    Aq40CthunVulnerableTrigger(PlayerbotAI* botAI) : Trigger(botAI, "aq40 cthun vulnerable") {}
    bool IsActive() override;
};

class Aq40CthunEyeCastTrigger : public Trigger
{
public:
    Aq40CthunEyeCastTrigger(PlayerbotAI* botAI) : Trigger(botAI, "aq40 cthun eye cast") {}
    bool IsActive() override;
};

#endif
