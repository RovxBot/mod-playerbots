#include "RaidBwlTriggers.h"

#include "RaidBwlSpellIds.h"
#include "SharedDefines.h"

namespace
{
Aura* GetFlameBuffetAura(PlayerbotAI* botAI, Unit* unit)
{
    if (!botAI || !unit)
    {
        return nullptr;
    }

    Aura* aura = botAI->GetAura("flame buffet", unit, false, true);
    if (aura)
    {
        return aura;
    }

    // Common classic id fallback.
    aura = unit->GetAura(23341);
    return aura;
}

Aura* GetFlamegorFrenzyAura(Unit* unit)
{
    if (!unit)
    {
        return nullptr;
    }

    return unit->GetAura(BwlSpellIds::FlamegorFrenzy);
}
}  // namespace

bool BwlFiremawEncounterTrigger::IsActive()
{
    if (!helper.IsInBwl() || !bot->IsInCombat())
    {
        return false;
    }

    if (Unit* firemaw = AI_VALUE2(Unit*, "find target", "firemaw"))
    {
        return firemaw->IsAlive();
    }

    return false;
}

bool BwlFiremawPositioningTrigger::IsActive()
{
    if (!helper.IsInBwl() || !bot->IsInCombat())
    {
        return false;
    }

    Unit* firemaw = AI_VALUE2(Unit*, "find target", "firemaw");
    if (!firemaw || !firemaw->IsAlive())
    {
        return false;
    }

    Aura* selfBuffet = GetFlameBuffetAura(botAI, bot);
    if (selfBuffet && selfBuffet->GetStackAmount() >= 6)
    {
        return false;
    }

    return true;
}

bool BwlFiremawHighFlameBuffetTrigger::IsActive()
{
    if (!helper.IsInBwl() || !bot->IsInCombat())
    {
        return false;
    }

    Unit* firemaw = AI_VALUE2(Unit*, "find target", "firemaw");
    if (!firemaw || !firemaw->IsAlive())
    {
        return false;
    }

    Aura* selfBuffet = GetFlameBuffetAura(botAI, bot);
    return selfBuffet && selfBuffet->GetStackAmount() >= 6;
}

bool BwlFiremawMainTankHighFlameBuffetTrigger::IsActive()
{
    if (!helper.IsInBwl() || !bot->IsInCombat() || !botAI->IsAssistTankOfIndex(bot, 0))
    {
        return false;
    }

    Unit* firemaw = AI_VALUE2(Unit*, "find target", "firemaw");
    if (!firemaw || !firemaw->IsAlive())
    {
        return false;
    }

    Unit* mainTank = AI_VALUE(Unit*, "main tank");
    if (!mainTank || mainTank == bot)
    {
        return false;
    }

    Aura* mainTankBuffet = GetFlameBuffetAura(botAI, mainTank);
    if (!mainTankBuffet || mainTankBuffet->GetStackAmount() < 8)
    {
        return false;
    }

    Aura* selfBuffet = GetFlameBuffetAura(botAI, bot);
    return !selfBuffet || selfBuffet->GetStackAmount() < 5;
}

bool BwlEbonrocEncounterTrigger::IsActive()
{
    if (!helper.IsInBwl() || !bot->IsInCombat())
    {
        return false;
    }

    if (Unit* ebonroc = AI_VALUE2(Unit*, "find target", "ebonroc"))
    {
        return ebonroc->IsAlive();
    }

    return false;
}

bool BwlEbonrocPositioningTrigger::IsActive()
{
    if (!helper.IsInBwl() || !bot->IsInCombat())
    {
        return false;
    }

    if (Unit* ebonroc = AI_VALUE2(Unit*, "find target", "ebonroc"))
    {
        return ebonroc->IsAlive();
    }

    return false;
}

bool BwlEbonrocMainTankShadowTrigger::IsActive()
{
    if (!helper.IsInBwl() || !bot->IsInCombat() || !botAI->IsAssistTank(bot))
    {
        return false;
    }

    Unit* ebonroc = AI_VALUE2(Unit*, "find target", "ebonroc");
    if (!ebonroc || !ebonroc->IsAlive())
    {
        return false;
    }

    Unit* mainTank = AI_VALUE(Unit*, "main tank");
    if (!mainTank || mainTank == bot)
    {
        return false;
    }

    Aura* shadow = mainTank->GetAura(BwlSpellIds::ShadowOfEbonroc);
    if (!shadow)
    {
        shadow = botAI->GetAura("shadow of ebonroc", mainTank, false, true);
    }

    return shadow != nullptr;
}

bool BwlFlamegorEncounterTrigger::IsActive()
{
    if (!helper.IsInBwl() || !bot->IsInCombat())
    {
        return false;
    }

    if (Unit* flamegor = AI_VALUE2(Unit*, "find target", "flamegor"))
    {
        return flamegor->IsAlive();
    }

    return false;
}

bool BwlFlamegorPositioningTrigger::IsActive()
{
    if (!helper.IsInBwl() || !bot->IsInCombat())
    {
        return false;
    }

    if (Unit* flamegor = AI_VALUE2(Unit*, "find target", "flamegor"))
    {
        return flamegor->IsAlive();
    }

    return false;
}

bool BwlFlamegorFrenzyTrigger::IsActive()
{
    if (!helper.IsInBwl() || !bot->IsInCombat())
    {
        return false;
    }

    Unit* flamegor = AI_VALUE2(Unit*, "find target", "flamegor");
    if (!flamegor || !flamegor->IsAlive())
    {
        return false;
    }

    if (bot->getClass() != CLASS_HUNTER)
    {
        return false;
    }

    return GetFlamegorFrenzyAura(flamegor) != nullptr;
}
