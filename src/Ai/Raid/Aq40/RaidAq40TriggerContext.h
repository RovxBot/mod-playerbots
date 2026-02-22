#ifndef _PLAYERBOT_RAIDAQ40TRIGGERCONTEXT_H
#define _PLAYERBOT_RAIDAQ40TRIGGERCONTEXT_H

#include "AiObjectContext.h"
#include "NamedObjectContext.h"
#include "RaidAq40Triggers.h"

class RaidAq40TriggerContext : public NamedObjectContext<Trigger>
{
public:
    RaidAq40TriggerContext()
    {
        creators["aq40 engage"] = &RaidAq40TriggerContext::engage;
    }

private:
    static Trigger* engage(PlayerbotAI* botAI) { return new Aq40EngageTrigger(botAI); }
};

#endif
