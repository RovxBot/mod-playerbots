#ifndef _PLAYERBOT_RAIDBWLBOSSHELPER_H
#define _PLAYERBOT_RAIDBWLBOSSHELPER_H

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <limits>
#include <string>

#include "AiObject.h"
#include "AiObjectContext.h"
#include "PlayerbotAI.h"
#include "RaidBwlSpellIds.h"
#include "SharedDefines.h"
#include "Spell.h"
#include "Unit.h"

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
        Unit* unit = context->GetValue<Unit*>("find target", targetName)->Get();
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

        if (IsCreatureEntry(unit, BwlCreatureIds::ChromaticDrakonid))
        {
            return true;
        }

        // Fallback to name for custom creature data.
        std::string const name = ToLower(unit->GetName());
        return name.find("chromatic drakonid") != std::string::npos;
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

        GuidVector attackers = context->GetValue<GuidVector>("attackers")->Get();
        if (HasNefarianPhaseOneAddsInUnits(attackers))
        {
            return true;
        }

        GuidVector nearby = context->GetValue<GuidVector>("nearest npcs")->Get();
        return HasNefarianPhaseOneAddsInUnits(nearby);
    }

    bool IsBwlBoss(Unit* unit) const
    {
        if (!unit || !unit->IsAlive())
        {
            return false;
        }

        if (IsAnyCreatureEntry(unit, {BwlCreatureIds::RazorgoreTheUntamed, BwlCreatureIds::GrethokTheController,
                                      BwlCreatureIds::VaelastraszTheCorrupt, BwlCreatureIds::BroodlordLashlayer,
                                      BwlCreatureIds::Firemaw, BwlCreatureIds::Ebonroc, BwlCreatureIds::Flamegor,
                                      BwlCreatureIds::Chromaggus, BwlCreatureIds::LordVictorNefarius, BwlCreatureIds::Nefarian}))
        {
            return true;
        }

        return IsBwlBossName(ToLower(unit->GetName()));
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

        GuidVector attackers = context->GetValue<GuidVector>("attackers")->Get();
        GuidVector nearby = context->GetValue<GuidVector>("nearest npcs")->Get();
        return HasBossInUnits(attackers) || HasBossInUnits(nearby) || IsNefarianPhaseOneActive() || IsNefarianPhaseTwoActive();
    }

    bool IsDangerousTrash(Unit* unit) const
    {
        if (!unit || !unit->IsAlive())
        {
            return false;
        }

        if (IsAnyCreatureEntry(unit, {BwlCreatureIds::BlackwingSpellbinder, BwlCreatureIds::BlackwingWarlock,
                                      BwlCreatureIds::BlackwingTaskmaster, BwlCreatureIds::BlackwingMage,
                                      BwlCreatureIds::BlackwingLegionnaire, BwlCreatureIds::BlackwingTechnician,
                                      BwlCreatureIds::DeathTalonDragonspawn, BwlCreatureIds::DeathTalonWyrmguard,
                                      BwlCreatureIds::DeathTalonOverseer, BwlCreatureIds::DeathTalonFlamescale,
                                      BwlCreatureIds::DeathTalonSeether, BwlCreatureIds::DeathTalonCaptain,
                                      BwlCreatureIds::DeathTalonHatcher, BwlCreatureIds::ChromaticDrakonid,
                                      BwlCreatureIds::CorruptedRedWhelp, BwlCreatureIds::CorruptedGreenWhelp,
                                      BwlCreatureIds::CorruptedBlueWhelp, BwlCreatureIds::CorruptedBronzeWhelp}))
        {
            return true;
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

        GuidVector attackers = context->GetValue<GuidVector>("attackers")->Get();
        GuidVector nearby = context->GetValue<GuidVector>("nearest npcs")->Get();

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

        if (IsAnyCreatureEntry(unit, {BwlCreatureIds::DeathTalonDragonspawn, BwlCreatureIds::DeathTalonWyrmguard,
                                      BwlCreatureIds::DeathTalonOverseer, BwlCreatureIds::DeathTalonFlamescale,
                                      BwlCreatureIds::DeathTalonSeether, BwlCreatureIds::DeathTalonCaptain,
                                      BwlCreatureIds::DeathTalonHatcher}))
        {
            return true;
        }

        return ToLower(unit->GetName()).find("death talon") != std::string::npos;
    }

    bool IsDeathTalonSeether(Unit* unit) const
    {
        if (!unit || !unit->IsAlive())
        {
            return false;
        }

        if (IsCreatureEntry(unit, BwlCreatureIds::DeathTalonSeether))
        {
            return true;
        }

        return ToLower(unit->GetName()).find("death talon seether") != std::string::npos;
    }

    bool IsDeathTalonCaptain(Unit* unit) const
    {
        if (!unit || !unit->IsAlive())
        {
            return false;
        }

        if (IsCreatureEntry(unit, BwlCreatureIds::DeathTalonCaptain))
        {
            return true;
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
        GuidVector attackers = context->GetValue<GuidVector>("attackers")->Get();
        GuidVector nearby = context->GetValue<GuidVector>("nearest npcs")->Get();
        return HasEnragedDeathTalonSeetherInUnits(attackers) || HasEnragedDeathTalonSeetherInUnits(nearby);
    }

    bool HasUndetectedDeathTalonNearbyOrAttacking() const
    {
        GuidVector attackers = context->GetValue<GuidVector>("attackers")->Get();
        GuidVector nearby = context->GetValue<GuidVector>("nearest npcs")->Get();
        return HasUndetectedDeathTalonInUnits(attackers) || HasUndetectedDeathTalonInUnits(nearby);
    }

    int GetTrashPriority(Unit* unit) const
    {
        if (!unit || !unit->IsAlive())
        {
            return std::numeric_limits<int>::max();
        }

        switch (unit->GetEntry())
        {
            case BwlCreatureIds::BlackwingSpellbinder:
                return 10;
            case BwlCreatureIds::BlackwingWarlock:
                return 15;
            case BwlCreatureIds::BlackwingTaskmaster:
                return 20;
            case BwlCreatureIds::BlackwingMage:
                return 22;
            case BwlCreatureIds::BlackwingLegionnaire:
                return 24;
            case BwlCreatureIds::DeathTalonSeether:
                return 25;
            case BwlCreatureIds::DeathTalonOverseer:
                return 28;
            case BwlCreatureIds::DeathTalonFlamescale:
                return 30;
            case BwlCreatureIds::DeathTalonCaptain:
                return 35;
            case BwlCreatureIds::DeathTalonWyrmguard:
            case BwlCreatureIds::DeathTalonDragonspawn:
            case BwlCreatureIds::ChromaticDrakonid:
                return 40;
            case BwlCreatureIds::DeathTalonHatcher:
                return 45;
            case BwlCreatureIds::BlackwingTechnician:
                return 50;
            case BwlCreatureIds::CorruptedRedWhelp:
            case BwlCreatureIds::CorruptedGreenWhelp:
            case BwlCreatureIds::CorruptedBlueWhelp:
            case BwlCreatureIds::CorruptedBronzeWhelp:
                return 60;
            default:
                break;
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

        if (IsCreatureEntry(unit, BwlCreatureIds::BlackwingSpellbinder))
        {
            return true;
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

        if (IsAnyCreatureEntry(unit, {BwlCreatureIds::DeathTalonOverseer, BwlCreatureIds::ChromaticDrakonid}))
        {
            return true;
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

        switch (unit->GetEntry())
        {
            case BwlCreatureIds::DeathTalonWyrmguard:
                return 10;
            case BwlCreatureIds::DeathTalonCaptain:
                return 20;
            case BwlCreatureIds::DeathTalonSeether:
                return 30;
            case BwlCreatureIds::DeathTalonFlamescale:
                return 40;
            case BwlCreatureIds::DeathTalonOverseer:
                return 50;
            default:
                break;
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
        if (!spell || !spell->GetSpellInfo())
        {
            return false;
        }

        SpellInfo const* spellInfo = spell->GetSpellInfo();
        if (BwlSpellIds::MatchesAnySpellId(spellInfo, {BwlSpellIds::ChromaggusTimeLapse}))
        {
            return true;
        }

        if (!spellInfo->SpellName[LOCALE_enUS])
        {
            return false;
        }

        std::string const spellName = ToLower(spellInfo->SpellName[LOCALE_enUS]);
        return spellName.find("time lapse") != std::string::npos || spellName.find("time warp") != std::string::npos;
    }

private:
    static bool IsCreatureEntry(Unit* unit, uint32 entry)
    {
        return unit && unit->GetTypeId() == TYPEID_UNIT && unit->GetEntry() == entry;
    }

    static bool IsAnyCreatureEntry(Unit* unit, std::initializer_list<uint32> entries)
    {
        if (!unit || unit->GetTypeId() != TYPEID_UNIT)
        {
            return false;
        }

        uint32 const entry = unit->GetEntry();
        for (uint32 id : entries)
        {
            if (entry == id)
            {
                return true;
            }
        }

        return false;
    }

    static std::string ToLower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }

    static bool IsBwlBossName(std::string const& name)
    {
        return name == "razorgore the untamed" || name == "grethok the controller" || name == "vaelastrasz the corrupt" ||
            name == "broodlord lashlayer" || name == "firemaw" || name == "ebonroc" || name == "flamegor" ||
            name == "chromaggus" || name == "lord victor nefarius" || name == "nefarian";
    }

    static bool IsDangerousTrashName(std::string const& name)
    {
        if (name == "blackwing spellbinder" || name == "blackwing warlock" || name == "blackwing taskmaster" ||
            name == "blackwing mage" || name == "blackwing legionnaire" || name == "blackwing technician")
        {
            return true;
        }

        if (name == "death talon dragonspawn" || name == "death talon wyrmguard" || name == "death talon overseer" ||
            name == "death talon flamescale" || name == "death talon seether" || name == "death talon captain" ||
            name == "death talon hatcher" || name == "chromatic drakonid")
        {
            return true;
        }

        return name.find("corrupted") != std::string::npos && name.find("whelp") != std::string::npos;
    }
};

#endif
