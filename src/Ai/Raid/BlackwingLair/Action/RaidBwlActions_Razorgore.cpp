#include "RaidBwlActions.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <string>

#include "SharedDefines.h"

bool BwlRazorgoreChooseTargetAction::Execute(Event /*event*/)
{
    GuidVector attackers = context->GetValue<GuidVector>("attackers")->Get();
    GuidVector nearby = context->GetValue<GuidVector>("nearest npcs")->Get();
    Unit* target = nullptr;
    Unit* razorgore = nullptr;
    int bestPriority = std::numeric_limits<int>::max();
    float bestDistance = std::numeric_limits<float>::max();

    auto toLower = [](std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    };

    auto classifyPriority = [&](Unit* unit) -> int
    {
        if (!unit || !unit->IsAlive())
        {
            return std::numeric_limits<int>::max();
        }

        switch (unit->GetEntry())
        {
            case BwlCreatureIds::GrethokTheController:
                return 0;
            case BwlCreatureIds::BlackwingMage:
                return 10;
            case BwlCreatureIds::BlackwingLegionnaire:
                return 20;
            case BwlCreatureIds::DeathTalonDragonspawn:
            case BwlCreatureIds::DeathTalonWyrmguard:
                return 30;
            case BwlCreatureIds::BlackwingTaskmaster:
            case BwlCreatureIds::BlackwingWarlock:
            case BwlCreatureIds::BlackwingSpellbinder:
            case BwlCreatureIds::DeathTalonOverseer:
            case BwlCreatureIds::DeathTalonFlamescale:
            case BwlCreatureIds::DeathTalonSeether:
            case BwlCreatureIds::DeathTalonCaptain:
            case BwlCreatureIds::DeathTalonHatcher:
            case BwlCreatureIds::BlackwingTechnician:
                return 40;
            default:
                break;
        }

        // Fallback for custom creature data.
        std::string const name = toLower(unit->GetName());
        if (name.find("grethok the controller") != std::string::npos)
            return 0;
        if (name.find("blackwing mage") != std::string::npos)
            return 10;
        if (name.find("blackwing legionnaire") != std::string::npos)
            return 20;
        if (name.find("death talon dragonspawn") != std::string::npos || name.find("death talon wyrmguard") != std::string::npos)
            return 30;
        if (name.find("blackwing") != std::string::npos || name.find("death talon") != std::string::npos)
            return 40;

        return std::numeric_limits<int>::max();
    };

    auto consider = [&](Unit* unit)
    {
        int const priority = classifyPriority(unit);
        if (priority == std::numeric_limits<int>::max())
        {
            return;
        }

        float const distance = bot->GetExactDist2d(unit);
        if (!target || priority < bestPriority || (priority == bestPriority && distance < bestDistance))
        {
            target = unit;
            bestPriority = priority;
            bestDistance = distance;
        }
    };

    auto evaluate = [&](GuidVector const& units)
    {
        for (ObjectGuid const guid : units)
        {
            Unit* unit = botAI->GetUnit(guid);
            if (!unit || !unit->IsAlive())
            {
                continue;
            }

            if (unit->GetEntry() == BwlCreatureIds::RazorgoreTheUntamed || botAI->EqualLowercaseName(unit->GetName(), "razorgore the untamed"))
            {
                razorgore = unit;
                continue;
            }

            consider(unit);
        }
    };

    evaluate(attackers);
    evaluate(nearby);

    if (!razorgore)
    {
        razorgore = AI_VALUE2(Unit*, "find target", "razorgore the untamed");
    }

    // During egg phase players use the orb and Razorgore is normally not attackable.
    // Bots should only swap to Razorgore once add pressure is gone and he is attackable.
    if (!target && razorgore && !razorgore->HasUnitFlag(UNIT_FLAG_NON_ATTACKABLE))
    {
        target = razorgore;
    }

    if (!target)
    {
        return false;
    }

    if (AI_VALUE(Unit*, "current target") == target)
    {
        return false;
    }

    if (target == razorgore)
    {
        return Attack(target, true);
    }

    return Attack(target, false);
}
