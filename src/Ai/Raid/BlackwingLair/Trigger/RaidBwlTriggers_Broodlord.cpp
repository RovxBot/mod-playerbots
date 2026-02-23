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
