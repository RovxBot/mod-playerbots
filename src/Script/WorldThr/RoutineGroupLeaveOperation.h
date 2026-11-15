/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_ROUTINE_GROUP_LEAVE_OPERATION_H
#define PLAYERBOTS_ROUTINE_GROUP_LEAVE_OPERATION_H

#include <chrono>

#include "ObjectAccessor.h"
#include "PlayerbotAI.h"
#include "PlayerbotOperation.h"
#include "Playerbots.h"
#include "RoutineMaintenance.h"

class RoutineGroupLeaveOperation final : public PlayerbotOperation
{
public:
    RoutineGroupLeaveOperation(ObjectGuid guid, ObjectGuid groupGuid)
        : _guid(guid), _groupGuid(groupGuid), _expiresAt(std::chrono::steady_clock::now() + std::chrono::seconds(30))
    {
    }

    bool Execute() override
    {
        if (!IsValid() || !Playerbots::RoutineMaintenance::PolicyProvider.load(std::memory_order_acquire))
            return false;
        Player* bot = ObjectAccessor::FindPlayer(_guid);
        if (!bot || !bot->GetGroup() || bot->GetGroup()->GetGUID() != _groupGuid ||
            Playerbots::RoutineMaintenance::ShouldBlockRoutineMaintenance(
                bot, Playerbots::RoutineMaintenance::Action::GroupTeardown) ||
            Playerbots::RoutineMaintenance::ShouldBlockGroupTeardown(bot->GetGroup()))
            return false;
        auto* ai = GET_PLAYERBOT_AI(bot);
        if (!ai || IsSelfBot(bot) || ai->HasGameClientMaster())
            return false;
        // Reuse the original packet path and its core permissions.
        ai->LeaveOrDisbandGroup();
        return true;
    }

    ObjectGuid GetBotGuid() const override { return _guid; }
    std::string GetName() const override { return "RoutineGroupLeave"; }
    bool IsValid() const override { return std::chrono::steady_clock::now() <= _expiresAt; }

private:
    ObjectGuid _guid;
    ObjectGuid _groupGuid;
    std::chrono::steady_clock::time_point _expiresAt;
};

#endif
