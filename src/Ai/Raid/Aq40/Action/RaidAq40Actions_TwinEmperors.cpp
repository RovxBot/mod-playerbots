#include "RaidAq40Actions.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "CreatureAI.h"
#include "SharedDefines.h"
#include "../RaidAq40BossHelper.h"
#include "RaidBossHelpers.h"
#include "../RaidAq40SpellIds.h"

namespace
{
float constexpr kPi = 3.14159265f;
// Ground these Twin Emps positions in the repo's travel node data:
// - The Master's Eye = room center / mid-room staging lane.
// - Emperor Vek'lor / Vek'nilash = initial side references for pre-pull setup.
float constexpr kTwinRoomCenterX = -8953.3f;
float constexpr kTwinRoomCenterY = 1233.64f;
float constexpr kTwinRoomCenterZ = -99.718f;
float constexpr kTwinInitialVeklorX = -8874.06f;
float constexpr kTwinInitialVeklorY = 1204.74f;
float constexpr kTwinInitialVeklorZ = -104.17f;
float constexpr kTwinInitialVeknilashX = -9019.14f;
float constexpr kTwinInitialVeknilashY = 1180.07f;
float constexpr kTwinInitialVeknilashZ = -104.17f;
float constexpr kTwinTankStageDistanceFromBoss = 42.0f;
float constexpr kTwinTankPreTeleportDistanceFromBoss = 32.0f;
}
#include "../Util/RaidAq40Helpers.h"

namespace Aq40BossActions
{
Unit* FindTwinMutateBug(PlayerbotAI* botAI, GuidVector const& attackers)
{
    if (!botAI)
        return nullptr;

    for (ObjectGuid const guid : attackers)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit)
            continue;

        if (Aq40Helpers::IsTwinMutateBug(botAI, unit))
            return unit;
    }

    return nullptr;
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

bool IsTwinPrimaryTankOnActiveBoss(Player* bot, Aq40Helpers::TwinAssignments const& assignment)
{
    if (!bot)
        return false;

    if (Aq40BossHelper::IsDesignatedTwinWarlockTank(bot))
        return assignment.sideEmperor && assignment.sideEmperor == assignment.veklor;

    if (PlayerbotAI::IsTank(bot) && !PlayerbotAI::IsRanged(bot))
        return assignment.sideEmperor && assignment.sideEmperor == assignment.veknilash;

    return false;
}

bool ShouldDelayTwinPreTeleportStage(Player* bot, PlayerbotAI* botAI, Aq40Helpers::TwinAssignments const& assignment)
{
    if (!bot || !botAI || !assignment.veklor)
        return false;

    if (Aq40BossHelper::IsDesignatedTwinWarlockTank(bot))
        return false;

    if (!botAI->IsRanged(bot) && !botAI->IsHeal(bot))
        return false;

    if (Aq40SpellIds::HasAnyAura(botAI, bot, { Aq40SpellIds::TwinBlizzard }))
        return true;

    Spell* spell = assignment.veklor->GetCurrentSpell(CURRENT_GENERIC_SPELL);
    bool const castingArcaneBurst =
        spell && Aq40SpellIds::MatchesAnySpellId(spell->GetSpellInfo(), { Aq40SpellIds::TwinArcaneBurst });
    bool const castingBlizzard =
        spell && Aq40SpellIds::MatchesAnySpellId(spell->GetSpellInfo(), { Aq40SpellIds::TwinBlizzard });

    if (bot->GetDistance2d(assignment.veklor) <= (castingArcaneBurst ? 22.0f : 18.0f))
        return true;

    // Only delay for bots already standing in a Blizzard (aura check above).
    // The previous blanket "Vek'lor casting Blizzard within 36y" check blocked
    // nearly every ranged/healer from pre-staging, causing mass positioning
    // failures after teleports.
    return false;
}

bool IsTwinBugUnit(PlayerbotAI* botAI, Unit* bug)
{
    return bug && Aq40BossHelper::IsUnitNamedAny(botAI, bug,
        { "mutate bug", "qiraji scarab", "qiraji scorpion", "scarab", "scorpion" });
}

bool IsTwinNonTankDps(Player* bot, PlayerbotAI* botAI)
{
    return bot && botAI && !PlayerbotAI::IsTank(bot) && !botAI->IsHeal(bot);
}

bool GetTwinPositionCohortSlot(Player* bot, PlayerbotAI* botAI, bool healerCohort,
                               size_t& botIndex, size_t& count)
{
    if (!bot || !botAI)
        return false;

    Group const* group = bot->GetGroup();
    if (!group)
    {
        botIndex = 0;
        count = 1;
        return true;
    }

    std::vector<Player*> cohort;
    uint32 const healerSideIndex = healerCohort ? Aq40Helpers::GetStableTwinRoleIndex(bot, botAI) : 0u;
    for (GroupReference const* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || !member->IsAlive() || !Aq40BossHelper::IsEncounterParticipant(bot, member))
            continue;

        PlayerbotAI* memberAI = GET_PLAYERBOT_AI(member);
        if (!memberAI)
            continue;

        if (healerCohort)
        {
            if (!memberAI->IsHeal(member) ||
                Aq40Helpers::GetStableTwinRoleIndex(member, memberAI) != healerSideIndex)
                continue;
        }
        else if (!memberAI->IsRanged(member) || memberAI->IsHeal(member))
        {
            continue;
        }

        cohort.push_back(member);
    }

    auto const botIt = std::find(cohort.begin(), cohort.end(), bot);
    if (cohort.empty() || botIt == cohort.end())
        return false;

    botIndex = static_cast<size_t>(std::distance(cohort.begin(), botIt));
    count = cohort.size();
    return true;
}

float GetTwinCenteredSlotOffset(size_t botIndex, size_t count, float spacing)
{
    if (count <= 1)
        return 0.0f;

    float const centeredIndex =
        static_cast<float>(botIndex) - (static_cast<float>(count - 1) / 2.0f);
    return centeredIndex * spacing;
}

bool HasTwinBossAggro(Player* bot, Unit* boss)
{
    if (!bot || !boss)
        return false;

    ObjectGuid const botGuid = bot->GetGUID();
    ObjectGuid petGuid = ObjectGuid::Empty;
    if (Pet* pet = bot->GetPet())
        petGuid = pet->GetGUID();

    return boss->GetTarget() == botGuid || (petGuid && boss->GetTarget() == petGuid) ||
           bot->GetVictim() == boss;
}

bool IsTwinDpsDraggingMeleeBoss(Player* bot, PlayerbotAI* botAI, Aq40Helpers::TwinAssignments const& assignment)
{
    if (!IsTwinNonTankDps(bot, botAI) || !assignment.veknilash)
        return false;

    return HasTwinBossAggro(bot, assignment.veknilash);
}

void PinTwinTarget(PlayerbotAI* botAI, AiObjectContext* context, Unit* target)
{
    if (!botAI || !context || !target)
        return;

    ObjectGuid const guid = target->GetGUID();
    botAI->GetAiObjectContext()->GetValue<GuidVector>("prioritized targets")->Set({ guid });
    context->GetValue<ObjectGuid>("pull target")->Set(guid);
}

Unit* FindTwinPriorityBugTarget(Player* bot, PlayerbotAI* botAI, Aq40Helpers::TwinAssignments const& assignment,
                                std::vector<Unit*> const& bugs)
{
    Unit* preferred = nullptr;
    for (Unit* bug : bugs)
    {
        if (!IsTwinBugUnit(botAI, bug) || !Aq40Helpers::IsTwinCriticalSideBug(bot, botAI, assignment, bug))
            continue;

        bool const isMutateBug = Aq40Helpers::IsTwinMutateBug(botAI, bug);
        if (!preferred)
        {
            preferred = bug;
            continue;
        }

        bool const preferredIsMutate = Aq40Helpers::IsTwinMutateBug(botAI, preferred);
        if (isMutateBug != preferredIsMutate)
        {
            if (isMutateBug)
                preferred = bug;
            continue;
        }

        bool const isExplodeBug = Aq40Helpers::IsTwinExplodeBug(botAI, bug);
        bool const preferredExplodeBug = Aq40Helpers::IsTwinExplodeBug(botAI, preferred);
        if (isExplodeBug != preferredExplodeBug)
        {
            if (isExplodeBug)
                preferred = bug;
            continue;
        }

        if (bug->GetHealthPct() < preferred->GetHealthPct())
            preferred = bug;
    }

    return preferred;
}

Unit* FindTwinPetBugTarget(PlayerbotAI* botAI, std::vector<Unit*> const& bugs)
{
    Unit* preferred = nullptr;
    for (Unit* bug : bugs)
    {
        if (!IsTwinBugUnit(botAI, bug) || !bug->IsAlive())
            continue;

        if (!preferred)
        {
            preferred = bug;
            continue;
        }

        bool const isMutateBug = Aq40Helpers::IsTwinMutateBug(botAI, bug);
        bool const preferredIsMutate = Aq40Helpers::IsTwinMutateBug(botAI, preferred);
        if (isMutateBug != preferredIsMutate)
        {
            if (isMutateBug)
                preferred = bug;
            continue;
        }

        bool const isExplodeBug = Aq40Helpers::IsTwinExplodeBug(botAI, bug);
        bool const preferredIsExplode = Aq40Helpers::IsTwinExplodeBug(botAI, preferred);
        if (isExplodeBug != preferredIsExplode)
        {
            if (isExplodeBug)
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

        // Use physical proximity to determine if the member is on the same
        // side rather than comparing sideEmperor pointers.  During teleport
        // windows each bot resolves assignments independently, so the
        // pointers can disagree and falsely filter out members who are
        // actually killing the bug.
        if (!Aq40Helpers::IsLikelyOnSameTwinSide(member, assignment.sideEmperor, assignment.oppositeEmperor))
            continue;

        if (member->GetVictim() == sideBug || memberAI->GetAiObjectContext()->GetValue<Unit*>("current target")->Get() == sideBug)
            return false;
    }

    return true;
}

bool GetTwinFallbackAnchorPosition(Player* bot, PlayerbotAI* botAI, Aq40Helpers::TwinAssignments const& assignment,
                                   bool forPreTeleport, Position& outPosition)
{
    if (!bot || !botAI)
        return false;

    bool const isWarlockTank = Aq40BossHelper::IsDesignatedTwinWarlockTank(bot);
    bool const isMeleeTank = PlayerbotAI::IsTank(bot) && !PlayerbotAI::IsRanged(bot);
    bool const isHealer = botAI->IsHeal(bot);
    bool const isRanged = botAI->IsRanged(bot) && !isHealer;

    if (!isWarlockTank && !isMeleeTank && !isHealer && !isRanged)
        return false;

    float targetX = 0.0f;
    float targetY = 0.0f;
    float targetZ = 0.0f;
    float tolerance = 0.0f;
    bool const twinPullActive = bot->IsInCombat() || Aq40Helpers::IsTwinRaidCombatActive(bot);
    auto buildCenterFacingStagePoint = [bot](Position const& sideBossPosition, float forwardDistance, float lateralOffset,
                                             Position& stagePosition) -> bool
    {
        float directionX = kTwinRoomCenterX - sideBossPosition.GetPositionX();
        float directionY = kTwinRoomCenterY - sideBossPosition.GetPositionY();
        float const length = std::sqrt(directionX * directionX + directionY * directionY);
        if (length < 0.1f)
            return false;

        directionX /= length;
        directionY /= length;
        float const perpendicularX = -directionY;
        float const perpendicularY = directionX;
        float targetStageX = sideBossPosition.GetPositionX() + directionX * forwardDistance + perpendicularX * lateralOffset;
        float targetStageY = sideBossPosition.GetPositionY() + directionY * forwardDistance + perpendicularY * lateralOffset;
        float targetStageZ = sideBossPosition.GetPositionZ();
        if (!bot->GetMap()->CheckCollisionAndGetValidCoords(bot, bot->GetPositionX(), bot->GetPositionY(),
                bot->GetPositionZ(), targetStageX, targetStageY, targetStageZ))
            return false;

        stagePosition.Relocate(targetStageX, targetStageY, targetStageZ);
        return true;
    };

    Position sideBossPosition;
    bool haveSideBossPosition = false;
    bool onVeklorSide = false;
    if (assignment.sideEmperor == assignment.veklor && assignment.veklor)
    {
        onVeklorSide = true;
        sideBossPosition = assignment.veklor->GetPosition();
        haveSideBossPosition = true;
    }
    else if (assignment.sideEmperor == assignment.veknilash && assignment.veknilash)
    {
        sideBossPosition = assignment.veknilash->GetPosition();
        haveSideBossPosition = true;
    }
    else if (isWarlockTank || (isRanged && !isHealer))
    {
        onVeklorSide = true;
    }
    else if (isHealer)
    {
        onVeklorSide = Aq40Helpers::GetStableTwinRoleIndex(bot, botAI) == 1;
    }

    if (!haveSideBossPosition)
    {
        sideBossPosition.Relocate(onVeklorSide ? kTwinInitialVeklorX : kTwinInitialVeknilashX,
                                  onVeklorSide ? kTwinInitialVeklorY : kTwinInitialVeknilashY,
                                  onVeklorSide ? kTwinInitialVeklorZ : kTwinInitialVeknilashZ);
        haveSideBossPosition = true;
    }

    if (isWarlockTank || isMeleeTank)
    {
        float const stageDistance = forPreTeleport ? kTwinTankPreTeleportDistanceFromBoss : kTwinTankStageDistanceFromBoss;
        Position stagePosition;
        if (!buildCenterFacingStagePoint(sideBossPosition, stageDistance, 0.0f, stagePosition))
            return false;

        targetX = stagePosition.GetPositionX();
        targetY = stagePosition.GetPositionY();
        targetZ = stagePosition.GetPositionZ();
        tolerance = forPreTeleport ? 10.0f : 8.0f;
    }
    else if (isRanged)
    {
        size_t botIndex = 0;
        size_t count = 1;
        GetTwinPositionCohortSlot(bot, botAI, false, botIndex, count);
        float const lateralOffset = GetTwinCenteredSlotOffset(botIndex, count, 4.0f);
        Position stagePosition;
        float const forwardDistance = forPreTeleport ? 28.0f : (twinPullActive ? 34.0f : 44.0f);
        if (!buildCenterFacingStagePoint(sideBossPosition, forwardDistance, lateralOffset, stagePosition))
            return false;

        targetX = stagePosition.GetPositionX();
        targetY = stagePosition.GetPositionY();
        targetZ = stagePosition.GetPositionZ();
        tolerance = 6.0f;
    }
    else if (isHealer)
    {
        size_t botIndex = 0;
        size_t count = 1;
        GetTwinPositionCohortSlot(bot, botAI, true, botIndex, count);
        float const lateralOffset = GetTwinCenteredSlotOffset(botIndex, count, 5.0f);
        float const forwardDistance = onVeklorSide ?
            (forPreTeleport ? 30.0f : (twinPullActive ? 38.0f : 46.0f)) :
            (forPreTeleport ? 18.0f : (twinPullActive ? 24.0f : 32.0f));
        Position stagePosition;
        if (!buildCenterFacingStagePoint(sideBossPosition, forwardDistance, lateralOffset, stagePosition))
            return false;

        targetX = stagePosition.GetPositionX();
        targetY = stagePosition.GetPositionY();
        targetZ = stagePosition.GetPositionZ();
        tolerance = 7.0f;
    }
    if (bot->GetDistance(targetX, targetY, targetZ) <= tolerance)
        return false;

    if (!bot->GetMap()->CheckCollisionAndGetValidCoords(bot, bot->GetPositionX(), bot->GetPositionY(),
            bot->GetPositionZ(), targetX, targetY, targetZ))
        return false;

    outPosition.Relocate(targetX, targetY, targetZ);
    return true;
}

bool GetTwinFarSidePosition(Player* bot, Unit* sideBoss, Unit* oppositeBoss, float desiredRange,
                            float angleOffset, Position& outPosition)
{
    if (!bot || !sideBoss)
        return false;

    float angle = 0.0f;
    if (oppositeBoss)
        angle = std::atan2(sideBoss->GetPositionY() - oppositeBoss->GetPositionY(),
                           sideBoss->GetPositionX() - oppositeBoss->GetPositionX());
    else
        angle = std::atan2(bot->GetPositionY() - sideBoss->GetPositionY(),
                           bot->GetPositionX() - sideBoss->GetPositionX());

    angle += angleOffset;

    // Try the exact angle first, then small offsets if collision blocks the path.
    static constexpr float kRetryOffsets[] = { 0.0f, 0.3f, -0.3f, 0.6f, -0.6f };
    for (float off : kRetryOffsets)
    {
        float tryAngle = angle + off;
        float targetX = sideBoss->GetPositionX() + desiredRange * std::cos(tryAngle);
        float targetY = sideBoss->GetPositionY() + desiredRange * std::sin(tryAngle);
        float targetZ = bot->GetPositionZ();
        if (bot->GetMap()->CheckCollisionAndGetValidCoords(bot, bot->GetPositionX(), bot->GetPositionY(),
                bot->GetPositionZ(), targetX, targetY, targetZ))
        {
            outPosition.Relocate(targetX, targetY, targetZ);
            return true;
        }
    }

    return false;
}

bool GetTwinInnerSidePosition(Player* bot, Unit* sideBoss, Unit* oppositeBoss, float desiredRange,
                              float angleOffset, Position& outPosition)
{
    if (!bot || !sideBoss)
        return false;

    float angle = 0.0f;
    if (oppositeBoss)
        angle = std::atan2(oppositeBoss->GetPositionY() - sideBoss->GetPositionY(),
                           oppositeBoss->GetPositionX() - sideBoss->GetPositionX());
    else
        angle = std::atan2(kTwinRoomCenterY - sideBoss->GetPositionY(),
                           kTwinRoomCenterX - sideBoss->GetPositionX());

    angle += angleOffset;

    float targetX = sideBoss->GetPositionX() + desiredRange * std::cos(angle);
    float targetY = sideBoss->GetPositionY() + desiredRange * std::sin(angle);
    float targetZ = bot->GetPositionZ();
    if (!bot->GetMap()->CheckCollisionAndGetValidCoords(bot, bot->GetPositionX(), bot->GetPositionY(),
            bot->GetPositionZ(), targetX, targetY, targetZ))
        return false;

    outPosition.Relocate(targetX, targetY, targetZ);
    return true;
}

bool GetTwinStagePositioning(Player* bot, PlayerbotAI* botAI, GuidVector const& encounterUnits,
                             bool requireTeleportWindow, Position& outPosition, MovementPriority& outPriority)
{
    if (!bot || !botAI)
        return false;

    Aq40Helpers::TwinAssignments assignment = Aq40Helpers::GetTwinAssignments(bot, botAI, encounterUnits);
    if (requireTeleportWindow && !assignment.sideEmperor)
        return false;

    if (requireTeleportWindow && !Aq40Helpers::IsTwinPreTeleportWindow(bot, botAI, encounterUnits))
        return false;

    bool const isWarlockTank = Aq40BossHelper::IsDesignatedTwinWarlockTank(bot);
    bool const isTank = PlayerbotAI::IsTank(bot) && !PlayerbotAI::IsRanged(bot);
    bool const isHealer = botAI->IsHeal(bot);
    bool const isRanged = botAI->IsRanged(bot) && !isHealer;
    bool const isMeleeDps = !isWarlockTank && !isTank && !isHealer && !isRanged;
    if (!isWarlockTank && !isTank && !isHealer && !isRanged && !isMeleeDps)
        return false;

    if (assignment.sideEmperor && isMeleeDps && IsTwinDpsDraggingMeleeBoss(bot, botAI, assignment))
        return false;

    MovementPriority const priority =
        (!requireTeleportWindow && !bot->IsInCombat()) ? MovementPriority::MOVEMENT_FORCED : MovementPriority::MOVEMENT_COMBAT;
    outPriority = priority;

    if (isMeleeDps)
    {
        // During pre-teleport staging, position melee DPS near Vek'nilash on
        // the far side (away from Vek'lor) so they can continue attacking
        // instead of running to room centre and dealing zero damage.
        // Room centre is only used for pre-pull staging (no combat yet).
        if (requireTeleportWindow && assignment.veknilash)
        {
            Position meleeAnchor;
            if (GetTwinFarSidePosition(bot, assignment.veknilash, assignment.veklor, 8.0f, 0.0f, meleeAnchor))
            {
                if (bot->GetExactDist2d(meleeAnchor.GetPositionX(), meleeAnchor.GetPositionY()) <= 3.0f)
                    return false;

                outPosition = meleeAnchor;
                return true;
            }
        }

        if (bot->GetDistance(kTwinRoomCenterX, kTwinRoomCenterY, kTwinRoomCenterZ) <= 12.0f)
            return false;

        outPosition.Relocate(kTwinRoomCenterX, kTwinRoomCenterY, kTwinRoomCenterZ);
        return true;
    }

    Position fallbackAnchor;
    if (!GetTwinFallbackAnchorPosition(bot, botAI, assignment, requireTeleportWindow, fallbackAnchor))
        return false;

    outPosition = fallbackAnchor;
    return true;
}
}    // namespace

bool Aq40TwinEmperorsChooseTargetAction::Execute(Event /*event*/)
{
    GuidVector encounterUnits = Aq40Helpers::GetTwinEncounterUnits(bot, botAI, context->GetValue<GuidVector>("attackers")->Get());
    if (encounterUnits.empty())
        return false;

    Aq40Helpers::TwinAssignments assignment = Aq40Helpers::GetTwinAssignments(bot, botAI, encounterUnits);
    if (!assignment.veklor && !assignment.veknilash)
        return false;

    Unit* target = nullptr;
    bool const isWarlockTank = Aq40BossHelper::IsDesignatedTwinWarlockTank(bot);
    bool const isMeleeTank = PlayerbotAI::IsTank(bot) && !PlayerbotAI::IsRanged(bot);
    bool const isMeleeDps = !isMeleeTank && !botAI->IsRanged(bot) && !botAI->IsHeal(bot);
    bool const draggingMeleeBoss = IsTwinDpsDraggingMeleeBoss(bot, botAI, assignment);
    Unit* sideBug = FindTwinPriorityBugTarget(bot, botAI, assignment, Aq40BossActions::FindTwinSideBugs(botAI, encounterUnits));
    Unit* mutateBug = Aq40BossActions::FindTwinMutateBug(botAI, encounterUnits);
    if (isWarlockTank)
    {
        if (IsTwinAssignedSideBoss(assignment, assignment.veklor))
            target = assignment.veklor;
        else
        {
            // Side doesn't have Vek'lor — stop attacking the old target so
            // the class AI doesn't chase the wrong boss across the room.
            bot->AttackStop();
            context->GetValue<ObjectGuid>("pull target")->Set(ObjectGuid::Empty);
            return false;
        }
    }
    else if (isMeleeTank)
    {
        if (IsTwinAssignedSideBoss(assignment, assignment.veknilash))
            target = assignment.veknilash;
        else
        {
            bot->AttackStop();
            context->GetValue<ObjectGuid>("pull target")->Set(ObjectGuid::Empty);
            return false;
        }
    }
    else if (botAI->IsHeal(bot))
        return false;
    else if (draggingMeleeBoss)
        target = assignment.veknilash;
    else if (isMeleeDps && sideBug && ShouldReserveForTwinBug(bot, botAI, assignment, sideBug))
        target = sideBug;
    else if (isMeleeDps && assignment.veknilash && mutateBug &&
             Aq40Helpers::IsLikelyOnSameTwinSide(mutateBug, assignment.veknilash, assignment.veklor))
        target = mutateBug;
    else if (botAI->IsRanged(bot))
    {
        if (!assignment.veklor)
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

    float maxEngageRange = 0.0f;
    float desiredRange = 0.0f;
    if (isWarlockTank)
    {
        desiredRange = 24.0f;
        maxEngageRange = 30.0f;
    }
    else if (isMeleeTank || isMeleeDps)
    {
        desiredRange = 4.0f;
        maxEngageRange = 8.0f;
    }
    else
    {
        desiredRange = 28.0f;
        maxEngageRange = 36.0f;
    }

    if (bot->GetDistance2d(target) > maxEngageRange)
        return MoveNear(target, desiredRange, MovementPriority::MOVEMENT_COMBAT);

    return Attack(target);
}

bool Aq40TwinEmperorsHoldSplitAction::Execute(Event /*event*/)
{
    GuidVector encounterUnits = Aq40Helpers::GetTwinEncounterUnits(bot, botAI, context->GetValue<GuidVector>("attackers")->Get());
    Aq40Helpers::TwinAssignments assignment = Aq40Helpers::GetTwinAssignments(bot, botAI, encounterUnits);
    bool isWarlockTank = Aq40BossHelper::IsDesignatedTwinWarlockTank(bot);
    bool isMeleeTank = PlayerbotAI::IsTank(bot) && !PlayerbotAI::IsRanged(bot);
    bool isRangedDps = botAI->IsRanged(bot) && !botAI->IsHeal(bot);
    bool isMeleeDps = !isMeleeTank && !isRangedDps && !botAI->IsHeal(bot);
    if (!isMeleeTank && !isMeleeDps && !botAI->IsHeal(bot) && !isWarlockTank && !isRangedDps)
        return false;

    Unit* sideBoss = assignment.sideEmperor;
    Unit* oppositeBoss = assignment.oppositeEmperor;
    if (isRangedDps)
    {
        sideBoss = assignment.veklor;
        oppositeBoss = assignment.veknilash;
    }
    else if (isMeleeDps)
    {
        sideBoss = assignment.veknilash;
        oppositeBoss = assignment.veklor;
    }
    else if (botAI->IsHeal(bot) && !sideBoss)
    {
        bool const healerOnVeklorSide = Aq40Helpers::GetStableTwinRoleIndex(bot, botAI) == 1;
        sideBoss = healerOnVeklorSide ? assignment.veklor : assignment.veknilash;
        oppositeBoss = healerOnVeklorSide ? assignment.veknilash : assignment.veklor;
    }

    if (!sideBoss)
        return false;

    bool const waitingOnTeleportPickup =
        (isWarlockTank && sideBoss != assignment.veklor) ||
        (isMeleeTank && sideBoss != assignment.veknilash);

    Position fallbackAnchor;
    if (waitingOnTeleportPickup && (isWarlockTank || isMeleeTank))
    {
        // Check if the boss we actually want has already teleported in and is
        // within engage range.  If so, skip the hold entirely so higher-priority
        // actions (WarlockTank / ChooseTarget) can immediately pick it up.
        // This closes the 1-2 tick idle gap where the tank sits at AttackStop
        // while the side mapping catches up after a teleport.
        Unit* wantedBoss = isWarlockTank ? assignment.veklor : assignment.veknilash;
        if (wantedBoss && wantedBoss->IsAlive() &&
            bot->IsWithinLOSInMap(wantedBoss) &&
            bot->GetDistance2d(wantedBoss) <= (isWarlockTank ? 40.0f : 12.0f))
            return false;

        // Stop attacking the old boss so the class AI doesn't chase it
        // across the room while we reposition for the teleport pickup.
        bot->AttackStop();

        // Pre-cast Shadow Ward while waiting so the first Shadow Bolt after
        // Vek'lor teleports in is absorbed.  Fire-and-forget before positioning.
        if (isWarlockTank && !botAI->HasAura("shadow ward", bot) && botAI->CanCastSpell("shadow ward", bot))
            botAI->CastSpell("shadow ward", bot);

        if (GetTwinFallbackAnchorPosition(bot, botAI, assignment, false, fallbackAnchor))
        {
            return MoveTo(bot->GetMapId(), fallbackAnchor.GetPositionX(), fallbackAnchor.GetPositionY(),
                          fallbackAnchor.GetPositionZ(), false, false, false, true,
                          MovementPriority::MOVEMENT_COMBAT, true, false);
        }

        // Already at fallback anchor — return false so utility actions
        // (Shadow Ward, healthstone, etc.) can fire while we wait.
        return false;
    }

    if (!waitingOnTeleportPickup && (isWarlockTank || isMeleeTank) &&
        (!bot->IsWithinLOSInMap(sideBoss) || bot->GetDistance2d(sideBoss) > (isWarlockTank ? 34.0f : 12.0f)))
    {
        return MoveNear(sideBoss, isWarlockTank ? 24.0f : 4.0f, MovementPriority::MOVEMENT_COMBAT);
    }

    if (waitingOnTeleportPickup)
        return false;

    bool const waitingOnWarlockPickup =
        !isWarlockTank &&
        sideBoss == assignment.veklor &&
        !Aq40Helpers::IsTwinAssignedTankReady(bot, botAI, assignment);
    if (waitingOnWarlockPickup && (isRangedDps || botAI->IsHeal(bot)))
    {
        if (GetTwinFallbackAnchorPosition(bot, botAI, assignment, false, fallbackAnchor))
        {
            return MoveTo(bot->GetMapId(), fallbackAnchor.GetPositionX(), fallbackAnchor.GetPositionY(),
                          fallbackAnchor.GetPositionZ(), false, false, false, true,
                          MovementPriority::MOVEMENT_COMBAT, true, false);
        }

        return MoveNear(sideBoss, botAI->IsHeal(bot) ? 22.0f : 28.0f, MovementPriority::MOVEMENT_COMBAT);
    }

    if (!isWarlockTank && (isRangedDps || botAI->IsHeal(bot)) &&
        sideBoss == assignment.veklor && HasTwinBossAggro(bot, sideBoss))
    {
        Position tankAnchor;
        if (GetTwinFarSidePosition(bot, sideBoss, oppositeBoss, 24.0f, 0.0f, tankAnchor) &&
            bot->GetExactDist2d(tankAnchor.GetPositionX(), tankAnchor.GetPositionY()) > 3.5f)
        {
            return MoveTo(bot->GetMapId(), tankAnchor.GetPositionX(), tankAnchor.GetPositionY(), tankAnchor.GetPositionZ(),
                          false, false, false, true, MovementPriority::MOVEMENT_COMBAT, true, false);
        }
    }

    if (isRangedDps && oppositeBoss)
    {
        size_t botIndex = 0;
        size_t count = 1;
        GetTwinPositionCohortSlot(bot, botAI, false, botIndex, count);
        float const angleOffset = GetTwinCenteredSlotOffset(botIndex, count, 0.22f);
        Position innerPosition;
        if (GetTwinInnerSidePosition(bot, sideBoss, oppositeBoss, 28.0f, angleOffset, innerPosition))
        {
            if (bot->GetExactDist2d(innerPosition.GetPositionX(), innerPosition.GetPositionY()) <= 2.5f)
                return false;

            return MoveTo(bot->GetMapId(), innerPosition.GetPositionX(), innerPosition.GetPositionY(),
                          innerPosition.GetPositionZ(), false, false, false, true,
                          MovementPriority::MOVEMENT_COMBAT, true, false);
        }

        if (!bot->IsWithinLOSInMap(sideBoss) || bot->GetDistance2d(sideBoss) > 31.0f)
            return MoveNear(sideBoss, 28.0f, MovementPriority::MOVEMENT_COMBAT);
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
            return MoveTo(sideBoss, desiredRange, MovementPriority::MOVEMENT_COMBAT);

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
    GuidVector encounterUnits = Aq40Helpers::GetTwinEncounterUnits(bot, botAI, context->GetValue<GuidVector>("attackers")->Get());
    Aq40Helpers::TwinAssignments assignment = Aq40Helpers::GetTwinAssignments(bot, botAI, encounterUnits);
    if (ShouldDelayTwinPreTeleportStage(bot, botAI, assignment))
        return false;

    Position movePosition;
    MovementPriority movePriority = MovementPriority::MOVEMENT_COMBAT;
    if (!GetTwinStagePositioning(bot, botAI, encounterUnits, true, movePosition, movePriority))
        return false;

    if (bot->GetExactDist2d(movePosition.GetPositionX(), movePosition.GetPositionY()) <= 2.5f)
        return false;

    return MoveTo(bot->GetMapId(), movePosition.GetPositionX(), movePosition.GetPositionY(), movePosition.GetPositionZ(),
                  false, false, false, true, movePriority, true, false);
}

bool Aq40TwinEmperorsPreTeleportStageAction::isUseful()
{
    GuidVector encounterUnits = Aq40Helpers::GetTwinEncounterUnits(bot, botAI, context->GetValue<GuidVector>("attackers")->Get());
    Aq40Helpers::TwinAssignments assignment = Aq40Helpers::GetTwinAssignments(bot, botAI, encounterUnits);
    if (ShouldDelayTwinPreTeleportStage(bot, botAI, assignment))
        return false;

    Position movePosition;
    MovementPriority movePriority = MovementPriority::MOVEMENT_COMBAT;
    if (!GetTwinStagePositioning(bot, botAI, encounterUnits, true, movePosition, movePriority))
        return false;

    return bot->GetExactDist2d(movePosition.GetPositionX(), movePosition.GetPositionY()) > 2.5f;
}

bool Aq40TwinEmperorsPrePullStageAction::Execute(Event /*event*/)
{
    if (!Aq40Helpers::IsTwinPrePullReady(bot, botAI))
        return false;

    if (bot->IsMounted())
    {
        bot->RemoveAurasByType(SPELL_AURA_MOUNTED);
        return true;
    }

    GuidVector encounterUnits = Aq40Helpers::GetTwinPrePullUnits(bot, botAI);
    Position movePosition;
    MovementPriority movePriority = MovementPriority::MOVEMENT_FORCED;
    if (!GetTwinStagePositioning(bot, botAI, encounterUnits, false, movePosition, movePriority))
        return true;  // Already at staged position — hold here

    return MoveTo(bot->GetMapId(), movePosition.GetPositionX(), movePosition.GetPositionY(), movePosition.GetPositionZ(),
                  false, false, false, true, movePriority, true, false);
}

bool Aq40TwinEmperorsPrePullStageAction::isUseful()
{
    return Aq40Helpers::IsTwinPrePullReady(bot, botAI);
}

bool Aq40TwinEmperorsWarlockTankAction::Execute(Event /*event*/)
{
    if (!Aq40BossHelper::IsDesignatedTwinWarlockTank(bot))
        return false;

    GuidVector encounterUnits = Aq40Helpers::GetTwinEncounterUnits(bot, botAI, context->GetValue<GuidVector>("attackers")->Get());
    Aq40Helpers::TwinAssignments assignment = Aq40Helpers::GetTwinAssignments(bot, botAI, encounterUnits);
    if (!assignment.veklor)
        return false;

    // Safety net: if the side mapping doesn't think Vek'lor is on our side
    // but we ARE the designated warlock tank and Vek'lor is within cast range,
    // proceed anyway.  This prevents side-mapping edge cases from permanently
    // dropping Searing Pain threat on Vek'lor.
    if (!IsTwinPrimaryTankOnActiveBoss(bot, assignment))
    {
        if (!assignment.veklor->IsAlive() ||
            !bot->IsWithinLOSInMap(assignment.veklor) ||
            bot->GetDistance2d(assignment.veklor) > 40.0f)
            return false;
    }

    Unit* veklor = assignment.veklor;

    if (AI_VALUE(Unit*, "current target") != veklor)
        PinTwinTarget(botAI, context, veklor);

    // Hold a true far-side anchor away from Vek'nilash instead of a generic
    // 24y ring position. The logs showed repeated Heal Brother casts, which
    // means the Vek'lor tank was not preserving room separation reliably.
    bool const blizzardOnBot = Aq40SpellIds::HasAnyAura(botAI, bot, { Aq40SpellIds::TwinBlizzard });
    float const desiredRange = blizzardOnBot ? 29.0f : 24.0f;
    float const minRange = blizzardOnBot ? 24.0f : 21.0f;
    float const maxRange = blizzardOnBot ? 34.0f : 30.0f;
    float const angleOffset = blizzardOnBot ? (((bot->GetGUID().GetRawValue() & 1ULL) != 0) ? 0.45f : -0.45f) : 0.0f;

    Position anchorPosition;
    bool const hasAnchor =
        GetTwinFarSidePosition(bot, veklor, assignment.oppositeEmperor, desiredRange, angleOffset, anchorPosition);
    float const rangeToVeklor = bot->GetDistance2d(veklor);
    bool const hasLOS = bot->IsWithinLOSInMap(veklor);
    bool const hasThreat = HasTwinBossAggro(bot, veklor);

    // Priority 1: Establish threat — searing pain is the only ability that
    // matters before we have aggro.
    if (!hasThreat && hasLOS && botAI->CanCastSpell("searing pain", veklor))
        return botAI->CastSpell("searing pain", veklor);

    // Priority 2: Hard reposition — only when out of LOS or grossly out of
    // range.  Avoid interrupting casts for minor positional drift.
    bool const hardReposition = !hasLOS || rangeToVeklor < minRange || rangeToVeklor > maxRange;
    if (hardReposition)
    {
        bot->AttackStop();
        bot->InterruptNonMeleeSpells(true);

        if (hasAnchor)
            MoveTo(bot->GetMapId(), anchorPosition.GetPositionX(), anchorPosition.GetPositionY(),
                   anchorPosition.GetPositionZ(), false, false, false, true,
                   MovementPriority::MOVEMENT_COMBAT, true, false);
        else
            MoveTo(veklor, desiredRange, MovementPriority::MOVEMENT_COMBAT);

        return true;  // Always claim the action during repositioning
    }

    // Priority 3: Maintain spells — threat generation + utility between GCDs
    if (!botAI->HasAura("shadow ward", bot) && botAI->CanCastSpell("shadow ward", bot))
        return botAI->CastSpell("shadow ward", bot);

    if (!botAI->HasAura("curse of doom", veklor) && botAI->CanCastSpell("curse of doom", veklor))
        return botAI->CastSpell("curse of doom", veklor);

    if (botAI->CanCastSpell("searing pain", veklor))
        return botAI->CastSpell("searing pain", veklor);

    // Priority 4: Soft reposition — drift toward anchor between casts (GCD)
    if (hasAnchor && bot->GetExactDist2d(anchorPosition.GetPositionX(), anchorPosition.GetPositionY()) > 6.0f)
        MoveTo(bot->GetMapId(), anchorPosition.GetPositionX(), anchorPosition.GetPositionY(),
               anchorPosition.GetPositionZ(), false, false, false, true,
               MovementPriority::MOVEMENT_COMBAT, true, false);

    if (!hasThreat && bot->GetVictim() != veklor)
        Attack(veklor);

    // Return true in steady state to keep this action in control.
    // Returning false would drop to class AI, which doesn't prioritise
    // searing pain and lets warlock threat on Vek'lor stall.
    return true;
}

bool Aq40TwinEmperorsWarlockTankAction::isUseful()
{
    if (!Aq40BossHelper::IsDesignatedTwinWarlockTank(bot))
        return false;

    GuidVector encounterUnits = Aq40Helpers::GetTwinEncounterUnits(bot, botAI, context->GetValue<GuidVector>("attackers")->Get());
    Aq40Helpers::TwinAssignments assignment = Aq40Helpers::GetTwinAssignments(bot, botAI, encounterUnits);
    if (!assignment.veklor)
        return false;

    if (IsTwinPrimaryTankOnActiveBoss(bot, assignment))
        return true;

    // Safety net: mirror Execute()'s fallback — allow the action when the
    // side mapping disagrees but Vek'lor is alive and within cast range.
    // Without this the warlock tank goes idle during teleport recovery.
    return assignment.veklor->IsAlive() &&
           bot->IsWithinLOSInMap(assignment.veklor) &&
           bot->GetDistance2d(assignment.veklor) <= 40.0f;
}

bool Aq40TwinEmperorsAvoidArcaneBurstAction::Execute(Event /*event*/)
{
    if (Aq40BossHelper::IsDesignatedTwinWarlockTank(bot))
        return false;

    GuidVector encounterUnits = Aq40Helpers::GetTwinEncounterUnits(bot, botAI, context->GetValue<GuidVector>("attackers")->Get());
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

    Position safePosition;
    if (GetTwinFarSidePosition(bot, veklor, assignment.veknilash, desiredRange, 0.0f, safePosition))
    {
        if (bot->GetExactDist2d(safePosition.GetPositionX(), safePosition.GetPositionY()) <= 2.0f)
            return false;

        return MoveTo(bot->GetMapId(), safePosition.GetPositionX(), safePosition.GetPositionY(),
                      safePosition.GetPositionZ(), false, false, false, true,
                      MovementPriority::MOVEMENT_FORCED, true, false);
    }

    // Far-side position failed collision check (Vek'lor near walls).
    // Fall back to a simple directional escape away from the boss.
    return MoveAway(veklor, dangerRange + 5.0f);
}

bool Aq40TwinEmperorsAvoidBlizzardAction::Execute(Event /*event*/)
{
    if ((!botAI->IsRanged(bot) && !botAI->IsHeal(bot)) || Aq40BossHelper::IsDesignatedTwinWarlockTank(bot))
        return false;

    GuidVector encounterUnits = Aq40Helpers::GetTwinEncounterUnits(bot, botAI, context->GetValue<GuidVector>("attackers")->Get());
    Aq40Helpers::TwinAssignments assignment = Aq40Helpers::GetTwinAssignments(bot, botAI, encounterUnits);

    // Always process when the bot has the Blizzard debuff, even if sideEmperor
    // is stale after a teleport.  Only require the side mapping for predictive
    // avoidance (no aura yet).
    bool const hasBlizzardAura = Aq40SpellIds::HasAnyAura(botAI, bot, { Aq40SpellIds::TwinBlizzard });
    if (!hasBlizzardAura && (!assignment.veklor || assignment.sideEmperor != assignment.veklor))
        return false;

    if (!hasBlizzardAura)
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
    {
        // Arc-spread position hit a wall.  Move directly away from Vek'lor
        // so the bot escapes the Blizzard AoE instead of orbiting inside it.
        return MoveAway(assignment.veklor, botAI->IsHeal(bot) ? 30.0f : 32.0f);
    }

    bot->AttackStop();
    bot->InterruptNonMeleeSpells(true);
    return MoveTo(bot->GetMapId(), targetX, targetY, targetZ, false, false, false, true,
                  MovementPriority::MOVEMENT_FORCED, true, false);
}

bool Aq40TwinEmperorsEnforceSeparationAction::Execute(Event /*event*/)
{
    GuidVector encounterUnits = Aq40Helpers::GetTwinEncounterUnits(bot, botAI, context->GetValue<GuidVector>("attackers")->Get());
    Aq40Helpers::TwinAssignments assignment = Aq40Helpers::GetTwinAssignments(bot, botAI, encounterUnits);
    if (!assignment.veklor || !assignment.veknilash)
        return false;

    if (assignment.veklor->GetDistance2d(assignment.veknilash) >= 60.0f)
        return false;

    bool isWarlockTank = Aq40BossHelper::IsDesignatedTwinWarlockTank(bot);
    bool isMeleeTank = PlayerbotAI::IsTank(bot) && !PlayerbotAI::IsRanged(bot);
    if (!isWarlockTank && !isMeleeTank)
        return false;

    // Determine desired boss by role directly rather than relying on
    // IsTwinPrimaryTankOnActiveBoss, which depends on the sideEmperor
    // mapping and can fail right after a teleport swap.
    Unit* desiredBoss = isWarlockTank ? assignment.veklor : assignment.veknilash;
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
    {
        float const awayDist = std::max(12.0f, 60.0f - bot->GetDistance2d(otherBoss));
        return MoveAway(otherBoss, awayDist) || acted;
    }

    acted = MoveTo(bot->GetMapId(), targetX, targetY, targetZ, false, false, false, true,
                   MovementPriority::MOVEMENT_FORCED, true, false) || acted;

    return acted;
}

bool Aq40TwinEmperorsPetControlAction::Execute(Event /*event*/)
{
    Pet* pet = bot->GetPet();
    if (!pet)
        return false;

    GuidVector encounterUnits = Aq40Helpers::GetTwinEncounterUnits(bot, botAI, context->GetValue<GuidVector>("attackers")->Get());
    Aq40Helpers::TwinAssignments assignment = Aq40Helpers::GetTwinAssignments(bot, botAI, encounterUnits);
    if (!assignment.veklor || !assignment.veknilash)
        return false;

    Unit* petTarget = FindTwinPetBugTarget(botAI, Aq40BossActions::FindTwinSideBugs(botAI, encounterUnits));
    if (!petTarget)
        petTarget = assignment.veknilash;
    if (!petTarget || !petTarget->IsAlive())
        return false;

    if (pet->GetReactState() == REACT_PASSIVE)
        pet->SetReactState(REACT_DEFENSIVE);

    if (pet->GetVictim() == petTarget && pet->GetTarget() == petTarget->GetGUID())
        return false;

    pet->AttackStop();
    pet->SetTarget(petTarget->GetGUID());
    pet->ToCreature()->AI()->AttackStart(petTarget);
    return true;
}

bool Aq40TwinEmperorsMoveAwayFromBrotherAction::Execute(Event /*event*/)
{
    GuidVector encounterUnits = Aq40Helpers::GetTwinEncounterUnits(bot, botAI, context->GetValue<GuidVector>("attackers")->Get());
    Aq40Helpers::TwinAssignments assignment = Aq40Helpers::GetTwinAssignments(bot, botAI, encounterUnits);
    if (!assignment.sideEmperor || !assignment.oppositeEmperor)
        return false;

    ObjectGuid const botGuid = bot->GetGUID();
    ObjectGuid petGuid = ObjectGuid::Empty;
    if (Pet* pet = bot->GetPet())
        petGuid = pet->GetGUID();

    bool const sideBossOnBot =
        assignment.sideEmperor->GetTarget() == botGuid || (petGuid && assignment.sideEmperor->GetTarget() == petGuid);
    bool const oppositeBossOnBot =
        assignment.oppositeEmperor->GetTarget() == botGuid || (petGuid && assignment.oppositeEmperor->GetTarget() == petGuid);
    if (!sideBossOnBot && !oppositeBossOnBot)
        return false;

    if (petGuid)
        botAI->PetFollow();

    bot->AttackStop();
    bot->InterruptNonMeleeSpells(true);

    float desiredRange = 0.0f;
    if (Aq40BossHelper::IsDesignatedTwinWarlockTank(bot))
        desiredRange = 24.0f;
    else if (PlayerbotAI::IsTank(bot) && !PlayerbotAI::IsRanged(bot))
        desiredRange = 4.0f;
    else if (botAI->IsHeal(bot))
        desiredRange = 20.0f;
    else if (botAI->IsRanged(bot))
        desiredRange = 28.0f;
    else
        desiredRange = 6.0f;

    Position safeAnchor;
    if (GetTwinFarSidePosition(bot, assignment.sideEmperor, assignment.oppositeEmperor, desiredRange, 0.0f, safeAnchor) &&
        bot->GetExactDist2d(safeAnchor.GetPositionX(), safeAnchor.GetPositionY()) > 3.0f)
    {
        return MoveTo(bot->GetMapId(), safeAnchor.GetPositionX(), safeAnchor.GetPositionY(), safeAnchor.GetPositionZ(),
                      false, false, false, true, MovementPriority::MOVEMENT_FORCED, true, false);
    }

    Unit* moveAwayFrom = oppositeBossOnBot ? assignment.oppositeEmperor : assignment.sideEmperor;
    float const currentDistance = bot->GetDistance2d(moveAwayFrom);
    float const emergencyStep = std::max(12.0f, 45.0f - currentDistance);
    return MoveAway(moveAwayFrom, emergencyStep);
}
