#include "RaidAq40TwinEmperors.h"

#include <string>

#include "ObjectAccessor.h"
#include "Pet.h"
#include "Playerbots.h"
#include "ScriptMgr.h"
#include "Spell.h"
#include "../RaidAq40BossHelper.h"
#include "../RaidAq40SpellIds.h"

namespace
{
bool IsTwinEmperor(Unit* caster)
{
    if (!caster || caster->GetMapId() != Aq40BossHelper::MAP_ID)
        return false;

    std::string const casterName = caster->GetName();
    return casterName == "Emperor Vek'nilash" || casterName == "Emperor Vek'lor";
}

// Find a single bot with the aq40 strategy active in the same instance
// so we can gate script work behind strategy presence.
PlayerbotAI* FindFirstAq40CombatBotInInstance(Unit* contextUnit)
{
    if (!contextUnit || !contextUnit->GetMap())
        return nullptr;

    Map::PlayerList const& players = contextUnit->GetMap()->GetPlayers();
    for (Map::PlayerList::const_iterator it = players.begin(); it != players.end(); ++it)
    {
        Player* player = it->GetSource();
        if (!player || !player->IsAlive())
            continue;

        PlayerbotAI* botAI = GET_PLAYERBOT_AI(player);
        if (botAI && botAI->HasStrategy("aq40", BOT_STATE_COMBAT))
            return botAI;
    }

    return nullptr;
}

// Request spell interrupts for all bots near a center position that have
// the aq40 strategy active.  Used by spell hooks that need immediate
// movement (Blizzard, Arcane Burst).
void RequestInterruptForAq40BotsNear(Unit* center, float radius)
{
    if (!center || !center->GetMap())
        return;

    Map::PlayerList const& players = center->GetMap()->GetPlayers();
    for (Map::PlayerList::const_iterator it = players.begin(); it != players.end(); ++it)
    {
        Player* player = it->GetSource();
        if (!player || !player->IsAlive())
            continue;

        if (center->GetExactDist2d(player) > radius)
            continue;

        PlayerbotAI* botAI = GET_PLAYERBOT_AI(player);
        if (botAI && botAI->HasStrategy("aq40", BOT_STATE_COMBAT))
            botAI->RequestSpellInterrupt();
    }
}

// Request spell interrupts for all aq40-strategy bots in the instance.
void RequestInterruptForAllAq40Bots(Unit* contextUnit)
{
    if (!contextUnit || !contextUnit->GetMap())
        return;

    Map::PlayerList const& players = contextUnit->GetMap()->GetPlayers();
    for (Map::PlayerList::const_iterator it = players.begin(); it != players.end(); ++it)
    {
        Player* player = it->GetSource();
        if (!player || !player->IsAlive())
            continue;

        PlayerbotAI* botAI = GET_PLAYERBOT_AI(player);
        if (botAI && botAI->HasStrategy("aq40", BOT_STATE_COMBAT))
            botAI->RequestSpellInterrupt();
    }
}

// Enforce pet passive/follow for all bots near a center (Arcane Burst, Blizzard).
void EnforcePetSafetyNear(Unit* center, float radius)
{
    if (!center || !center->GetMap())
        return;

    Map::PlayerList const& players = center->GetMap()->GetPlayers();
    for (Map::PlayerList::const_iterator it = players.begin(); it != players.end(); ++it)
    {
        Player* player = it->GetSource();
        if (!player || !player->IsAlive())
            continue;

        if (center->GetExactDist2d(player) > radius)
            continue;

        Pet* pet = player->GetPet();
        if (!pet || !pet->IsAlive())
            continue;

        PlayerbotAI* botAI = GET_PLAYERBOT_AI(player);
        if (!botAI || !botAI->HasStrategy("aq40", BOT_STATE_COMBAT))
            continue;

        // Stop pet from attacking the boss — Arcane Burst / Blizzard
        if (pet->GetVictim())
        {
            pet->AttackStop();
            pet->GetMotionMaster()->MoveFollow(player, PET_FOLLOW_DIST, pet->GetFollowAngle());
        }
    }
}
}  // namespace

// ===========================================================================
// Twin Emperors spell-driven script listener.
//
// VALIDATION: see TWIN_EMPERORS_VALIDATION.md, Scenario 3 (Hazard Pressure).
//
// This listener converts boss spell casts into first-class encounter events.
// Each handler:
//   1. Records the event timestamp via NoteTwin*Cast().
//   2. Requests spell interrupts for nearby bots so dodge actions fire
//      immediately instead of waiting for the current cast to finish.
//   3. Enforces pet safety (Arcane Burst, Blizzard) by stopping pet attacks
//      and returning pets to follow.
//
// Arcane Burst validation target: in stable states, only the active Vek'lor
// tank should be within 20y.  Any non-tank hit logged in WoWCombatLog.txt
// during stable phases is a validation failure.
// ===========================================================================
class Aq40TwinEmperorsListenerScript : public AllSpellScript
{
public:
    Aq40TwinEmperorsListenerScript() : AllSpellScript("Aq40TwinEmperorsListenerScript") { }

    void OnSpellCast(Spell* /*spell*/, Unit* caster, SpellInfo const* spellInfo, bool /*skipCheck*/) override
    {
        if (!spellInfo || !caster)
            return;

        if (!IsTwinEmperor(caster))
            return;

        if (!FindFirstAq40CombatBotInInstance(caster))
            return;

        switch (spellInfo->Id)
        {
            case Aq40SpellIds::TwinTeleport:
                Aq40TwinEmperors::NoteTwinTeleportCast(caster);
                break;

            case Aq40SpellIds::TwinBlizzard:
                Aq40TwinEmperors::NoteTwinBlizzardCast(caster);
                // Blizzard is an AoE — interrupt bots that are near the caster
                // so they can move immediately instead of finishing a cast.
                RequestInterruptForAq40BotsNear(caster, 30.0f);
                break;

            case Aq40SpellIds::TwinArcaneBurst:
                Aq40TwinEmperors::NoteTwinArcaneBurstCast(caster);
                // Arcane Burst hits everything within ~20y of Vek'lor.
                // Interrupt bots in range and enforce pet safety.
                RequestInterruptForAq40BotsNear(caster, 25.0f);
                EnforcePetSafetyNear(caster, 25.0f);
                break;

            case Aq40SpellIds::TwinHealBrother:
                Aq40TwinEmperors::NoteTwinHealBrotherCast(caster);
                // Heal Brother means bosses are too close — emergency.
                // Interrupt all bots so recovery actions fire immediately.
                RequestInterruptForAllAq40Bots(caster);
                break;

            case Aq40SpellIds::TwinUppercut:
                Aq40TwinEmperors::NoteTwinUppercutCast(caster);
                break;

            case Aq40SpellIds::TwinUnbalancingStrike:
                Aq40TwinEmperors::NoteTwinUnbalancingStrikeCast(caster);
                break;

            default:
                break;
        }
    }
};

void AddSC_Aq40BotScripts()
{
    new Aq40TwinEmperorsListenerScript();
}
