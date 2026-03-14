#include "RaidBwlActions.h"

#include <cmath>

#include "SharedDefines.h"
#include "Spell.h"

bool BwlTurnOffSuppressionDeviceAction::Execute(Event /*event*/)
{
    bool usedAny = false;
    GuidVector gos = AI_VALUE(GuidVector, "nearest game objects");
    for (GuidVector::iterator i = gos.begin(); i != gos.end(); i++)
    {
        GameObject* go = botAI->GetGameObject(*i);
        if (!go)
        {
            continue;
        }
        if (go->GetEntry() != BwlGameObjects::SuppressionDevice || go->GetDistance(bot) >= 15.0f || go->GetGoState() != GO_STATE_READY)
        {
            continue;
        }
        go->SetGoState(GO_STATE_ACTIVE);
        usedAny = true;
    }
    return usedAny;
}

bool BwlTurnOffSuppressionDeviceAction::isUseful()
{
    GuidVector gos = AI_VALUE(GuidVector, "nearest game objects");
    for (GuidVector::iterator i = gos.begin(); i != gos.end(); i++)
    {
        GameObject* go = botAI->GetGameObject(*i);
        if (!go)
        {
            continue;
        }
        if (go->GetEntry() == BwlGameObjects::SuppressionDevice && go->GetDistance(bot) < 15.0f && go->GetGoState() == GO_STATE_READY)
        {
            return true;
        }
    }
    return false;
}

bool BwlBroodlordChooseTargetAction::Execute(Event /*event*/)
{
    Unit* broodlord = AI_VALUE2(Unit*, "find target", "broodlord lashlayer");
    if (!broodlord || !broodlord->IsAlive())
    {
        return false;
    }

    if (AI_VALUE(Unit*, "current target") == broodlord)
    {
        return false;
    }

    return Attack(broodlord, true);
}

bool BwlBroodlordPositionAction::Execute(Event /*event*/)
{
    BwlBossHelper helper(botAI);
    Unit* broodlord = AI_VALUE2(Unit*, "find target", "broodlord lashlayer");
    if (!broodlord || !broodlord->IsAlive())
    {
        return false;
    }

    bool const isPrimaryTank = helper.IsEncounterPrimaryTank(bot);
    bool const isBackupTank = helper.IsEncounterBackupTank(bot, 0);
    bool const isTankRole = isPrimaryTank || isBackupTank;
    bool const hasAggro = broodlord->GetVictim() == bot;

    // ── Tanks ──────────────────────────────────────────────────────────
    // Stay in melee range of the boss.  No fixed spot – just close the
    // gap so the boss doesn't path around.
    if (isTankRole)
    {
        if (bot->GetExactDist2d(broodlord) < 5.0f)
        {
            return false;
        }

        return MoveTo(bot->GetMapId(), broodlord->GetPositionX(), broodlord->GetPositionY(),
                      broodlord->GetPositionZ(), false, false, false, false,
                      MovementPriority::MOVEMENT_COMBAT);
    }

    // ── Non-tank with aggro (Knock Away snap) ──────────────────────────
    // Hold position so the boss doesn't get kited across the room while
    // the tank picks it back up.
    if (hasAggro)
    {
        return false;
    }

    // ── Blast Wave detection ───────────────────────────────────────────
    bool blastWaveCasting = false;
    if (broodlord->HasUnitState(UNIT_STATE_CASTING))
    {
        Spell* spell = broodlord->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        if (!spell)
        {
            spell = broodlord->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
        }
        if (spell && spell->GetSpellInfo())
        {
            if (BwlSpellIds::MatchesAnySpellId(spell->GetSpellInfo(),
                                               {BwlSpellIds::BroodlordBlastWave}))
            {
                blastWaveCasting = true;
            }
        }
    }

    // ── Ranged / Healers ───────────────────────────────────────────────
    // Positioning doesn't matter as long as they stay out of Blast Wave
    // (10 yd) and Cleave range.  Just maintain minimum safe distance from
    // the boss – no orientation tracking, so a boss snap changes nothing.
    if (botAI->IsRanged(bot) || botAI->IsHeal(bot))
    {
        constexpr float safeDistance = 20.0f;

        if (bot->GetExactDist2d(broodlord) >= safeDistance)
        {
            return false;
        }

        // Too close – move directly away from the boss.
        float dx = bot->GetPositionX() - broodlord->GetPositionX();
        float dy = bot->GetPositionY() - broodlord->GetPositionY();
        float mag = std::sqrt(dx * dx + dy * dy);
        if (mag < 0.001f)
        {
            // On top of the boss – pick an arbitrary direction.
            dx = 1.0f;
            dy = 0.0f;
            mag = 1.0f;
        }

        float targetX = broodlord->GetPositionX() + (dx / mag) * safeDistance;
        float targetY = broodlord->GetPositionY() + (dy / mag) * safeDistance;

        return MoveTo(bot->GetMapId(), targetX, targetY, bot->GetPositionZ(),
                      false, false, false, false, MovementPriority::MOVEMENT_COMBAT);
    }

    // ── Melee DPS ──────────────────────────────────────────────────────
    // Deterministic slot-based spread behind the boss (same pattern as
    // BWL Shared trash positioning in RaidBwlActions_Shared.cpp).
    // Back off during Blast Wave casts.
    float const facing = broodlord->GetOrientation();
    uint32 const slot = botAI->GetGroupSlotIndex(bot);
    float spread = ((slot % 4) - 1.5f) * 0.16f;
    float angle = facing + static_cast<float>(M_PI) + spread;
    float distance = blastWaveCasting ? 14.0f : 4.0f;

    float targetX = broodlord->GetPositionX() + std::cos(angle) * distance;
    float targetY = broodlord->GetPositionY() + std::sin(angle) * distance;
    float targetZ = bot->GetPositionZ();

    if (bot->GetExactDist2d(targetX, targetY) < 2.0f)
    {
        return false;
    }

    if (MoveTo(bot->GetMapId(), targetX, targetY, targetZ, false, false, false, false,
               MovementPriority::MOVEMENT_COMBAT))
    {
        return true;
    }

    return MoveInside(bot->GetMapId(), targetX, targetY, targetZ, 2.0f,
                      MovementPriority::MOVEMENT_COMBAT);
}
