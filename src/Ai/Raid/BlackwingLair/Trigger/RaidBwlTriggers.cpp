#include "RaidBwlTriggers.h"

#include "RaidBwlSpellIds.h"
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

bool BwlAfflictionBronzeTrigger::IsActive()
{
    if (!helper.IsInBwl())
    {
        return false;
    }

    return helper.HasBronzeAffliction() && helper.HasHourglassSand();
}

bool BwlMissingOnyxiaScaleCloakTrigger::IsActive()
{
    if (!helper.IsInBwl() || !bot->IsInCombat())
    {
        return false;
    }

    return !botAI->HasAura(BwlSpellIds::OnyxiaScaleCloakAura, bot);
}
