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

    // Fankriss baseline strategy:
    // - kill Spawn of Fankriss immediately
    // - fall back to boss pressure between spawn waves
    triggers.push_back(new TriggerNode("aq40 fankriss active",
        { NextAction("aq40 fankriss choose target", ACTION_RAID + 2) }));
    triggers.push_back(new TriggerNode("aq40 fankriss spawn active",
        { NextAction("aq40 fankriss choose target", ACTION_RAID + 4) }));

    // Huhuran baseline strategy:
    // - maintain boss focus
    // - move ranged non-tanks outward during poison/enrage phase
    triggers.push_back(new TriggerNode("aq40 huhuran active",
        { NextAction("aq40 huhuran choose target", ACTION_RAID + 2) }));
    triggers.push_back(new TriggerNode("aq40 huhuran poison phase",
        { NextAction("aq40 huhuran poison spread", ACTION_RAID + 4) }));

    // Twin Emperors baseline strategy:
    // - tanks/melee favor Vek'nilash, ranged non-tanks favor Vek'lor
    // - recover target assignment quickly after teleport/target drift
    triggers.push_back(new TriggerNode("aq40 twin emperors active",
        {
            NextAction("aq40 twin emperors choose target", ACTION_RAID + 2),
            NextAction("aq40 twin emperors hold split", ACTION_RAID + 3),
            NextAction("aq40 twin emperors warlock tank", ACTION_RAID + 4),
        }));
    triggers.push_back(new TriggerNode("aq40 twin emperors role mismatch",
        {
            NextAction("aq40 twin emperors choose target", ACTION_RAID + 5),
            NextAction("aq40 twin emperors hold split", ACTION_RAID + 4),
        }));
    triggers.push_back(new TriggerNode("aq40 twin emperors arcane burst risk",
        { NextAction("aq40 twin emperors avoid arcane burst", ACTION_RAID + 5) }));
    triggers.push_back(new TriggerNode("aq40 twin emperors need separation",
        { NextAction("aq40 twin emperors enforce separation", ACTION_RAID + 6) }));

    // C'Thun pass 1 strategy:
    // - maintain spread and add kill priority
    // - react to Dark Glare with lateral movement
    // - stomach team kills Flesh Tentacle and exits on high acid stacks
    triggers.push_back(new TriggerNode("aq40 cthun active",
        {
            NextAction("aq40 cthun choose target", ACTION_RAID + 2),
            NextAction("aq40 cthun maintain spread", ACTION_RAID + 3),
        }));
    triggers.push_back(new TriggerNode("aq40 cthun phase2",
        { NextAction("aq40 cthun phase2 add priority", ACTION_RAID + 3) }));
    triggers.push_back(new TriggerNode("aq40 cthun adds present",
        { NextAction("aq40 cthun phase2 add priority", ACTION_RAID + 4) }));
    triggers.push_back(new TriggerNode("aq40 cthun dark glare",
        { NextAction("aq40 cthun avoid dark glare", ACTION_RAID + 5) }));
    triggers.push_back(new TriggerNode("aq40 cthun eye cast",
        { NextAction("aq40 cthun interrupt eye", ACTION_RAID + 5) }));
    triggers.push_back(new TriggerNode("aq40 cthun in stomach",
        {
            NextAction("aq40 cthun stomach dps", ACTION_RAID + 4),
            NextAction("aq40 cthun stomach exit", ACTION_RAID + 5),
        }));
    triggers.push_back(new TriggerNode("aq40 cthun vulnerable",
        { NextAction("aq40 cthun vulnerable burst", ACTION_RAID + 4) }));
}

void RaidAq40Strategy::InitMultipliers(std::vector<Multiplier*>& multipliers)
{
    multipliers.push_back(new Aq40GenericMultiplier(botAI));
}
