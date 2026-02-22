#ifndef _PLAYERBOT_RAIDAQ40BOSSHELPER_H
#define _PLAYERBOT_RAIDAQ40BOSSHELPER_H

#include "Player.h"

namespace Aq40BossHelper
{
static constexpr uint32 MAP_ID = 531;  // Temple of Ahn'Qiraj

inline bool IsInAq40(Player const* player)
{
    return player && player->GetMapId() == MAP_ID;
}
}  // namespace Aq40BossHelper

#endif
