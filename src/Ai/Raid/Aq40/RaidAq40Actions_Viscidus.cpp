#include "RaidAq40Actions.h"

namespace Aq40BossActions
{
Unit* FindViscidusTarget(PlayerbotAI* botAI, GuidVector const& attackers)
{
    return FindUnitByAnyName(botAI, attackers, { "viscidus" });
}
}  // namespace Aq40BossActions
