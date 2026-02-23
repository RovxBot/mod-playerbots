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
        creators["aq40 bug trio active"] = &RaidAq40TriggerContext::bug_trio_active;
        creators["aq40 bug trio heal cast"] = &RaidAq40TriggerContext::bug_trio_heal_cast;
        creators["aq40 bug trio poison cloud"] = &RaidAq40TriggerContext::bug_trio_poison_cloud;
        creators["aq40 fankriss active"] = &RaidAq40TriggerContext::fankriss_active;
        creators["aq40 fankriss spawn active"] = &RaidAq40TriggerContext::fankriss_spawn_active;
        creators["aq40 trash active"] = &RaidAq40TriggerContext::trash_active;
        creators["aq40 trash dangerous aoe"] = &RaidAq40TriggerContext::trash_dangerous_aoe;
        creators["aq40 huhuran active"] = &RaidAq40TriggerContext::huhuran_active;
        creators["aq40 huhuran poison phase"] = &RaidAq40TriggerContext::huhuran_poison_phase;
        creators["aq40 twin emperors active"] = &RaidAq40TriggerContext::twin_emperors_active;
        creators["aq40 twin emperors role mismatch"] = &RaidAq40TriggerContext::twin_emperors_role_mismatch;
        creators["aq40 twin emperors arcane burst risk"] =
            &RaidAq40TriggerContext::twin_emperors_arcane_burst_risk;
        creators["aq40 twin emperors need separation"] =
            &RaidAq40TriggerContext::twin_emperors_need_separation;
        creators["aq40 ouro active"] = &RaidAq40TriggerContext::ouro_active;
        creators["aq40 ouro scarabs present"] = &RaidAq40TriggerContext::ouro_scarabs_present;
        creators["aq40 ouro sweep risk"] = &RaidAq40TriggerContext::ouro_sweep_risk;
        creators["aq40 ouro submerge hazard"] = &RaidAq40TriggerContext::ouro_submerge_hazard;
        creators["aq40 viscidus active"] = &RaidAq40TriggerContext::viscidus_active;
        creators["aq40 viscidus frozen"] = &RaidAq40TriggerContext::viscidus_frozen;
        creators["aq40 viscidus globs present"] = &RaidAq40TriggerContext::viscidus_globs_present;
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
    static Trigger* bug_trio_active(PlayerbotAI* botAI) { return new Aq40BugTrioActiveTrigger(botAI); }
    static Trigger* bug_trio_heal_cast(PlayerbotAI* botAI) { return new Aq40BugTrioHealCastTrigger(botAI); }
    static Trigger* bug_trio_poison_cloud(PlayerbotAI* botAI) { return new Aq40BugTrioPoisonCloudTrigger(botAI); }
    static Trigger* fankriss_active(PlayerbotAI* botAI) { return new Aq40FankrissActiveTrigger(botAI); }
    static Trigger* fankriss_spawn_active(PlayerbotAI* botAI) { return new Aq40FankrissSpawnedTrigger(botAI); }
    static Trigger* trash_active(PlayerbotAI* botAI) { return new Aq40TrashActiveTrigger(botAI); }
    static Trigger* trash_dangerous_aoe(PlayerbotAI* botAI) { return new Aq40TrashDangerousAoeTrigger(botAI); }
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
    static Trigger* twin_emperors_need_separation(PlayerbotAI* botAI)
    {
        return new Aq40TwinEmperorsNeedSeparationTrigger(botAI);
    }
    static Trigger* ouro_active(PlayerbotAI* botAI) { return new Aq40OuroActiveTrigger(botAI); }
    static Trigger* ouro_scarabs_present(PlayerbotAI* botAI) { return new Aq40OuroScarabsTrigger(botAI); }
    static Trigger* ouro_sweep_risk(PlayerbotAI* botAI) { return new Aq40OuroSweepTrigger(botAI); }
    static Trigger* ouro_submerge_hazard(PlayerbotAI* botAI) { return new Aq40OuroSubmergeTrigger(botAI); }
    static Trigger* viscidus_active(PlayerbotAI* botAI) { return new Aq40ViscidusActiveTrigger(botAI); }
    static Trigger* viscidus_frozen(PlayerbotAI* botAI) { return new Aq40ViscidusFrozenTrigger(botAI); }
    static Trigger* viscidus_globs_present(PlayerbotAI* botAI) { return new Aq40ViscidusGlobTrigger(botAI); }
    static Trigger* cthun_active(PlayerbotAI* botAI) { return new Aq40CthunActiveTrigger(botAI); }
    static Trigger* cthun_phase2(PlayerbotAI* botAI) { return new Aq40CthunPhase2Trigger(botAI); }
    static Trigger* cthun_adds_present(PlayerbotAI* botAI) { return new Aq40CthunAddsPresentTrigger(botAI); }
    static Trigger* cthun_dark_glare(PlayerbotAI* botAI) { return new Aq40CthunDarkGlareTrigger(botAI); }
    static Trigger* cthun_in_stomach(PlayerbotAI* botAI) { return new Aq40CthunInStomachTrigger(botAI); }
    static Trigger* cthun_vulnerable(PlayerbotAI* botAI) { return new Aq40CthunVulnerableTrigger(botAI); }
    static Trigger* cthun_eye_cast(PlayerbotAI* botAI) { return new Aq40CthunEyeCastTrigger(botAI); }
};

#endif
