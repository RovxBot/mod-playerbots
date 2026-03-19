#include "RaidAq40Actions.h"

#include <cmath>

#include "../RaidAq40BossHelper.h"
#include "../RaidAq40SpellIds.h"

namespace Aq40BossActions
{
Unit* FindBugTrioTarget(PlayerbotAI* botAI, GuidVector const& attackers)
{
    Unit* yauj = AI_VALUE2(Unit*, "find target", "princess yauj");
    if (yauj)
        return yauj;

    Unit* vem = AI_VALUE2(Unit*, "find target", "vem");
    if (vem)
        return vem;

    return AI_VALUE2(Unit*, "find target", "lord kri");
}
}  // namespace Aq40BossActions

namespace
{
Unit* FindBugTrioYauj(PlayerbotAI* botAI)
{
    return AI_VALUE2(Unit*, "find target", "princess yauj");
}

Unit* FindBugTrioKri(PlayerbotAI* botAI)
{
    return AI_VALUE2(Unit*, "find target", "lord kri");
}

Unit* FindBugTrioVem(PlayerbotAI* botAI)
{
    return AI_VALUE2(Unit*, "find target", "vem");
}
}  // namespace

bool Aq40BugTrioChooseTargetAction::Execute(Event /*event*/)
{
    Unit* yauj = FindBugTrioYauj(botAI);
    Unit* kri = FindBugTrioKri(botAI);
    Unit* vem = FindBugTrioVem(botAI);

    Unit* target = nullptr;
    if (yauj)
    {
        Spell* spell = yauj->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        if (spell && Aq40SpellIds::MatchesAnySpellId(spell->GetSpellInfo(), { Aq40SpellIds::BugTrioYaujHeal }))
            target = yauj;
    }

    // Kill order: Yauj first (stop heals), then Vem, then Kri last
    // (delay poison cloud as long as possible).
    if (!target && yauj)
        target = yauj;
    if (!target && vem)
        target = vem;
    if (!target && kri)
        target = kri;

    if (!target)
        return false;

    if (AI_VALUE(Unit*, "current target") == target && bot->GetVictim() == target)
        return false;

    return Attack(target);
}

bool Aq40BugTrioAvoidPoisonCloudAction::Execute(Event /*event*/)
{
    if (Aq40BossHelper::IsEncounterTank(bot, bot))
        return false;

    Unit* kri = FindBugTrioKri(botAI);
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
