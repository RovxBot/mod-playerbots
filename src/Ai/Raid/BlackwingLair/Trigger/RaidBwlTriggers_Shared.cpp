#include "RaidBwlTriggers.h"

#include "RaidBwlSpellIds.h"

bool BwlMissingOnyxiaScaleCloakTrigger::IsActive()
{
    if (!helper.IsInBwl() || !bot->IsInCombat())
    {
        return false;
    }

    return !botAI->HasAura(BwlSpellIds::OnyxiaScaleCloakAura, bot);
}
