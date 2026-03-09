#include "RaidBwlActions.h"

#include <cmath>
#include <limits>

#include "SharedDefines.h"

namespace
{
// GPS-verified fixed tank spot for Chromaggus.
// Any bot that has aggro must run here so the boss doesn't spin through the raid.
constexpr float ChromaggusTankSpotX = -7448.44f;
constexpr float ChromaggusTankSpotY = -1057.89f;
constexpr float ChromaggusTankSpotZ = 476.55f;

struct ChromaggusLosAnchor
{
    float x;
    float y;
    float z;
};

constexpr ChromaggusLosAnchor ChromaggusLosAnchors[] = {
    {-7466.0f, -1080.5f, 476.5f},
    {-7457.0f, -1087.0f, 476.5f},
    {-7447.0f, -1091.0f, 476.5f},
    {-7437.0f, -1087.0f, 476.5f},
    {-7428.0f, -1080.5f, 476.5f},
};
}  // namespace

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
    BwlBossHelper helper(botAI);
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
    bool const isPrimaryTank = helper.IsEncounterPrimaryTank(bot);
    bool const isBackupTank = helper.IsEncounterBackupTank(bot, 0);
    bool const isTankRole = isPrimaryTank || isBackupTank;
    bool const hasAggro = chromaggus->GetVictim() == bot;

    // Anyone with aggro (tank or not) must go to the fixed tank spot
    // so the boss stays in position and doesn't spin through the raid.
    if (isTankRole || hasAggro)
    {
        float const dist = bot->GetDistance2d(ChromaggusTankSpotX, ChromaggusTankSpotY);
        if (dist <= 3.0f && std::fabs(bot->GetPositionZ() - ChromaggusTankSpotZ) <= 2.0f)
        {
            return false;
        }

        if (MoveTo(bot->GetMapId(), ChromaggusTankSpotX, ChromaggusTankSpotY, ChromaggusTankSpotZ,
                   false, false, false, false, MovementPriority::MOVEMENT_COMBAT))
        {
            return true;
        }

        return MoveInside(bot->GetMapId(), ChromaggusTankSpotX, ChromaggusTankSpotY, ChromaggusTankSpotZ,
                          2.0f, MovementPriority::MOVEMENT_COMBAT);
    }

    // Hold a doorway-safe lane:
    // melee on rear flank, ranged/healers deeper
    // to keep line-of-sight control for breath windows.
    float angleOffset = 0.0f;
    float distance = 0.0f;

    if (botAI->IsRanged(bot) || botAI->IsHeal(bot))
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

    return BwlSpellIds::GetAnyAura(chromaggus, {BwlSpellIds::ChromaggusFrenzy}) != nullptr;
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
    for (ChromaggusLosAnchor const& anchor : ChromaggusLosAnchors)
    {
        if (chromaggus->IsWithinLOS(anchor.x, anchor.y, anchor.z))
        {
            continue;
        }

        float dx = anchor.x - botX;
        float dy = anchor.y - botY;
        float distSq = dx * dx + dy * dy;
        if (distSq < bestDistSq)
        {
            bestDistSq = distSq;
            bestX = anchor.x;
            bestY = anchor.y;
            bestZ = anchor.z;
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
    return MoveTo(bot->GetMapId(), fallbackX, fallbackY, bot->GetPositionZ(), false, false, false, true, MovementPriority::MOVEMENT_FORCED);
}

bool BwlUseHourglassSandAction::Execute(Event /*event*/)
{
    if (!bot->HasItemCount(BwlItems::HourglassSand, 1, false))
    {
        bot->RemoveAurasDueToSpell(BwlSpellIds::AfflictionBronze);
        return !botAI->HasAura(BwlSpellIds::AfflictionBronze, bot);
    }

    return botAI->CastSpell(BwlSpellIds::HourglassSandCure, bot);
}

bool BwlUseHourglassSandAction::isUseful()
{
    return botAI->HasAura(BwlSpellIds::AfflictionBronze, bot);
}
