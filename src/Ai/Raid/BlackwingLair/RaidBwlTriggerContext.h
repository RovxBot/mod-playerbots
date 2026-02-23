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
        creators["bwl broodlord encounter"] = &RaidBwlTriggerContext::bwl_broodlord_encounter;
        creators["bwl broodlord positioning"] = &RaidBwlTriggerContext::bwl_broodlord_positioning;
        creators["bwl broodlord main tank mortal strike"] = &RaidBwlTriggerContext::bwl_broodlord_main_tank_mortal_strike;
        creators["bwl firemaw encounter"] = &RaidBwlTriggerContext::bwl_firemaw_encounter;
        creators["bwl firemaw positioning"] = &RaidBwlTriggerContext::bwl_firemaw_positioning;
        creators["bwl firemaw high flame buffet"] = &RaidBwlTriggerContext::bwl_firemaw_high_flame_buffet;
        creators["bwl firemaw main tank high flame buffet"] = &RaidBwlTriggerContext::bwl_firemaw_main_tank_high_flame_buffet;
        creators["bwl ebonroc encounter"] = &RaidBwlTriggerContext::bwl_ebonroc_encounter;
        creators["bwl ebonroc positioning"] = &RaidBwlTriggerContext::bwl_ebonroc_positioning;
        creators["bwl ebonroc main tank shadow"] = &RaidBwlTriggerContext::bwl_ebonroc_main_tank_shadow;
        creators["bwl flamegor encounter"] = &RaidBwlTriggerContext::bwl_flamegor_encounter;
        creators["bwl flamegor positioning"] = &RaidBwlTriggerContext::bwl_flamegor_positioning;
        creators["bwl flamegor frenzy"] = &RaidBwlTriggerContext::bwl_flamegor_frenzy;
        creators["bwl chromaggus encounter"] = &RaidBwlTriggerContext::bwl_chromaggus_encounter;
        creators["bwl chromaggus positioning"] = &RaidBwlTriggerContext::bwl_chromaggus_positioning;
        creators["bwl chromaggus frenzy"] = &RaidBwlTriggerContext::bwl_chromaggus_frenzy;
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
    static Trigger* bwl_broodlord_encounter(PlayerbotAI* ai) { return new BwlBroodlordEncounterTrigger(ai); }
    static Trigger* bwl_broodlord_positioning(PlayerbotAI* ai) { return new BwlBroodlordPositioningTrigger(ai); }
    static Trigger* bwl_broodlord_main_tank_mortal_strike(PlayerbotAI* ai)
    {
        return new BwlBroodlordMainTankMortalStrikeTrigger(ai);
    }
    static Trigger* bwl_firemaw_encounter(PlayerbotAI* ai) { return new BwlFiremawEncounterTrigger(ai); }
    static Trigger* bwl_firemaw_positioning(PlayerbotAI* ai) { return new BwlFiremawPositioningTrigger(ai); }
    static Trigger* bwl_firemaw_high_flame_buffet(PlayerbotAI* ai) { return new BwlFiremawHighFlameBuffetTrigger(ai); }
    static Trigger* bwl_firemaw_main_tank_high_flame_buffet(PlayerbotAI* ai)
    {
        return new BwlFiremawMainTankHighFlameBuffetTrigger(ai);
    }
    static Trigger* bwl_ebonroc_encounter(PlayerbotAI* ai) { return new BwlEbonrocEncounterTrigger(ai); }
    static Trigger* bwl_ebonroc_positioning(PlayerbotAI* ai) { return new BwlEbonrocPositioningTrigger(ai); }
    static Trigger* bwl_ebonroc_main_tank_shadow(PlayerbotAI* ai) { return new BwlEbonrocMainTankShadowTrigger(ai); }
    static Trigger* bwl_flamegor_encounter(PlayerbotAI* ai) { return new BwlFlamegorEncounterTrigger(ai); }
    static Trigger* bwl_flamegor_positioning(PlayerbotAI* ai) { return new BwlFlamegorPositioningTrigger(ai); }
    static Trigger* bwl_flamegor_frenzy(PlayerbotAI* ai) { return new BwlFlamegorFrenzyTrigger(ai); }
    static Trigger* bwl_chromaggus_encounter(PlayerbotAI* ai) { return new BwlChromaggusEncounterTrigger(ai); }
    static Trigger* bwl_chromaggus_positioning(PlayerbotAI* ai) { return new BwlChromaggusPositioningTrigger(ai); }
    static Trigger* bwl_chromaggus_frenzy(PlayerbotAI* ai) { return new BwlChromaggusFrenzyTrigger(ai); }
    static Trigger* bwl_suppression_device(PlayerbotAI* ai) { return new BwlSuppressionDeviceTrigger(ai); }
    static Trigger* bwl_affliction_bronze(PlayerbotAI* ai) { return new BwlAfflictionBronzeTrigger(ai); }
};

#endif
