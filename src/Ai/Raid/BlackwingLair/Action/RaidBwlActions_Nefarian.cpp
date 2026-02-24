#include "RaidBwlActions.h"
#include <cmath>

bool BwlNefarianPhaseOneChooseTargetAction::Execute(Event /*event*/)
{
    BwlBossHelper helper(botAI);
    GuidVector attackers = context->GetValue<GuidVector>("attackers")->Get();
    Unit* target = nullptr;

    for (ObjectGuid const guid : attackers)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit || !unit->IsAlive())
        {
            continue;
        }

        if (helper.IsNefarianPhaseOneAdd(unit))
        {
            target = unit;
            break;
        }
    }

    // Fallback in case attackers list is stale.
    if (!target)
    {
        target = AI_VALUE2(Unit*, "find target", "chromatic drakonid");
    }

    if (!target)
    {
        return false;
    }

    if (AI_VALUE(Unit*, "current target") == target)
    {
        return false;
    }

    return Attack(target, false);
}

bool BwlNefarianPhaseOneTunnelPositionAction::Execute(Event /*event*/)
{
    Unit* anchor = AI_VALUE2(Unit*, "find target", "lord victor nefarius");

    BwlBossHelper helper(botAI);
    if (!anchor || !anchor->IsAlive())
    {
        anchor = AI_VALUE2(Unit*, "find target", "chromatic drakonid");
    }

    if (!anchor || !anchor->IsAlive())
    {
        GuidVector attackers = context->GetValue<GuidVector>("attackers")->Get();
        for (ObjectGuid const guid : attackers)
        {
            Unit* unit = botAI->GetUnit(guid);
            if (helper.IsNefarianPhaseOneAdd(unit))
            {
                anchor = unit;
                break;
            }
        }
    }

    if (!anchor || !anchor->IsAlive())
    {
        GuidVector nearby = context->GetValue<GuidVector>("nearest npcs")->Get();
        for (ObjectGuid const guid : nearby)
        {
            Unit* unit = botAI->GetUnit(guid);
            if (helper.IsNefarianPhaseOneAdd(unit))
            {
                anchor = unit;
                break;
            }
        }
    }

    if (!anchor || !anchor->IsAlive())
    {
        return false;
    }

    float targetX = anchor->GetPositionX();
    float targetY = anchor->GetPositionY();
    float targetZ = bot->GetPositionZ();
    float const facing = anchor->GetOrientation();
    uint32 const slot = botAI->GetGroupSlotIndex(bot);

    // Two tunnel split:
    // left lane handles one drakonid stream, right lane handles the other.
    bool const leftTunnel = (slot % 2 == 0);

    float angleOffset = leftTunnel ? static_cast<float>(M_PI / 2.0f) : static_cast<float>(-M_PI / 2.0f);
    float distance = 0.0f;

    if (botAI->IsMainTank(bot) || botAI->IsAssistTank(bot))
    {
        distance = 24.0f;
    }
    else if (botAI->IsRanged(bot) || botAI->IsHeal(bot))
    {
        distance = botAI->IsHeal(bot) ? 28.0f : 31.0f;
    }
    else
    {
        distance = 22.0f;
    }

    float spread = ((slot % 5) - 2.0f) * 0.09f;
    float angle = facing + angleOffset + spread;
    targetX += std::cos(angle) * distance;
    targetY += std::sin(angle) * distance;

    if (MoveTo(bot->GetMapId(), targetX, targetY, targetZ, false, false, false, false, MovementPriority::MOVEMENT_COMBAT))
    {
        return true;
    }

    return MoveInside(bot->GetMapId(), targetX, targetY, targetZ, 2.5f, MovementPriority::MOVEMENT_COMBAT);
}

bool BwlNefarianPhaseTwoChooseTargetAction::Execute(Event /*event*/)
{
    Unit* nefarian = AI_VALUE2(Unit*, "find target", "nefarian");
    if (!nefarian || !nefarian->IsAlive())
    {
        return false;
    }

    if (AI_VALUE(Unit*, "current target") == nefarian)
    {
        return false;
    }

    return Attack(nefarian, true);
}

bool BwlNefarianPhaseTwoPositionAction::Execute(Event /*event*/)
{
    Unit* nefarian = AI_VALUE2(Unit*, "find target", "nefarian");
    if (!nefarian || !nefarian->IsAlive())
    {
        return false;
    }

    float targetX = nefarian->GetPositionX();
    float targetY = nefarian->GetPositionY();
    float targetZ = bot->GetPositionZ();
    float const facing = nefarian->GetOrientation();
    uint32 const slot = botAI->GetGroupSlotIndex(bot);

    // P2 dragon setup:
    // MT in front, OT off-angle for recovery, raid stacked to side/rear
    // so shadow flame line and fear movement are controlled.
    float angleOffset = 0.0f;
    float distance = 0.0f;

    if (botAI->IsMainTank(bot))
    {
        angleOffset = 0.0f;
        distance = 7.0f;
    }
    else if (botAI->IsAssistTankOfIndex(bot, 0))
    {
        angleOffset = static_cast<float>(M_PI / 2.0f);
        distance = 10.0f;
    }
    else if (botAI->IsRanged(bot) || botAI->IsHeal(bot))
    {
        float spread = ((slot % 6) - 2.5f) * 0.10f;
        angleOffset = static_cast<float>(-M_PI / 2.0f) + spread;
        distance = botAI->IsHeal(bot) ? 30.0f : 34.0f;
    }
    else
    {
        float spread = ((slot % 5) - 2.0f) * 0.16f;
        angleOffset = static_cast<float>(-M_PI / 2.0f) + spread;
        distance = 8.0f;
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
