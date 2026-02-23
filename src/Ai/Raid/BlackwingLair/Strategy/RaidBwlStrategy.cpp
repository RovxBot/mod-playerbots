#include "RaidBwlStrategy.h"

#include "RaidBwlMultipliers.h"
#include "Strategy.h"

void RaidBwlStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode("bwl missing onyxia scale cloak",
        { NextAction("bwl warn onyxia scale cloak", ACTION_RAID) }));

    triggers.push_back(new TriggerNode("bwl razorgore encounter",
        { NextAction("bwl razorgore choose target", ACTION_RAID + 1) }));

    triggers.push_back(new TriggerNode("bwl vaelastrasz encounter",
        { NextAction("bwl vaelastrasz choose target", ACTION_RAID + 1) }));

    triggers.push_back(new TriggerNode("bwl vaelastrasz positioning",
        { NextAction("bwl vaelastrasz position", ACTION_RAID + 1) }));

    triggers.push_back(new TriggerNode("bwl vaelastrasz burning adrenaline self",
        { NextAction("move from group", ACTION_EMERGENCY + 6) }));

    triggers.push_back(new TriggerNode("bwl vaelastrasz main tank burning adrenaline",
        { NextAction("taunt spell", ACTION_EMERGENCY + 6) }));

    triggers.push_back(new TriggerNode("bwl broodlord encounter",
        { NextAction("bwl broodlord choose target", ACTION_RAID + 1) }));

    triggers.push_back(new TriggerNode("bwl broodlord positioning",
        { NextAction("bwl broodlord position", ACTION_RAID + 1) }));

    triggers.push_back(new TriggerNode("bwl broodlord main tank mortal strike",
        { NextAction("bwl broodlord choose target", ACTION_EMERGENCY + 5) }));

    triggers.push_back(new TriggerNode("bwl firemaw encounter",
        { NextAction("bwl firemaw choose target", ACTION_RAID + 1) }));

    triggers.push_back(new TriggerNode("bwl firemaw positioning",
        { NextAction("bwl firemaw position", ACTION_RAID + 1) }));

    triggers.push_back(new TriggerNode("bwl firemaw high flame buffet",
        { NextAction("move from group", ACTION_EMERGENCY + 6) }));

    triggers.push_back(new TriggerNode("bwl firemaw main tank high flame buffet",
        { NextAction("bwl firemaw choose target", ACTION_EMERGENCY + 5) }));

    triggers.push_back(new TriggerNode("bwl ebonroc encounter",
        { NextAction("bwl ebonroc choose target", ACTION_RAID + 1) }));

    triggers.push_back(new TriggerNode("bwl ebonroc positioning",
        { NextAction("bwl ebonroc position", ACTION_RAID + 1) }));

    triggers.push_back(new TriggerNode("bwl ebonroc main tank shadow",
        { NextAction("taunt spell", ACTION_EMERGENCY + 6), NextAction("bwl ebonroc choose target", ACTION_EMERGENCY + 5) }));

    triggers.push_back(new TriggerNode("bwl flamegor encounter",
        { NextAction("bwl flamegor choose target", ACTION_RAID + 1) }));

    triggers.push_back(new TriggerNode("bwl flamegor positioning",
        { NextAction("bwl flamegor position", ACTION_RAID + 1) }));

    triggers.push_back(new TriggerNode("bwl flamegor frenzy",
        { NextAction("bwl flamegor tranq", ACTION_EMERGENCY + 7) }));

    triggers.push_back(new TriggerNode("bwl chromaggus encounter",
        { NextAction("bwl chromaggus choose target", ACTION_RAID + 1) }));

    triggers.push_back(new TriggerNode("bwl chromaggus positioning",
        { NextAction("bwl chromaggus position", ACTION_RAID + 1) }));

    triggers.push_back(new TriggerNode("bwl chromaggus frenzy",
        { NextAction("bwl chromaggus tranq", ACTION_EMERGENCY + 7) }));

    triggers.push_back(new TriggerNode("bwl chromaggus breath los",
        { NextAction("bwl chromaggus los hide", ACTION_EMERGENCY + 7) }));

    triggers.push_back(new TriggerNode("bwl chromaggus main tank time lapse",
        { NextAction("taunt spell", ACTION_EMERGENCY + 6), NextAction("bwl chromaggus choose target", ACTION_EMERGENCY + 5) }));

    triggers.push_back(new TriggerNode("bwl nefarian phase one",
        { NextAction("bwl nefarian phase one choose target", ACTION_RAID + 1) }));

    triggers.push_back(new TriggerNode("bwl nefarian phase one tunnel positioning",
        { NextAction("bwl nefarian phase one tunnel position", ACTION_RAID + 1) }));

    triggers.push_back(new TriggerNode("bwl nefarian phase two",
        { NextAction("bwl nefarian phase two choose target", ACTION_RAID + 1) }));

    triggers.push_back(new TriggerNode("bwl nefarian phase two positioning",
        { NextAction("bwl nefarian phase two position", ACTION_RAID + 1) }));

    triggers.push_back(new TriggerNode("bwl suppression device",
                        { NextAction("bwl turn off suppression device", ACTION_RAID) }));

    triggers.push_back(new TriggerNode("bwl affliction bronze",
        { NextAction("bwl use hourglass sand", ACTION_RAID) }));
}

void RaidBwlStrategy::InitMultipliers(std::vector<Multiplier*>& multipliers)
{
    multipliers.push_back(new BwlGenericMultiplier(botAI));
}
