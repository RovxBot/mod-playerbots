#include "RaidAq40Actions.h"

namespace Aq40BossActions
{
Unit* FindSkeramTarget(PlayerbotAI* botAI, GuidVector const& attackers)
{
    return FindUnitByAnyName(botAI, attackers, { "the prophet skeram" });
}
}  // namespace Aq40BossActions
