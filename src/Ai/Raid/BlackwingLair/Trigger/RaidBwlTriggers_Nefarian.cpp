#include "RaidBwlTriggers.h"

#include <algorithm>
#include <cctype>
#include <string>

#include "SharedDefines.h"

namespace
{
bool ContainsToken(std::string value, char const* token)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value.find(token) != std::string::npos;
}

bool IsNefarianPhaseOneAdd(Unit* unit)
{
    if (!unit || !unit->IsAlive())
    {
        return false;
    }

    std::string name = unit->GetName();
    return ContainsToken(name, "drakonid") || ContainsToken(name, "dragonspawn") || ContainsToken(name, "wyrmguard");
}

bool HasPhaseOneAdds(PlayerbotAI* botAI, GuidVector const& units)
{
    if (!botAI || units.empty())
    {
        return false;
    }

    for (ObjectGuid const guid : units)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (IsNefarianPhaseOneAdd(unit))
        {
            return true;
        }
    }

    return false;
}

bool IsNefarianPhaseTwoActive(Unit* nefarian)
{
    if (!nefarian || !nefarian->IsAlive())
    {
        return false;
    }

    return !nefarian->HasUnitFlag(UNIT_FLAG_NON_ATTACKABLE);
}
}  // namespace

bool BwlNefarianPhaseOneTrigger::IsActive()
{
    if (!helper.IsInBwl() || !bot->IsInCombat())
    {
        return false;
    }

    Unit* nefarian = AI_VALUE2(Unit*, "find target", "nefarian");
    if (IsNefarianPhaseTwoActive(nefarian))
    {
        return false;
    }

    if (Unit* victor = AI_VALUE2(Unit*, "find target", "lord victor nefarius"))
    {
        if (victor->IsAlive())
        {
            return true;
        }
    }

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    if (HasPhaseOneAdds(botAI, attackers))
    {
        return true;
    }

    GuidVector npcs = AI_VALUE(GuidVector, "nearest npcs");
    return HasPhaseOneAdds(botAI, npcs);
}

bool BwlNefarianPhaseOneTunnelPositioningTrigger::IsActive()
{
    if (!helper.IsInBwl() || !bot->IsInCombat())
    {
        return false;
    }

    Unit* nefarian = AI_VALUE2(Unit*, "find target", "nefarian");
    if (IsNefarianPhaseTwoActive(nefarian))
    {
        return false;
    }

    if (Unit* victor = AI_VALUE2(Unit*, "find target", "lord victor nefarius"))
    {
        return victor->IsAlive();
    }

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    if (HasPhaseOneAdds(botAI, attackers))
    {
        return true;
    }

    GuidVector npcs = AI_VALUE(GuidVector, "nearest npcs");
    return HasPhaseOneAdds(botAI, npcs);
}

bool BwlNefarianPhaseTwoTrigger::IsActive()
{
    if (!helper.IsInBwl() || !bot->IsInCombat())
    {
        return false;
    }

    Unit* nefarian = AI_VALUE2(Unit*, "find target", "nefarian");
    return IsNefarianPhaseTwoActive(nefarian);
}

bool BwlNefarianPhaseTwoPositioningTrigger::IsActive()
{
    if (!helper.IsInBwl() || !bot->IsInCombat())
    {
        return false;
    }

    Unit* nefarian = AI_VALUE2(Unit*, "find target", "nefarian");
    return IsNefarianPhaseTwoActive(nefarian);
}
