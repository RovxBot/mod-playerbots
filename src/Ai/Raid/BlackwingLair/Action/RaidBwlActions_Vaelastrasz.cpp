#include "RaidBwlActions.h"

bool BwlVaelastraszChooseTargetAction::Execute(Event /*event*/)
{
    Unit* vael = AI_VALUE2(Unit*, "find target", "vaelastrasz the corrupt");
    if (!vael || !vael->IsAlive())
    {
        return false;
    }

    if (AI_VALUE(Unit*, "current target") == vael)
    {
        return false;
    }

    return Attack(vael, true);
}
