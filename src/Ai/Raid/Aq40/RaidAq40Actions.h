#ifndef _PLAYERBOT_RAIDAQ40ACTIONS_H
#define _PLAYERBOT_RAIDAQ40ACTIONS_H

#include <initializer_list>
#include <vector>

#include "AttackAction.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"

namespace Aq40BossActions
{
Unit* FindUnitByAnyName(PlayerbotAI* botAI, GuidVector const& attackers, std::initializer_list<char const*> names);
std::vector<Unit*> FindUnitsByAnyName(PlayerbotAI* botAI, GuidVector const& attackers, std::initializer_list<char const*> names);
Unit* FindSkeramTarget(PlayerbotAI* botAI, GuidVector const& attackers);
Unit* FindBugTrioTarget(PlayerbotAI* botAI, GuidVector const& attackers);
Unit* FindSarturaTarget(PlayerbotAI* botAI, GuidVector const& attackers);
Unit* FindFankrissTarget(PlayerbotAI* botAI, GuidVector const& attackers);
Unit* FindHuhuranTarget(PlayerbotAI* botAI, GuidVector const& attackers);
Unit* FindTwinEmperorsTarget(PlayerbotAI* botAI, GuidVector const& attackers);
Unit* FindOuroTarget(PlayerbotAI* botAI, GuidVector const& attackers);
Unit* FindCthunTarget(PlayerbotAI* botAI, GuidVector const& attackers);
Unit* FindViscidusTarget(PlayerbotAI* botAI, GuidVector const& attackers);
}  // namespace Aq40BossActions

class Aq40ChooseTargetAction : public AttackAction
{
public:
    Aq40ChooseTargetAction(PlayerbotAI* botAI) : AttackAction(botAI, "aq40 choose target") {}
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

#endif
