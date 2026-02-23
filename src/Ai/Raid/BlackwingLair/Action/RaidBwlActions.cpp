#include "RaidBwlActions.h"

#include "Playerbots.h"

bool BwlWarnOnyxiaScaleCloakAction::Execute(Event /*event*/)
{
    botAI->TellMasterNoFacing("Warning: missing Onyxia Scale Cloak aura in BWL.");
    return true;
}

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

bool BwlUseHourglassSandAction::Execute(Event /*event*/)
{
    return botAI->CastSpell(BwlSpellIds::HourglassSandCure, bot);
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

bool BwlUseHourglassSandAction::isUseful()
{
    if (!botAI->HasAura(BwlSpellIds::AfflictionBronze, bot))
    {
        return false;
    }

    return bot->HasItemCount(BwlItems::HourglassSand, 1, false);
}
