#include "RaidBwlActions.h"

#include "SharedDefines.h"

bool BwlRazorgoreChooseTargetAction::Execute(Event /*event*/)
{
    GuidVector attackers = context->GetValue<GuidVector>("attackers")->Get();
    Unit* target = nullptr;
    Unit* razorgore = nullptr;

    std::vector<Unit*> mages;
    std::vector<Unit*> legionnaires;
    std::vector<Unit*> dragonkin;
    std::vector<Unit*> otherBlackwing;

    for (ObjectGuid const guid : attackers)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit || !unit->IsAlive())
        {
            continue;
        }

        std::string name = unit->GetName();
        if (botAI->EqualLowercaseName(name, "razorgore the untamed"))
        {
            razorgore = unit;
            continue;
        }

        // Pre-fight controller should still be burned down fast.
        if (botAI->EqualLowercaseName(name, "grethok the controller"))
        {
            target = unit;
            break;
        }

        if (botAI->EqualLowercaseName(name, "blackwing mage"))
        {
            mages.push_back(unit);
            continue;
        }

        if (botAI->EqualLowercaseName(name, "blackwing legionnaire"))
        {
            legionnaires.push_back(unit);
            continue;
        }

        if (botAI->EqualLowercaseName(name, "death talon dragonspawn") ||
            botAI->EqualLowercaseName(name, "death talon wyrmguard"))
        {
            dragonkin.push_back(unit);
            continue;
        }

        if (name.find("Blackwing") != std::string::npos || name.find("Death Talon") != std::string::npos)
        {
            otherBlackwing.push_back(unit);
        }
    }

    if (!razorgore)
    {
        razorgore = AI_VALUE2(Unit*, "find target", "razorgore the untamed");
    }

    if (!target && !mages.empty())
    {
        target = mages.front();
    }
    if (!target && !legionnaires.empty())
    {
        target = legionnaires.front();
    }
    if (!target && !dragonkin.empty())
    {
        target = dragonkin.front();
    }
    if (!target && !otherBlackwing.empty())
    {
        target = otherBlackwing.front();
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
