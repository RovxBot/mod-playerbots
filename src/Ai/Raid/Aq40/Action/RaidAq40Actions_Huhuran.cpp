#include "RaidAq40Actions.h"

#include <cmath>

#include "../RaidAq40BossHelper.h"

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
    if (Aq40BossHelper::IsEncounterTank(bot, bot) || !botAI->IsRanged(bot))
        return false;

    GuidVector attackers = context->GetValue<GuidVector>("attackers")->Get();
    Unit* huhuran = Aq40BossActions::FindHuhuranTarget(botAI, attackers);
    if (!huhuran)
        return false;

    float currentDistance = bot->GetDistance2d(huhuran);
    float desiredDistance = 28.0f;
    if (currentDistance >= desiredDistance - 2.0f)
        return false;

    return MoveTo(huhuran, desiredDistance, MovementPriority::MOVEMENT_COMBAT);
}
