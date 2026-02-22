#include "RaidAq40Strategy.h"

#include "RaidAq40Multipliers.h"

void RaidAq40Strategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode("aq40 engage", { NextAction("aq40 choose target", ACTION_RAID + 1) }));

    // Skeram baseline strategy:
    // - quickly reacquire target after blink/teleport
    // - prioritize interrupt windows
    // - respond to mind control pressure
    // - split-phase handling and execute-phase focus
    triggers.push_back(new TriggerNode("aq40 skeram active",
        { NextAction("aq40 skeram acquire platform target", ACTION_RAID + 2) }));
    triggers.push_back(new TriggerNode("aq40 skeram blinked",
        { NextAction("aq40 skeram acquire platform target", ACTION_RAID + 3) }));
    triggers.push_back(new TriggerNode("aq40 skeram interrupt cast",
        { NextAction("aq40 skeram interrupt", ACTION_RAID + 4) }));
    triggers.push_back(new TriggerNode("aq40 skeram mc detected",
        { NextAction("aq40 skeram control mind control", ACTION_RAID + 3) }));
    triggers.push_back(new TriggerNode("aq40 skeram split active",
        { NextAction("aq40 skeram interrupt", ACTION_RAID + 3) }));
    triggers.push_back(new TriggerNode("aq40 skeram execute phase",
        { NextAction("aq40 skeram focus real boss", ACTION_RAID + 4) }));

    // Sartura baseline strategy:
    // - prioritize royal guards before boss
    // - non-tanks step out during whirlwind windows
    triggers.push_back(new TriggerNode("aq40 sartura active",
        { NextAction("aq40 sartura choose target", ACTION_RAID + 2) }));
    triggers.push_back(new TriggerNode("aq40 sartura whirlwind",
        { NextAction("aq40 sartura avoid whirlwind", ACTION_RAID + 4) }));
}

void RaidAq40Strategy::InitMultipliers(std::vector<Multiplier*>& multipliers)
{
    multipliers.push_back(new Aq40GenericMultiplier(botAI));
}
