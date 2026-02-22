#include "RaidAq40Actions.h"

namespace Aq40BossActions
{
Unit* FindTwinEmperorsTarget(PlayerbotAI* botAI, GuidVector const& attackers)
{
    return FindUnitByAnyName(botAI, attackers, { "emperor vek'nilash", "emperor vek'lor" });
}
}  // namespace Aq40BossActions
