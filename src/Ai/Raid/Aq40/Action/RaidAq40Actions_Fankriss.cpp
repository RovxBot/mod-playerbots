#include "RaidAq40Actions.h"

#include "../RaidAq40SpellIds.h"

namespace Aq40BossActions
{
Unit* FindFankrissTarget(PlayerbotAI* botAI, GuidVector const& attackers)
{
    return FindUnitByAnyName(botAI, attackers, { "fankriss the unyielding" });
}

std::vector<Unit*> FindFankrissSpawns(PlayerbotAI* botAI, GuidVector const& attackers)
{
    return FindUnitsByAnyName(botAI, attackers, { "spawn of fankriss" });
}
}    // namespace Aq40BossActions

bool Aq40FankrissTankSwapAction::Execute(Event /*event*/)
{
    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, context->GetValue<GuidVector>("attackers")->Get());
    Unit* fankriss = Aq40BossActions::FindFankrissTarget(botAI, encounterUnits);
    if (!fankriss)
        return false;

    // This action only fires for the current Fankriss tank with high Mortal Wound stacks.
    // If we are the primary tank, the backup should taunt; if we are a backup, the other
    // tank should pick up.  Either way, we stop attacking Fankriss and switch to a spawn
    // (if any) so we can drop stacks while another tank takes over.
    bool const hasBossAggro = Aq40BossHelper::IsUnitFocusedOnPlayer(fankriss, bot);
    if (!hasBossAggro)
        return false;

    // Try to find a spawn to attack while we drop stacks.
    std::vector<Unit*> spawns = Aq40BossActions::FindFankrissSpawns(botAI, encounterUnits);
    if (!spawns.empty())
    {
        // Pick a spawn not already held by another tank if possible.
        for (Unit* spawn : spawns)
        {
            if (!Aq40BossHelper::IsUnitHeldByEncounterTank(bot, spawn))
                return Attack(spawn);
        }
        return Attack(spawns.front());
    }

    // No spawns available — stop attacking to let the other tank build threat.
    // Returning true signals that the action "succeeded" (swap initiated).
    // The other tank will taunt naturally via the engine's tank-assist behavior.
    bot->AttackStop();
    return true;
}
