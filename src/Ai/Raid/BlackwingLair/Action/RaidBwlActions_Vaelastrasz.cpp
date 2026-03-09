#include "RaidBwlActions.h"

#include <cmath>

#include "SharedDefines.h"

bool BwlVaelastraszChooseTargetAction::Execute(Event /*event*/)
{
    BwlBossHelper helper(botAI);
    Unit* vael = helper.FindAliveTarget("vaelastrasz the corrupt");
    if (!vael)
    {
        return false;
    }

    if (AI_VALUE(Unit*, "current target") == vael)
    {
        return false;
    }

    return Attack(vael, true);
}

bool BwlVaelastraszPositionAction::Execute(Event /*event*/)
{
    BwlBossHelper helper(botAI);
    Unit* vael = helper.FindAliveTarget("vaelastrasz the corrupt");
    if (!vael)
    {
        return false;
    }

    // Burning Adrenaline movement-out is handled by higher-priority generic move-from-group.
    if (helper.HasBurningAdrenaline(bot))
    {
        return false;
    }

    float targetX = vael->GetPositionX();
    float targetY = vael->GetPositionY();
    float targetZ = bot->GetPositionZ();

    float const facing = vael->GetOrientation();
    uint32 const slot = botAI->GetGroupSlotIndex(bot);

    // Guide-based clock positions:
    // main tank at 12 o'clock (front), off-tank near 9 o'clock,
    // melee around 3 o'clock / rear flank, ranged-healers behind with spread.
    float angleOffset = 0.0f;
    float distance = 0.0f;

    if (helper.IsEncounterPrimaryTank(bot))
    {
        angleOffset = 0.0f;
        distance = 6.0f;
    }
    else if (helper.IsEncounterBackupTank(bot, 0))
    {
        angleOffset = static_cast<float>(M_PI / 2.0f);
        distance = 8.0f;
    }
    else if (helper.IsEncounterBackupTank(bot, 1))
    {
        angleOffset = static_cast<float>(M_PI / 2.0f) + 0.35f;
        distance = 9.0f;
    }
    else if (botAI->IsRanged(bot) || botAI->IsHeal(bot))
    {
        float spread = ((slot % 5) - 2) * 0.10f;
        // Keep ranged/healers on melee side but farther out to avoid direct tail sweep line.
        angleOffset = static_cast<float>(-M_PI / 2.0f) + spread;
        distance = botAI->IsHeal(bot) ? 18.0f : 21.0f;
    }
    else
    {
        float spread = ((slot % 4) - 1.5f) * 0.20f;
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
