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
        creators["aq40 bug trio choose target"] = &RaidAq40ActionContext::bug_trio_choose_target;
        creators["aq40 bug trio avoid poison cloud"] = &RaidAq40ActionContext::bug_trio_avoid_poison_cloud;
        creators["aq40 fankriss choose target"] = &RaidAq40ActionContext::fankriss_choose_target;
        creators["aq40 trash choose target"] = &RaidAq40ActionContext::trash_choose_target;
        creators["aq40 trash avoid dangerous aoe"] = &RaidAq40ActionContext::trash_avoid_dangerous_aoe;
        creators["aq40 huhuran choose target"] = &RaidAq40ActionContext::huhuran_choose_target;
        creators["aq40 huhuran poison spread"] = &RaidAq40ActionContext::huhuran_poison_spread;
        creators["aq40 twin emperors choose target"] = &RaidAq40ActionContext::twin_emperors_choose_target;
        creators["aq40 twin emperors hold split"] = &RaidAq40ActionContext::twin_emperors_hold_split;
        creators["aq40 twin emperors warlock tank"] = &RaidAq40ActionContext::twin_emperors_warlock_tank;
        creators["aq40 twin emperors avoid arcane burst"] =
            &RaidAq40ActionContext::twin_emperors_avoid_arcane_burst;
        creators["aq40 twin emperors enforce separation"] =
            &RaidAq40ActionContext::twin_emperors_enforce_separation;
        creators["aq40 ouro choose target"] = &RaidAq40ActionContext::ouro_choose_target;
        creators["aq40 ouro hold melee contact"] = &RaidAq40ActionContext::ouro_hold_melee_contact;
        creators["aq40 ouro avoid sweep"] = &RaidAq40ActionContext::ouro_avoid_sweep;
        creators["aq40 ouro avoid submerge"] = &RaidAq40ActionContext::ouro_avoid_submerge;
        creators["aq40 viscidus choose target"] = &RaidAq40ActionContext::viscidus_choose_target;
        creators["aq40 viscidus use frost"] = &RaidAq40ActionContext::viscidus_use_frost;
        creators["aq40 viscidus shatter"] = &RaidAq40ActionContext::viscidus_shatter;
        creators["aq40 cthun choose target"] = &RaidAq40ActionContext::cthun_choose_target;
        creators["aq40 cthun maintain spread"] = &RaidAq40ActionContext::cthun_maintain_spread;
        creators["aq40 cthun avoid dark glare"] = &RaidAq40ActionContext::cthun_avoid_dark_glare;
        creators["aq40 cthun stomach dps"] = &RaidAq40ActionContext::cthun_stomach_dps;
        creators["aq40 cthun stomach exit"] = &RaidAq40ActionContext::cthun_stomach_exit;
        creators["aq40 cthun phase2 add priority"] = &RaidAq40ActionContext::cthun_phase2_add_priority;
        creators["aq40 cthun vulnerable burst"] = &RaidAq40ActionContext::cthun_vulnerable_burst;
        creators["aq40 cthun interrupt eye"] = &RaidAq40ActionContext::cthun_interrupt_eye;
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
    static Action* bug_trio_choose_target(PlayerbotAI* botAI) { return new Aq40BugTrioChooseTargetAction(botAI); }
    static Action* bug_trio_avoid_poison_cloud(PlayerbotAI* botAI)
    {
        return new Aq40BugTrioAvoidPoisonCloudAction(botAI);
    }
    static Action* fankriss_choose_target(PlayerbotAI* botAI) { return new Aq40FankrissChooseTargetAction(botAI); }
    static Action* trash_choose_target(PlayerbotAI* botAI) { return new Aq40TrashChooseTargetAction(botAI); }
    static Action* trash_avoid_dangerous_aoe(PlayerbotAI* botAI)
    {
        return new Aq40TrashAvoidDangerousAoeAction(botAI);
    }
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
    static Action* twin_emperors_avoid_arcane_burst(PlayerbotAI* botAI)
    {
        return new Aq40TwinEmperorsAvoidArcaneBurstAction(botAI);
    }
    static Action* twin_emperors_enforce_separation(PlayerbotAI* botAI)
    {
        return new Aq40TwinEmperorsEnforceSeparationAction(botAI);
    }
    static Action* ouro_choose_target(PlayerbotAI* botAI) { return new Aq40OuroChooseTargetAction(botAI); }
    static Action* ouro_hold_melee_contact(PlayerbotAI* botAI)
    {
        return new Aq40OuroHoldMeleeContactAction(botAI);
    }
    static Action* ouro_avoid_sweep(PlayerbotAI* botAI) { return new Aq40OuroAvoidSweepAction(botAI); }
    static Action* ouro_avoid_submerge(PlayerbotAI* botAI)
    {
        return new Aq40OuroAvoidSubmergeAction(botAI);
    }
    static Action* viscidus_choose_target(PlayerbotAI* botAI) { return new Aq40ViscidusChooseTargetAction(botAI); }
    static Action* viscidus_use_frost(PlayerbotAI* botAI) { return new Aq40ViscidusUseFrostAction(botAI); }
    static Action* viscidus_shatter(PlayerbotAI* botAI) { return new Aq40ViscidusShatterAction(botAI); }
    static Action* cthun_choose_target(PlayerbotAI* botAI) { return new Aq40CthunChooseTargetAction(botAI); }
    static Action* cthun_maintain_spread(PlayerbotAI* botAI) { return new Aq40CthunMaintainSpreadAction(botAI); }
    static Action* cthun_avoid_dark_glare(PlayerbotAI* botAI) { return new Aq40CthunAvoidDarkGlareAction(botAI); }
    static Action* cthun_stomach_dps(PlayerbotAI* botAI) { return new Aq40CthunStomachDpsAction(botAI); }
    static Action* cthun_stomach_exit(PlayerbotAI* botAI) { return new Aq40CthunStomachExitAction(botAI); }
    static Action* cthun_phase2_add_priority(PlayerbotAI* botAI)
    {
        return new Aq40CthunPhase2AddPriorityAction(botAI);
    }
    static Action* cthun_vulnerable_burst(PlayerbotAI* botAI)
    {
        return new Aq40CthunVulnerableBurstAction(botAI);
    }
    static Action* cthun_interrupt_eye(PlayerbotAI* botAI) { return new Aq40CthunInterruptEyeAction(botAI); }
};

#endif
