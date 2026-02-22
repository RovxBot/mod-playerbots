#include "RaidAq40Actions.h"

namespace Aq40BossActions
{
Unit* FindSarturaTarget(PlayerbotAI* botAI, GuidVector const& attackers)
{
    return FindUnitByAnyName(botAI, attackers, { "battleguard sartura" });
}

std::vector<Unit*> FindSarturaGuards(PlayerbotAI* botAI, GuidVector const& attackers)
{
    return FindUnitsByAnyName(botAI, attackers, { "sartura's royal guard" });
}
}  // namespace Aq40BossActions
