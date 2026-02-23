#include "RaidAq40Actions.h"

#include <cmath>

namespace Aq40BossActions
{
Unit* FindHuhuranTarget(PlayerbotAI* botAI, GuidVector const& attackers)
{
    return FindUnitByAnyName(botAI, attackers, { "princess huhuran" });
}
}  // namespace Aq40BossActions

bool Aq40HuhuranChooseTargetAction::Execute(Event /*event*/)
{
    GuidVector attackers = context->GetValue<GuidVector>("attackers")->Get();
    Unit* target = Aq40BossActions::FindHuhuranTarget(botAI, attackers);
    if (!target || AI_VALUE(Unit*, "current target") == target)
        return false;

    return Attack(target);
}

bool Aq40HuhuranPoisonSpreadAction::Execute(Event /*event*/)
{
    // During poison/enrage windows, keep ranged non-tanks further out so
    // closest slots stay available for assigned tanks/melee soakers.
    if (botAI->IsTank(bot) || !botAI->IsRanged(bot))
        return false;

    GuidVector attackers = context->GetValue<GuidVector>("attackers")->Get();
    Unit* huhuran = Aq40BossActions::FindHuhuranTarget(botAI, attackers);
    if (!huhuran)
        return false;

    float d = bot->GetDistance2d(huhuran);
    if (d >= 24.0f)
        return false;

    float dx = bot->GetPositionX() - huhuran->GetPositionX();
    float dy = bot->GetPositionY() - huhuran->GetPositionY();
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.1f)
    {
        dx = std::cos(bot->GetOrientation());
        dy = std::sin(bot->GetOrientation());
        len = 1.0f;
    }

    float retreatDistance = 28.0f;
    float moveX = huhuran->GetPositionX() + (dx / len) * retreatDistance;
    float moveY = huhuran->GetPositionY() + (dy / len) * retreatDistance;

    return MoveTo(bot->GetMapId(), moveX, moveY, bot->GetPositionZ(), false, false, false, false,
                  MovementPriority::MOVEMENT_COMBAT);
}
