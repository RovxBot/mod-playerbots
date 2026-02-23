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
