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

    triggers.push_back(new TriggerNode("bwl vaelastrasz burning adrenaline self",
        { NextAction("move from group", ACTION_EMERGENCY + 6) }));

    triggers.push_back(new TriggerNode("bwl vaelastrasz main tank burning adrenaline",
        { NextAction("taunt spell", ACTION_EMERGENCY + 6) }));

    triggers.push_back(new TriggerNode("bwl suppression device",
                        { NextAction("bwl turn off suppression device", ACTION_RAID) }));

    triggers.push_back(new TriggerNode("bwl affliction bronze",
        { NextAction("bwl use hourglass sand", ACTION_RAID) }));
}

void RaidBwlStrategy::InitMultipliers(std::vector<Multiplier*>& multipliers)
{
    multipliers.push_back(new BwlGenericMultiplier(botAI));
}
