#ifndef _PLAYERBOT_RAIDAQ40SPELLIDS_H
#define _PLAYERBOT_RAIDAQ40SPELLIDS_H

#include <initializer_list>

#include "PlayerbotAI.h"

namespace Aq40SpellIds
{
// Sourced from AzerothCore Temple of Ahn'Qiraj scripts.
static constexpr uint32 SkeramArcaneExplosion = 26192;
static constexpr uint32 SkeramTrueFulfillment = 785;
static constexpr uint32 BugTrioToxicVolley = 25812;
static constexpr uint32 BugTrioPoisonCloud = 26590;
static constexpr uint32 BugTrioYaujHeal = 25807;
static constexpr uint32 SarturaWhirlwind = 26083;
static constexpr uint32 SarturaGuardWhirlwind = 26038;
static constexpr uint32 Aq40DefenderMeteor = 26558;
static constexpr uint32 Aq40DefenderPlague = 26556;
static constexpr uint32 Aq40DefenderShadowStorm = 26555;
static constexpr uint32 Aq40DefenderThunderclap = 26554;
static constexpr uint32 Aq40EradicatorShockBlast = 26458;
static constexpr uint32 Aq40WarderFireNova = 26073;
static constexpr uint32 Aq40WarderFear = 26070;
static constexpr uint32 Aq40WarderRoots = 26071;
static constexpr uint32 Aq40WarderSilence = 26069;
static constexpr uint32 Aq40NullifierNullify = 26552;
static constexpr uint32 TwinArcaneBurst = 568;
static constexpr uint32 OuroSweep = 26103;
static constexpr uint32 OuroSandBlast = 26102;
static constexpr uint32 OuroGroundRupture = 26100;
static constexpr uint32 OuroBerserk = 26615;
static constexpr uint32 CthunDarkGlare = 26029;
static constexpr uint32 CthunMindFlay = 26143;
static constexpr uint32 CthunDigestiveAcid = 26476;
static constexpr uint32 ViscidusPoisonShock = 25993;
static constexpr uint32 ViscidusPoisonboltVolley = 25991;
static constexpr uint32 ViscidusSlowed = 26034;
static constexpr uint32 ViscidusSlowedMore = 26036;
static constexpr uint32 ViscidusFreeze = 25937;

inline bool HasAnyAura(PlayerbotAI* botAI, Unit* unit, std::initializer_list<uint32> spellIds)
{
    if (!botAI || !unit)
        return false;

    for (uint32 spellId : spellIds)
    {
        if (botAI->HasAura(spellId, unit))
            return true;
    }

    return false;
}

inline Aura* GetAnyAura(Unit* unit, std::initializer_list<uint32> spellIds)
{
    if (!unit)
        return nullptr;

    for (uint32 spellId : spellIds)
    {
        if (Aura* aura = unit->GetAura(spellId))
            return aura;
    }

    return nullptr;
}

inline bool MatchesAnySpellId(SpellInfo const* info, std::initializer_list<uint32> spellIds)
{
    if (!info)
        return false;

    for (uint32 spellId : spellIds)
    {
        if (info->Id == spellId)
            return true;
    }

    return false;
}
}  // namespace Aq40SpellIds

#endif
