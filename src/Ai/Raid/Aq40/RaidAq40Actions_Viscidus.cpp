#include "RaidAq40Actions.h"

#include "RaidAq40SpellIds.h"

namespace Aq40BossActions
{
Unit* FindViscidusTarget(PlayerbotAI* botAI, GuidVector const& attackers)
{
    return FindUnitByAnyName(botAI, attackers, { "viscidus" });
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

Unit* FindViscidusGlobTarget(PlayerbotAI* botAI, GuidVector const& attackers)
{
    std::vector<Unit*> globs = Aq40BossActions::FindUnitsByAnyName(botAI, attackers, { "glob of viscidus" });
    return FindLowestHealthUnit(globs);
}
}  // namespace

bool Aq40ViscidusChooseTargetAction::Execute(Event /*event*/)
{
    GuidVector attackers = context->GetValue<GuidVector>("attackers")->Get();
    if (attackers.empty())
        return false;

    Unit* target = FindViscidusGlobTarget(botAI, attackers);
    if (!target)
        target = Aq40BossActions::FindViscidusTarget(botAI, attackers);
    if (!target)
        target = Aq40BossActions::FindUnitByAnyName(botAI, attackers, { "toxic slime" });

    if (!target || AI_VALUE(Unit*, "current target") == target)
        return false;

    return Attack(target);
}

bool Aq40ViscidusUseFrostAction::Execute(Event /*event*/)
{
    if (botAI->IsTank(bot) || botAI->IsHeal(bot) || !botAI->IsRanged(bot))
        return false;

    GuidVector attackers = context->GetValue<GuidVector>("attackers")->Get();
    Unit* viscidus = Aq40BossActions::FindViscidusTarget(botAI, attackers);
    if (!viscidus)
        return false;

    if (Aq40SpellIds::HasAnyAura(botAI, viscidus,
            { Aq40SpellIds::ViscidusFreeze, Aq40SpellIds::ViscidusSlowedMore }) ||
        FindViscidusGlobTarget(botAI, attackers))
        return false;

    static std::initializer_list<char const*> frostSpells = { "frostbolt", "ice lance", "frost shock", "icy touch",
                                                               "frostfire bolt" };
    for (char const* spell : frostSpells)
    {
        if (botAI->CanCastSpell(spell, viscidus) && botAI->CastSpell(spell, viscidus))
            return true;
    }

    if (AI_VALUE(Unit*, "current target") != viscidus)
        return Attack(viscidus);

    return false;
}

bool Aq40ViscidusShatterAction::Execute(Event /*event*/)
{
    if (botAI->IsRanged(bot) && !botAI->IsTank(bot))
        return false;

    GuidVector attackers = context->GetValue<GuidVector>("attackers")->Get();
    Unit* viscidus = Aq40BossActions::FindViscidusTarget(botAI, attackers);
    if (!viscidus)
        return false;

    if (!Aq40SpellIds::HasAnyAura(botAI, viscidus,
            { Aq40SpellIds::ViscidusFreeze, Aq40SpellIds::ViscidusSlowedMore }))
        return false;

    if (AI_VALUE(Unit*, "current target") != viscidus)
        return Attack(viscidus);

    if (bot->GetDistance2d(viscidus) > 6.0f)
        return Attack(viscidus);

    return false;
}
