#include "RaidAq40Actions.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "CreatureAI.h"
#include "../RaidAq40BossHelper.h"
#include "RaidBossHelpers.h"
#include "../RaidAq40SpellIds.h"

namespace
{
float constexpr kPi = 3.14159265f;
float constexpr kTwinRoomCenterX = -8961.7f;
float constexpr kTwinRoomCenterY = 1273.65f;
float constexpr kTwinRoomCenterZ = -112.25f;
float constexpr kTwinLeftAnchorX = -8894.3f;
float constexpr kTwinLeftAnchorY = 1285.5f;
float constexpr kTwinRightAnchorX = -9029.1f;
float constexpr kTwinRightAnchorY = 1261.8f;
}
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

std::vector<Unit*> FindTwinSideBugs(PlayerbotAI* botAI, GuidVector const& attackers)
{
    return FindUnitsByAnyName(botAI, attackers, { "mutate bug", "qiraji scarab", "qiraji scorpion", "scarab", "scorpion" });
}
}    // namespace Aq40BossActions

namespace
{
bool IsTwinAssignedSideBoss(Aq40Helpers::TwinAssignments const& assignment, Unit* boss)
{
    return boss && assignment.sideEmperor == boss;
}

void PinTwinTarget(PlayerbotAI* botAI, AiObjectContext* context, Unit* target)
{
    if (!botAI || !context || !target)
        return;

    ObjectGuid const guid = target->GetGUID();
    botAI->GetAiObjectContext()->GetValue<GuidVector>("prioritized targets")->Set({ guid });
    context->GetValue<ObjectGuid>("pull target")->Set(guid);
}

Unit* FindTwinPriorityBugTarget(PlayerbotAI* botAI, Aq40Helpers::TwinAssignments const& assignment,
                                std::vector<Unit*> const& bugs)
{
    Unit* preferred = nullptr;
    for (Unit* bug : bugs)
    {
        if (!bug || !bug->IsAlive() ||
            !Aq40Helpers::IsLikelyOnSameTwinSide(bug, assignment.sideEmperor, assignment.oppositeEmperor))
            continue;

        bool const isMutateBug = botAI->EqualLowercaseName(bug->GetName(), "mutate bug");
        if (!preferred)
        {
            preferred = bug;
            continue;
        }

        bool const preferredIsMutate = botAI->EqualLowercaseName(preferred->GetName(), "mutate bug");
        if (isMutateBug != preferredIsMutate)
        {
            if (isMutateBug)
                preferred = bug;
            continue;
        }

        if (bug->GetHealthPct() < preferred->GetHealthPct())
            preferred = bug;
    }

    return preferred;
}

bool ShouldReserveForTwinBug(Player* bot, PlayerbotAI* botAI, Aq40Helpers::TwinAssignments const& assignment, Unit* sideBug)
{
    if (!bot || !botAI || !sideBug || !assignment.sideEmperor)
        return false;

    bool const isRanged = botAI->IsRanged(bot) && !botAI->IsHeal(bot);
    bool const isMeleeDps = !isRanged && !botAI->IsHeal(bot) && !PlayerbotAI::IsTank(bot);
    if (!isRanged && !isMeleeDps)
        return false;

    bool const bugOnVeklorSide = assignment.sideEmperor == assignment.veklor;
    if (bugOnVeklorSide != isRanged)
        return false;

    Group const* group = bot->GetGroup();
    if (!group)
        return true;

    for (GroupReference const* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        PlayerbotAI* memberAI = GET_PLAYERBOT_AI(member);
        if (!member || member == bot || !memberAI || !member->IsAlive() || !Aq40BossHelper::IsNearEncounter(bot, member))
            continue;

        bool const memberIsRanged = memberAI->IsRanged(member) && !memberAI->IsHeal(member);
        bool const memberIsMeleeDps = !memberIsRanged && !memberAI->IsHeal(member) && !PlayerbotAI::IsTank(member);
        if ((isRanged && !memberIsRanged) || (isMeleeDps && !memberIsMeleeDps))
            continue;

        GuidVector attackers = memberAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get();
        GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(memberAI, attackers);
        Aq40Helpers::TwinAssignments memberAssignment = Aq40Helpers::GetTwinAssignments(member, memberAI, encounterUnits);
        if (memberAssignment.sideEmperor != assignment.sideEmperor)
            continue;

        if (member->GetVictim() == sideBug || memberAI->GetAiObjectContext()->GetValue<Unit*>("current target")->Get() == sideBug)
            return false;
    }

    return true;
}

bool GetTwinFallbackAnchorPosition(Player* bot, PlayerbotAI* botAI, Aq40Helpers::TwinAssignments const& assignment,
                                   bool forPreTeleport, Position& outPosition)
{
    if (!bot || !botAI || !assignment.sideEmperor)
        return false;

    bool const isWarlockTank = Aq40BossHelper::IsDesignatedTwinWarlockTank(bot);
    bool const isMeleeTank = PlayerbotAI::IsTank(bot) && !PlayerbotAI::IsRanged(bot);
    bool const isHealer = botAI->IsHeal(bot);
    bool const isRanged = botAI->IsRanged(bot) && !isHealer;

    if (!isWarlockTank && !isMeleeTank && !isHealer && !isRanged)
        return false;
    if (!forPreTeleport && !isWarlockTank && !isMeleeTank)
        return false;

    float targetX = kTwinRoomCenterX;
    float targetY = kTwinRoomCenterY;
    float targetZ = kTwinRoomCenterZ;
    float tolerance = 8.0f;

    bool const onLeftSide = assignment.sideEmperor->GetPositionX() > kTwinRoomCenterX;
    if (isWarlockTank || isMeleeTank)
    {
        targetX = onLeftSide ? kTwinLeftAnchorX : kTwinRightAnchorX;
        targetY = onLeftSide ? kTwinLeftAnchorY : kTwinRightAnchorY;
        tolerance = forPreTeleport ? 10.0f : 8.0f;
    }
    else if (!forPreTeleport)
    {
        tolerance = 14.0f;
    }

    if (bot->GetDistance(targetX, targetY, targetZ) <= tolerance)
        return false;

    if (!bot->GetMap()->CheckCollisionAndGetValidCoords(bot, bot->GetPositionX(), bot->GetPositionY(),
            bot->GetPositionZ(), targetX, targetY, targetZ))
        return false;

    outPosition.Relocate(targetX, targetY, targetZ);
    return true;
}
}    // namespace

bool Aq40TwinEmperorsChooseTargetAction::Execute(Event /*event*/)
{
    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, context->GetValue<GuidVector>("attackers")->Get());
    if (encounterUnits.empty())
        return false;

    Aq40Helpers::TwinAssignments assignment = Aq40Helpers::GetTwinAssignments(bot, botAI, encounterUnits);
    Unit* sideBoss = assignment.sideEmperor;
    if (!sideBoss)
        sideBoss = Aq40BossActions::FindTwinEmperorsTarget(botAI, encounterUnits);
    if (!sideBoss)
        return false;

    Unit* target = nullptr;
    bool const isWarlockTank = Aq40BossHelper::IsDesignatedTwinWarlockTank(bot);
    bool const isMeleeTank = PlayerbotAI::IsTank(bot) && !PlayerbotAI::IsRanged(bot);
    bool const isMeleeDps = !isMeleeTank && !botAI->IsRanged(bot) && !botAI->IsHeal(bot);
    Unit* sideBug = FindTwinPriorityBugTarget(botAI, assignment, Aq40BossActions::FindTwinSideBugs(botAI, encounterUnits));
    Unit* mutateBug = Aq40BossActions::FindTwinMutateBug(botAI, encounterUnits);
    if (isWarlockTank)
    {
        if (IsTwinAssignedSideBoss(assignment, assignment.veklor))
            target = assignment.veklor;
        else
            return false;
    }
    else if (isMeleeTank)
    {
        if (IsTwinAssignedSideBoss(assignment, assignment.veknilash))
            target = assignment.veknilash;
        else
            return false;
    }
    else if (botAI->IsHeal(bot))
        return false;
    else if (isMeleeDps && sideBug && ShouldReserveForTwinBug(bot, botAI, assignment, sideBug))
        target = sideBug;
    else if (isMeleeDps && IsTwinAssignedSideBoss(assignment, assignment.veknilash) && mutateBug &&
             Aq40Helpers::IsLikelyOnSameTwinSide(mutateBug, assignment.sideEmperor, assignment.oppositeEmperor))
        target = mutateBug;
    else if (botAI->IsRanged(bot))
    {
        if (!IsTwinAssignedSideBoss(assignment, assignment.veklor))
            return false;
        target = sideBug && ShouldReserveForTwinBug(bot, botAI, assignment, sideBug) ? sideBug : assignment.veklor;
    }
    else
    {
        if (!IsTwinAssignedSideBoss(assignment, assignment.veknilash))
            return false;
        target = assignment.veknilash;
    }

    if (!target)
        return false;

    // RTI marking: the melee tank marks both bosses so the raid has
    // clear icons to follow.  Diamond = Vek'nilash (physical), Square = Vek'lor (magic).
    if (isMeleeTank && assignment.veknilash && assignment.veklor)
    {
        MarkTargetWithDiamond(bot, assignment.veknilash);
        MarkTargetWithSquare(bot, assignment.veklor);
    }

    // Twin Emperors are immune to taunt, so threat must be built organically.
    // Non-tank bots must wait for the assigned tank to establish threat before
    // attacking a boss — both at initial pull and after teleport recovery.
    // Additionally, enforce a short DPS pause during the teleport recovery
    // window so tanks can re-establish aggro before DPS piles on.
    bool const targetingBoss = target == assignment.veklor || target == assignment.veknilash;
    if (targetingBoss && !isWarlockTank && !isMeleeTank &&
        (Aq40Helpers::IsTwinTeleportRecoveryWindow(bot, botAI, encounterUnits) ||
         !Aq40Helpers::IsTwinAssignedTankReady(bot, botAI, assignment)))
        return false;

    if (AI_VALUE(Unit*, "current target") == target && bot->GetVictim() == target)
        return false;

    PinTwinTarget(botAI, context, target);

    // Four Horsemen pattern: if the target is not in line of sight
    // (e.g. after teleport swap, boss is across the room), move closer
    // before attempting Attack which would fail silently.
    // Use role-appropriate ranges so ranged DPS/healers don't collapse to melee.
    if (!bot->IsWithinLOSInMap(target))
    {
        float losRange;
        if (isWarlockTank)
            losRange = 24.0f;
        else if (botAI->IsRanged(bot) || botAI->IsHeal(bot))
            losRange = 28.0f;
        else
            losRange = 4.0f;
        return MoveNear(target, losRange, MovementPriority::MOVEMENT_COMBAT);
    }

    return Attack(target);
}

bool Aq40TwinEmperorsHoldSplitAction::Execute(Event /*event*/)
{
    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, context->GetValue<GuidVector>("attackers")->Get());
    Aq40Helpers::TwinAssignments assignment = Aq40Helpers::GetTwinAssignments(bot, botAI, encounterUnits);
    if (!assignment.sideEmperor)
        return false;

    bool isWarlockTank = Aq40BossHelper::IsDesignatedTwinWarlockTank(bot);
    bool isMeleeTank = PlayerbotAI::IsTank(bot) && !PlayerbotAI::IsRanged(bot);
    bool isRangedDps = botAI->IsRanged(bot) && !botAI->IsHeal(bot);
    bool isMeleeDps = !isMeleeTank && !isRangedDps && !botAI->IsHeal(bot);
    if (!isMeleeTank && !isMeleeDps && !botAI->IsHeal(bot) && !isWarlockTank && !isRangedDps)
        return false;

    Unit* sideBoss = assignment.sideEmperor;
    Unit* oppositeBoss = assignment.oppositeEmperor;
    if (!sideBoss)
        return false;

    Position fallbackAnchor;
    if (GetTwinFallbackAnchorPosition(bot, botAI, assignment, false, fallbackAnchor))
    {
        return MoveTo(bot->GetMapId(), fallbackAnchor.GetPositionX(), fallbackAnchor.GetPositionY(),
                      fallbackAnchor.GetPositionZ(), false, false, false, true,
                      MovementPriority::MOVEMENT_COMBAT, true, false);
    }

    float desiredRange = 0.0f;
    if (botAI->IsHeal(bot))
        desiredRange = 20.0f;
    else if (isWarlockTank)
        desiredRange = sideBoss == assignment.veklor ? 24.0f : 28.0f;
    else if (isMeleeTank)
        desiredRange = sideBoss == assignment.veknilash ? 4.0f : 25.0f;
    else if (isRangedDps)
        desiredRange = 28.0f;
    else
        desiredRange = sideBoss == assignment.veknilash ? 4.0f : 22.0f;

    // Position on the far side of our boss, away from the opposite boss,
    // to help maintain separation between the twins.
    if (oppositeBoss)
    {
        float const awayAngle = std::atan2(sideBoss->GetPositionY() - oppositeBoss->GetPositionY(),
                                           sideBoss->GetPositionX() - oppositeBoss->GetPositionX());
        float targetX = sideBoss->GetPositionX() + desiredRange * std::cos(awayAngle);
        float targetY = sideBoss->GetPositionY() + desiredRange * std::sin(awayAngle);
        float targetZ = bot->GetPositionZ();

        if (bot->GetExactDist2d(targetX, targetY) <= 3.0f)
            return false;

        if (!bot->GetMap()->CheckCollisionAndGetValidCoords(bot, bot->GetPositionX(), bot->GetPositionY(),
                bot->GetPositionZ(), targetX, targetY, targetZ))
            return false;

        return MoveTo(bot->GetMapId(), targetX, targetY, targetZ, false, false, false, true,
                      MovementPriority::MOVEMENT_COMBAT, true, false);
    }

    float distance = bot->GetDistance2d(sideBoss);
    if (std::abs(distance - desiredRange) <= 3.0f)
        return false;

    return MoveTo(sideBoss, desiredRange, MovementPriority::MOVEMENT_COMBAT);
}

bool Aq40TwinEmperorsPreTeleportStageAction::Execute(Event /*event*/)
{
    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, context->GetValue<GuidVector>("attackers")->Get());
    Aq40Helpers::TwinAssignments assignment = Aq40Helpers::GetTwinAssignments(bot, botAI, encounterUnits);
    if (!assignment.sideEmperor || !Aq40Helpers::IsTwinPreTeleportWindow(bot, botAI, encounterUnits))
        return false;

    bool const isWarlockTank = Aq40BossHelper::IsDesignatedTwinWarlockTank(bot);
    bool const isTank = PlayerbotAI::IsTank(bot) && !PlayerbotAI::IsRanged(bot);
    bool const isHealer = botAI->IsHeal(bot);
    bool const isRanged = botAI->IsRanged(bot) && !isHealer;
    bool const isMeleeDps = !isWarlockTank && !isTank && !isHealer && !isRanged;
    if (!isWarlockTank && !isTank && !isHealer && !isRanged && !isMeleeDps)
        return false;

    if (isMeleeDps)
    {
        if (bot->GetDistance(kTwinRoomCenterX, kTwinRoomCenterY, kTwinRoomCenterZ) <= 12.0f)
            return false;

        return MoveTo(bot->GetMapId(), kTwinRoomCenterX, kTwinRoomCenterY, kTwinRoomCenterZ, false, false, false, true,
                      MovementPriority::MOVEMENT_COMBAT, true, false);
    }

    Position fallbackAnchor;
    if (!GetTwinFallbackAnchorPosition(bot, botAI, assignment, true, fallbackAnchor))
        return false;

    return MoveTo(bot->GetMapId(), fallbackAnchor.GetPositionX(), fallbackAnchor.GetPositionY(),
                  fallbackAnchor.GetPositionZ(), false, false, false, true,
                  MovementPriority::MOVEMENT_COMBAT, true, false);
}

bool Aq40TwinEmperorsWarlockTankAction::Execute(Event /*event*/)
{
    if (!Aq40BossHelper::IsDesignatedTwinWarlockTank(bot))
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, context->GetValue<GuidVector>("attackers")->Get());
    Aq40Helpers::TwinAssignments assignment = Aq40Helpers::GetTwinAssignments(bot, botAI, encounterUnits);
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
    else if (AI_VALUE(Unit*, "current target") != veklor || bot->GetVictim() != veklor)
    {
        PinTwinTarget(botAI, context, veklor);
        acted = Attack(veklor) || acted;
    }

    return acted;
}

bool Aq40TwinEmperorsAvoidArcaneBurstAction::Execute(Event /*event*/)
{
    if (Aq40BossHelper::IsDesignatedTwinWarlockTank(bot))
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, context->GetValue<GuidVector>("attackers")->Get());
    Aq40Helpers::TwinAssignments assignment = Aq40Helpers::GetTwinAssignments(bot, botAI, encounterUnits);
    if (!assignment.veklor)
        return false;

    Unit* veklor = assignment.veklor;
    Spell* spell = veklor->GetCurrentSpell(CURRENT_GENERIC_SPELL);
    bool const castingArcaneBurst =
        spell && Aq40SpellIds::MatchesAnySpellId(spell->GetSpellInfo(), { Aq40SpellIds::TwinArcaneBurst });
    float const dangerRange = castingArcaneBurst ? 22.0f : 18.0f;
    if (bot->GetDistance2d(veklor) > dangerRange)
        return false;

    float desiredRange = botAI->IsHeal(bot) ? 22.0f : 28.0f;
    bot->AttackStop();
    bot->InterruptNonMeleeSpells(true);
    return MoveTo(veklor, desiredRange, MovementPriority::MOVEMENT_COMBAT);
}

bool Aq40TwinEmperorsAvoidBlizzardAction::Execute(Event /*event*/)
{
    if ((!botAI->IsRanged(bot) && !botAI->IsHeal(bot)) || Aq40BossHelper::IsDesignatedTwinWarlockTank(bot))
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, context->GetValue<GuidVector>("attackers")->Get());
    Aq40Helpers::TwinAssignments assignment = Aq40Helpers::GetTwinAssignments(bot, botAI, encounterUnits);
    if (!assignment.veklor || assignment.sideEmperor != assignment.veklor)
        return false;

    if (!Aq40SpellIds::HasAnyAura(botAI, bot, { Aq40SpellIds::TwinBlizzard }))
    {
        Spell* spell = assignment.veklor->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        if (!(spell && Aq40SpellIds::MatchesAnySpellId(spell->GetSpellInfo(), { Aq40SpellIds::TwinBlizzard })))
            return false;
    }

    Group const* group = bot->GetGroup();
    if (!group)
        return false;

    std::vector<Player*> healers;
    std::vector<Player*> rangedDps;
    for (GroupReference const* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || !member->IsAlive() || !Aq40BossHelper::IsNearEncounter(bot, member))
            continue;

        PlayerbotAI* memberAI = GET_PLAYERBOT_AI(member);
        if (!memberAI)
            continue;

        if ((!memberAI->IsRanged(member) && !memberAI->IsHeal(member)) ||
            Aq40BossHelper::IsDesignatedTwinWarlockTank(member))
            continue;

    // Only include members assigned to the Vek'lor side.
    // DPS roles always follow Vek'lor, healers use stable role index.
        bool const memberOnVeklorSide =
            (memberAI->IsRanged(member) && !memberAI->IsHeal(member)) ||
            (memberAI->IsHeal(member) &&
             Aq40Helpers::GetStableTwinRoleIndex(member, memberAI) ==
             Aq40Helpers::GetStableTwinRoleIndex(bot, botAI));
        if (!memberOnVeklorSide)
            continue;

        if (memberAI->IsHeal(member))
            healers.push_back(member);
        else
            rangedDps.push_back(member);
    }

    std::vector<Player*>& cohort = botAI->IsHeal(bot) ? healers : rangedDps;
    auto botIt = std::find(cohort.begin(), cohort.end(), bot);
    if (cohort.empty() || botIt == cohort.end())
        return false;

    size_t const botIndex = static_cast<size_t>(std::distance(cohort.begin(), botIt));
    size_t const count = cohort.size();
    float const arcSpan = botAI->IsHeal(bot) ? kPi / 2.0f : 2.0f * kPi / 3.0f;
    float const radius = botAI->IsHeal(bot) ? 36.0f : 32.0f;
    float const centerAngle = std::atan2(assignment.veklor->GetPositionY() - assignment.veknilash->GetPositionY(),
                                         assignment.veklor->GetPositionX() - assignment.veknilash->GetPositionX());
    float const arcStart = centerAngle - arcSpan / 2.0f;
    float const angle = count == 1 ? centerAngle :
        arcStart + arcSpan * static_cast<float>(botIndex) / static_cast<float>(count - 1);
    float targetX = assignment.veklor->GetPositionX() + radius * std::cos(angle);
    float targetY = assignment.veklor->GetPositionY() + radius * std::sin(angle);
    float targetZ = bot->GetPositionZ();

    if (bot->GetExactDist2d(targetX, targetY) <= 1.5f)
        return false;

    if (!bot->GetMap()->CheckCollisionAndGetValidCoords(bot, bot->GetPositionX(), bot->GetPositionY(),
            bot->GetPositionZ(), targetX, targetY, targetZ))
        return false;

    bot->AttackStop();
    bot->InterruptNonMeleeSpells(true);
    return MoveTo(bot->GetMapId(), targetX, targetY, targetZ, false, false, false, true,
                  MovementPriority::MOVEMENT_FORCED, true, false);
}

bool Aq40TwinEmperorsEnforceSeparationAction::Execute(Event /*event*/)
{
    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, context->GetValue<GuidVector>("attackers")->Get());
    Aq40Helpers::TwinAssignments assignment = Aq40Helpers::GetTwinAssignments(bot, botAI, encounterUnits);
    if (!assignment.veklor || !assignment.veknilash)
        return false;

    if (assignment.veklor->GetDistance2d(assignment.veknilash) >= 60.0f)
        return false;

    bool isWarlockTank = Aq40BossHelper::IsDesignatedTwinWarlockTank(bot);
    bool isMeleeTank = PlayerbotAI::IsTank(bot) && !PlayerbotAI::IsRanged(bot);
    if (!isWarlockTank && !isMeleeTank)
        return false;

    Unit* desiredBoss = isWarlockTank ? assignment.veklor : assignment.veknilash;
    // Only enforce separation when actively tanking (correct boss on our side).
    if (assignment.sideEmperor != desiredBoss)
        return false;

    Unit* otherBoss = isWarlockTank ? assignment.veknilash : assignment.veklor;
    if (!desiredBoss || !otherBoss)
        return false;

    bool acted = false;
    if (AI_VALUE(Unit*, "current target") != desiredBoss || bot->GetVictim() != desiredBoss)
        acted = Attack(desiredBoss) || acted;

    // Move to a position on the far side of our boss, away from the other boss,
    // so that our boss is pulled toward us and away from its twin.
    float const desiredRange = isWarlockTank ? 24.0f : 4.0f;
    float const awayAngle = std::atan2(desiredBoss->GetPositionY() - otherBoss->GetPositionY(),
                                       desiredBoss->GetPositionX() - otherBoss->GetPositionX());
    float targetX = desiredBoss->GetPositionX() + desiredRange * std::cos(awayAngle);
    float targetY = desiredBoss->GetPositionY() + desiredRange * std::sin(awayAngle);
    float targetZ = bot->GetPositionZ();

    if (bot->GetExactDist2d(targetX, targetY) <= 2.0f)
        return acted;

    if (!bot->GetMap()->CheckCollisionAndGetValidCoords(bot, bot->GetPositionX(), bot->GetPositionY(),
            bot->GetPositionZ(), targetX, targetY, targetZ))
        return acted;

    acted = MoveTo(bot->GetMapId(), targetX, targetY, targetZ, false, false, false, true,
                   MovementPriority::MOVEMENT_FORCED, true, false) || acted;

    return acted;
}

    // Vek'lor is immune to physical damage.  Pets assigned to that side would
    // generate nothing but IMMUNE spam.  Set them passive when the owner is on
    // the Vek'lor side, and restore defensive mode on the Vek'nilash side.
bool Aq40TwinEmperorsPetControlAction::Execute(Event /*event*/)
{
    Pet* pet = bot->GetPet();
    if (!pet)
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, context->GetValue<GuidVector>("attackers")->Get());
    Aq40Helpers::TwinAssignments assignment = Aq40Helpers::GetTwinAssignments(bot, botAI, encounterUnits);
    if (!assignment.veklor || !assignment.veknilash)
        return false;

    bool const onVeklorSide = assignment.sideEmperor == assignment.veklor;

    if (onVeklorSide)
    {
    // Check if there are side bugs the pet could attack instead.
        std::vector<Unit*> sideBugs = Aq40BossActions::FindTwinSideBugs(botAI, encounterUnits);
        Unit* bugTarget = nullptr;
        for (Unit* bug : sideBugs)
        {
            if (bug && bug->IsAlive() &&
                Aq40Helpers::IsLikelyOnSameTwinSide(bug, assignment.sideEmperor, assignment.oppositeEmperor))
            {
                bugTarget = bug;
                break;
            }
        }

        if (bugTarget)
        {
            if (pet->GetReactState() == REACT_PASSIVE)
                pet->SetReactState(REACT_DEFENSIVE);

            if (pet->GetVictim() != bugTarget)
            {
                pet->AttackStop();
                pet->SetTarget(bugTarget->GetGUID());
                pet->ToCreature()->AI()->AttackStart(bugTarget);
                return true;
            }
            return false;
        }

    // No bugs available — go passive so the pet doesn't hit the immune boss.
        if (pet->GetReactState() != REACT_PASSIVE)
        {
            pet->AttackStop();
            pet->SetReactState(REACT_PASSIVE);
            return true;
        }
        return false;
    }

    // Vek'nilash side — physical damage works fine, restore normal behaviour.
    if (pet->GetReactState() == REACT_PASSIVE)
    {
        pet->SetReactState(REACT_DEFENSIVE);
        return true;
    }
    return false;
}

bool Aq40TwinEmperorsMoveAwayFromBrotherAction::Execute(Event /*event*/)
{
    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, context->GetValue<GuidVector>("attackers")->Get());
    Aq40Helpers::TwinAssignments assignment = Aq40Helpers::GetTwinAssignments(bot, botAI, encounterUnits);
    if (!assignment.sideEmperor || !assignment.oppositeEmperor)
        return false;

    ObjectGuid const botGuid = bot->GetGUID();
    ObjectGuid petGuid = ObjectGuid::Empty;
    if (Pet* pet = bot->GetPet())
        petGuid = pet->GetGUID();

    Unit* moveAwayFrom = nullptr;
    if (assignment.sideEmperor->GetTarget() == botGuid || (petGuid && assignment.sideEmperor->GetTarget() == petGuid))
        moveAwayFrom = assignment.oppositeEmperor;
    else if (assignment.oppositeEmperor->GetTarget() == botGuid ||
             (petGuid && assignment.oppositeEmperor->GetTarget() == petGuid))
        moveAwayFrom = assignment.sideEmperor;

    if (!moveAwayFrom)
        return false;

    float const currentDistance = bot->GetDistance2d(moveAwayFrom);
    float const desiredDistance = 120.0f;
    if (currentDistance >= desiredDistance)
        return false;

    if (petGuid)
        botAI->PetFollow();

    bot->AttackStop();
    bot->InterruptNonMeleeSpells(true);
    return MoveAway(moveAwayFrom, desiredDistance - currentDistance);
}
