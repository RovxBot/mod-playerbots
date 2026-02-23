#ifndef _PLAYERBOT_RAIDBWLBOSSHELPER_H
#define _PLAYERBOT_RAIDBWLBOSSHELPER_H

#include <algorithm>
#include <cctype>
#include <limits>
#include <string>

#include "AiObject.h"
#include "PlayerbotAI.h"
#include "RaidBwlSpellIds.h"
#include "SharedDefines.h"
#include "Spell.h"

class BwlBossHelper : public AiObject
{
public:
    BwlBossHelper(PlayerbotAI* botAI) : AiObject(botAI) {}

    bool IsInBwl() const { return bot->GetMapId() == 469; }
    bool IsInBwlCombat() const { return IsInBwl() && bot->IsInCombat(); }
    bool HasBronzeAffliction() const { return botAI->HasAura(BwlSpellIds::AfflictionBronze, bot); }
    bool HasHourglassSand() const { return bot->HasItemCount(BwlItems::HourglassSand, 1, false); }

    Unit* FindAliveTarget(char const* targetName) const
    {
        Unit* unit = AI_VALUE2(Unit*, "find target", targetName);
        if (!unit || !unit->IsAlive())
        {
            return nullptr;
        }
        return unit;
    }

    bool IsNefarianPhaseTwoActive() const
    {
        Unit* nefarian = FindAliveTarget("nefarian");
        return nefarian && !nefarian->HasUnitFlag(UNIT_FLAG_NON_ATTACKABLE);
    }

    bool IsNefarianPhaseOneAdd(Unit* unit) const
    {
        if (!unit || !unit->IsAlive())
        {
            return false;
        }

        std::string const name = ToLower(unit->GetName());
        return name.find("drakonid") != std::string::npos || name.find("dragonspawn") != std::string::npos ||
            name.find("wyrmguard") != std::string::npos;
    }

    bool HasNefarianPhaseOneAddsInUnits(GuidVector const& units) const
    {
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

    bool IsNefarianPhaseOneActive() const
    {
        if (IsNefarianPhaseTwoActive())
        {
            return false;
        }

        Unit* victor = FindAliveTarget("lord victor nefarius");
        if (victor)
        {
            return true;
        }

        GuidVector attackers = AI_VALUE(GuidVector, "attackers");
        if (HasNefarianPhaseOneAddsInUnits(attackers))
        {
            return true;
        }

        GuidVector nearby = AI_VALUE(GuidVector, "nearest npcs");
        return HasNefarianPhaseOneAddsInUnits(nearby);
    }

    bool IsBwlBoss(Unit* unit) const
    {
        if (!unit || !unit->IsAlive())
        {
            return false;
        }

        std::string const name = ToLower(unit->GetName());
        return IsBwlBossName(name);
    }

    bool HasBossInUnits(GuidVector const& units) const
    {
        for (ObjectGuid const guid : units)
        {
            Unit* unit = botAI->GetUnit(guid);
            if (IsBwlBoss(unit))
            {
                return true;
            }
        }
        return false;
    }

    bool IsAnyBwlBossEncounterActive() const
    {
        if (!IsInBwlCombat())
        {
            return false;
        }

        GuidVector attackers = AI_VALUE(GuidVector, "attackers");
        GuidVector nearby = AI_VALUE(GuidVector, "nearest npcs");
        return HasBossInUnits(attackers) || HasBossInUnits(nearby) || IsNefarianPhaseOneActive() || IsNefarianPhaseTwoActive();
    }

    bool IsDangerousTrash(Unit* unit) const
    {
        if (!unit || !unit->IsAlive())
        {
            return false;
        }
        return IsDangerousTrashName(ToLower(unit->GetName()));
    }

    bool HasDangerousTrashInUnits(GuidVector const& units) const
    {
        for (ObjectGuid const guid : units)
        {
            Unit* unit = botAI->GetUnit(guid);
            if (IsDangerousTrash(unit))
            {
                return true;
            }
        }
        return false;
    }

    bool IsDangerousTrashEncounterActive() const
    {
        if (!IsInBwlCombat())
        {
            return false;
        }

        GuidVector attackers = AI_VALUE(GuidVector, "attackers");
        GuidVector nearby = AI_VALUE(GuidVector, "nearest npcs");

        if (HasBossInUnits(attackers) || HasBossInUnits(nearby) || IsNefarianPhaseOneActive() || IsNefarianPhaseTwoActive())
        {
            return false;
        }

        return HasDangerousTrashInUnits(attackers) || HasDangerousTrashInUnits(nearby);
    }

    bool IsDeathTalonMob(Unit* unit) const
    {
        if (!unit || !unit->IsAlive())
        {
            return false;
        }

        return ToLower(unit->GetName()).find("death talon") != std::string::npos;
    }

    bool IsDeathTalonSeether(Unit* unit) const
    {
        if (!unit || !unit->IsAlive())
        {
            return false;
        }

        return ToLower(unit->GetName()).find("death talon seether") != std::string::npos;
    }

    bool IsDeathTalonCaptain(Unit* unit) const
    {
        if (!unit || !unit->IsAlive())
        {
            return false;
        }

        return ToLower(unit->GetName()).find("death talon captain") != std::string::npos;
    }

    bool HasEnragedDeathTalonSeether(Unit* seether) const
    {
        if (!IsDeathTalonSeether(seether))
        {
            return false;
        }

        return botAI->GetAura("enrage", seether, false, true) || botAI->GetAura("frenzy", seether, false, true);
    }

    bool HasEnragedDeathTalonSeetherInUnits(GuidVector const& units) const
    {
        for (ObjectGuid const guid : units)
        {
            Unit* unit = botAI->GetUnit(guid);
            if (HasEnragedDeathTalonSeether(unit))
            {
                return true;
            }
        }
        return false;
    }

    bool HasUndetectedDeathTalonInUnits(GuidVector const& units) const
    {
        for (ObjectGuid const guid : units)
        {
            Unit* unit = botAI->GetUnit(guid);
            if (!IsDeathTalonMob(unit))
            {
                continue;
            }
            if (!botAI->GetAura("detect magic", unit, false, true))
            {
                return true;
            }
        }
        return false;
    }

    bool HasEnragedDeathTalonSeetherNearbyOrAttacking() const
    {
        GuidVector attackers = AI_VALUE(GuidVector, "attackers");
        GuidVector nearby = AI_VALUE(GuidVector, "nearest npcs");
        return HasEnragedDeathTalonSeetherInUnits(attackers) || HasEnragedDeathTalonSeetherInUnits(nearby);
    }

    bool HasUndetectedDeathTalonNearbyOrAttacking() const
    {
        GuidVector attackers = AI_VALUE(GuidVector, "attackers");
        GuidVector nearby = AI_VALUE(GuidVector, "nearest npcs");
        return HasUndetectedDeathTalonInUnits(attackers) || HasUndetectedDeathTalonInUnits(nearby);
    }

    int GetTrashPriority(Unit* unit) const
    {
        if (!unit || !unit->IsAlive())
        {
            return std::numeric_limits<int>::max();
        }

        std::string const name = ToLower(unit->GetName());

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
        if (name.find("death talon flamescale") != std::string::npos)
            return 30;
        if (name.find("death talon captain") != std::string::npos)
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

    bool IsMagicImmuneTrash(Unit* unit) const
    {
        if (!unit || !unit->IsAlive())
        {
            return false;
        }

        std::string const name = ToLower(unit->GetName());
        return name.find("blackwing spellbinder") != std::string::npos;
    }

    bool IsCasterPreferredTrash(Unit* unit) const
    {
        if (!unit || !unit->IsAlive())
        {
            return false;
        }

        std::string const name = ToLower(unit->GetName());
        return name.find("death talon overseer") != std::string::npos || name.find("chromatic drakonid") != std::string::npos;
    }

    int GetDetectMagicPriority(Unit* unit) const
    {
        if (!IsDeathTalonMob(unit))
        {
            return std::numeric_limits<int>::max();
        }

        std::string const name = ToLower(unit->GetName());
        if (name.find("death talon wyrmguard") != std::string::npos)
            return 10;
        if (name.find("death talon captain") != std::string::npos)
            return 20;
        if (name.find("death talon seether") != std::string::npos)
            return 30;
        if (name.find("death talon flamescale") != std::string::npos)
            return 40;
        if (name.find("death talon overseer") != std::string::npos)
            return 50;

        return 90;
    }

    bool IsChromaggusCastingTimeLapse() const
    {
        Unit* chromaggus = FindAliveTarget("chromaggus");
        if (!chromaggus || !chromaggus->HasUnitState(UNIT_STATE_CASTING))
        {
            return false;
        }

        Spell* spell = chromaggus->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        if (!spell)
        {
            spell = chromaggus->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
        }
        if (!spell || !spell->GetSpellInfo() || !spell->GetSpellInfo()->SpellName[LOCALE_enUS])
        {
            return false;
        }

        std::string const spellName = ToLower(spell->GetSpellInfo()->SpellName[LOCALE_enUS]);
        return spellName.find("time lapse") != std::string::npos || spellName.find("time warp") != std::string::npos;
    }

private:
    static std::string ToLower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }

    static bool IsBwlBossName(std::string const& name)
    {
        return name.find("razorgore the untamed") != std::string::npos || name.find("grethok the controller") != std::string::npos ||
            name.find("vaelastrasz the corrupt") != std::string::npos || name.find("broodlord lashlayer") != std::string::npos ||
            name.find("firemaw") != std::string::npos || name.find("ebonroc") != std::string::npos ||
            name.find("flamegor") != std::string::npos || name.find("chromaggus") != std::string::npos ||
            name.find("lord victor nefarius") != std::string::npos || name.find("nefarian") != std::string::npos;
    }

    static bool IsDangerousTrashName(std::string const& name)
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
};

#endif
