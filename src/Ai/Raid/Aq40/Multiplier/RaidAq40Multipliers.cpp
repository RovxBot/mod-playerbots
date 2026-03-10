#include "RaidAq40Multipliers.h"

#include "Action.h"
#include "ChooseTargetActions.h"
#include "FollowActions.h"
#include "MovementActions.h"
#include "ObjectGuid.h"
#include "Playerbots.h"
#include "../RaidAq40BossHelper.h"
#include "../RaidAq40SpellIds.h"

float Aq40GenericMultiplier::GetValue(Action* /*action*/)
{
    return 1.0f;
}

namespace
{
}  // namespace

float Aq40BugTrioMultiplier::GetValue(Action* action)
{
    if (!action || !Aq40BossHelper::IsInAq40(bot) || !bot->IsInCombat())
        return 1.0f;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    if (!Aq40BossHelper::HasAnyNamedUnit(botAI, attackers, { "lord kri", "princess yauj", "vem", "yauj brood" }))
        return 1.0f;

    Unit* kri = Aq40BossHelper::FindUnitByAnyName(botAI, attackers, { "lord kri" });
    if (!kri)
        return 1.0f;

    bool poisonCloudWindow = kri->GetHealthPct() <= 5.0f ||
                             Aq40SpellIds::HasAnyAura(botAI, kri, { Aq40SpellIds::BugTrioPoisonCloud });
    if (!poisonCloudWindow || Aq40BossHelper::IsEncounterTank(bot, bot))
        return 1.0f;

    if (bot->GetDistance2d(kri) > 12.0f)
        return 1.0f;

    std::string const actionName = action->getName();
    if (actionName == "aq40 bug trio avoid poison cloud")
        return 3.0f;

    if (dynamic_cast<MovementAction*>(action))
        return 1.0f;

    return 0.35f;
}

float Aq40OuroMultiplier::GetValue(Action* action)
{
    if (!action || !Aq40BossHelper::IsInAq40(bot) || !bot->IsInCombat())
        return 1.0f;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    Unit* ouro = Aq40BossHelper::FindUnitByAnyName(botAI, attackers, { "ouro" });
    if (!ouro)
        return 1.0f;

    std::string const actionName = action->getName();
    bool meleeOrTank = Aq40BossHelper::IsEncounterTank(bot, bot) || !botAI->IsRanged(bot);
    if (meleeOrTank && bot->GetDistance2d(ouro) > 8.0f)
    {
        if (actionName == "aq40 ouro hold melee contact")
            return 3.0f;

        if (!dynamic_cast<MovementAction*>(action))
            return 0.5f;
    }

    return 1.0f;
}

float Aq40TwinEmperorsMultiplier::GetValue(Action* action)
{
    if (!action || !Aq40BossHelper::IsInAq40(bot) || !bot->IsInCombat())
        return 1.0f;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    if (!Aq40BossHelper::HasAnyNamedUnit(botAI, attackers, { "emperor vek'nilash", "emperor vek'lor" }))
        return 1.0f;

    std::string const actionName = action->getName();
    bool isTwinControlAction =
        actionName == "aq40 twin emperors choose target" ||
        actionName == "aq40 twin emperors hold split" ||
        actionName == "aq40 twin emperors warlock tank" ||
        actionName == "aq40 twin emperors avoid arcane burst" ||
        actionName == "aq40 twin emperors enforce separation";

    if (isTwinControlAction)
        return 1.0f;

    if (dynamic_cast<CombatFormationMoveAction*>(action) ||
        dynamic_cast<FollowAction*>(action) ||
        dynamic_cast<FleeAction*>(action))
        return 0.0f;

    if (dynamic_cast<DpsAssistAction*>(action) || dynamic_cast<TankAssistAction*>(action))
        return 0.0f;

    return 1.0f;
}

float Aq40ViscidusMultiplier::GetValue(Action* action)
{
    if (!action || !Aq40BossHelper::IsInAq40(bot) || !bot->IsInCombat())
        return 1.0f;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    Unit* viscidus = Aq40BossHelper::FindUnitByAnyName(botAI, attackers, { "viscidus" });
    if (!viscidus)
        return 1.0f;

    bool frozen = Aq40SpellIds::HasAnyAura(botAI, viscidus,
        { Aq40SpellIds::ViscidusFreeze, Aq40SpellIds::ViscidusSlowedMore });
    std::string const actionName = action->getName();

    if (frozen)
    {
        if (actionName == "aq40 viscidus shatter")
            return 2.8f;
        if (actionName == "aq40 viscidus use frost")
            return 0.0f;
    }
    else
    {
        if (actionName == "aq40 viscidus use frost" && botAI->IsRanged(bot) && !botAI->IsHeal(bot))
            return 2.2f;
        if (actionName == "aq40 viscidus shatter")
            return 0.4f;
    }

    return 1.0f;
}

float Aq40CthunMultiplier::GetValue(Action* action)
{
    if (!action || !Aq40BossHelper::IsInAq40(bot) || !bot->IsInCombat())
        return 1.0f;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    if (!Aq40BossHelper::HasAnyNamedUnit(botAI, attackers,
                                         { "c'thun", "eye of c'thun", "eye tentacle", "claw tentacle",
                                           "giant eye tentacle", "giant claw tentacle", "flesh tentacle" }))
        return 1.0f;

    std::string const actionName = action->getName();
    bool isCthunControlAction =
        actionName == "aq40 cthun choose target" ||
        actionName == "aq40 cthun maintain spread" ||
        actionName == "aq40 cthun avoid dark glare" ||
        actionName == "aq40 cthun stomach dps" ||
        actionName == "aq40 cthun stomach exit" ||
        actionName == "aq40 cthun phase2 add priority" ||
        actionName == "aq40 cthun vulnerable burst" ||
        actionName == "aq40 cthun interrupt eye";

    if (isCthunControlAction)
        return 1.0f;

    if (dynamic_cast<CombatFormationMoveAction*>(action) ||
        dynamic_cast<FollowAction*>(action) ||
        dynamic_cast<FleeAction*>(action))
        return 0.0f;

    return 1.0f;
}
