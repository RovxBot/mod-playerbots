#include "RaidAq40Triggers.h"

bool Aq40EngageTrigger::IsActive()
{
    if (!Aq40BossHelper::IsInAq40(bot))
        return false;

    if (!bot->IsInCombat())
        return false;

    return !AI_VALUE(GuidVector, "attackers").empty();
}
