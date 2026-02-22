#include "RaidAq40Actions.h"

namespace Aq40BossActions
{
Unit* FindFankrissTarget(PlayerbotAI* botAI, GuidVector const& attackers)
{
    return FindUnitByAnyName(botAI, attackers, { "fankriss the unyielding" });
}

std::vector<Unit*> FindFankrissSpawns(PlayerbotAI* botAI, GuidVector const& attackers)
{
    return FindUnitsByAnyName(botAI, attackers, { "spawn of fankriss" });
}
}  // namespace Aq40BossActions
