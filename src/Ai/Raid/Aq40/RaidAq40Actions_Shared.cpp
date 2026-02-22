#include "RaidAq40Actions.h"

#include "RaidAq40BossHelper.h"

namespace Aq40BossActions
{
Unit* FindUnitByAnyName(PlayerbotAI* botAI, GuidVector const& attackers, std::initializer_list<char const*> names)
{
    for (ObjectGuid const guid : attackers)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit)
            continue;

        for (char const* name : names)
        {
            if (botAI->EqualLowercaseName(unit->GetName(), name))
                return unit;
        }
    }

    return nullptr;
}
}  // namespace Aq40BossActions

bool Aq40ChooseTargetAction::Execute(Event /*event*/)
{
    if (!Aq40BossHelper::IsInAq40(bot))
        return false;

    GuidVector attackers = context->GetValue<GuidVector>("attackers")->Get();
    if (attackers.empty())
        return false;

    Unit* target = nullptr;

    // Favor fight-ending or high-impact targets first.
    target = Aq40BossActions::FindCthunTarget(botAI, attackers);
    if (!target)
        target = Aq40BossActions::FindTwinEmperorsTarget(botAI, attackers);
    if (!target)
        target = Aq40BossActions::FindHuhuranTarget(botAI, attackers);
    if (!target)
        target = Aq40BossActions::FindFankrissTarget(botAI, attackers);
    if (!target)
        target = Aq40BossActions::FindSarturaTarget(botAI, attackers);
    if (!target)
        target = Aq40BossActions::FindBugTrioTarget(botAI, attackers);
    if (!target)
        target = Aq40BossActions::FindSkeramTarget(botAI, attackers);
    if (!target)
        target = Aq40BossActions::FindOuroTarget(botAI, attackers);
    if (!target)
        target = Aq40BossActions::FindViscidusTarget(botAI, attackers);

    if (!target)
    {
        for (ObjectGuid const guid : attackers)
        {
            target = botAI->GetUnit(guid);
            if (target)
                break;
        }
    }

    if (!target || AI_VALUE(Unit*, "current target") == target)
        return false;

    return Attack(target);
}
