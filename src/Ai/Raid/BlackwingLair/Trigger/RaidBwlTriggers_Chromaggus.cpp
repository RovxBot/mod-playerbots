#include "RaidBwlTriggers.h"

#include "SharedDefines.h"

bool BwlChromaggusEncounterTrigger::IsActive()
{
    if (!helper.IsInBwl() || !bot->IsInCombat())
    {
        return false;
    }

    if (Unit* chromaggus = AI_VALUE2(Unit*, "find target", "chromaggus"))
    {
        return chromaggus->IsAlive();
    }

    return false;
}

bool BwlChromaggusPositioningTrigger::IsActive()
{
    if (!helper.IsInBwl() || !bot->IsInCombat())
    {
        return false;
    }

    if (Unit* chromaggus = AI_VALUE2(Unit*, "find target", "chromaggus"))
    {
        return chromaggus->IsAlive();
    }

    return false;
}

bool BwlChromaggusFrenzyTrigger::IsActive()
{
    if (!helper.IsInBwl() || !bot->IsInCombat())
    {
        return false;
    }

    if (bot->getClass() != CLASS_HUNTER)
    {
        return false;
    }

    Unit* chromaggus = AI_VALUE2(Unit*, "find target", "chromaggus");
    if (!chromaggus || !chromaggus->IsAlive())
    {
        return false;
    }

    return botAI->HasAura("frenzy", chromaggus);
}

bool BwlAfflictionBronzeTrigger::IsActive()
{
    if (!helper.IsInBwl())
    {
        return false;
    }

    return helper.HasBronzeAffliction() && helper.HasHourglassSand();
}
