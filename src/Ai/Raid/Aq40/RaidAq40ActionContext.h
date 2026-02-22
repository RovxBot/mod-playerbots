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
    }

private:
    static Action* choose_target(PlayerbotAI* botAI) { return new Aq40ChooseTargetAction(botAI); }
};

#endif
