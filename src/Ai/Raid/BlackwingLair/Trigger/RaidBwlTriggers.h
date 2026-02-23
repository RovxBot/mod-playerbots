#ifndef _PLAYERBOT_RAIDBWLTRIGGERS_H
#define _PLAYERBOT_RAIDBWLTRIGGERS_H

#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "RaidBwlBossHelper.h"
#include "Trigger.h"

class BwlSuppressionDeviceTrigger : public Trigger
{
public:
    BwlSuppressionDeviceTrigger(PlayerbotAI* botAI) : Trigger(botAI, "bwl suppression device"), helper(botAI) {}
    bool IsActive() override;

private:
    BwlBossHelper helper;
};

class BwlAfflictionBronzeTrigger : public Trigger
{
public:
    BwlAfflictionBronzeTrigger(PlayerbotAI* botAI) : Trigger(botAI, "bwl affliction bronze"), helper(botAI) {}
    bool IsActive() override;

private:
    BwlBossHelper helper;
};

class BwlMissingOnyxiaScaleCloakTrigger : public Trigger
{
public:
    BwlMissingOnyxiaScaleCloakTrigger(PlayerbotAI* botAI)
        : Trigger(botAI, "bwl missing onyxia scale cloak", 30), helper(botAI)
    {
    }
    bool IsActive() override;

private:
    BwlBossHelper helper;
};

class BwlRazorgoreEncounterTrigger : public Trigger
{
public:
    BwlRazorgoreEncounterTrigger(PlayerbotAI* botAI) : Trigger(botAI, "bwl razorgore encounter"), helper(botAI) {}
    bool IsActive() override;

private:
    BwlBossHelper helper;
};

class BwlVaelastraszEncounterTrigger : public Trigger
{
public:
    BwlVaelastraszEncounterTrigger(PlayerbotAI* botAI) : Trigger(botAI, "bwl vaelastrasz encounter"), helper(botAI) {}
    bool IsActive() override;

private:
    BwlBossHelper helper;
};

class BwlVaelastraszBurningAdrenalineSelfTrigger : public Trigger
{
public:
    BwlVaelastraszBurningAdrenalineSelfTrigger(PlayerbotAI* botAI)
        : Trigger(botAI, "bwl vaelastrasz burning adrenaline self"), helper(botAI)
    {
    }
    bool IsActive() override;

private:
    BwlBossHelper helper;
};

class BwlVaelastraszMainTankBurningAdrenalineTrigger : public Trigger
{
public:
    BwlVaelastraszMainTankBurningAdrenalineTrigger(PlayerbotAI* botAI)
        : Trigger(botAI, "bwl vaelastrasz main tank burning adrenaline"), helper(botAI)
    {
    }
    bool IsActive() override;

private:
    BwlBossHelper helper;
};

#endif
