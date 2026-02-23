#ifndef _PLAYERBOT_RAIDAQ40ACTIONCONTEXT_H
#define _PLAYERBOT_RAIDAQ40ACTIONCONTEXT_H

#include "Action.h"
#include "NamedObjectContext.h"
#include "RaidAq40Actions.h"

class RaidAq40ActionContext : public NamedObjectContext<Action>
{
public:
    RaidAq40ActionContext()
    {
        creators["aq40 choose target"] = &RaidAq40ActionContext::choose_target;
        creators["aq40 skeram acquire platform target"] = &RaidAq40ActionContext::skeram_acquire_platform_target;
        creators["aq40 skeram interrupt"] = &RaidAq40ActionContext::skeram_interrupt;
        creators["aq40 skeram focus real boss"] = &RaidAq40ActionContext::skeram_focus_real_boss;
        creators["aq40 skeram control mind control"] = &RaidAq40ActionContext::skeram_control_mind_control;
        creators["aq40 sartura choose target"] = &RaidAq40ActionContext::sartura_choose_target;
        creators["aq40 sartura avoid whirlwind"] = &RaidAq40ActionContext::sartura_avoid_whirlwind;
        creators["aq40 fankriss choose target"] = &RaidAq40ActionContext::fankriss_choose_target;
        creators["aq40 huhuran choose target"] = &RaidAq40ActionContext::huhuran_choose_target;
        creators["aq40 huhuran poison spread"] = &RaidAq40ActionContext::huhuran_poison_spread;
        creators["aq40 twin emperors choose target"] = &RaidAq40ActionContext::twin_emperors_choose_target;
        creators["aq40 twin emperors hold split"] = &RaidAq40ActionContext::twin_emperors_hold_split;
        creators["aq40 twin emperors warlock tank"] = &RaidAq40ActionContext::twin_emperors_warlock_tank;
    }

private:
    static Action* choose_target(PlayerbotAI* botAI) { return new Aq40ChooseTargetAction(botAI); }
    static Action* skeram_acquire_platform_target(PlayerbotAI* botAI)
    {
        return new Aq40SkeramAcquirePlatformTargetAction(botAI);
    }
    static Action* skeram_interrupt(PlayerbotAI* botAI) { return new Aq40SkeramInterruptAction(botAI); }
    static Action* skeram_focus_real_boss(PlayerbotAI* botAI) { return new Aq40SkeramFocusRealBossAction(botAI); }
    static Action* skeram_control_mind_control(PlayerbotAI* botAI)
    {
        return new Aq40SkeramControlMindControlAction(botAI);
    }
    static Action* sartura_choose_target(PlayerbotAI* botAI) { return new Aq40SarturaChooseTargetAction(botAI); }
    static Action* sartura_avoid_whirlwind(PlayerbotAI* botAI)
    {
        return new Aq40SarturaAvoidWhirlwindAction(botAI);
    }
    static Action* fankriss_choose_target(PlayerbotAI* botAI) { return new Aq40FankrissChooseTargetAction(botAI); }
    static Action* huhuran_choose_target(PlayerbotAI* botAI) { return new Aq40HuhuranChooseTargetAction(botAI); }
    static Action* huhuran_poison_spread(PlayerbotAI* botAI) { return new Aq40HuhuranPoisonSpreadAction(botAI); }
    static Action* twin_emperors_choose_target(PlayerbotAI* botAI)
    {
        return new Aq40TwinEmperorsChooseTargetAction(botAI);
    }
    static Action* twin_emperors_hold_split(PlayerbotAI* botAI) { return new Aq40TwinEmperorsHoldSplitAction(botAI); }
    static Action* twin_emperors_warlock_tank(PlayerbotAI* botAI)
    {
        return new Aq40TwinEmperorsWarlockTankAction(botAI);
    }
};

#endif
