#ifndef _PLAYERBOT_RAIDBWLSPELLIDS_H
#define _PLAYERBOT_RAIDBWLSPELLIDS_H

#include <initializer_list>

#include "Define.h"
#include "PlayerbotAI.h"

namespace BwlSpellIds
{
    static constexpr uint32 OnyxiaScaleCloakAura = 22683;
    static constexpr uint32 AfflictionBronze = 23170;
    static constexpr uint32 HourglassSandCure = 23645;
    static constexpr uint32 BurningAdrenaline = 18173;
    static constexpr uint32 BurningAdrenalineAlt = 23620;
    static constexpr uint32 EssenceOfTheRed = 23513;
    static constexpr uint32 ShadowOfEbonroc = 23340;
    static constexpr uint32 FlamegorFrenzy = 23342;
    static constexpr uint32 BroodlordBlastWave = 23331;
    static constexpr uint32 DeathTalonSeetherEnrage = 22428;
    static constexpr uint32 ChromaggusIncinerate = 23308;
    static constexpr uint32 ChromaggusFrostBurn = 23187;
    static constexpr uint32 ChromaggusTimeLapse = 23310;
    static constexpr uint32 ChromaggusCorrosiveAcid = 23313;
    static constexpr uint32 ChromaggusIgniteFlesh = 23315;
    static constexpr uint32 ChromaggusFrenzy = 23128;

    inline bool HasAnyAura(PlayerbotAI* botAI, Unit* unit, std::initializer_list<uint32> spellIds)
    {
        if (!botAI || !unit)
        {
            return false;
        }

        for (uint32 spellId : spellIds)
        {
            if (botAI->HasAura(spellId, unit))
            {
                return true;
            }
        }

        return false;
    }

    inline Aura* GetAnyAura(Unit* unit, std::initializer_list<uint32> spellIds)
    {
        if (!unit)
        {
            return nullptr;
        }

        for (uint32 spellId : spellIds)
        {
            if (Aura* aura = unit->GetAura(spellId))
            {
                return aura;
            }
        }

        return nullptr;
    }

    inline bool MatchesAnySpellId(SpellInfo const* info, std::initializer_list<uint32> spellIds)
    {
        if (!info)
        {
            return false;
        }

        for (uint32 spellId : spellIds)
        {
            if (info->Id == spellId)
            {
                return true;
            }
        }

        return false;
    }
}  // namespace BwlSpellIds

namespace BwlGameObjects
{
    static constexpr uint32 SuppressionDevice = 179784;
}

namespace BwlCreatureIds
{
    static constexpr uint32 RazorgoreTheUntamed = 12435;
    static constexpr uint32 GrethokTheController = 12557;
    static constexpr uint32 VaelastraszTheCorrupt = 13020;
    static constexpr uint32 BroodlordLashlayer = 12017;
    static constexpr uint32 Firemaw = 11983;
    static constexpr uint32 Ebonroc = 14601;
    static constexpr uint32 Flamegor = 11981;
    static constexpr uint32 Chromaggus = 14020;
    static constexpr uint32 LordVictorNefarius = 10162;
    static constexpr uint32 Nefarian = 11583;

    static constexpr uint32 BlackwingMage = 12420;
    static constexpr uint32 BlackwingLegionnaire = 12416;
    static constexpr uint32 BlackwingSpellbinder = 12457;
    static constexpr uint32 BlackwingTaskmaster = 12458;
    static constexpr uint32 BlackwingWarlock = 12459;
    static constexpr uint32 BlackwingTechnician = 13996;

    static constexpr uint32 DeathTalonDragonspawn = 12422;
    static constexpr uint32 DeathTalonWyrmguard = 12460;
    static constexpr uint32 DeathTalonOverseer = 12461;
    static constexpr uint32 DeathTalonFlamescale = 12463;
    static constexpr uint32 DeathTalonSeether = 12464;
    static constexpr uint32 DeathTalonCaptain = 12467;
    static constexpr uint32 DeathTalonHatcher = 12468;

    static constexpr uint32 ChromaticDrakonid = 14302;
    static constexpr uint32 CorruptedRedWhelp = 14022;
    static constexpr uint32 CorruptedGreenWhelp = 14023;
    static constexpr uint32 CorruptedBlueWhelp = 14024;
    static constexpr uint32 CorruptedBronzeWhelp = 14025;
}

namespace BwlItems
{
    static constexpr uint32 HourglassSand = 19183;
}

#endif
