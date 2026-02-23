#include "RaidBwlActions.h"

#include <cmath>
#include <limits>

#include "SharedDefines.h"

bool BwlChromaggusChooseTargetAction::Execute(Event /*event*/)
{
    Unit* chromaggus = AI_VALUE2(Unit*, "find target", "chromaggus");
    if (!chromaggus || !chromaggus->IsAlive())
    {
        return false;
    }

    if (AI_VALUE(Unit*, "current target") == chromaggus)
    {
        return false;
    }

    return Attack(chromaggus, true);
}

bool BwlChromaggusPositionAction::Execute(Event /*event*/)
{
    Unit* chromaggus = AI_VALUE2(Unit*, "find target", "chromaggus");
    if (!chromaggus || !chromaggus->IsAlive())
    {
        return false;
    }

    float targetX = chromaggus->GetPositionX();
    float targetY = chromaggus->GetPositionY();
    float targetZ = bot->GetPositionZ();
    float const facing = chromaggus->GetOrientation();
    uint32 const slot = botAI->GetGroupSlotIndex(bot);

    // Hold a doorway-safe lane:
    // tanks stay on the dragon, melee on rear flank, ranged/healers deeper
    // to keep line-of-sight control for breath windows.
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
        float spread = ((slot % 6) - 2.5f) * 0.10f;
        angleOffset = static_cast<float>(-M_PI / 2.0f) + spread;
        distance = botAI->IsHeal(bot) ? 22.0f : 26.0f;
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

bool BwlChromaggusTranqAction::isUseful()
{
    if (bot->getClass() != CLASS_HUNTER)
    {
        return false;
    }

    Unit* chromaggus = AI_VALUE2(Unit*, "find target", "chromaggus");
    if (!chromaggus || !chromaggus->IsAlive())
    {
        return false;
    }

    return botAI->HasAura("frenzy", chromaggus);
}

bool BwlChromaggusTranqAction::Execute(Event /*event*/)
{
    Unit* chromaggus = AI_VALUE2(Unit*, "find target", "chromaggus");
    if (!chromaggus || !chromaggus->IsAlive())
    {
        return false;
    }

    return botAI->CastSpell("tranquilizing shot", chromaggus);
}

bool BwlChromaggusLosHideAction::Execute(Event /*event*/)
{
    Unit* chromaggus = AI_VALUE2(Unit*, "find target", "chromaggus");
    if (!chromaggus || !chromaggus->IsAlive())
    {
        return false;
    }

    float bestX = 0.0f;
    float bestY = 0.0f;
    float bestZ = bot->GetPositionZ();
    float bestDistSq = std::numeric_limits<float>::max();

    float const botX = bot->GetPositionX();
    float const botY = bot->GetPositionY();
    float const botZ = bot->GetPositionZ();
    float const twoPi = static_cast<float>(2.0 * M_PI);
    float const radii[] = {6.0f, 10.0f, 14.0f, 18.0f, 22.0f, 26.0f};

    for (float radius : radii)
    {
        for (uint32 i = 0; i < 16; ++i)
        {
            float angle = (static_cast<float>(i) / 16.0f) * twoPi;
            float testX = botX + std::cos(angle) * radius;
            float testY = botY + std::sin(angle) * radius;
            float testZ = botZ;

            if (chromaggus->IsWithinLOS(testX, testY, testZ))
            {
                continue;
            }

            float dx = testX - botX;
            float dy = testY - botY;
            float distSq = dx * dx + dy * dy;
            if (distSq < bestDistSq)
            {
                bestDistSq = distSq;
                bestX = testX;
                bestY = testY;
                bestZ = testZ;
            }
        }
    }

    if (bestDistSq != std::numeric_limits<float>::max())
    {
        return MoveTo(bot->GetMapId(), bestX, bestY, bestZ, false, false, false, true, MovementPriority::MOVEMENT_FORCED);
    }

    // Fallback: run toward doorway side opposite the dragon's facing.
    float fallbackX = chromaggus->GetPositionX();
    float fallbackY = chromaggus->GetPositionY();
    float const fallbackAngle = chromaggus->GetOrientation() + static_cast<float>(M_PI);
    fallbackX += std::cos(fallbackAngle) * 26.0f;
    fallbackY += std::sin(fallbackAngle) * 26.0f;
    return MoveTo(bot->GetMapId(), fallbackX, fallbackY, botZ, false, false, false, true, MovementPriority::MOVEMENT_FORCED);
}

bool BwlUseHourglassSandAction::Execute(Event /*event*/)
{
    return botAI->CastSpell(BwlSpellIds::HourglassSandCure, bot);
}

bool BwlUseHourglassSandAction::isUseful()
{
    if (!botAI->HasAura(BwlSpellIds::AfflictionBronze, bot))
    {
        return false;
    }

    return bot->HasItemCount(BwlItems::HourglassSand, 1, false);
}
