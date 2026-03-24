#include "RaidAq40Actions.h"

#include <algorithm>
#include <cmath>

#include "HunterBuffStrategies.h"
#include "../RaidAq40BossHelper.h"
#include "../Trigger/RaidAq40Triggers.h"

namespace
{
float constexpr kPi = 3.14159265f;

uint32 GetHuhuranSpreadOrdinal(Player* bot, PlayerbotAI* botAI, bool forMelee, uint32& outCohortSize)
{
    Group const* group = bot->GetGroup();
    if (!group)
    {
        outCohortSize = forMelee ? 8u : 12u;
        return static_cast<uint32>(bot->GetGUID().GetCounter() % outCohortSize);
    }

    uint32 index = 0;
    uint32 botOrdinal = 0;
    bool found = false;
    for (GroupReference const* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || !member->IsAlive() || !Aq40BossHelper::IsNearEncounter(bot, member))
            continue;

        if (Aq40BossHelper::IsEncounterTank(member, member))
            continue;

        // Human non-tanks count in the ranged cohort so bots spread around
        // them rather than stacking on top.  See C'Thun spread for rationale.
        PlayerbotAI* memberAI = GET_PLAYERBOT_AI(member);
        bool memberIsMelee;
        if (memberAI)
            memberIsMelee = !memberAI->IsRanged(member) && !memberAI->IsHeal(member);
        else
            memberIsMelee = false;  // default humans to ranged
        if (forMelee != memberIsMelee)
            continue;

        if (member->GetGUID() == bot->GetGUID())
        {
            botOrdinal = index;
            found = true;
        }

        ++index;
    }

    outCohortSize = std::max(index, forMelee ? 8u : 12u);
    if (found)
        return botOrdinal;

    outCohortSize = forMelee ? 8u : 12u;
    return static_cast<uint32>(bot->GetGUID().GetCounter() % outCohortSize);
}
}  // namespace

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
    if (!target || (AI_VALUE(Unit*, "current target") == target && bot->GetVictim() == target))
        return false;

    return Attack(target);
}

bool Aq40HuhuranPoisonSpreadAction::Execute(Event /*event*/)
{
    // During poison/enrage windows, non-tanks spread angularly around the boss
    // so fewer players soak Noxious Poison (hits 15 closest).
    if (Aq40BossHelper::IsEncounterTank(bot, bot))
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, context->GetValue<GuidVector>("attackers")->Get());
    Unit* huhuran = Aq40BossActions::FindHuhuranTarget(botAI, encounterUnits);
    if (!huhuran)
        return false;

    bool const isMelee = !botAI->IsRanged(bot) && !botAI->IsHeal(bot);
    float const desiredDistance = isMelee ? 18.0f : 28.0f;
    uint32 totalSlots = 0;
    uint32 const slot = GetHuhuranSpreadOrdinal(bot, botAI, isMelee, totalSlots);
    float const angle = static_cast<float>(slot) * ((2.0f * kPi) / static_cast<float>(totalSlots));

    float const moveX = huhuran->GetPositionX() + std::cos(angle) * desiredDistance;
    float const moveY = huhuran->GetPositionY() + std::sin(angle) * desiredDistance;

    if (bot->GetDistance2d(moveX, moveY) < 3.0f)
        return false;

    return MoveTo(bot->GetMapId(), moveX, moveY, huhuran->GetPositionZ(), false, false, false, true,
                  MovementPriority::MOVEMENT_COMBAT, true, false);
}

bool Aq40HuhuranNatureResistTotemAction::Execute(Event /*event*/)
{
    if (bot->getClass() != CLASS_SHAMAN)
        return false;

    // Check if NR totem is already active on the bot (any rank).
    if (botAI->HasAura("nature resistance totem", bot))
        return false;

    // Only the first eligible bot shaman near the encounter should cast.
    // Skip humans (can't be commanded) and bots that can't cast right now
    // (silenced, oom, doesn't know the spell).
    Group const* group = bot->GetGroup();
    if (group)
    {
        for (GroupReference const* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || !member->IsAlive())
                continue;

            if (member->getClass() != CLASS_SHAMAN)
                continue;

            if (!Aq40BossHelper::IsSameInstance(bot, member))
                continue;

            if (bot->GetDistance2d(member) > 60.0f)
                continue;

            // Skip humans.
            PlayerbotAI* memberAI = GET_PLAYERBOT_AI(member);
            if (!memberAI)
                continue;

            // Skip bots that already have the totem buff or cannot cast.
            if (memberAI->HasAura("nature resistance totem", member) ||
                !memberAI->CanCastSpell("nature resistance totem", member))
                continue;

            // First eligible bot shaman wins.
            if (member != bot)
                return false;
            break;
        }
    }

    if (botAI->CanCastSpell("nature resistance totem", bot))
        return botAI->CastSpell("nature resistance totem", bot);

    return false;
}

bool Aq40HuhuranNatureResistanceAction::isUseful()
{
    Aq40HuhuranNatureResistanceTrigger trigger(botAI);
    return trigger.IsActive();
}

bool Aq40HuhuranNatureResistanceAction::Execute(Event /*event*/)
{
    // Mirror BossNatureResistanceAction: add the hunter NR combat strategy
    // so the "aspect of the wild" trigger (priority 20) keeps the aura active
    // against the generic Hawk/Dragonhawk trigger (priority 27.5).
    HunterNatureResistanceStrategy hunterNatureResistanceStrategy(botAI);
    botAI->ChangeStrategy("+" + hunterNatureResistanceStrategy.getName(), BotState::BOT_STATE_COMBAT);
    botAI->DoSpecificAction("aspect of the wild", Event(), true);
    return true;
}
