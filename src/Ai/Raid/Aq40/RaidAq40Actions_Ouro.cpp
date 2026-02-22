#include "RaidAq40Actions.h"

namespace Aq40BossActions
{
Unit* FindOuroTarget(PlayerbotAI* botAI, GuidVector const& attackers)
{
    return FindUnitByAnyName(botAI, attackers, { "ouro" });
}
}  // namespace Aq40BossActions
