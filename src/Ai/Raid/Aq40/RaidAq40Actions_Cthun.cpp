#include "RaidAq40Actions.h"

namespace Aq40BossActions
{
Unit* FindCthunTarget(PlayerbotAI* botAI, GuidVector const& attackers)
{
    return FindUnitByAnyName(botAI, attackers, { "c'thun", "eye of c'thun", "claw tentacle", "giant eye tentacle" });
}
}  // namespace Aq40BossActions
