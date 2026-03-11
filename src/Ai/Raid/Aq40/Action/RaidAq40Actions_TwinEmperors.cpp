#include "RaidAq40Actions.h"

#include <cmath>

#include "../RaidAq40BossHelper.h"
#include "../Util/RaidAq40Helpers.h"

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

bool Aq40TwinEmperorsChooseTargetAction::Execute(Event /*event*/)
{
    GuidVector attackers = context->GetValue<GuidVector>("attackers")->Get();
    if (attackers.empty())
        return false;

    Aq40Helpers::TwinAssignments assignment = Aq40Helpers::GetTwinAssignments(bot, botAI, attackers);
    bool const inRecoveryWindow = Aq40Helpers::IsTwinTeleportRecoveryWindow(bot, botAI, attackers);
    Unit* sideBoss = assignment.sideEmperor;
    if (!sideBoss)
        sideBoss = Aq40BossActions::FindTwinEmperorsTarget(botAI, attackers);
    if (!sideBoss)
        return false;

    Unit* target = nullptr;
    bool isWarlockTank = Aq40BossHelper::IsDesignatedTwinWarlockTank(bot);
    bool isMeleeTank = PlayerbotAI::IsTank(bot) && !PlayerbotAI::IsRanged(bot);
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
    else if (inRecoveryWindow && !Aq40Helpers::IsTwinAssignedTankReady(bot, botAI, assignment))
        return false;
    else if (isMeleeDps && mutateBug &&
             Aq40Helpers::IsLikelyOnSameTwinSide(mutateBug, assignment.sideEmperor, assignment.oppositeEmperor))
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
    Aq40Helpers::TwinAssignments assignment = Aq40Helpers::GetTwinAssignments(bot, botAI, attackers);
    if (!assignment.sideEmperor)
        return false;

    bool isWarlockTank = Aq40BossHelper::IsDesignatedTwinWarlockTank(bot);
    bool isMeleeTank = PlayerbotAI::IsTank(bot) && !PlayerbotAI::IsRanged(bot);
    bool isRangedDps = botAI->IsRanged(bot) && !botAI->IsHeal(bot);
    if (!isMeleeTank && !botAI->IsHeal(bot) && !isWarlockTank && !isRangedDps)
        return false;

    Unit* sideBoss = assignment.sideEmperor;
    if (isRangedDps && assignment.veklor)
        sideBoss = assignment.veklor;

    float distance = bot->GetDistance2d(sideBoss);
    float desiredRange = 0.0f;
    if (botAI->IsHeal(bot))
        desiredRange = 20.0f;
    else if (isWarlockTank)
        desiredRange = sideBoss == assignment.veklor ? 24.0f : 10.0f;
    else if (isRangedDps)
        desiredRange = 28.0f;
    else
        desiredRange = sideBoss == assignment.veknilash ? 4.0f : 22.0f;

    if (std::abs(distance - desiredRange) <= 3.0f)
        return false;

    return MoveTo(sideBoss, desiredRange, MovementPriority::MOVEMENT_COMBAT);
}

bool Aq40TwinEmperorsWarlockTankAction::Execute(Event /*event*/)
{
    if (!Aq40BossHelper::IsDesignatedTwinWarlockTank(bot))
        return false;

    GuidVector attackers = context->GetValue<GuidVector>("attackers")->Get();
    Aq40Helpers::TwinAssignments assignment = Aq40Helpers::GetTwinAssignments(bot, botAI, attackers);
    if (!assignment.veklor || assignment.sideEmperor != assignment.veklor)
        return false;

    Unit* veklor = assignment.veklor;
    bool acted = false;

    float d = bot->GetDistance2d(veklor);
    if (d < 20.0f || d > 32.0f)
    {
        acted = MoveTo(veklor, 24.0f, MovementPriority::MOVEMENT_COMBAT) || acted;
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
    if (Aq40BossHelper::IsDesignatedTwinWarlockTank(bot))
        return false;

    GuidVector attackers = context->GetValue<GuidVector>("attackers")->Get();
    Aq40Helpers::TwinAssignments assignment = Aq40Helpers::GetTwinAssignments(bot, botAI, attackers);
    if (!assignment.veklor)
        return false;

    Unit* veklor = assignment.veklor;
    if (bot->GetDistance2d(veklor) > 18.0f)
        return false;

    float desiredRange = botAI->IsHeal(bot) ? 22.0f : 28.0f;
    bot->AttackStop();
    bot->InterruptNonMeleeSpells(true);
    return MoveTo(veklor, desiredRange, MovementPriority::MOVEMENT_COMBAT);
}

bool Aq40TwinEmperorsEnforceSeparationAction::Execute(Event /*event*/)
{
    GuidVector attackers = context->GetValue<GuidVector>("attackers")->Get();
    Aq40Helpers::TwinAssignments assignment = Aq40Helpers::GetTwinAssignments(bot, botAI, attackers);
    if (!assignment.veklor || !assignment.veknilash)
        return false;

    if (assignment.veklor->GetDistance2d(assignment.veknilash) >= 22.0f)
        return false;

    bool isWarlockTank = Aq40BossHelper::IsDesignatedTwinWarlockTank(bot);
    bool isMeleeTank = PlayerbotAI::IsTank(bot) && !PlayerbotAI::IsRanged(bot);
    if (!isWarlockTank && !isMeleeTank)
        return false;

    Unit* desiredBoss = isWarlockTank ? assignment.veklor : assignment.veknilash;
    if (!desiredBoss)
        return false;

    bool acted = false;
    if (AI_VALUE(Unit*, "current target") != desiredBoss)
        acted = Attack(desiredBoss) || acted;

    float desiredRange = isWarlockTank ? 24.0f : 4.0f;
    acted = MoveTo(desiredBoss, desiredRange, MovementPriority::MOVEMENT_COMBAT) || acted;

    return acted;
}
