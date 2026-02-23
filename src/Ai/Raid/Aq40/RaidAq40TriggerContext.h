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
        creators["aq40 fankriss active"] = &RaidAq40TriggerContext::fankriss_active;
        creators["aq40 fankriss spawn active"] = &RaidAq40TriggerContext::fankriss_spawn_active;
        creators["aq40 huhuran active"] = &RaidAq40TriggerContext::huhuran_active;
        creators["aq40 huhuran poison phase"] = &RaidAq40TriggerContext::huhuran_poison_phase;
        creators["aq40 twin emperors active"] = &RaidAq40TriggerContext::twin_emperors_active;
        creators["aq40 twin emperors role mismatch"] = &RaidAq40TriggerContext::twin_emperors_role_mismatch;
        creators["aq40 twin emperors arcane burst risk"] =
            &RaidAq40TriggerContext::twin_emperors_arcane_burst_risk;
        creators["aq40 cthun active"] = &RaidAq40TriggerContext::cthun_active;
        creators["aq40 cthun phase2"] = &RaidAq40TriggerContext::cthun_phase2;
        creators["aq40 cthun adds present"] = &RaidAq40TriggerContext::cthun_adds_present;
        creators["aq40 cthun dark glare"] = &RaidAq40TriggerContext::cthun_dark_glare;
        creators["aq40 cthun in stomach"] = &RaidAq40TriggerContext::cthun_in_stomach;
        creators["aq40 cthun vulnerable"] = &RaidAq40TriggerContext::cthun_vulnerable;
        creators["aq40 cthun eye cast"] = &RaidAq40TriggerContext::cthun_eye_cast;
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
    static Trigger* fankriss_active(PlayerbotAI* botAI) { return new Aq40FankrissActiveTrigger(botAI); }
    static Trigger* fankriss_spawn_active(PlayerbotAI* botAI) { return new Aq40FankrissSpawnedTrigger(botAI); }
    static Trigger* huhuran_active(PlayerbotAI* botAI) { return new Aq40HuhuranActiveTrigger(botAI); }
    static Trigger* huhuran_poison_phase(PlayerbotAI* botAI) { return new Aq40HuhuranPoisonPhaseTrigger(botAI); }
    static Trigger* twin_emperors_active(PlayerbotAI* botAI) { return new Aq40TwinEmperorsActiveTrigger(botAI); }
    static Trigger* twin_emperors_role_mismatch(PlayerbotAI* botAI)
    {
        return new Aq40TwinEmperorsRoleMismatchTrigger(botAI);
    }
    static Trigger* twin_emperors_arcane_burst_risk(PlayerbotAI* botAI)
    {
        return new Aq40TwinEmperorsArcaneBurstRiskTrigger(botAI);
    }
    static Trigger* cthun_active(PlayerbotAI* botAI) { return new Aq40CthunActiveTrigger(botAI); }
    static Trigger* cthun_phase2(PlayerbotAI* botAI) { return new Aq40CthunPhase2Trigger(botAI); }
    static Trigger* cthun_adds_present(PlayerbotAI* botAI) { return new Aq40CthunAddsPresentTrigger(botAI); }
    static Trigger* cthun_dark_glare(PlayerbotAI* botAI) { return new Aq40CthunDarkGlareTrigger(botAI); }
    static Trigger* cthun_in_stomach(PlayerbotAI* botAI) { return new Aq40CthunInStomachTrigger(botAI); }
    static Trigger* cthun_vulnerable(PlayerbotAI* botAI) { return new Aq40CthunVulnerableTrigger(botAI); }
    static Trigger* cthun_eye_cast(PlayerbotAI* botAI) { return new Aq40CthunEyeCastTrigger(botAI); }
};

#endif
