#include "RaidAq40Multipliers.h"

#include "Action.h"
#include "AttackAction.h"
#include "ChooseTargetActions.h"
#include "FollowActions.h"
#include "GenericSpellActions.h"
#include "MovementActions.h"
#include "ObjectGuid.h"
#include "Playerbots.h"
#include "ReachTargetActions.h"
#include "../Action/RaidAq40Actions.h"
#include "../RaidAq40BossHelper.h"
#include "../RaidAq40SpellIds.h"
#include "../Util/RaidAq40Helpers.h"

float Aq40GenericMultiplier::GetValue(Action* action)
{
    if (!action || !Aq40BossHelper::IsInAq40(bot) || !bot->IsInCombat())
        return 1.0f;

    if (Aq40SpellIds::HasAnyAura(botAI, bot, { Aq40SpellIds::Aq40DefenderPlague }))
    {
        if (action->getName() == "aq40 trash avoid dangerous aoe")
            return 4.0f;

        return 0.0f;
    }

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
        return 3.5f;

    if (dynamic_cast<MovementAction*>(action) &&
        !dynamic_cast<Aq40BugTrioAvoidPoisonCloudAction*>(action))
        return 0.0f;

    if (dynamic_cast<AttackAction*>(action) ||
        dynamic_cast<ReachTargetAction*>(action) ||
        dynamic_cast<CastReachTargetSpellAction*>(action))
        return 0.0f;

    return 1.0f;
}

float Aq40SarturaMultiplier::GetValue(Action* action)
{
    if (!action || !Aq40BossHelper::IsInAq40(bot) || !bot->IsInCombat() || Aq40BossHelper::IsEncounterTank(bot, bot))
        return 1.0f;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    bool whirlwindRisk = false;
    for (ObjectGuid const guid : attackers)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit)
            continue;

        bool const isSarturaMob = botAI->EqualLowercaseName(unit->GetName(), "battleguard sartura") ||
                                  botAI->EqualLowercaseName(unit->GetName(), "sartura's royal guard");
        if (!isSarturaMob || bot->GetDistance2d(unit) > 18.0f)
            continue;

        bool const spinning = unit->GetCurrentSpell(CURRENT_GENERIC_SPELL) ||
                              Aq40SpellIds::HasAnyAura(botAI, unit,
                                  { Aq40SpellIds::SarturaWhirlwind, Aq40SpellIds::SarturaGuardWhirlwind }) ||
                              botAI->HasAura("whirlwind", unit);
        if (spinning)
        {
            whirlwindRisk = true;
            break;
        }
    }

    if (!whirlwindRisk)
        return 1.0f;

    if (dynamic_cast<Aq40SarturaAvoidWhirlwindAction*>(action))
        return 3.5f;

    if (dynamic_cast<CastReachTargetSpellAction*>(action) ||
        (dynamic_cast<MovementAction*>(action) &&
         !dynamic_cast<Aq40SarturaAvoidWhirlwindAction*>(action)))
        return 0.0f;

    return 1.0f;
}

float Aq40HuhuranMultiplier::GetValue(Action* action)
{
    if (!action || !Aq40BossHelper::IsInAq40(bot) || !bot->IsInCombat())
        return 1.0f;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    Unit* huhuran = Aq40BossHelper::FindUnitByAnyName(botAI, attackers, { "princess huhuran" });
    if (!huhuran || Aq40BossHelper::IsEncounterTank(bot, bot) || !botAI->IsRanged(bot))
        return 1.0f;

    bool const poisonPhase = huhuran->GetHealthPct() <= 32.0f ||
                             Aq40SpellIds::HasAnyAura(botAI, huhuran, { Aq40SpellIds::HuhuranFrenzy });
    if (!poisonPhase)
        return 1.0f;

    if (dynamic_cast<Aq40HuhuranPoisonSpreadAction*>(action))
        return 3.0f;

    if (dynamic_cast<CombatFormationMoveAction*>(action) ||
        dynamic_cast<FollowAction*>(action) ||
        dynamic_cast<FleeAction*>(action) ||
        (dynamic_cast<MovementAction*>(action) &&
         !dynamic_cast<Aq40HuhuranPoisonSpreadAction*>(action)))
        return 0.0f;

    return 1.0f;
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

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    bool const inStomach = Aq40Helpers::IsCthunInStomach(bot, botAI);
    bool const darkGlare = [&]()
    {
        if (inStomach)
            return false;

        Unit* eye = Aq40BossHelper::FindUnitByAnyName(botAI, attackers, { "eye of c'thun" });
        if (!eye)
            return false;

        Spell* spell = eye->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        return (spell && Aq40SpellIds::MatchesAnySpellId(spell->GetSpellInfo(), { Aq40SpellIds::CthunDarkGlare })) ||
               Aq40SpellIds::HasAnyAura(botAI, eye, { Aq40SpellIds::CthunDarkGlare }) ||
               botAI->HasAura("dark glare", eye);
    }();
    bool const vulnerable = Aq40Helpers::IsCthunVulnerableNow(botAI, attackers);
    bool const addsPresent = Aq40BossHelper::HasAnyNamedUnit(botAI, attackers,
        { "eye tentacle", "claw tentacle", "giant eye tentacle", "giant claw tentacle", "flesh tentacle" });

    if (inStomach)
    {
        if (dynamic_cast<Aq40CthunStomachExitAction*>(action))
        {
            Aura* acid = Aq40SpellIds::GetAnyAura(bot, { Aq40SpellIds::CthunDigestiveAcid });
            if (!acid)
                acid = botAI->GetAura("digestive acid", bot, false, true);

            uint32 exitStacks = 10;
            if (Aq40BossHelper::IsEncounterPrimaryTank(bot, bot))
                exitStacks = 1;
            else if (botAI->IsHeal(bot))
                exitStacks = 5;

            if (acid && acid->GetStackAmount() >= exitStacks)
                return 4.0f;
        }

        if (dynamic_cast<Aq40CthunStomachDpsAction*>(action))
            return 3.0f;

        if (dynamic_cast<MovementAction*>(action) &&
            !dynamic_cast<Aq40CthunStomachExitAction*>(action))
            return 0.0f;
    }

    if (darkGlare)
    {
        if (dynamic_cast<Aq40CthunAvoidDarkGlareAction*>(action))
            return 4.0f;

        if (dynamic_cast<CastReachTargetSpellAction*>(action) ||
            (dynamic_cast<MovementAction*>(action) &&
             !dynamic_cast<Aq40CthunAvoidDarkGlareAction*>(action)))
            return 0.0f;
    }

    if (!inStomach && !darkGlare && !vulnerable && !addsPresent &&
        !Aq40BossHelper::IsEncounterTank(bot, bot) && (botAI->IsRanged(bot) || botAI->IsHeal(bot)))
    {
        if (dynamic_cast<Aq40CthunMaintainSpreadAction*>(action))
            return 2.5f;

        if (dynamic_cast<CombatFormationMoveAction*>(action) ||
            dynamic_cast<FollowAction*>(action) ||
            dynamic_cast<FleeAction*>(action) ||
            (dynamic_cast<MovementAction*>(action) &&
             !dynamic_cast<Aq40CthunMaintainSpreadAction*>(action)))
            return 0.0f;
    }

    if (dynamic_cast<CombatFormationMoveAction*>(action) ||
        dynamic_cast<FollowAction*>(action) ||
        dynamic_cast<FleeAction*>(action))
        return 0.0f;

    return 1.0f;
}
