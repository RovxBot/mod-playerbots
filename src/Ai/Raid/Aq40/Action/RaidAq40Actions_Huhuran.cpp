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
    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, context->GetValue<GuidVector>("attackers")->Get());
    Unit* target = Aq40BossActions::FindHuhuranTarget(botAI, encounterUnits);
    if (!target || AI_VALUE(Unit*, "current target") == target)
        return false;

    return Attack(target);
}

bool Aq40HuhuranPoisonSpreadAction::Execute(Event /*event*/)
{
    // During poison/enrage windows, keep ranged non-tanks further out so
    // Tanks stay in. Ranged and non-tank melee DPS spread outward so
    // fewer players soak Noxious Poison (hits 15 closest).
    if (Aq40BossHelper::IsEncounterTank(bot, bot))
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, context->GetValue<GuidVector>("attackers")->Get());
    Unit* huhuran = Aq40BossActions::FindHuhuranTarget(botAI, encounterUnits);
    if (!huhuran)
        return false;

    // Ranged: full spread to 28 yards. Melee DPS: back off to 18 yards
    // to reduce soak count while staying in reasonable dps range.
    float desiredDistance = botAI->IsRanged(bot) ? 28.0f : 18.0f;
    float currentDistance = bot->GetDistance2d(huhuran);
    if (currentDistance >= desiredDistance - 2.0f)
        return false;

    return MoveTo(huhuran, desiredDistance, MovementPriority::MOVEMENT_COMBAT);
}
