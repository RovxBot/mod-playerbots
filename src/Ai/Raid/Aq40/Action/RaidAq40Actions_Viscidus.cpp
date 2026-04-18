#include "RaidAq40Actions.h"

#include <cmath>

#include "../RaidAq40BossHelper.h"
#include "../RaidAq40SpellIds.h"

namespace Aq40BossActions
{
Unit* FindViscidusTarget(PlayerbotAI* botAI, GuidVector const& attackers)
{
    return FindUnitByAnyName(botAI, attackers, { "viscidus" });
}
}    // namespace Aq40BossActions

namespace
{
// FindLowestHealthUnit now lives in Aq40BossHelper.

Unit* FindViscidusGlobTarget(PlayerbotAI* botAI, GuidVector const& attackers)
{
    std::vector<Unit*> globs = Aq40BossActions::FindUnitsByAnyName(botAI, attackers, { "glob of viscidus" });
    return Aq40BossHelper::FindLowestHealthUnit(globs);
}
}    // namespace

bool Aq40ViscidusChooseTargetAction::Execute(Event /*event*/)
{
    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, context->GetValue<GuidVector>("attackers")->Get());
    if (encounterUnits.empty())
        return false;

    Unit* target = FindViscidusGlobTarget(botAI, encounterUnits);
    if (!target)
        target = Aq40BossActions::FindViscidusTarget(botAI, encounterUnits);
    if (!target)
        target = Aq40BossActions::FindUnitByAnyName(botAI, encounterUnits, { "toxic slime" });

    if (!target || (AI_VALUE(Unit*, "current target") == target && bot->GetVictim() == target))
        return false;

    return Attack(target);
}

bool Aq40ViscidusUseFrostAction::Execute(Event /*event*/)
{
    // Allow any DPS (melee or ranged) to apply frost. Melee shamans
    // (Frost Shock), DKs (Icy Touch), and others all contribute.
    if (Aq40BossHelper::IsEncounterTank(bot, bot) || botAI->IsHeal(bot))
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, context->GetValue<GuidVector>("attackers")->Get());
    Unit* viscidus = Aq40BossActions::FindViscidusTarget(botAI, encounterUnits);
    if (!viscidus)
        return false;

    if (Aq40SpellIds::HasAnyAura(botAI, viscidus,
            { Aq40SpellIds::ViscidusFreeze }) ||
        FindViscidusGlobTarget(botAI, encounterUnits))
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
    // All DPS (melee and ranged) converge during freeze window.
    // Viscidus requires 75-150 melee hits to shatter; ranged-heavy raids
    // need everyone in melee to meet the threshold.
    if (botAI->IsHeal(bot))
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, context->GetValue<GuidVector>("attackers")->Get());
    Unit* viscidus = Aq40BossActions::FindViscidusTarget(botAI, encounterUnits);
    if (!viscidus)
        return false;

    if (!Aq40SpellIds::HasAnyAura(botAI, viscidus,
            { Aq40SpellIds::ViscidusFreeze }))
        return false;

    if (AI_VALUE(Unit*, "current target") != viscidus)
        return Attack(viscidus);

    if (bot->GetDistance2d(viscidus) > 6.0f)
    {
        float dx = bot->GetPositionX() - viscidus->GetPositionX();
        float dy = bot->GetPositionY() - viscidus->GetPositionY();
        float len = std::sqrt(dx * dx + dy * dy);
        if (len < 0.1f)
        {
            dx = std::cos(bot->GetOrientation());
            dy = std::sin(bot->GetOrientation());
            len = 1.0f;
        }

        float desired = 4.0f;
        float moveX = viscidus->GetPositionX() + (dx / len) * desired;
        float moveY = viscidus->GetPositionY() + (dy / len) * desired;
        return MoveTo(bot->GetMapId(), moveX, moveY, bot->GetPositionZ(), false, false, false, false,
                      MovementPriority::MOVEMENT_COMBAT);
    }

    return false;
}
