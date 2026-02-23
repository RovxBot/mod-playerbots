#include "RaidAq40Actions.h"

namespace Aq40BossActions
{
Unit* FindTwinEmperorsTarget(PlayerbotAI* botAI, GuidVector const& attackers)
{
    return FindUnitByAnyName(botAI, attackers, { "emperor vek'nilash", "emperor vek'lor" });
}
}  // namespace Aq40BossActions

bool Aq40TwinEmperorsChooseTargetAction::Execute(Event /*event*/)
{
    GuidVector attackers = context->GetValue<GuidVector>("attackers")->Get();
    if (attackers.empty())
        return false;

    // Baseline split:
    // - tanks / melee stay on Vek'nilash (physical emperor)
    // - ranged non-tanks stay on Vek'lor (caster emperor)
    Unit* preferred = nullptr;
    Unit* fallback = nullptr;
    bool favorVeknilash = botAI->IsTank(bot) || !botAI->IsRanged(bot);
    if (favorVeknilash)
    {
        preferred = Aq40BossActions::FindUnitByAnyName(botAI, attackers, { "emperor vek'nilash" });
        fallback = Aq40BossActions::FindUnitByAnyName(botAI, attackers, { "emperor vek'lor" });
    }
    else
    {
        preferred = Aq40BossActions::FindUnitByAnyName(botAI, attackers, { "emperor vek'lor" });
        fallback = Aq40BossActions::FindUnitByAnyName(botAI, attackers, { "emperor vek'nilash" });
    }

    Unit* target = preferred ? preferred : fallback;
    if (!target || AI_VALUE(Unit*, "current target") == target)
        return false;

    return Attack(target);
}
