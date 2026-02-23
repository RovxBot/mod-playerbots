#include "RaidBwlActions.h"

#include <cmath>

bool BwlFiremawChooseTargetAction::Execute(Event /*event*/)
{
    Unit* firemaw = AI_VALUE2(Unit*, "find target", "firemaw");
    if (!firemaw || !firemaw->IsAlive())
    {
        return false;
    }

    if (AI_VALUE(Unit*, "current target") == firemaw)
    {
        return false;
    }

    return Attack(firemaw, true);
}

bool BwlFiremawPositionAction::Execute(Event /*event*/)
{
    Unit* firemaw = AI_VALUE2(Unit*, "find target", "firemaw");
    if (!firemaw || !firemaw->IsAlive())
    {
        return false;
    }

    float targetX = firemaw->GetPositionX();
    float targetY = firemaw->GetPositionY();
    float targetZ = bot->GetPositionZ();
    float const facing = firemaw->GetOrientation();
    uint32 const slot = botAI->GetGroupSlotIndex(bot);

    float angleOffset = 0.0f;
    float distance = 0.0f;

    if (botAI->IsMainTank(bot))
    {
        angleOffset = 0.0f;
        distance = 6.0f;
    }
    else if (botAI->IsAssistTankOfIndex(bot, 0))
    {
        angleOffset = static_cast<float>(M_PI / 2.0f);
        distance = 8.0f;
    }
    else if (botAI->IsRanged(bot) || botAI->IsHeal(bot))
    {
        // Hold in a safe lane that can LoS quickly behind nearby pillars/corners.
        float spread = ((slot % 6) - 2.5f) * 0.12f;
        angleOffset = static_cast<float>(-M_PI / 2.0f) + spread;
        distance = botAI->IsHeal(bot) ? 20.0f : 24.0f;
    }
    else
    {
        float spread = ((slot % 4) - 1.5f) * 0.18f;
        angleOffset = static_cast<float>(-M_PI / 2.0f) + spread;
        distance = 7.0f;
    }

    float angle = facing + angleOffset;
    targetX += std::cos(angle) * distance;
    targetY += std::sin(angle) * distance;

    if (MoveTo(bot->GetMapId(), targetX, targetY, targetZ, false, false, false, false, MovementPriority::MOVEMENT_COMBAT))
    {
        return true;
    }

    return MoveInside(bot->GetMapId(), targetX, targetY, targetZ, 2.0f, MovementPriority::MOVEMENT_COMBAT);
}
