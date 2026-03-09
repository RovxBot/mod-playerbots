#include "RaidAq40Actions.h"

#include <cmath>

namespace Aq40BossActions
{
Unit* FindOuroTarget(PlayerbotAI* botAI, GuidVector const& attackers)
{
    return FindUnitByAnyName(botAI, attackers, { "ouro" });
}
}  // namespace Aq40BossActions

namespace
{
Unit* FindLowestHealthUnit(std::vector<Unit*> const& units)
{
    Unit* chosen = nullptr;
    for (Unit* unit : units)
    {
        if (!unit)
            continue;

        if (!chosen || unit->GetHealthPct() < chosen->GetHealthPct())
            chosen = unit;
    }

    return chosen;
}

Unit* FindOuroScarabs(PlayerbotAI* botAI, GuidVector const& attackers)
{
    std::vector<Unit*> scarabs =
        Aq40BossActions::FindUnitsByAnyName(botAI, attackers, { "qiraji scarab", "scarab" });
    return FindLowestHealthUnit(scarabs);
}

Unit* FindNearestDirtMound(Player* bot, PlayerbotAI* botAI, GuidVector const& attackers)
{
    std::vector<Unit*> mounds = Aq40BossActions::FindUnitsByAnyName(botAI, attackers, { "dirt mound" });
    Unit* closest = nullptr;
    float closestDistance = 9999.0f;
    for (Unit* mound : mounds)
    {
        if (!mound)
            continue;

        float d = bot->GetDistance2d(mound);
        if (d < closestDistance)
        {
            closestDistance = d;
            closest = mound;
        }
    }

    return closest;
}
}  // namespace

bool Aq40OuroChooseTargetAction::Execute(Event /*event*/)
{
    GuidVector attackers = context->GetValue<GuidVector>("attackers")->Get();
    if (attackers.empty())
        return false;

    Unit* target = FindOuroScarabs(botAI, attackers);
    if (!target)
        target = Aq40BossActions::FindOuroTarget(botAI, attackers);
    if (!target)
        target = FindNearestDirtMound(bot, botAI, attackers);

    if (!target || AI_VALUE(Unit*, "current target") == target)
        return false;

    return Attack(target);
}

bool Aq40OuroHoldMeleeContactAction::Execute(Event /*event*/)
{
    if (!(botAI->IsTank(bot) || !botAI->IsRanged(bot)))
        return false;

    GuidVector attackers = context->GetValue<GuidVector>("attackers")->Get();
    Unit* ouro = Aq40BossActions::FindOuroTarget(botAI, attackers);
    if (!ouro)
        return false;

    float d = bot->GetDistance2d(ouro);
    if (d <= 8.0f)
        return false;

    float dx = bot->GetPositionX() - ouro->GetPositionX();
    float dy = bot->GetPositionY() - ouro->GetPositionY();
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.1f)
    {
        dx = std::cos(bot->GetOrientation());
        dy = std::sin(bot->GetOrientation());
        len = 1.0f;
    }

    float desired = 4.0f;
    float moveX = ouro->GetPositionX() + (dx / len) * desired;
    float moveY = ouro->GetPositionY() + (dy / len) * desired;
    return MoveTo(bot->GetMapId(), moveX, moveY, bot->GetPositionZ(), false, false, false, false,
                  MovementPriority::MOVEMENT_COMBAT);
}

bool Aq40OuroAvoidSweepAction::Execute(Event /*event*/)
{
    if (botAI->IsTank(bot))
        return false;

    GuidVector attackers = context->GetValue<GuidVector>("attackers")->Get();
    Unit* ouro = Aq40BossActions::FindOuroTarget(botAI, attackers);
    if (!ouro)
        return false;

    if (bot->GetDistance2d(ouro) > 10.0f)
        return false;

    float dx = bot->GetPositionX() - ouro->GetPositionX();
    float dy = bot->GetPositionY() - ouro->GetPositionY();
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.1f)
    {
        dx = std::cos(bot->GetOrientation());
        dy = std::sin(bot->GetOrientation());
        len = 1.0f;
    }

    float desired = 16.0f;
    float moveX = ouro->GetPositionX() + (dx / len) * desired;
    float moveY = ouro->GetPositionY() + (dy / len) * desired;
    return MoveTo(bot->GetMapId(), moveX, moveY, bot->GetPositionZ(), false, false, false, false,
                  MovementPriority::MOVEMENT_COMBAT);
}

bool Aq40OuroAvoidSubmergeAction::Execute(Event /*event*/)
{
    GuidVector attackers = context->GetValue<GuidVector>("attackers")->Get();
    Unit* mound = FindNearestDirtMound(bot, botAI, attackers);
    if (!mound)
        return false;

    float d = bot->GetDistance2d(mound);
    if (d > 16.0f)
        return false;

    float dx = bot->GetPositionX() - mound->GetPositionX();
    float dy = bot->GetPositionY() - mound->GetPositionY();
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.1f)
    {
        dx = std::cos(bot->GetOrientation());
        dy = std::sin(bot->GetOrientation());
        len = 1.0f;
    }

    float desired = 26.0f;
    float moveX = mound->GetPositionX() + (dx / len) * desired;
    float moveY = mound->GetPositionY() + (dy / len) * desired;
    return MoveTo(bot->GetMapId(), moveX, moveY, bot->GetPositionZ(), false, false, false, false,
                  MovementPriority::MOVEMENT_COMBAT);
}
