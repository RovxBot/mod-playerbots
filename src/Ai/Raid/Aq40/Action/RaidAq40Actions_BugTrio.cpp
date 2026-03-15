#include "RaidAq40Actions.h"

#include <cmath>

#include "../RaidAq40BossHelper.h"
#include "../RaidAq40SpellIds.h"

namespace Aq40BossActions
{
Unit* FindBugTrioTarget(PlayerbotAI* botAI, GuidVector const& attackers)
{
    return FindUnitByAnyName(botAI, attackers, { "lord kri", "princess yauj", "vem" });
}
}  // namespace Aq40BossActions

namespace
{
Unit* FindBugTrioYauj(PlayerbotAI* botAI, GuidVector const& attackers)
{
    return Aq40BossActions::FindUnitByAnyName(botAI, attackers, { "princess yauj" });
}

Unit* FindBugTrioKri(PlayerbotAI* botAI, GuidVector const& attackers)
{
    return Aq40BossActions::FindUnitByAnyName(botAI, attackers, { "lord kri" });
}

Unit* FindBugTrioVem(PlayerbotAI* botAI, GuidVector const& attackers)
{
    return Aq40BossActions::FindUnitByAnyName(botAI, attackers, { "vem" });
}

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
}  // namespace

bool Aq40BugTrioChooseTargetAction::Execute(Event /*event*/)
{
    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, context->GetValue<GuidVector>("attackers")->Get());
    if (encounterUnits.empty())
        return false;

    Unit* yauj = FindBugTrioYauj(botAI, encounterUnits);
    Unit* kri = FindBugTrioKri(botAI, encounterUnits);
    Unit* vem = FindBugTrioVem(botAI, encounterUnits);

    Unit* target = nullptr;
    if (yauj)
    {
        Spell* spell = yauj->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        if (spell && Aq40SpellIds::MatchesAnySpellId(spell->GetSpellInfo(), { Aq40SpellIds::BugTrioYaujHeal }))
            target = yauj;
    }

    if (!target && !Aq40BossHelper::IsEncounterTank(bot, bot))
    {
        std::vector<Unit*> broods = Aq40BossActions::FindUnitsByAnyName(botAI, encounterUnits, { "yauj brood" });
        target = FindLowestHealthUnit(broods);
    }

    // Kill order: Yauj first (stop heals), then Vem, then Kri last
    // (delay poison cloud as long as possible).
    if (!target && yauj)
        target = yauj;
    if (!target && vem)
        target = vem;
    if (!target && kri)
        target = kri;

    if (!target || AI_VALUE(Unit*, "current target") == target)
        return false;

    return Attack(target);
}

bool Aq40BugTrioAvoidPoisonCloudAction::Execute(Event /*event*/)
{
    if (Aq40BossHelper::IsEncounterTank(bot, bot))
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, context->GetValue<GuidVector>("attackers")->Get());
    Unit* kri = FindBugTrioKri(botAI, encounterUnits);
    if (!kri)
        return false;

    bool poisonCloudWindow = kri->GetHealthPct() <= 5.0f ||
                             Aq40SpellIds::HasAnyAura(botAI, kri, { Aq40SpellIds::BugTrioPoisonCloud });
    if (!poisonCloudWindow)
        return false;

    float currentDistance = bot->GetDistance2d(kri);
    float desiredDistance = 18.0f;
    if (currentDistance >= desiredDistance)
        return false;

    bot->AttackStop();
    bot->InterruptNonMeleeSpells(true);
    return MoveAway(kri, desiredDistance - currentDistance);
}
