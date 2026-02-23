#include "RaidBwlTriggers.h"

bool BwlAfflictionBronzeTrigger::IsActive()
{
    if (!helper.IsInBwl())
    {
        return false;
    }

    return helper.HasBronzeAffliction() && helper.HasHourglassSand();
}
