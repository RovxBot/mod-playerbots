#include "RaidBwlActions.h"

bool BwlRazorgoreChooseTargetAction::Execute(Event /*event*/)
{
    Unit* razorgore = AI_VALUE2(Unit*, "find target", "razorgore the untamed");
    if (!razorgore || !razorgore->IsAlive())
    {
        return false;
    }

    if (AI_VALUE(Unit*, "current target") == razorgore)
    {
        return false;
    }

    return Attack(razorgore, true);
}
