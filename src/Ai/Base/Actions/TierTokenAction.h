/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_TIERTOKENACTION_H
#define PLAYERBOTS_TIERTOKENACTION_H

#include "Action.h"

class PlayerbotAI;

class ConvertTierTokenAction : public Action
{
public:
    ConvertTierTokenAction(PlayerbotAI* botAI) : Action(botAI, "convert tier token") {}

    bool Execute(Event event) override;
};

#endif
