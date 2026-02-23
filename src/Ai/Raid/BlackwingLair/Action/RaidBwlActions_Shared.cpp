#include "RaidBwlActions.h"

bool BwlWarnOnyxiaScaleCloakAction::Execute(Event /*event*/)
{
    botAI->TellMasterNoFacing("Warning: missing Onyxia Scale Cloak aura in BWL.");
    return true;
}
