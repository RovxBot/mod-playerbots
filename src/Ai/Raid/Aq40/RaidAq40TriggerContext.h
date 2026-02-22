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
        creators["aq40 skeram active"] = &RaidAq40TriggerContext::skeram_active;
        creators["aq40 skeram blinked"] = &RaidAq40TriggerContext::skeram_blinked;
        creators["aq40 skeram interrupt cast"] = &RaidAq40TriggerContext::skeram_interrupt_cast;
        creators["aq40 skeram mc detected"] = &RaidAq40TriggerContext::skeram_mc_detected;
        creators["aq40 skeram split active"] = &RaidAq40TriggerContext::skeram_split_active;
        creators["aq40 skeram execute phase"] = &RaidAq40TriggerContext::skeram_execute_phase;
        creators["aq40 sartura active"] = &RaidAq40TriggerContext::sartura_active;
        creators["aq40 sartura whirlwind"] = &RaidAq40TriggerContext::sartura_whirlwind;
    }

private:
    static Trigger* engage(PlayerbotAI* botAI) { return new Aq40EngageTrigger(botAI); }
    static Trigger* skeram_active(PlayerbotAI* botAI) { return new Aq40SkeramActiveTrigger(botAI); }
    static Trigger* skeram_blinked(PlayerbotAI* botAI) { return new Aq40SkeramBlinkTrigger(botAI); }
    static Trigger* skeram_interrupt_cast(PlayerbotAI* botAI) { return new Aq40SkeramArcaneExplosionTrigger(botAI); }
    static Trigger* skeram_mc_detected(PlayerbotAI* botAI) { return new Aq40SkeramMindControlTrigger(botAI); }
    static Trigger* skeram_split_active(PlayerbotAI* botAI) { return new Aq40SkeramSplitTrigger(botAI); }
    static Trigger* skeram_execute_phase(PlayerbotAI* botAI) { return new Aq40SkeramExecutePhaseTrigger(botAI); }
    static Trigger* sartura_active(PlayerbotAI* botAI) { return new Aq40SarturaActiveTrigger(botAI); }
    static Trigger* sartura_whirlwind(PlayerbotAI* botAI) { return new Aq40SarturaWhirlwindTrigger(botAI); }
};

#endif
