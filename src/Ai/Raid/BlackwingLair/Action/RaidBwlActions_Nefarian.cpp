#include "RaidBwlActions.h"
#include <cmath>
#include <limits>
#include <unordered_map>

#include "Timer.h"

namespace
{
// Stable Nefarian balcony anchor used when Victor is temporarily unavailable in target cache.
constexpr float NefarianP1FallbackAnchorX = -7588.27f;
constexpr float NefarianP1FallbackAnchorY = -1261.92f;
constexpr float NefarianP1FallbackAnchorZ = 482.03f;

// Fixed tunnel lane anchors for P1 setup. These avoid orbiting around
// Victor's runtime orientation and keep the raid near actual add spawns.
constexpr float NefarianP1LeftTunnelX = -7614.35f;
constexpr float NefarianP1LeftTunnelY = -1251.58f;
constexpr float NefarianP1RightTunnelX = -7562.19f;
constexpr float NefarianP1RightTunnelY = -1272.26f;

struct NefarianP2PositionState
{
    float lastFacing = 0.0f;
    uint32 lastFacingChangeMs = 0;
    uint32 lastRepositionMs = 0;
    bool initialized = false;
};

std::unordered_map<uint32, NefarianP2PositionState> sNefarianP2PositionStates;

bool ShouldDelayNefarianP2Reposition(Player* bot, Unit* nefarian, bool isTankRole)
{
    if (!bot || !nefarian || isTankRole)
    {
        return false;
    }

    uint32 const nowMs = getMSTime();
    uint32 const botKey = bot->GetGUID().GetCounter();
    NefarianP2PositionState& state = sNefarianP2PositionStates[botKey];

    float const facing = nefarian->GetOrientation();
    if (!state.initialized)
    {
        state.initialized = true;
        state.lastFacing = facing;
        state.lastFacingChangeMs = nowMs;
        return false;
    }

    float const deltaFacing = std::fabs(std::remainder(facing - state.lastFacing, static_cast<float>(2.0 * M_PI)));
    if (deltaFacing >= 0.30f)
    {
        state.lastFacing = facing;
        state.lastFacingChangeMs = nowMs;
    }

    // Let Nefarian settle after spin/threat flips to prevent raid-wide reposition ping-pong.
    if (nowMs - state.lastFacingChangeMs < 1200)
    {
        return true;
    }

    // Also rate-limit non-tank repositions to avoid micro-adjust storms.
    if (nowMs - state.lastRepositionMs < 900)
    {
        return true;
    }

    return false;
}

void MarkNefarianP2Reposition(Player* bot, bool isTankRole)
{
    if (!bot || isTankRole)
    {
        return;
    }

    sNefarianP2PositionStates[bot->GetGUID().GetCounter()].lastRepositionMs = getMSTime();
}
}  // namespace

bool BwlNefarianPhaseOneChooseTargetAction::Execute(Event /*event*/)
{
    BwlBossHelper helper(botAI);
    GuidVector attackers = context->GetValue<GuidVector>("attackers")->Get();
    GuidVector nearby = context->GetValue<GuidVector>("nearest npcs")->Get();
    Unit* target = nullptr;
    float bestDist = std::numeric_limits<float>::max();

    auto evaluate = [&](GuidVector const& units)
    {
        for (ObjectGuid const guid : units)
        {
            Unit* unit = botAI->GetUnit(guid);
            if (!unit || !unit->IsAlive() || !helper.IsNefarianPhaseOneAdd(unit))
            {
                continue;
            }

            // Prefer adds attacking us/raid first, then nearest fallback.
            bool const isThreateningRaid = unit->GetVictim() != nullptr;
            float scoreDist = bot->GetExactDist2d(unit);
            if (isThreateningRaid)
            {
                scoreDist -= 15.0f;
            }

            if (!target || scoreDist < bestDist)
            {
                target = unit;
                bestDist = scoreDist;
            }
        }
    };

    evaluate(attackers);
    evaluate(nearby);

    // Fallback in case attackers list is stale.
    if (!target)
    {
        target = AI_VALUE2(Unit*, "find target", "chromatic drakonid");
    }

    if (!target)
    {
        return false;
    }

    if (AI_VALUE(Unit*, "current target") == target)
    {
        return false;
    }

    return Attack(target, false);
}

bool BwlNefarianPhaseOneTunnelPositionAction::Execute(Event /*event*/)
{
    uint32 const slot = botAI->GetGroupSlotIndex(bot);
    bool const leftTunnel = (slot % 2 == 0);
    float targetX = leftTunnel ? NefarianP1LeftTunnelX : NefarianP1RightTunnelX;
    float targetY = leftTunnel ? NefarianP1LeftTunnelY : NefarianP1RightTunnelY;
    float targetZ = NefarianP1FallbackAnchorZ;

    // Build a local lane basis from tunnel -> room center.
    float toCenterX = NefarianP1FallbackAnchorX - targetX;
    float toCenterY = NefarianP1FallbackAnchorY - targetY;
    float len = std::sqrt(toCenterX * toCenterX + toCenterY * toCenterY);
    if (len > 0.001f)
    {
        toCenterX /= len;
        toCenterY /= len;
    }

    // Perpendicular axis for lane spread.
    float sideX = -toCenterY;
    float sideY = toCenterX;

    // Tanks step slightly forward to meet adds; others hold a bit behind.
    float forwardOffset = 0.0f;
    if (botAI->IsMainTank(bot) || botAI->IsAssistTank(bot))
    {
        forwardOffset = 4.0f;
    }
    else if (botAI->IsRanged(bot) || botAI->IsHeal(bot))
    {
        forwardOffset = -1.5f;
    }
    else
    {
        forwardOffset = 1.5f;
    }

    float sideSpread = ((slot % 5) - 2.0f) * 1.2f;
    targetX += toCenterX * forwardOffset + sideX * sideSpread;
    targetY += toCenterY * forwardOffset + sideY * sideSpread;

    if (MoveTo(bot->GetMapId(), targetX, targetY, targetZ, false, false, false, false, MovementPriority::MOVEMENT_COMBAT))
    {
        return true;
    }

    return MoveInside(bot->GetMapId(), targetX, targetY, targetZ, 2.5f, MovementPriority::MOVEMENT_COMBAT);
}

bool BwlNefarianPhaseTwoChooseTargetAction::Execute(Event /*event*/)
{
    Unit* nefarian = AI_VALUE2(Unit*, "find target", "nefarian");
    if (!nefarian || !nefarian->IsAlive())
    {
        return false;
    }

    if (AI_VALUE(Unit*, "current target") == nefarian)
    {
        return false;
    }

    return Attack(nefarian, true);
}

bool BwlNefarianPhaseTwoPositionAction::Execute(Event /*event*/)
{
    Unit* nefarian = AI_VALUE2(Unit*, "find target", "nefarian");
    if (!nefarian || !nefarian->IsAlive())
    {
        return false;
    }

    float targetX = nefarian->GetPositionX();
    float targetY = nefarian->GetPositionY();
    float targetZ = nefarian->GetPositionZ();
    float const facing = nefarian->GetOrientation();
    uint32 const slot = botAI->GetGroupSlotIndex(bot);
    bool const isMainTank = botAI->IsMainTank(bot);
    bool const isAssistTank = botAI->IsAssistTankOfIndex(bot, 0);
    bool const isTankRole = isMainTank || isAssistTank;

    if (ShouldDelayNefarianP2Reposition(bot, nefarian, isTankRole))
    {
        return false;
    }

    // P2 dragon setup:
    // MT in front, OT off-angle for recovery, raid stacked to side/rear
    // so shadow flame line and fear movement are controlled.
    float angleOffset = 0.0f;
    float distance = 0.0f;

    if (isMainTank)
    {
        angleOffset = 0.0f;
        distance = 7.0f;
    }
    else if (isAssistTank)
    {
        angleOffset = static_cast<float>(M_PI / 2.0f);
        distance = 10.0f;
    }
    else if (botAI->IsHeal(bot))
    {
        // Healers hold a central rear pocket so both lock/hunter lanes stay in healing range.
        float spread = ((slot % 5) - 2.0f) * 0.08f;
        angleOffset = static_cast<float>(-M_PI / 2.0f) + spread;
        distance = 30.0f;
    }
    else if (bot->getClass() == CLASS_HUNTER)
    {
        // Hunter lane: rear-right arc from healers.
        float spread = ((slot % 4) - 1.5f) * 0.08f;
        angleOffset = static_cast<float>(-M_PI / 2.0f) + 0.55f + spread;
        distance = 33.0f;
    }
    else if (bot->getClass() == CLASS_WARLOCK)
    {
        // Warlock lane: rear-left arc from healers.
        float spread = ((slot % 4) - 1.5f) * 0.08f;
        angleOffset = static_cast<float>(-M_PI / 2.0f) - 0.55f + spread;
        distance = 33.0f;
    }
    else if (botAI->IsRanged(bot))
    {
        float spread = ((slot % 6) - 2.5f) * 0.10f;
        angleOffset = static_cast<float>(-M_PI / 2.0f) + spread;
        distance = 34.0f;
    }
    else
    {
        float spread = ((slot % 5) - 2.0f) * 0.16f;
        angleOffset = static_cast<float>(-M_PI / 2.0f) + spread;
        distance = 8.0f;
    }

    float angle = facing + angleOffset;
    targetX += std::cos(angle) * distance;
    targetY += std::sin(angle) * distance;

    // Avoid movement churn from tiny facing updates/fear recovery jitter.
    if (bot->GetDistance2d(targetX, targetY) <= 1.5f && std::fabs(bot->GetPositionZ() - targetZ) <= 2.0f)
    {
        return false;
    }

    if (MoveTo(bot->GetMapId(), targetX, targetY, targetZ, false, false, false, false, MovementPriority::MOVEMENT_COMBAT))
    {
        MarkNefarianP2Reposition(bot, isTankRole);
        return true;
    }

    if (MoveInside(bot->GetMapId(), targetX, targetY, targetZ, 2.0f, MovementPriority::MOVEMENT_COMBAT))
    {
        MarkNefarianP2Reposition(bot, isTankRole);
        return true;
    }

    return false;
}
