#ifndef _PLAYERBOT_RAIDBWLACTIONS_H
#define _PLAYERBOT_RAIDBWLACTIONS_H

#include "Action.h"
#include "AttackAction.h"
#include "GenericActions.h"
#include "MovementActions.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "RaidBwlSpellIds.h"

class BwlWarnOnyxiaScaleCloakAction : public Action
{
public:
    BwlWarnOnyxiaScaleCloakAction(PlayerbotAI* botAI) : Action(botAI, "bwl warn onyxia scale cloak") {}
    bool Execute(Event event) override;
};

class BwlRazorgoreChooseTargetAction : public AttackAction
{
public:
    BwlRazorgoreChooseTargetAction(PlayerbotAI* botAI) : AttackAction(botAI, "bwl razorgore choose target") {}
    bool Execute(Event event) override;
};

class BwlVaelastraszChooseTargetAction : public AttackAction
{
public:
    BwlVaelastraszChooseTargetAction(PlayerbotAI* botAI) : AttackAction(botAI, "bwl vaelastrasz choose target") {}
    bool Execute(Event event) override;
};

class BwlVaelastraszPositionAction : public MovementAction
{
public:
    BwlVaelastraszPositionAction(PlayerbotAI* botAI) : MovementAction(botAI, "bwl vaelastrasz position") {}
    bool Execute(Event event) override;
};

class BwlBroodlordChooseTargetAction : public AttackAction
{
public:
    BwlBroodlordChooseTargetAction(PlayerbotAI* botAI) : AttackAction(botAI, "bwl broodlord choose target") {}
    bool Execute(Event event) override;
};

class BwlBroodlordPositionAction : public MovementAction
{
public:
    BwlBroodlordPositionAction(PlayerbotAI* botAI) : MovementAction(botAI, "bwl broodlord position") {}
    bool Execute(Event event) override;
};

class BwlFiremawChooseTargetAction : public AttackAction
{
public:
    BwlFiremawChooseTargetAction(PlayerbotAI* botAI) : AttackAction(botAI, "bwl firemaw choose target") {}
    bool Execute(Event event) override;
};

class BwlFiremawPositionAction : public MovementAction
{
public:
    BwlFiremawPositionAction(PlayerbotAI* botAI) : MovementAction(botAI, "bwl firemaw position") {}
    bool Execute(Event event) override;
};

class BwlEbonrocChooseTargetAction : public AttackAction
{
public:
    BwlEbonrocChooseTargetAction(PlayerbotAI* botAI) : AttackAction(botAI, "bwl ebonroc choose target") {}
    bool Execute(Event event) override;
};

class BwlEbonrocPositionAction : public MovementAction
{
public:
    BwlEbonrocPositionAction(PlayerbotAI* botAI) : MovementAction(botAI, "bwl ebonroc position") {}
    bool Execute(Event event) override;
};

class BwlFlamegorChooseTargetAction : public AttackAction
{
public:
    BwlFlamegorChooseTargetAction(PlayerbotAI* botAI) : AttackAction(botAI, "bwl flamegor choose target") {}
    bool Execute(Event event) override;
};

class BwlFlamegorPositionAction : public MovementAction
{
public:
    BwlFlamegorPositionAction(PlayerbotAI* botAI) : MovementAction(botAI, "bwl flamegor position") {}
    bool Execute(Event event) override;
};

class BwlFlamegorTranqAction : public Action
{
public:
    BwlFlamegorTranqAction(PlayerbotAI* botAI) : Action(botAI, "bwl flamegor tranq") {}
    bool Execute(Event event) override;
    bool isUseful() override;
};

class BwlChromaggusChooseTargetAction : public AttackAction
{
public:
    BwlChromaggusChooseTargetAction(PlayerbotAI* botAI) : AttackAction(botAI, "bwl chromaggus choose target") {}
    bool Execute(Event event) override;
};

class BwlChromaggusPositionAction : public MovementAction
{
public:
    BwlChromaggusPositionAction(PlayerbotAI* botAI) : MovementAction(botAI, "bwl chromaggus position") {}
    bool Execute(Event event) override;
};

class BwlChromaggusTranqAction : public Action
{
public:
    BwlChromaggusTranqAction(PlayerbotAI* botAI) : Action(botAI, "bwl chromaggus tranq") {}
    bool Execute(Event event) override;
    bool isUseful() override;
};

class BwlChromaggusLosHideAction : public MovementAction
{
public:
    BwlChromaggusLosHideAction(PlayerbotAI* botAI) : MovementAction(botAI, "bwl chromaggus los hide") {}
    bool Execute(Event event) override;
};

class BwlNefarianPhaseOneChooseTargetAction : public AttackAction
{
public:
    BwlNefarianPhaseOneChooseTargetAction(PlayerbotAI* botAI) : AttackAction(botAI, "bwl nefarian phase one choose target") {}
    bool Execute(Event event) override;
};

class BwlNefarianPhaseOneTunnelPositionAction : public MovementAction
{
public:
    BwlNefarianPhaseOneTunnelPositionAction(PlayerbotAI* botAI)
        : MovementAction(botAI, "bwl nefarian phase one tunnel position")
    {
    }
    bool Execute(Event event) override;
};

class BwlNefarianPhaseTwoChooseTargetAction : public AttackAction
{
public:
    BwlNefarianPhaseTwoChooseTargetAction(PlayerbotAI* botAI) : AttackAction(botAI, "bwl nefarian phase two choose target") {}
    bool Execute(Event event) override;
};

class BwlNefarianPhaseTwoPositionAction : public MovementAction
{
public:
    BwlNefarianPhaseTwoPositionAction(PlayerbotAI* botAI) : MovementAction(botAI, "bwl nefarian phase two position") {}
    bool Execute(Event event) override;
};

class BwlTrashChooseTargetAction : public AttackAction
{
public:
    BwlTrashChooseTargetAction(PlayerbotAI* botAI) : AttackAction(botAI, "bwl trash choose target") {}
    bool Execute(Event event) override;
};

class BwlTrashTranqSeetherAction : public Action
{
public:
    BwlTrashTranqSeetherAction(PlayerbotAI* botAI) : Action(botAI, "bwl trash tranq seether") {}
    bool Execute(Event event) override;
    bool isUseful() override;
};

class BwlTurnOffSuppressionDeviceAction : public Action
{
public:
    BwlTurnOffSuppressionDeviceAction(PlayerbotAI* botAI) : Action(botAI, "bwl turn off suppression device") {}
    bool Execute(Event event) override;
    bool isUseful() override;
};

class BwlUseHourglassSandAction : public Action
{
public:
    BwlUseHourglassSandAction(PlayerbotAI* botAI) : Action(botAI, "bwl use hourglass sand") {}
    bool Execute(Event event) override;
    bool isUseful() override;
};

#endif
