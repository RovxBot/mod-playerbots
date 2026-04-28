#ifndef _PLAYERBOT_RAIDAQ40ACTIONS_H
#define _PLAYERBOT_RAIDAQ40ACTIONS_H

#include <initializer_list>
#include <vector>

#include "AttackAction.h"
#include "MovementActions.h"
#include "../RaidAq40BossHelper.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"

namespace Aq40BossActions
{
Unit* FindUnitByAnyName(PlayerbotAI* botAI, GuidVector const& attackers, std::initializer_list<char const*> names);
std::vector<Unit*> FindUnitsByAnyName(PlayerbotAI* botAI, GuidVector const& attackers, std::initializer_list<char const*> names);
Unit* FindSkeramTarget(PlayerbotAI* botAI, GuidVector const& attackers, bool preferLowestHealth = false);
bool HasSkeramSkullTarget(PlayerbotAI* botAI);
Unit* FindBugTrioTarget(PlayerbotAI* botAI, GuidVector const& attackers);
Unit* FindSarturaTarget(PlayerbotAI* botAI, GuidVector const& attackers);
std::vector<Unit*> FindSarturaGuards(PlayerbotAI* botAI, GuidVector const& attackers);
Unit* FindFankrissTarget(PlayerbotAI* botAI, GuidVector const& attackers);
std::vector<Unit*> FindFankrissSpawns(PlayerbotAI* botAI, GuidVector const& attackers);
Unit* FindTrashTarget(PlayerbotAI* botAI, GuidVector const& attackers);
Unit* FindHuhuranTarget(PlayerbotAI* botAI, GuidVector const& attackers);
Unit* FindOuroTarget(PlayerbotAI* botAI, GuidVector const& attackers);
Unit* FindCthunTarget(PlayerbotAI* botAI, GuidVector const& attackers);
Unit* FindViscidusTarget(PlayerbotAI* botAI, GuidVector const& attackers);
}    // namespace Aq40BossActions

class Aq40ManageResistanceStrategiesAction : public Action
{
public:
    Aq40ManageResistanceStrategiesAction(PlayerbotAI* botAI)
        : Action(botAI, "aq40 manage resistance strategies")
    {
    }
    bool isUseful() override;
    bool Execute(Event event) override;
};

class Aq40EraseTimersAndTrackersAction : public Action
{
public:
    Aq40EraseTimersAndTrackersAction(PlayerbotAI* botAI)
        : Action(botAI, "aq40 erase timers and trackers")
    {
    }
    bool isUseful() override;
    bool Execute(Event event) override;
};

class Aq40SkeramAcquirePlatformTargetAction : public AttackAction
{
public:
    Aq40SkeramAcquirePlatformTargetAction(PlayerbotAI* botAI)
        : AttackAction(botAI, "aq40 skeram acquire platform target")
    {
    }
    bool Execute(Event event) override;
};

class Aq40SkeramInterruptAction : public AttackAction
{
public:
    Aq40SkeramInterruptAction(PlayerbotAI* botAI) : AttackAction(botAI, "aq40 skeram interrupt") {}
    bool Execute(Event event) override;
};

class Aq40SkeramFocusRealBossAction : public AttackAction
{
public:
    Aq40SkeramFocusRealBossAction(PlayerbotAI* botAI) : AttackAction(botAI, "aq40 skeram focus real boss") {}
    bool Execute(Event event) override;
};

class Aq40SkeramControlMindControlAction : public AttackAction
{
public:
    Aq40SkeramControlMindControlAction(PlayerbotAI* botAI)
        : AttackAction(botAI, "aq40 skeram control mind control")
    {
    }
    bool Execute(Event event) override;
};

class Aq40SarturaChooseTargetAction : public AttackAction
{
public:
    Aq40SarturaChooseTargetAction(PlayerbotAI* botAI) : AttackAction(botAI, "aq40 sartura choose target") {}
    bool Execute(Event event) override;
};

class Aq40SarturaAvoidWhirlwindAction : public MovementAction
{
public:
    Aq40SarturaAvoidWhirlwindAction(PlayerbotAI* botAI) : MovementAction(botAI, "aq40 sartura avoid whirlwind") {}
    bool Execute(Event event) override;
};

class Aq40BugTrioChooseTargetAction : public AttackAction
{
public:
    Aq40BugTrioChooseTargetAction(PlayerbotAI* botAI) : AttackAction(botAI, "aq40 bug trio choose target") {}
    bool Execute(Event event) override;
};

class Aq40BugTrioInterruptHealAction : public AttackAction
{
public:
    Aq40BugTrioInterruptHealAction(PlayerbotAI* botAI) : AttackAction(botAI, "aq40 bug trio interrupt heal") {}
    bool Execute(Event event) override;
};

class Aq40BugTrioAvoidPoisonCloudAction : public MovementAction
{
public:
    Aq40BugTrioAvoidPoisonCloudAction(PlayerbotAI* botAI)
        : MovementAction(botAI, "aq40 bug trio avoid poison cloud")
    {
    }
    bool Execute(Event event) override;
};

class Aq40FankrissChooseTargetAction : public AttackAction
{
public:
    Aq40FankrissChooseTargetAction(PlayerbotAI* botAI) : AttackAction(botAI, "aq40 fankriss choose target") {}
    bool Execute(Event event) override;
};

class Aq40FankrissTankSwapAction : public AttackAction
{
public:
    Aq40FankrissTankSwapAction(PlayerbotAI* botAI) : AttackAction(botAI, "aq40 fankriss tank swap") {}
    bool Execute(Event event) override;
};

class Aq40TrashChooseTargetAction : public AttackAction
{
public:
    Aq40TrashChooseTargetAction(PlayerbotAI* botAI) : AttackAction(botAI, "aq40 trash choose target") {}
    bool isUseful() override;
    bool Execute(Event event) override;
};

class Aq40TrashAvoidDangerousAoeAction : public MovementAction
{
public:
    Aq40TrashAvoidDangerousAoeAction(PlayerbotAI* botAI)
        : MovementAction(botAI, "aq40 trash avoid dangerous aoe")
    {
    }
    bool isUseful() override;
    bool Execute(Event event) override;
};

class Aq40HuhuranChooseTargetAction : public AttackAction
{
public:
    Aq40HuhuranChooseTargetAction(PlayerbotAI* botAI) : AttackAction(botAI, "aq40 huhuran choose target") {}
    bool Execute(Event event) override;
};

class Aq40HuhuranPoisonSpreadAction : public MovementAction
{
public:
    Aq40HuhuranPoisonSpreadAction(PlayerbotAI* botAI) : MovementAction(botAI, "aq40 huhuran poison spread") {}
    bool Execute(Event event) override;
};

class Aq40TwinEmperorsChooseTargetAction : public AttackAction
{
public:
    Aq40TwinEmperorsChooseTargetAction(PlayerbotAI* botAI)
        : AttackAction(botAI, "aq40 twin emperors choose target")
    {
    }
    bool Execute(Event event) override;
};

class Aq40TwinEmperorsHealerSupportAction : public MovementAction
{
public:
    Aq40TwinEmperorsHealerSupportAction(PlayerbotAI* botAI)
        : MovementAction(botAI, "aq40 twin emperors healer support")
    {
    }
    bool Execute(Event event) override;
};

class Aq40TwinEmperorsPostSwapHoldAction : public MovementAction
{
public:
    Aq40TwinEmperorsPostSwapHoldAction(PlayerbotAI* botAI)
        : MovementAction(botAI, "aq40 twin emperors post swap hold")
    {
    }
    bool Execute(Event event) override;
};

class Aq40TwinEmperorsDodgeBlizzardAction : public MovementAction
{
public:
    Aq40TwinEmperorsDodgeBlizzardAction(PlayerbotAI* botAI)
        : MovementAction(botAI, "aq40 twin emperors dodge blizzard")
    {
    }
    bool Execute(Event event) override;
};

class Aq40TwinEmperorsDodgeExplodeBugAction : public MovementAction
{
public:
    Aq40TwinEmperorsDodgeExplodeBugAction(PlayerbotAI* botAI)
        : MovementAction(botAI, "aq40 twin emperors dodge explode bug")
    {
    }
    bool Execute(Event event) override;
};

class Aq40TwinEmperorsAvoidVeklorAction : public MovementAction
{
public:
    Aq40TwinEmperorsAvoidVeklorAction(PlayerbotAI* botAI)
        : MovementAction(botAI, "aq40 twin emperors avoid veklor")
    {
    }
    bool Execute(Event event) override;
};

class Aq40TwinEmperorsHoldSplitAction : public AttackAction
{
public:
    Aq40TwinEmperorsHoldSplitAction(PlayerbotAI* botAI)
        : AttackAction(botAI, "aq40 twin emperors hold split")
    {
    }
    bool Execute(Event event) override;
};

class Aq40TwinEmperorsPrePullStageAction : public MovementAction
{
public:
    Aq40TwinEmperorsPrePullStageAction(PlayerbotAI* botAI)
        : MovementAction(botAI, "aq40 twin emperors pre pull stage")
    {
    }
    bool isUseful() override;
    bool Execute(Event event) override;
};

class Aq40TwinEmperorsWarlockTankAction : public AttackAction
{
public:
    Aq40TwinEmperorsWarlockTankAction(PlayerbotAI* botAI)
        : AttackAction(botAI, "aq40 twin emperors warlock tank")
    {
    }
    bool isUseful() override;
    bool Execute(Event event) override;
};


class Aq40OuroChooseTargetAction : public AttackAction
{
public:
    Aq40OuroChooseTargetAction(PlayerbotAI* botAI) : AttackAction(botAI, "aq40 ouro choose target") {}
    bool Execute(Event event) override;
};

class Aq40OuroHoldMeleeContactAction : public MovementAction
{
public:
    Aq40OuroHoldMeleeContactAction(PlayerbotAI* botAI)
        : MovementAction(botAI, "aq40 ouro hold melee contact")
    {
    }
    bool isUseful() override { return Aq40BossHelper::IsEncounterTank(bot, bot); }
    bool Execute(Event event) override;
};

class Aq40OuroAvoidSweepAction : public MovementAction
{
public:
    Aq40OuroAvoidSweepAction(PlayerbotAI* botAI) : MovementAction(botAI, "aq40 ouro avoid sweep") {}
    bool Execute(Event event) override;
};

class Aq40OuroAvoidSandBlastAction : public MovementAction
{
public:
    Aq40OuroAvoidSandBlastAction(PlayerbotAI* botAI) : MovementAction(botAI, "aq40 ouro avoid sand blast") {}
    bool Execute(Event event) override;
};

class Aq40OuroAvoidSubmergeAction : public MovementAction
{
public:
    Aq40OuroAvoidSubmergeAction(PlayerbotAI* botAI) : MovementAction(botAI, "aq40 ouro avoid submerge") {}
    bool Execute(Event event) override;
};

class Aq40ViscidusChooseTargetAction : public AttackAction
{
public:
    Aq40ViscidusChooseTargetAction(PlayerbotAI* botAI) : AttackAction(botAI, "aq40 viscidus choose target") {}
    bool Execute(Event event) override;
};

class Aq40ViscidusUseFrostAction : public AttackAction
{
public:
    Aq40ViscidusUseFrostAction(PlayerbotAI* botAI) : AttackAction(botAI, "aq40 viscidus use frost") {}
    bool Execute(Event event) override;
};

class Aq40ViscidusShatterAction : public AttackAction
{
public:
    Aq40ViscidusShatterAction(PlayerbotAI* botAI) : AttackAction(botAI, "aq40 viscidus shatter") {}
    bool Execute(Event event) override;
};

class Aq40CthunChooseTargetAction : public AttackAction
{
public:
    Aq40CthunChooseTargetAction(PlayerbotAI* botAI) : AttackAction(botAI, "aq40 cthun choose target") {}
    bool Execute(Event event) override;
};

class Aq40CthunMaintainSpreadAction : public MovementAction
{
public:
    Aq40CthunMaintainSpreadAction(PlayerbotAI* botAI) : MovementAction(botAI, "aq40 cthun maintain spread") {}
    bool Execute(Event event) override;
};

class Aq40CthunAvoidDarkGlareAction : public MovementAction
{
public:
    Aq40CthunAvoidDarkGlareAction(PlayerbotAI* botAI) : MovementAction(botAI, "aq40 cthun avoid dark glare") {}
    bool Execute(Event event) override;
};

class Aq40CthunStomachDpsAction : public AttackAction
{
public:
    Aq40CthunStomachDpsAction(PlayerbotAI* botAI) : AttackAction(botAI, "aq40 cthun stomach dps") {}
    bool Execute(Event event) override;
};

class Aq40CthunStomachExitAction : public MovementAction
{
public:
    Aq40CthunStomachExitAction(PlayerbotAI* botAI) : MovementAction(botAI, "aq40 cthun stomach exit") {}
    bool Execute(Event event) override;
};

class Aq40CthunPhase2AddPriorityAction : public AttackAction
{
public:
    Aq40CthunPhase2AddPriorityAction(PlayerbotAI* botAI)
        : AttackAction(botAI, "aq40 cthun phase2 add priority")
    {
    }
    bool Execute(Event event) override;
};

class Aq40CthunVulnerableBurstAction : public AttackAction
{
public:
    Aq40CthunVulnerableBurstAction(PlayerbotAI* botAI) : AttackAction(botAI, "aq40 cthun vulnerable burst") {}
    bool Execute(Event event) override;
};

class Aq40CthunInterruptEyeAction : public AttackAction
{
public:
    Aq40CthunInterruptEyeAction(PlayerbotAI* botAI) : AttackAction(botAI, "aq40 cthun interrupt eye") {}
    bool Execute(Event event) override;
};

#endif
