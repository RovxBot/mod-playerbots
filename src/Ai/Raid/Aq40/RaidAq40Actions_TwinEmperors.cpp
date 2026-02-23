#include "RaidAq40Actions.h"

#include <cmath>
#include <limits>

namespace Aq40BossActions
{
Unit* FindTwinEmperorsTarget(PlayerbotAI* botAI, GuidVector const& attackers)
{
    return FindUnitByAnyName(botAI, attackers, { "emperor vek'nilash", "emperor vek'lor" });
}

Unit* FindTwinMutateBug(PlayerbotAI* botAI, GuidVector const& attackers)
{
    return FindUnitByAnyName(botAI, attackers, { "mutate bug" });
}
}  // namespace Aq40BossActions

namespace
{
struct TwinAssignments
{
    Unit* sideEmperor = nullptr;
    Unit* oppositeEmperor = nullptr;
    Unit* veklor = nullptr;
    Unit* veknilash = nullptr;
};

uint32 GetAliveWarlockOrdinal(Player* player)
{
    if (!player || player->getClass() != CLASS_WARLOCK || !player->IsAlive())
        return std::numeric_limits<uint32>::max();

    Group* group = player->GetGroup();
    if (!group)
        return 0;

    uint32 ordinal = 0;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || !member->IsAlive() || member->getClass() != CLASS_WARLOCK)
            continue;

        if (member->GetGUID() == player->GetGUID())
            return ordinal;

        ++ordinal;
    }

    return std::numeric_limits<uint32>::max();
}

bool IsDesignatedWarlockTank(Player* player)
{
    uint32 ordinal = GetAliveWarlockOrdinal(player);
    if (ordinal == std::numeric_limits<uint32>::max())
        return false;

    // Auto-designate up to two lock tanks in stable group order.
    return ordinal < 2;
}

bool IsTwinRoleMatch(Player* bot, PlayerbotAI* botAI, Player* member)
{
    if (!member || !member->IsAlive())
        return false;

    bool botIsWarlockTank = IsDesignatedWarlockTank(bot);
    bool botIsMeleeTank = botAI->IsTank(bot) && !botAI->IsRanged(bot);
    bool botIsHealer = botAI->IsHeal(bot);

    if (botIsWarlockTank)
        return IsDesignatedWarlockTank(member);
    if (botIsMeleeTank)
        return GET_PLAYERBOT_AI(member) && GET_PLAYERBOT_AI(member)->IsTank(member) && !GET_PLAYERBOT_AI(member)->IsRanged(member);
    if (botIsHealer)
        return GET_PLAYERBOT_AI(member) && GET_PLAYERBOT_AI(member)->IsHeal(member);

    return true;
}

bool IsLikelyOnSameTwinSide(Unit* unit, Unit* sideEmperor, Unit* oppositeEmperor)
{
    if (!unit || !sideEmperor)
        return false;

    if (!oppositeEmperor)
        return true;

    return unit->GetDistance2d(sideEmperor) <= unit->GetDistance2d(oppositeEmperor);
}

uint32 GetTwinSideIndex(Player* bot, PlayerbotAI* botAI)
{
    Group* group = bot->GetGroup();
    if (!group)
        return static_cast<uint32>(bot->GetGUID().GetCounter() % 2);

    uint32 roleIndex = 0;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || !member->IsAlive())
            continue;

        if (!IsTwinRoleMatch(bot, botAI, member))
            continue;

        if (member->GetGUID() == bot->GetGUID())
            return roleIndex % 2;

        ++roleIndex;
    }

    return static_cast<uint32>(bot->GetGUID().GetCounter() % 2);
}

TwinAssignments GetTwinAssignments(Player* bot, PlayerbotAI* botAI, GuidVector const& attackers)
{
    TwinAssignments result;
    result.veklor = Aq40BossActions::FindUnitByAnyName(botAI, attackers, { "emperor vek'lor" });
    result.veknilash = Aq40BossActions::FindUnitByAnyName(botAI, attackers, { "emperor vek'nilash" });
    if (!result.veklor || !result.veknilash)
        return result;

    bool splitByX = std::abs(result.veklor->GetPositionX() - result.veknilash->GetPositionX()) >=
                    std::abs(result.veklor->GetPositionY() - result.veknilash->GetPositionY());
    float veklorAxis = splitByX ? result.veklor->GetPositionX() : result.veklor->GetPositionY();
    float veknilashAxis = splitByX ? result.veknilash->GetPositionX() : result.veknilash->GetPositionY();
    Unit* lowSide = veklorAxis < veknilashAxis ? result.veklor : result.veknilash;
    Unit* highSide = lowSide == result.veklor ? result.veknilash : result.veklor;

    uint32 sideIndex = GetTwinSideIndex(bot, botAI);
    result.sideEmperor = sideIndex == 0 ? lowSide : highSide;
    result.oppositeEmperor = result.sideEmperor == lowSide ? highSide : lowSide;
    return result;
}
}  // namespace

bool Aq40TwinEmperorsChooseTargetAction::Execute(Event /*event*/)
{
    GuidVector attackers = context->GetValue<GuidVector>("attackers")->Get();
    if (attackers.empty())
        return false;

    TwinAssignments assignment = GetTwinAssignments(bot, botAI, attackers);
    Unit* sideBoss = assignment.sideEmperor;
    if (!sideBoss)
        sideBoss = Aq40BossActions::FindTwinEmperorsTarget(botAI, attackers);
    if (!sideBoss)
        return false;

    Unit* target = nullptr;
    bool isWarlockTank = IsDesignatedWarlockTank(bot);
    bool isMeleeTank = botAI->IsTank(bot) && !botAI->IsRanged(bot);
    bool isMeleeDps = !isMeleeTank && !botAI->IsRanged(bot) && !botAI->IsHeal(bot);
    Unit* mutateBug = Aq40BossActions::FindTwinMutateBug(botAI, attackers);
    if (isWarlockTank)
    {
        // Warlock tank is dedicated to Vek'lor on the assigned side.
        if (assignment.veklor && sideBoss == assignment.veklor)
            target = assignment.veklor;
        else
            return false;
    }
    else if (isMeleeTank)
    {
        // Melee tank is dedicated to Vek'nilash on the assigned side.
        if (assignment.veknilash && sideBoss == assignment.veknilash)
            target = assignment.veknilash;
        else
            return false;
    }
    else if (botAI->IsHeal(bot))
        return false;
    else if (isMeleeDps && mutateBug && IsLikelyOnSameTwinSide(mutateBug, assignment.sideEmperor, assignment.oppositeEmperor))
        target = mutateBug;
    else if (botAI->IsRanged(bot))
        target = assignment.veklor;
    else
        target = assignment.veknilash;

    if (!target || AI_VALUE(Unit*, "current target") == target)
        return false;

    return Attack(target);
}

bool Aq40TwinEmperorsHoldSplitAction::Execute(Event /*event*/)
{
    GuidVector attackers = context->GetValue<GuidVector>("attackers")->Get();
    TwinAssignments assignment = GetTwinAssignments(bot, botAI, attackers);
    if (!assignment.sideEmperor)
        return false;

    // Split only tanks/healers by side. Damage dealers stay on target logic.
    bool isWarlockTank = IsDesignatedWarlockTank(bot);
    if (!botAI->IsTank(bot) && !botAI->IsHeal(bot) && !isWarlockTank)
        return false;

    Unit* sideBoss = assignment.sideEmperor;
    float distance = bot->GetDistance2d(sideBoss);
    float desiredRange = 0.0f;
    if (botAI->IsHeal(bot))
        desiredRange = 20.0f;
    else if (isWarlockTank)
        desiredRange = 24.0f;
    else
        desiredRange = 4.0f;

    if (std::abs(distance - desiredRange) <= 3.0f)
        return false;

    float dx = bot->GetPositionX() - sideBoss->GetPositionX();
    float dy = bot->GetPositionY() - sideBoss->GetPositionY();
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.1f)
    {
        dx = std::cos(bot->GetOrientation());
        dy = std::sin(bot->GetOrientation());
        len = 1.0f;
    }

    float moveX = sideBoss->GetPositionX() + (dx / len) * desiredRange;
    float moveY = sideBoss->GetPositionY() + (dy / len) * desiredRange;
    return MoveTo(bot->GetMapId(), moveX, moveY, bot->GetPositionZ(), false, false, false, false,
                  MovementPriority::MOVEMENT_COMBAT);
}

bool Aq40TwinEmperorsWarlockTankAction::Execute(Event /*event*/)
{
    if (!IsDesignatedWarlockTank(bot))
        return false;

    GuidVector attackers = context->GetValue<GuidVector>("attackers")->Get();
    TwinAssignments assignment = GetTwinAssignments(bot, botAI, attackers);
    if (!assignment.veklor || assignment.sideEmperor != assignment.veklor)
        return false;

    Unit* veklor = assignment.veklor;
    bool acted = false;

    float d = bot->GetDistance2d(veklor);
    if (d < 20.0f || d > 32.0f)
    {
        float dx = bot->GetPositionX() - veklor->GetPositionX();
        float dy = bot->GetPositionY() - veklor->GetPositionY();
        float len = std::sqrt(dx * dx + dy * dy);
        if (len < 0.1f)
        {
            dx = std::cos(bot->GetOrientation());
            dy = std::sin(bot->GetOrientation());
            len = 1.0f;
        }

        float desired = 24.0f;
        float moveX = veklor->GetPositionX() + (dx / len) * desired;
        float moveY = veklor->GetPositionY() + (dy / len) * desired;
        acted = MoveTo(bot->GetMapId(), moveX, moveY, bot->GetPositionZ(), false, false, false, false,
                       MovementPriority::MOVEMENT_COMBAT) || acted;
    }

    if (botAI->CanCastSpell("shadow ward", bot) && !botAI->HasAura("shadow ward", bot))
        acted = botAI->CastSpell("shadow ward", bot) || acted;

    if (botAI->CanCastSpell("searing pain", veklor))
        acted = botAI->CastSpell("searing pain", veklor) || acted;
    else if (AI_VALUE(Unit*, "current target") != veklor)
        acted = Attack(veklor) || acted;

    return acted;
}

bool Aq40TwinEmperorsAvoidArcaneBurstAction::Execute(Event /*event*/)
{
    if (IsDesignatedWarlockTank(bot))
        return false;

    GuidVector attackers = context->GetValue<GuidVector>("attackers")->Get();
    TwinAssignments assignment = GetTwinAssignments(bot, botAI, attackers);
    if (!assignment.veklor)
        return false;

    Unit* veklor = assignment.veklor;
    if (bot->GetDistance2d(veklor) > 18.0f)
        return false;

    float desiredRange = botAI->IsHeal(bot) ? 22.0f : 28.0f;
    float dx = bot->GetPositionX() - veklor->GetPositionX();
    float dy = bot->GetPositionY() - veklor->GetPositionY();
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.1f)
    {
        dx = std::cos(bot->GetOrientation());
        dy = std::sin(bot->GetOrientation());
        len = 1.0f;
    }

    float moveX = veklor->GetPositionX() + (dx / len) * desiredRange;
    float moveY = veklor->GetPositionY() + (dy / len) * desiredRange;
    return MoveTo(bot->GetMapId(), moveX, moveY, bot->GetPositionZ(), false, false, false, false,
                  MovementPriority::MOVEMENT_COMBAT);
}
