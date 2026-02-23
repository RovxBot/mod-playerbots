#ifndef _PLAYERBOT_RAIDAQ40SPELLIDS_H
#define _PLAYERBOT_RAIDAQ40SPELLIDS_H

#include <initializer_list>

#include "PlayerbotAI.h"

namespace Aq40SpellIds
{
// Sourced from AzerothCore Temple of Ahn'Qiraj scripts.
static constexpr uint32 SkeramArcaneExplosion = 26192;
static constexpr uint32 SkeramTrueFulfillment = 785;
static constexpr uint32 SarturaWhirlwind = 26083;
static constexpr uint32 SarturaGuardWhirlwind = 26038;
static constexpr uint32 TwinArcaneBurst = 568;
static constexpr uint32 CthunDarkGlare = 26029;
static constexpr uint32 CthunMindFlay = 26143;
static constexpr uint32 CthunDigestiveAcid = 26476;

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
