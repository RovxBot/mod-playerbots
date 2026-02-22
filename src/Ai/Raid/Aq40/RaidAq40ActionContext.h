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
};

#endif
