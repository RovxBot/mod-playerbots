#include "RaidAq40Actions.h"

#include <algorithm>

namespace
{
bool IsAttackableSkeramTarget(Player* bot, Unit* target)
{
    return bot && target && target->IsInWorld() && target->IsAlive() && target->GetMapId() == bot->GetMapId() &&
           !target->IsFriendlyTo(bot) && (target->GetUnitFlags() & UNIT_FLAG_NOT_SELECTABLE) != UNIT_FLAG_NOT_SELECTABLE;
}
}

namespace Aq40BossActions
{
Unit* FindSkeramTarget(PlayerbotAI* botAI, GuidVector const& attackers, bool preferLowestHealth)
{
    if (!botAI)
        return nullptr;

    Player* bot = botAI->GetBot();
    std::vector<Unit*> skerams = FindUnitsByAnyName(botAI, attackers, { "the prophet skeram" });
    if (!bot)
        return skerams.empty() ? nullptr : skerams.front();

    skerams.erase(std::remove_if(skerams.begin(), skerams.end(), [bot](Unit* skeram)
    {
        return !IsAttackableSkeramTarget(bot, skeram);
    }), skerams.end());

    if (skerams.empty())
        return nullptr;

    std::sort(skerams.begin(), skerams.end(), [bot, preferLowestHealth](Unit* left, Unit* right)
    {
        bool const leftPrimaryHeld = Aq40BossHelper::IsUnitHeldByEncounterTank(bot, left, true);
        bool const rightPrimaryHeld = Aq40BossHelper::IsUnitHeldByEncounterTank(bot, right, true);
        if (leftPrimaryHeld != rightPrimaryHeld)
            return leftPrimaryHeld > rightPrimaryHeld;

        bool const leftHeld = Aq40BossHelper::IsUnitHeldByEncounterTank(bot, left);
        bool const rightHeld = Aq40BossHelper::IsUnitHeldByEncounterTank(bot, right);
        if (leftHeld != rightHeld)
            return leftHeld > rightHeld;

        if (preferLowestHealth && left->GetHealthPct() != right->GetHealthPct())
            return left->GetHealthPct() < right->GetHealthPct();

        bool const leftLos = bot->IsWithinLOSInMap(left);
        bool const rightLos = bot->IsWithinLOSInMap(right);
        if (leftLos != rightLos)
            return leftLos > rightLos;

        float const leftDistance = bot->GetDistance2d(left);
        float const rightDistance = bot->GetDistance2d(right);
        if (leftDistance != rightDistance)
            return leftDistance < rightDistance;

        if (!preferLowestHealth && left->GetHealthPct() != right->GetHealthPct())
            return left->GetHealthPct() < right->GetHealthPct();

        return left->GetGUID().GetRawValue() < right->GetGUID().GetRawValue();
    });

    return skerams.front();
}
}    // namespace Aq40BossActions
