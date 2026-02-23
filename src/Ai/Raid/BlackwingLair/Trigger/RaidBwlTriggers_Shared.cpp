#include "RaidBwlTriggers.h"

#include <algorithm>
#include <cctype>
#include <string>

#include "RaidBwlSpellIds.h"
#include "SharedDefines.h"

namespace
{
std::string ToLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool IsBwlBossName(std::string const& name)
{
    return name.find("razorgore the untamed") != std::string::npos || name.find("grethok the controller") != std::string::npos ||
           name.find("vaelastrasz the corrupt") != std::string::npos || name.find("broodlord lashlayer") != std::string::npos ||
           name.find("firemaw") != std::string::npos || name.find("ebonroc") != std::string::npos ||
           name.find("flamegor") != std::string::npos || name.find("chromaggus") != std::string::npos ||
           name.find("lord victor nefarius") != std::string::npos || name.find("nefarian") != std::string::npos;
}

bool IsBwlDangerTrashName(std::string const& name)
{
    if (name.find("blackwing spellbinder") != std::string::npos || name.find("blackwing warlock") != std::string::npos ||
        name.find("blackwing taskmaster") != std::string::npos || name.find("blackwing technician") != std::string::npos)
    {
        return true;
    }

    if (name.find("death talon") != std::string::npos || name.find("chromatic drakonid") != std::string::npos)
    {
        return true;
    }

    return name.find("corrupted") != std::string::npos && name.find("whelp") != std::string::npos;
}

bool HasDangerousTrashInUnits(PlayerbotAI* botAI, GuidVector const& units)
{
    if (!botAI || units.empty())
    {
        return false;
    }

    for (ObjectGuid const guid : units)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit || !unit->IsAlive())
        {
            continue;
        }

        std::string const name = ToLower(unit->GetName());
        if (IsBwlDangerTrashName(name))
        {
            return true;
        }
    }

    return false;
}

bool HasBossInUnits(PlayerbotAI* botAI, GuidVector const& units)
{
    if (!botAI || units.empty())
    {
        return false;
    }

    for (ObjectGuid const guid : units)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit || !unit->IsAlive())
        {
            continue;
        }

        std::string const name = ToLower(unit->GetName());
        if (IsBwlBossName(name))
        {
            return true;
        }
    }

    return false;
}

bool HasEnragedDeathTalonSeether(PlayerbotAI* botAI, GuidVector const& units)
{
    if (!botAI || units.empty())
    {
        return false;
    }

    for (ObjectGuid const guid : units)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit || !unit->IsAlive())
        {
            continue;
        }

        std::string const name = ToLower(unit->GetName());
        if (name.find("death talon seether") == std::string::npos)
        {
            continue;
        }

        if (botAI->GetAura("enrage", unit, false, true) || botAI->GetAura("frenzy", unit, false, true))
        {
            return true;
        }
    }

    return false;
}
}  // namespace

bool BwlMissingOnyxiaScaleCloakTrigger::IsActive()
{
    if (!helper.IsInBwl() || !bot->IsInCombat())
    {
        return false;
    }

    return !botAI->HasAura(BwlSpellIds::OnyxiaScaleCloakAura, bot);
}

bool BwlTrashDangerousEncounterTrigger::IsActive()
{
    if (!helper.IsInBwl() || !bot->IsInCombat())
    {
        return false;
    }

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    GuidVector nearby = AI_VALUE(GuidVector, "nearest npcs");

    // Let dedicated boss encounter logic drive behavior when a known boss is active.
    if (HasBossInUnits(botAI, attackers) || HasBossInUnits(botAI, nearby))
    {
        return false;
    }

    return HasDangerousTrashInUnits(botAI, attackers) || HasDangerousTrashInUnits(botAI, nearby);
}

bool BwlDeathTalonSeetherEnrageTrigger::IsActive()
{
    if (!helper.IsInBwl() || !bot->IsInCombat() || bot->getClass() != CLASS_HUNTER)
    {
        return false;
    }

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    GuidVector nearby = AI_VALUE(GuidVector, "nearest npcs");

    return HasEnragedDeathTalonSeether(botAI, attackers) || HasEnragedDeathTalonSeether(botAI, nearby);
}
