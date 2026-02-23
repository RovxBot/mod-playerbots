#ifndef _PLAYERBOT_RAIDBWLTRIGGERCONTEXT_H
#define _PLAYERBOT_RAIDBWLTRIGGERCONTEXT_H

#include "AiObjectContext.h"
#include "NamedObjectContext.h"
#include "RaidBwlTriggers.h"

class RaidBwlTriggerContext : public NamedObjectContext<Trigger>
{
public:
    RaidBwlTriggerContext()
    {
        creators["bwl missing onyxia scale cloak"] = &RaidBwlTriggerContext::bwl_missing_onyxia_scale_cloak;
        creators["bwl razorgore encounter"] = &RaidBwlTriggerContext::bwl_razorgore_encounter;
        creators["bwl vaelastrasz encounter"] = &RaidBwlTriggerContext::bwl_vaelastrasz_encounter;
        creators["bwl vaelastrasz burning adrenaline self"] = &RaidBwlTriggerContext::bwl_vaelastrasz_burning_adrenaline_self;
        creators["bwl vaelastrasz main tank burning adrenaline"] =
            &RaidBwlTriggerContext::bwl_vaelastrasz_main_tank_burning_adrenaline;
        creators["bwl vaelastrasz positioning"] = &RaidBwlTriggerContext::bwl_vaelastrasz_positioning;
        creators["bwl suppression device"] = &RaidBwlTriggerContext::bwl_suppression_device;
        creators["bwl affliction bronze"] = &RaidBwlTriggerContext::bwl_affliction_bronze;
    }

private:
    static Trigger* bwl_missing_onyxia_scale_cloak(PlayerbotAI* ai) { return new BwlMissingOnyxiaScaleCloakTrigger(ai); }
    static Trigger* bwl_razorgore_encounter(PlayerbotAI* ai) { return new BwlRazorgoreEncounterTrigger(ai); }
    static Trigger* bwl_vaelastrasz_encounter(PlayerbotAI* ai) { return new BwlVaelastraszEncounterTrigger(ai); }
    static Trigger* bwl_vaelastrasz_burning_adrenaline_self(PlayerbotAI* ai)
    {
        return new BwlVaelastraszBurningAdrenalineSelfTrigger(ai);
    }
    static Trigger* bwl_vaelastrasz_main_tank_burning_adrenaline(PlayerbotAI* ai)
    {
        return new BwlVaelastraszMainTankBurningAdrenalineTrigger(ai);
    }
    static Trigger* bwl_vaelastrasz_positioning(PlayerbotAI* ai) { return new BwlVaelastraszPositioningTrigger(ai); }
    static Trigger* bwl_suppression_device(PlayerbotAI* ai) { return new BwlSuppressionDeviceTrigger(ai); }
    static Trigger* bwl_affliction_bronze(PlayerbotAI* ai) { return new BwlAfflictionBronzeTrigger(ai); }
};

#endif
