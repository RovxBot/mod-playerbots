/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "NewRpgTriggers.h"
#include "PlayerbotAI.h"

bool NewRpgStatusTrigger::IsActive() { return status == botAI->rpgInfo.GetStatus(); }

bool ProfessionPhaseTrigger::IsActive()
{
    return botAI->rpgInfo.GetStatus() == RPG_DO_PROFESSIONS && botAI->rpgInfo.Profession.Phase == _phase &&
           !botAI->rpgInfo.Profession.Waiting;
}
