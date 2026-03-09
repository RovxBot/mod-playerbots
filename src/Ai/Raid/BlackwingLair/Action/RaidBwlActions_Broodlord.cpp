#include "RaidBwlActions.h"

#include <cmath>

#include "SharedDefines.h"
#include "Spell.h"

namespace
{
// GPS-verified fixed tank spot for Broodlord Lashlayer.
constexpr float BroodlordTankSpotX = -7573.97f;
constexpr float BroodlordTankSpotY = -1034.39f;
constexpr float BroodlordTankSpotZ = 449.34f;
}  // namespace

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

    // Tanks and any bot that has boss aggro go to the fixed tank spot.
    bool const hasAggro = broodlord->GetVictim() == bot;
    if (isTankRole || hasAggro)
    {
        float const dist = bot->GetDistance(BroodlordTankSpotX, BroodlordTankSpotY, BroodlordTankSpotZ);
        if (dist < 3.0f)
        {
            return false;
        }

        if (MoveTo(bot->GetMapId(), BroodlordTankSpotX, BroodlordTankSpotY, BroodlordTankSpotZ,
                   false, false, false, false, MovementPriority::MOVEMENT_COMBAT))
        {
            return true;
        }

        return MoveInside(bot->GetMapId(), BroodlordTankSpotX, BroodlordTankSpotY, BroodlordTankSpotZ,
                          2.0f, MovementPriority::MOVEMENT_COMBAT);
    }

    // Non-tanks position relative to boss facing.
    float targetX = broodlord->GetPositionX();
    float targetY = broodlord->GetPositionY();
    float targetZ = bot->GetPositionZ();

    float const facing = broodlord->GetOrientation();
    uint32 const slot = botAI->GetGroupSlotIndex(bot);

    float angleOffset = 0.0f;
    float distance = 0.0f;

    // During Blast Wave casts, melee should temporarily back out.
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
            SpellInfo const* spellInfo = spell->GetSpellInfo();
            if (BwlSpellIds::MatchesAnySpellId(spellInfo, {BwlSpellIds::BroodlordBlastWave}))
            {
                blastWaveCasting = true;
            }
        }
    }

    if (botAI->IsRanged(bot) || botAI->IsHeal(bot))
    {
        float spread = ((slot % 6) - 2.5f) * 0.15f;
        angleOffset = static_cast<float>(-M_PI / 2.0f) + spread;
        distance = botAI->IsHeal(bot) ? 15.0f : 18.0f;
    }
    else
    {
        float spread = ((slot % 4) - 1.5f) * 0.18f;
        angleOffset = static_cast<float>(-M_PI / 2.0f) + spread;
        distance = blastWaveCasting ? 14.0f : 6.0f;
    }

    float angle = facing + angleOffset;
    targetX += std::cos(angle) * distance;
    targetY += std::sin(angle) * distance;

    if (MoveTo(bot->GetMapId(), targetX, targetY, targetZ, false, false, false, false, MovementPriority::MOVEMENT_COMBAT))
    {
        return true;
    }

    return MoveInside(bot->GetMapId(), targetX, targetY, targetZ, 2.0f, MovementPriority::MOVEMENT_COMBAT);
}
