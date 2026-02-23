#ifndef _PLAYERBOT_RAIDAQ40BOSSHELPER_H
#define _PLAYERBOT_RAIDAQ40BOSSHELPER_H

#include <limits>

#include "Player.h"

namespace Aq40BossHelper
{
static constexpr uint32 MAP_ID = 531;  // Temple of Ahn'Qiraj

inline bool IsInAq40(Player const* player)
{
    return player && player->GetMapId() == MAP_ID;
}

inline uint32 GetAliveWarlockOrdinal(Player* player)
{
    if (!player || player->getClass() != CLASS_WARLOCK || !player->IsAlive())
        return std::numeric_limits<uint32>::max();

    Group* group = player->GetGroup();
    if (!group)
        return 0;

    uint32 ordinal = 0;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || !member->IsAlive() || member->getClass() != CLASS_WARLOCK)
            continue;

        if (member->GetGUID() == player->GetGUID())
            return ordinal;

        ++ordinal;
    }

    return std::numeric_limits<uint32>::max();
}

inline bool IsDesignatedTwinWarlockTank(Player* player)
{
    uint32 ordinal = GetAliveWarlockOrdinal(player);
    return ordinal != std::numeric_limits<uint32>::max() && ordinal < 2;
}
}  // namespace Aq40BossHelper

#endif
