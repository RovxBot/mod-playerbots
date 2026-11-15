/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_ROUTINE_MAINTENANCE_H
#define PLAYERBOTS_ROUTINE_MAINTENANCE_H

#include "Group.h"
#include "Player.h"
#include "RoutineMaintenancePolicy.h"

namespace Playerbots::RoutineMaintenance
{
inline bool ShouldBlockGroupTeardown(Group const* group)
{
    if (!PolicyProvider.load(std::memory_order_acquire) || !group)
        return false;
    // Offline members count too. This runs only at a disruptive operation, with
    // party/raid size as the bound, never by enumerating a guild or realm.
    for (auto const& member : group->GetMemberSlots())
        if (ShouldBlockRoutineMaintenance(member.guid.GetCounter(), Action::GroupTeardown))
            return true;
    return false;
}

inline bool ShouldBlockRoutineMaintenance(Player const* bot, Action action)
{
    if (!PolicyProvider.load(std::memory_order_acquire) || !bot)
        return false;
    return ShouldBlockRoutineMaintenance(bot->GetGUID().GetCounter(), action);
}
}  // namespace Playerbots::RoutineMaintenance

#endif
