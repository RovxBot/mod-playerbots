#include "RaidAq40Multipliers.h"

#include "Action.h"
#include "MovementActions.h"
#include "ObjectGuid.h"
#include "Playerbots.h"
#include "RaidAq40BossHelper.h"
#include "RaidAq40SpellIds.h"

float Aq40GenericMultiplier::GetValue(Action* /*action*/)
{
    return 1.0f;
}

namespace
{
bool IsAnyNamedUnit(PlayerbotAI* botAI, GuidVector const& attackers, std::initializer_list<char const*> names)
{
    for (ObjectGuid const guid : attackers)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit)
            continue;

        for (char const* name : names)
        {
            if (botAI->EqualLowercaseName(unit->GetName(), name))
                return true;
        }
    }

    return false;
}

Unit* FindNamedUnit(PlayerbotAI* botAI, GuidVector const& attackers, std::initializer_list<char const*> names)
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
}  // namespace

float Aq40BugTrioMultiplier::GetValue(Action* action)
{
    if (!action || !Aq40BossHelper::IsInAq40(bot) || !bot->IsInCombat())
        return 1.0f;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    if (!IsAnyNamedUnit(botAI, attackers, { "lord kri", "princess yauj", "vem", "yauj brood" }))
        return 1.0f;

    Unit* kri = FindNamedUnit(botAI, attackers, { "lord kri" });
    if (!kri)
        return 1.0f;

    bool poisonCloudWindow = kri->GetHealthPct() <= 5.0f ||
                             Aq40SpellIds::HasAnyAura(botAI, kri, { Aq40SpellIds::BugTrioPoisonCloud });
    if (!poisonCloudWindow || botAI->IsTank(bot))
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
    Unit* ouro = FindNamedUnit(botAI, attackers, { "ouro" });
    if (!ouro)
        return 1.0f;

    std::string const actionName = action->getName();
    bool meleeOrTank = botAI->IsTank(bot) || !botAI->IsRanged(bot);
    if (meleeOrTank && bot->GetDistance2d(ouro) > 8.0f)
    {
        if (actionName == "aq40 ouro hold melee contact")
            return 3.0f;

        if (!dynamic_cast<MovementAction*>(action))
            return 0.5f;
    }

    return 1.0f;
}

float Aq40ViscidusMultiplier::GetValue(Action* action)
{
    if (!action || !Aq40BossHelper::IsInAq40(bot) || !bot->IsInCombat())
        return 1.0f;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    Unit* viscidus = FindNamedUnit(botAI, attackers, { "viscidus" });
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
