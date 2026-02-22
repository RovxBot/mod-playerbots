#include "RaidAq40Actions.h"

namespace Aq40BossActions
{
Unit* FindBugTrioTarget(PlayerbotAI* botAI, GuidVector const& attackers)
{
    return FindUnitByAnyName(botAI, attackers, { "lord kri", "princess yauj", "vem" });
}
}  // namespace Aq40BossActions
