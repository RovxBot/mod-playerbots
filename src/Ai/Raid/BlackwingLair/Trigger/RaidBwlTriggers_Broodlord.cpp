#include "RaidBwlTriggers.h"

#include "SharedDefines.h"

bool BwlSuppressionDeviceTrigger::IsActive()
{
    if (!helper.IsInBwl())
    {
        return false;
    }

    GuidVector gos = AI_VALUE(GuidVector, "nearest game objects");
    for (GuidVector::iterator i = gos.begin(); i != gos.end(); i++)
    {
        GameObject* go = botAI->GetGameObject(*i);
        if (!go)
        {
            continue;
        }
        if (go->GetEntry() != BwlGameObjects::SuppressionDevice || go->GetDistance(bot) >= 15.0f || go->GetGoState() != GO_STATE_READY)
        {
            continue;
        }
        return true;
    }
    return false;
}

bool BwlBroodlordEncounterTrigger::IsActive()
{
    if (!helper.IsInBwl() || !bot->IsInCombat())
    {
        return false;
    }

    if (Unit* broodlord = AI_VALUE2(Unit*, "find target", "broodlord lashlayer"))
    {
        return broodlord->IsAlive();
    }

    return false;
}

bool BwlBroodlordPositioningTrigger::IsActive()
{
    if (!helper.IsInBwl() || !bot->IsInCombat())
    {
        return false;
    }

    if (Unit* broodlord = AI_VALUE2(Unit*, "find target", "broodlord lashlayer"))
    {
        return broodlord->IsAlive();
    }

    return false;
}

bool BwlBroodlordMainTankMortalStrikeTrigger::IsActive()
{
    if (!helper.IsInBwl() || !bot->IsInCombat() || !botAI->IsAssistTankOfIndex(bot, 0))
    {
        return false;
    }

    Unit* broodlord = AI_VALUE2(Unit*, "find target", "broodlord lashlayer");
    if (!broodlord || !broodlord->IsAlive())
    {
        return false;
    }

    Unit* mainTank = AI_VALUE(Unit*, "main tank");
    if (!mainTank || mainTank == bot)
    {
        return false;
    }

    if (Aura* msAura = botAI->GetAura("mortal strike", mainTank, false, true))
    {
        return msAura->GetDuration() > 1000;
    }

    return false;
}
