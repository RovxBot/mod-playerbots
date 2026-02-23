#include "RaidBwlActions.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <string>

namespace
{
std::string ToLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

int GetBwlTrashPriority(Unit* unit)
{
    if (!unit || !unit->IsAlive())
    {
        return std::numeric_limits<int>::max();
    }

    std::string const name = ToLower(unit->GetName());

    // Research-based kill order for dangerous BWL trash mechanics.
    if (name.find("blackwing spellbinder") != std::string::npos)
        return 10;
    if (name.find("blackwing warlock") != std::string::npos)
        return 15;
    if (name.find("enchanted felguard") != std::string::npos || name.find("felguard") != std::string::npos)
        return 18;
    if (name.find("blackwing taskmaster") != std::string::npos)
        return 20;
    if (name.find("death talon seether") != std::string::npos)
        return 25;
    if (name.find("death talon overseer") != std::string::npos)
        return 28;
    if (name.find("death talon captain") != std::string::npos)
        return 30;
    if (name.find("death talon flamescale") != std::string::npos)
        return 35;
    if (name.find("death talon wyrmguard") != std::string::npos || name.find("death talon dragonspawn") != std::string::npos ||
        name.find("chromatic drakonid") != std::string::npos)
        return 40;
    if (name.find("death talon hatcher") != std::string::npos)
        return 45;
    if (name.find("blackwing technician") != std::string::npos)
        return 50;
    if (name.find("corrupted") != std::string::npos && name.find("whelp") != std::string::npos)
        return 60;

    if (name.find("blackwing") != std::string::npos || name.find("death talon") != std::string::npos)
        return 90;

    return std::numeric_limits<int>::max();
}

bool IsMagicImmuneBwlTrash(Unit* unit)
{
    if (!unit || !unit->IsAlive())
    {
        return false;
    }

    std::string const name = ToLower(unit->GetName());
    // Confirmed BWL trash immunity: spellbinders are magic-immune.
    return name.find("blackwing spellbinder") != std::string::npos;
}

bool IsLikelyCasterPreferredTrash(Unit* unit)
{
    if (!unit || !unit->IsAlive())
    {
        return false;
    }

    std::string const name = ToLower(unit->GetName());
    // Overseers and chromatic drakonids are generally handled by casters probing vulnerabilities.
    return name.find("death talon overseer") != std::string::npos || name.find("chromatic drakonid") != std::string::npos;
}
}  // namespace

bool BwlWarnOnyxiaScaleCloakAction::Execute(Event /*event*/)
{
    botAI->TellMasterNoFacing("Warning: missing Onyxia Scale Cloak aura in BWL.");
    return true;
}

bool BwlTrashChooseTargetAction::Execute(Event /*event*/)
{
    Unit* bestTarget = nullptr;
    int bestPriority = std::numeric_limits<int>::max();
    Unit* fallbackTarget = nullptr;
    int fallbackPriority = std::numeric_limits<int>::max();

    bool const isCaster = botAI->IsCaster(bot);
    bool const isPhysical = !isCaster;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    GuidVector nearby = AI_VALUE(GuidVector, "nearest npcs");

    auto evaluate = [&](GuidVector const& units)
    {
        for (ObjectGuid const guid : units)
        {
            Unit* unit = botAI->GetUnit(guid);
            int const priority = GetBwlTrashPriority(unit);
            if (priority == std::numeric_limits<int>::max())
            {
                continue;
            }

            // Second pass: role-aware validity for "melee-only/caster-only" style trash handling.
            bool validForRole = true;
            int adjustedPriority = priority;

            if (isCaster && IsMagicImmuneBwlTrash(unit))
            {
                validForRole = false;
                adjustedPriority += 1000;
            }
            else if (isPhysical && IsLikelyCasterPreferredTrash(unit))
            {
                // Keep tanks/melee on physical-friendly threats first where possible.
                adjustedPriority += 6;
            }

            if (priority < fallbackPriority)
            {
                fallbackPriority = priority;
                fallbackTarget = unit;
            }

            if (validForRole && adjustedPriority < bestPriority)
            {
                bestPriority = adjustedPriority;
                bestTarget = unit;
            }
        }
    };

    evaluate(attackers);
    evaluate(nearby);

    if (!bestTarget)
    {
        bestTarget = fallbackTarget;
        bestPriority = fallbackPriority;
    }

    if (!bestTarget || bestPriority == std::numeric_limits<int>::max())
    {
        return false;
    }

    if (AI_VALUE(Unit*, "current target") == bestTarget)
    {
        return false;
    }

    return Attack(bestTarget, false);
}
