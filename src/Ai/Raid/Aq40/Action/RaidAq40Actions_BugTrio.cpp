#include "RaidAq40Actions.h"

#include <cmath>

#include "../RaidAq40BossHelper.h"
#include "../RaidAq40SpellIds.h"
#include "../../RaidBossHelpers.h"
#include "RtiTargetValue.h"

namespace Aq40BossActions
{
Unit* FindBugTrioUnit(PlayerbotAI* botAI, GuidVector const& units, char const* name)
{
    if (!botAI)
        return nullptr;

    return Aq40BossHelper::FindUnitByAnyName(botAI, units, { name });
}

Unit* FindBugTrioTarget(PlayerbotAI* botAI, GuidVector const& attackers)
{
    Unit* yauj = FindBugTrioUnit(botAI, attackers, "princess yauj");
    if (yauj)
        return yauj;

    Unit* vem = FindBugTrioUnit(botAI, attackers, "vem");
    if (vem)
        return vem;

    return FindBugTrioUnit(botAI, attackers, "lord kri");
}

Unit* FindBugTrioTankOwnedTarget(PlayerbotAI* botAI, Player* bot, GuidVector const& attackers)
{
    Unit* yauj = FindBugTrioUnit(botAI, attackers, "princess yauj");
    if (yauj && Aq40BossHelper::IsUnitHeldByEncounterTank(bot, yauj))
        return yauj;

    Unit* vem = FindBugTrioUnit(botAI, attackers, "vem");
    if (vem && Aq40BossHelper::IsUnitHeldByEncounterTank(bot, vem))
        return vem;

    Unit* kri = FindBugTrioUnit(botAI, attackers, "lord kri");
    if (kri && Aq40BossHelper::IsUnitHeldByEncounterTank(bot, kri))
        return kri;

    return nullptr;
}
}    // namespace Aq40BossActions

namespace
{
bool IsBugTrioKillTarget(PlayerbotAI* botAI, Unit* unit)
{
    return unit && unit->IsAlive() &&
           Aq40BossHelper::IsUnitNamedAny(botAI, unit, { "princess yauj", "vem", "lord kri" });
}

GuidVector GetBugTrioCombatUnits(PlayerbotAI* botAI)
{
    if (!botAI)
        return {};

    GuidVector const attackers = botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get();
    return Aq40BossHelper::GetActiveCombatUnits(botAI, attackers);
}

GuidVector GetBugTrioEncounterUnits(PlayerbotAI* botAI)
{
    if (!botAI)
        return {};

    GuidVector const attackers = botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get();
    return Aq40BossHelper::GetEncounterUnits(botAI, attackers);
}

Unit* FindBugTrioSkullTarget(Player* bot, PlayerbotAI* botAI)
{
    if (!bot || !botAI)
        return nullptr;

    Group* group = bot->GetGroup();
    if (!group)
        return nullptr;

    ObjectGuid const skullGuid = group->GetTargetIcon(RtiTargetValue::skullIndex);
    if (!skullGuid)
        return nullptr;

    Unit* skullTarget = botAI->GetUnit(skullGuid);
    return IsBugTrioKillTarget(botAI, skullTarget) ? skullTarget : nullptr;
}
}    // namespace

bool Aq40BugTrioChooseTargetAction::Execute(Event /*event*/)
{
    GuidVector const activeUnits = GetBugTrioCombatUnits(botAI);
    if (!Aq40BossHelper::HasAnyNamedUnit(botAI, activeUnits, { "princess yauj", "vem", "lord kri", "yauj brood" }))
        return false;

    GuidVector const encounterUnits = GetBugTrioEncounterUnits(botAI);
    Unit* skullTarget = FindBugTrioSkullTarget(bot, botAI);
    Unit* target = skullTarget;
    if (!target)
    {
        target = Aq40BossHelper::IsEncounterTank(bot, bot) ?
            Aq40BossActions::FindBugTrioTarget(botAI, encounterUnits) :
            Aq40BossActions::FindBugTrioTankOwnedTarget(botAI, bot, encounterUnits);
    }

    if (!target)
        return false;

    if (AI_VALUE(Unit*, "current target") == target && bot->GetVictim() == target)
        return false;

    if (Aq40BossHelper::IsEncounterTank(bot, bot) && !skullTarget)
        MarkTargetWithSkull(bot, target);

    return Attack(target);
}

bool Aq40BugTrioInterruptHealAction::Execute(Event /*event*/)
{
    GuidVector const activeUnits = GetBugTrioCombatUnits(botAI);
    if (!Aq40BossHelper::HasAnyNamedUnit(botAI, activeUnits, { "princess yauj", "vem", "lord kri", "yauj brood" }))
        return false;

    GuidVector const encounterUnits = GetBugTrioEncounterUnits(botAI);
    Unit* yauj = Aq40BossActions::FindBugTrioUnit(botAI, encounterUnits, "princess yauj");
    if (!yauj)
        return false;

    Spell* spell = yauj->GetCurrentSpell(CURRENT_GENERIC_SPELL);
    if (!spell || !Aq40SpellIds::MatchesAnySpellId(spell->GetSpellInfo(), { Aq40SpellIds::BugTrioYaujHeal }))
        return false;

    if (AI_VALUE(Unit*, "current target") != yauj)
        return Attack(yauj);

    return botAI->DoSpecificAction("interrupt spell", Event(), true);
}

bool Aq40BugTrioAvoidPoisonCloudAction::Execute(Event /*event*/)
{
    if (Aq40BossHelper::IsEncounterTank(bot, bot))
        return false;

    GuidVector const activeUnits = GetBugTrioCombatUnits(botAI);
    if (!Aq40BossHelper::HasAnyNamedUnit(botAI, activeUnits, { "princess yauj", "vem", "lord kri", "yauj brood" }))
        return false;

    GuidVector const encounterUnits = GetBugTrioEncounterUnits(botAI);
    Unit* kri = Aq40BossActions::FindBugTrioUnit(botAI, encounterUnits, "lord kri");
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
