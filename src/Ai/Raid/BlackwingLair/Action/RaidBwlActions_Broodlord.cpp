#include "RaidBwlActions.h"

bool BwlTurnOffSuppressionDeviceAction::Execute(Event /*event*/)
{
    bool usedAny = false;
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
        go->Use(bot);
        usedAny = true;
    }
    return usedAny;
}

bool BwlTurnOffSuppressionDeviceAction::isUseful()
{
    GuidVector gos = AI_VALUE(GuidVector, "nearest game objects");
    for (GuidVector::iterator i = gos.begin(); i != gos.end(); i++)
    {
        GameObject* go = botAI->GetGameObject(*i);
        if (!go)
        {
            continue;
        }
        if (go->GetEntry() == BwlGameObjects::SuppressionDevice && go->GetDistance(bot) < 15.0f && go->GetGoState() == GO_STATE_READY)
        {
            return true;
        }
    }
    return false;
}
