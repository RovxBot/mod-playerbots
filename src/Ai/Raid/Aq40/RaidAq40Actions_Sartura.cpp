#include "RaidAq40Actions.h"

namespace Aq40BossActions
{
Unit* FindSarturaTarget(PlayerbotAI* botAI, GuidVector const& attackers)
{
    return FindUnitByAnyName(botAI, attackers, { "battleguard sartura" });
}
}  // namespace Aq40BossActions
