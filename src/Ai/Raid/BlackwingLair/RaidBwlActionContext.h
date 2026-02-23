#ifndef _PLAYERBOT_RAIDBWLACTIONCONTEXT_H
#define _PLAYERBOT_RAIDBWLACTIONCONTEXT_H

#include "Action.h"
#include "NamedObjectContext.h"
#include "RaidBwlActions.h"

class RaidBwlActionContext : public NamedObjectContext<Action>
{
public:
    RaidBwlActionContext()
    {
        creators["bwl warn onyxia scale cloak"] = &RaidBwlActionContext::bwl_warn_onyxia_scale_cloak;
        creators["bwl razorgore choose target"] = &RaidBwlActionContext::bwl_razorgore_choose_target;
        creators["bwl vaelastrasz choose target"] = &RaidBwlActionContext::bwl_vaelastrasz_choose_target;
        creators["bwl vaelastrasz position"] = &RaidBwlActionContext::bwl_vaelastrasz_position;
        creators["bwl broodlord choose target"] = &RaidBwlActionContext::bwl_broodlord_choose_target;
        creators["bwl broodlord position"] = &RaidBwlActionContext::bwl_broodlord_position;
        creators["bwl firemaw choose target"] = &RaidBwlActionContext::bwl_firemaw_choose_target;
        creators["bwl firemaw position"] = &RaidBwlActionContext::bwl_firemaw_position;
        creators["bwl ebonroc choose target"] = &RaidBwlActionContext::bwl_ebonroc_choose_target;
        creators["bwl ebonroc position"] = &RaidBwlActionContext::bwl_ebonroc_position;
        creators["bwl flamegor choose target"] = &RaidBwlActionContext::bwl_flamegor_choose_target;
        creators["bwl flamegor position"] = &RaidBwlActionContext::bwl_flamegor_position;
        creators["bwl flamegor tranq"] = &RaidBwlActionContext::bwl_flamegor_tranq;
        creators["bwl chromaggus choose target"] = &RaidBwlActionContext::bwl_chromaggus_choose_target;
        creators["bwl chromaggus position"] = &RaidBwlActionContext::bwl_chromaggus_position;
        creators["bwl chromaggus tranq"] = &RaidBwlActionContext::bwl_chromaggus_tranq;
        creators["bwl chromaggus los hide"] = &RaidBwlActionContext::bwl_chromaggus_los_hide;
        creators["bwl turn off suppression device"] = &RaidBwlActionContext::bwl_turn_off_suppression_device;
        creators["bwl use hourglass sand"] = &RaidBwlActionContext::bwl_use_hourglass_sand;
    }

private:
    static Action* bwl_warn_onyxia_scale_cloak(PlayerbotAI* botAI) { return new BwlWarnOnyxiaScaleCloakAction(botAI); }
    static Action* bwl_razorgore_choose_target(PlayerbotAI* botAI) { return new BwlRazorgoreChooseTargetAction(botAI); }
    static Action* bwl_vaelastrasz_choose_target(PlayerbotAI* botAI) { return new BwlVaelastraszChooseTargetAction(botAI); }
    static Action* bwl_vaelastrasz_position(PlayerbotAI* botAI) { return new BwlVaelastraszPositionAction(botAI); }
    static Action* bwl_broodlord_choose_target(PlayerbotAI* botAI) { return new BwlBroodlordChooseTargetAction(botAI); }
    static Action* bwl_broodlord_position(PlayerbotAI* botAI) { return new BwlBroodlordPositionAction(botAI); }
    static Action* bwl_firemaw_choose_target(PlayerbotAI* botAI) { return new BwlFiremawChooseTargetAction(botAI); }
    static Action* bwl_firemaw_position(PlayerbotAI* botAI) { return new BwlFiremawPositionAction(botAI); }
    static Action* bwl_ebonroc_choose_target(PlayerbotAI* botAI) { return new BwlEbonrocChooseTargetAction(botAI); }
    static Action* bwl_ebonroc_position(PlayerbotAI* botAI) { return new BwlEbonrocPositionAction(botAI); }
    static Action* bwl_flamegor_choose_target(PlayerbotAI* botAI) { return new BwlFlamegorChooseTargetAction(botAI); }
    static Action* bwl_flamegor_position(PlayerbotAI* botAI) { return new BwlFlamegorPositionAction(botAI); }
    static Action* bwl_flamegor_tranq(PlayerbotAI* botAI) { return new BwlFlamegorTranqAction(botAI); }
    static Action* bwl_chromaggus_choose_target(PlayerbotAI* botAI) { return new BwlChromaggusChooseTargetAction(botAI); }
    static Action* bwl_chromaggus_position(PlayerbotAI* botAI) { return new BwlChromaggusPositionAction(botAI); }
    static Action* bwl_chromaggus_tranq(PlayerbotAI* botAI) { return new BwlChromaggusTranqAction(botAI); }
    static Action* bwl_chromaggus_los_hide(PlayerbotAI* botAI) { return new BwlChromaggusLosHideAction(botAI); }
    static Action* bwl_turn_off_suppression_device(PlayerbotAI* botAI) { return new BwlTurnOffSuppressionDeviceAction(botAI); }
    static Action* bwl_use_hourglass_sand(PlayerbotAI* botAI) { return new BwlUseHourglassSandAction(botAI); }
};

#endif
