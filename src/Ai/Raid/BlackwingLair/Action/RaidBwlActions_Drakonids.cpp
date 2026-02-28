#include "RaidBwlActions.h"

#include <cmath>
#include <unordered_map>

#include "RaidBwlSpellIds.h"
#include "SharedDefines.h"
#include "Timer.h"

namespace
{
struct DrakePositionState
{
    float lastFacing = 0.0f;
    uint32 lastFacingChangeMs = 0;
    uint32 lastRepositionMs = 0;
    bool initialized = false;
};

std::unordered_map<uint32, DrakePositionState> sDrakePositionStates;

bool ShouldDelayDrakeReposition(Player* bot, Unit* drake, bool isTankRole)
{
    if (!bot || !drake || isTankRole)
    {
        return false;
    }

    uint32 const nowMs = getMSTime();
    uint32 const botKey = bot->GetGUID().GetCounter();
    DrakePositionState& state = sDrakePositionStates[botKey];

    float const facing = drake->GetOrientation();
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

    // Let the drake settle after spin/threat flips to prevent raid-wide reposition ping-pong.
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

void MarkDrakeReposition(Player* bot, bool isTankRole)
{
    if (!bot || isTankRole)
    {
        return;
    }

    sDrakePositionStates[bot->GetGUID().GetCounter()].lastRepositionMs = getMSTime();
}
}  // namespace

bool BwlFiremawChooseTargetAction::Execute(Event /*event*/)
{
    Unit* firemaw = AI_VALUE2(Unit*, "find target", "firemaw");
    if (!firemaw || !firemaw->IsAlive())
    {
        return false;
    }

    if (AI_VALUE(Unit*, "current target") == firemaw)
    {
        return false;
    }

    return Attack(firemaw, true);
}

bool BwlFiremawPositionAction::Execute(Event /*event*/)
{
    Unit* firemaw = AI_VALUE2(Unit*, "find target", "firemaw");
    if (!firemaw || !firemaw->IsAlive())
    {
        return false;
    }

    float targetX = firemaw->GetPositionX();
    float targetY = firemaw->GetPositionY();
    float targetZ = bot->GetPositionZ();
    float const facing = firemaw->GetOrientation();
    uint32 const slot = botAI->GetGroupSlotIndex(bot);

    float angleOffset = 0.0f;
    float distance = 0.0f;

    bool const isMainTank = botAI->IsMainTank(bot);
    bool const isAssistTank = botAI->IsAssistTankOfIndex(bot, 0);
    bool const isTankRole = isMainTank || isAssistTank;

    if (ShouldDelayDrakeReposition(bot, firemaw, isTankRole))
    {
        return false;
    }

    if (isMainTank)
    {
        angleOffset = 0.0f;
        distance = 6.0f;
    }
    else if (isAssistTank)
    {
        angleOffset = static_cast<float>(M_PI / 2.0f);
        distance = 8.0f;
    }
    else if (botAI->IsRanged(bot) || botAI->IsHeal(bot))
    {
        // Hold in a safe lane that can LoS quickly behind nearby pillars/corners.
        float spread = ((slot % 6) - 2.5f) * 0.12f;
        angleOffset = static_cast<float>(-M_PI / 2.0f) + spread;
        distance = botAI->IsHeal(bot) ? 20.0f : 24.0f;
    }
    else
    {
        float spread = ((slot % 4) - 1.5f) * 0.18f;
        angleOffset = static_cast<float>(-M_PI / 2.0f) + spread;
        distance = 7.0f;
    }

    float angle = facing + angleOffset;
    targetX += std::cos(angle) * distance;
    targetY += std::sin(angle) * distance;

    if (MoveTo(bot->GetMapId(), targetX, targetY, targetZ, false, false, false, false, MovementPriority::MOVEMENT_COMBAT))
    {
        MarkDrakeReposition(bot, isTankRole);
        return true;
    }

    if (MoveInside(bot->GetMapId(), targetX, targetY, targetZ, 2.0f, MovementPriority::MOVEMENT_COMBAT))
    {
        MarkDrakeReposition(bot, isTankRole);
        return true;
    }

    return false;
}

bool BwlEbonrocChooseTargetAction::Execute(Event /*event*/)
{
    Unit* ebonroc = AI_VALUE2(Unit*, "find target", "ebonroc");
    if (!ebonroc || !ebonroc->IsAlive())
    {
        return false;
    }

    if (AI_VALUE(Unit*, "current target") == ebonroc)
    {
        return false;
    }

    return Attack(ebonroc, true);
}

bool BwlEbonrocPositionAction::Execute(Event /*event*/)
{
    Unit* ebonroc = AI_VALUE2(Unit*, "find target", "ebonroc");
    if (!ebonroc || !ebonroc->IsAlive())
    {
        return false;
    }

    float targetX = ebonroc->GetPositionX();
    float targetY = ebonroc->GetPositionY();
    float targetZ = bot->GetPositionZ();
    float const facing = ebonroc->GetOrientation();
    uint32 const slot = botAI->GetGroupSlotIndex(bot);

    float angleOffset = 0.0f;
    float distance = 0.0f;

    bool const isMainTank = botAI->IsMainTank(bot);
    bool const isAssistTank = botAI->IsAssistTankOfIndex(bot, 0);
    bool const isTankRole = isMainTank || isAssistTank;

    if (ShouldDelayDrakeReposition(bot, ebonroc, isTankRole))
    {
        return false;
    }

    if (isMainTank)
    {
        angleOffset = 0.0f;
        distance = 6.0f;
    }
    else if (isAssistTank)
    {
        angleOffset = static_cast<float>(M_PI / 2.0f);
        distance = 8.0f;
    }
    else if (botAI->IsRanged(bot) || botAI->IsHeal(bot))
    {
        float spread = ((slot % 6) - 2.5f) * 0.12f;
        angleOffset = static_cast<float>(-M_PI / 2.0f) + spread;
        distance = botAI->IsHeal(bot) ? 20.0f : 24.0f;
    }
    else
    {
        float spread = ((slot % 4) - 1.5f) * 0.18f;
        angleOffset = static_cast<float>(-M_PI / 2.0f) + spread;
        distance = 7.0f;
    }

    float angle = facing + angleOffset;
    targetX += std::cos(angle) * distance;
    targetY += std::sin(angle) * distance;

    if (MoveTo(bot->GetMapId(), targetX, targetY, targetZ, false, false, false, false, MovementPriority::MOVEMENT_COMBAT))
    {
        MarkDrakeReposition(bot, isTankRole);
        return true;
    }

    if (MoveInside(bot->GetMapId(), targetX, targetY, targetZ, 2.0f, MovementPriority::MOVEMENT_COMBAT))
    {
        MarkDrakeReposition(bot, isTankRole);
        return true;
    }

    return false;
}

bool BwlFlamegorChooseTargetAction::Execute(Event /*event*/)
{
    Unit* flamegor = AI_VALUE2(Unit*, "find target", "flamegor");
    if (!flamegor || !flamegor->IsAlive())
    {
        return false;
    }

    if (AI_VALUE(Unit*, "current target") == flamegor)
    {
        return false;
    }

    return Attack(flamegor, true);
}

bool BwlFlamegorPositionAction::Execute(Event /*event*/)
{
    Unit* flamegor = AI_VALUE2(Unit*, "find target", "flamegor");
    if (!flamegor || !flamegor->IsAlive())
    {
        return false;
    }

    float targetX = flamegor->GetPositionX();
    float targetY = flamegor->GetPositionY();
    float targetZ = bot->GetPositionZ();
    float const facing = flamegor->GetOrientation();
    uint32 const slot = botAI->GetGroupSlotIndex(bot);

    float angleOffset = 0.0f;
    float distance = 0.0f;

    bool const isMainTank = botAI->IsMainTank(bot);
    bool const isAssistTank = botAI->IsAssistTankOfIndex(bot, 0);
    bool const isTankRole = isMainTank || isAssistTank;

    if (ShouldDelayDrakeReposition(bot, flamegor, isTankRole))
    {
        return false;
    }

    if (isMainTank)
    {
        angleOffset = 0.0f;
        distance = 6.0f;
    }
    else if (isAssistTank)
    {
        angleOffset = static_cast<float>(M_PI / 2.0f);
        distance = 8.0f;
    }
    else if (botAI->IsRanged(bot) || botAI->IsHeal(bot))
    {
        float spread = ((slot % 6) - 2.5f) * 0.12f;
        angleOffset = static_cast<float>(-M_PI / 2.0f) + spread;
        distance = botAI->IsHeal(bot) ? 20.0f : 24.0f;
    }
    else
    {
        float spread = ((slot % 4) - 1.5f) * 0.18f;
        angleOffset = static_cast<float>(-M_PI / 2.0f) + spread;
        distance = 7.0f;
    }

    float angle = facing + angleOffset;
    targetX += std::cos(angle) * distance;
    targetY += std::sin(angle) * distance;

    if (MoveTo(bot->GetMapId(), targetX, targetY, targetZ, false, false, false, false, MovementPriority::MOVEMENT_COMBAT))
    {
        MarkDrakeReposition(bot, isTankRole);
        return true;
    }

    if (MoveInside(bot->GetMapId(), targetX, targetY, targetZ, 2.0f, MovementPriority::MOVEMENT_COMBAT))
    {
        MarkDrakeReposition(bot, isTankRole);
        return true;
    }

    return false;
}

bool BwlFlamegorTranqAction::isUseful()
{
    if (bot->getClass() != CLASS_HUNTER)
    {
        return false;
    }

    Unit* flamegor = AI_VALUE2(Unit*, "find target", "flamegor");
    if (!flamegor || !flamegor->IsAlive())
    {
        return false;
    }

    if (flamegor->GetAura(BwlSpellIds::FlamegorFrenzy))
    {
        return true;
    }

    return false;
}

bool BwlFlamegorTranqAction::Execute(Event /*event*/)
{
    Unit* flamegor = AI_VALUE2(Unit*, "find target", "flamegor");
    if (!flamegor || !flamegor->IsAlive())
    {
        return false;
    }

    return botAI->CastSpell("tranquilizing shot", flamegor);
}
