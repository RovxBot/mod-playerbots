#include "RaidAq40Actions.h"

namespace Aq40BossActions
{
Unit* FindFankrissTarget(PlayerbotAI* botAI, GuidVector const& attackers)
{
    return FindUnitByAnyName(botAI, attackers, { "fankriss the unyielding" });
}
}  // namespace Aq40BossActions
