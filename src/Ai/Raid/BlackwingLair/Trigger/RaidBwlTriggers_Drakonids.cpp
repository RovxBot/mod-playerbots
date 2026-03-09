#include "RaidBwlTriggers.h"

#include "Util/RaidBwlSpellIds.h"
#include "SharedDefines.h"
#include "Timer.h"

#include <unordered_map>

namespace
{
struct FiremawBuffetLosState
{
    uint8 lastObservedStacks = 0;
    uint32 lastLosAttemptMs = 0;
};

std::unordered_map<uint32, FiremawBuffetLosState> sFiremawBuffetLosStates;

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
    if (!helper.IsInBwl() || !bot->IsInCombat() || helper.IsDangerousTrashEncounterActive())
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
    if (!helper.IsInBwl() || !bot->IsInCombat() || helper.IsDangerousTrashEncounterActive())
    {
        return false;
    }

    Unit* firemaw = AI_VALUE2(Unit*, "find target", "firemaw");
    if (!firemaw || !firemaw->IsAlive())
    {
        return false;
    }

    return true;
}

bool BwlFiremawHighFlameBuffetTrigger::IsActive()
{
    if (!helper.IsInBwl() || !bot->IsInCombat() || helper.IsDangerousTrashEncounterActive())
    {
        return false;
    }

    Unit* firemaw = AI_VALUE2(Unit*, "find target", "firemaw");
    if (!firemaw || !firemaw->IsAlive())
    {
        return false;
    }

    if (helper.IsEncounterTank(bot) || firemaw->GetVictim() == bot)
    {
        return false;
    }

    uint32 const botKey = bot->GetGUID().GetCounter();
    FiremawBuffetLosState& state = sFiremawBuffetLosStates[botKey];
    Aura* selfBuffet = GetFlameBuffetAura(botAI, bot);
    uint8 const stacks = selfBuffet ? selfBuffet->GetStackAmount() : 0;
    uint32 const nowMs = getMSTime();

    if (stacks < 6)
    {
        state.lastObservedStacks = stacks;
        return false;
    }

    // Fire once when we newly cross into the hide threshold, then wait for the next Buffet cycle.
    bool const crossedThresholdNow = state.lastObservedStacks < 6;
    bool const cycleReady = nowMs - state.lastLosAttemptMs >= 6000;
    state.lastObservedStacks = stacks;

    if (!crossedThresholdNow && !cycleReady)
    {
        return false;
    }

    state.lastLosAttemptMs = nowMs;
    return true;
}

bool BwlFiremawMainTankHighFlameBuffetTrigger::IsActive()
{
    if (!helper.IsInBwl() || !bot->IsInCombat() || helper.IsDangerousTrashEncounterActive() || !helper.IsEncounterBackupTank(bot, 0))
    {
        return false;
    }

    Unit* firemaw = AI_VALUE2(Unit*, "find target", "firemaw");
    if (!firemaw || !firemaw->IsAlive())
    {
        return false;
    }

    Player* mainTank = helper.GetEncounterPrimaryTank();
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
    if (!helper.IsInBwl() || !bot->IsInCombat() || helper.IsDangerousTrashEncounterActive())
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
    if (!helper.IsInBwl() || !bot->IsInCombat() || helper.IsDangerousTrashEncounterActive())
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
    if (!helper.IsInBwl() || !bot->IsInCombat() || helper.IsDangerousTrashEncounterActive() ||
        !helper.IsEncounterBackupTank(bot, 0))
    {
        return false;
    }

    Unit* ebonroc = AI_VALUE2(Unit*, "find target", "ebonroc");
    if (!ebonroc || !ebonroc->IsAlive())
    {
        return false;
    }

    Player* mainTank = helper.GetEncounterPrimaryTank();
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
    if (!helper.IsInBwl() || !bot->IsInCombat() || helper.IsDangerousTrashEncounterActive())
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
    if (!helper.IsInBwl() || !bot->IsInCombat() || helper.IsDangerousTrashEncounterActive())
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
    if (!helper.IsInBwl() || !bot->IsInCombat() || helper.IsDangerousTrashEncounterActive())
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
