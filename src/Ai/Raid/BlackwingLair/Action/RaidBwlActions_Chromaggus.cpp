#include "RaidBwlActions.h"

bool BwlUseHourglassSandAction::Execute(Event /*event*/)
{
    return botAI->CastSpell(BwlSpellIds::HourglassSandCure, bot);
}

bool BwlUseHourglassSandAction::isUseful()
{
    if (!botAI->HasAura(BwlSpellIds::AfflictionBronze, bot))
    {
        return false;
    }

    return bot->HasItemCount(BwlItems::HourglassSand, 1, false);
}
