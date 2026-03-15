#include "RaidAq40Actions.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_map>

#include "GameObject.h"
#include "ObjectGuid.h"
#include "../RaidAq40BossHelper.h"
#include "../RaidAq40SpellIds.h"
#include "../Util/RaidAq40Helpers.h"
#include "Timer.h"

namespace Aq40BossActions
{
Unit* FindCthunTarget(PlayerbotAI* botAI, GuidVector const& attackers)
{
    return FindUnitByAnyName(botAI, attackers, { "c'thun", "eye of c'thun", "claw tentacle", "giant eye tentacle" });
}
}  // namespace Aq40BossActions

namespace
{
float constexpr kPi = 3.14159265f;
uint32 constexpr kCthunGiantWavePeriodMs = 20000;
std::unordered_map<uint64, Position> sCthunSpreadPositions;

enum class CthunExpectedGiantType : uint8
{
    Claw = 0,
    Eye = 1
};

Unit* FindCthunBody(PlayerbotAI* botAI, GuidVector const& attackers)
{
    return Aq40BossActions::FindUnitByAnyName(botAI, attackers, { "c'thun" });
}

Unit* FindCthunEye(PlayerbotAI* botAI, GuidVector const& attackers)
{
    return Aq40BossActions::FindUnitByAnyName(botAI, attackers, { "eye of c'thun" });
}

Unit* FindHighestPriorityCthunAdd(PlayerbotAI* botAI, GuidVector const& attackers)
{
    Unit* add = Aq40BossActions::FindUnitByAnyName(botAI, attackers, { "flesh tentacle" });
    if (add)
        return add;

    add = Aq40BossActions::FindUnitByAnyName(botAI, attackers, { "eye tentacle" });
    if (add)
        return add;

    add = Aq40BossActions::FindUnitByAnyName(botAI, attackers, { "giant eye tentacle" });
    if (add)
        return add;

    add = Aq40BossActions::FindUnitByAnyName(botAI, attackers, { "giant claw tentacle" });
    if (add)
        return add;

    return Aq40BossActions::FindUnitByAnyName(botAI, attackers, { "claw tentacle" });
}

CthunExpectedGiantType GetExpectedGiantType(PlayerbotAI* botAI, GuidVector const& attackers)
{
    uint32 elapsed = Aq40Helpers::GetCthunPhase2ElapsedMs(botAI, attackers);
    uint32 window = (elapsed / kCthunGiantWavePeriodMs) % 2;
    return window == 0 ? CthunExpectedGiantType::Claw : CthunExpectedGiantType::Eye;
}

Unit* FindTankPriorityCthunAdd(PlayerbotAI* botAI, GuidVector const& attackers)
{
    CthunExpectedGiantType expected = GetExpectedGiantType(botAI, attackers);
    if (expected == CthunExpectedGiantType::Eye)
    {
        Unit* giantEye = Aq40BossActions::FindUnitByAnyName(botAI, attackers, { "giant eye tentacle" });
        if (giantEye)
            return giantEye;
    }

    Unit* add = Aq40BossActions::FindUnitByAnyName(botAI, attackers, { "giant claw tentacle" });
    if (add)
        return add;

    if (expected == CthunExpectedGiantType::Claw)
    {
        Unit* giantEye = Aq40BossActions::FindUnitByAnyName(botAI, attackers, { "giant eye tentacle" });
        if (giantEye)
            return giantEye;
    }

    return FindHighestPriorityCthunAdd(botAI, attackers);
}

uint32 GetSpreadOrdinal(Player* bot, PlayerbotAI* botAI, bool forMelee)
{
    Group* group = bot->GetGroup();
    uint32 const fallbackSlots = forMelee ? 8u : 12u;
    if (!group)
        return static_cast<uint32>(bot->GetGUID().GetCounter() % fallbackSlots);

    uint32 index = 0;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || !member->IsAlive() || member->GetMapId() != bot->GetMapId())
            continue;

        PlayerbotAI* memberAI = GET_PLAYERBOT_AI(member);
        if (!memberAI || memberAI->IsTank(member))
            continue;

        bool const memberIsMelee = !memberAI->IsRanged(member) && !memberAI->IsHeal(member);
        if (forMelee != memberIsMelee)
            continue;

        if (member->GetGUID() == bot->GetGUID())
            return index;

        ++index;
    }

    return static_cast<uint32>(bot->GetGUID().GetCounter() % fallbackSlots);
}

Position GetAssignedCthunSpreadPosition(Player* bot, PlayerbotAI* botAI, Unit* boss, bool forMelee)
{
    // Clear stale positions on wipe/re-pull (pattern from
    // sTwinWarlockAssignmentsByInstance in RaidAq40BossHelper.h).
    if (!bot->IsInCombat())
        sCthunSpreadPositions.clear();

    uint64 const botGuid = bot->GetGUID().GetRawValue();
    auto itr = sCthunSpreadPositions.find(botGuid);
    if (itr != sCthunSpreadPositions.end())
        return itr->second;

    if (forMelee)
    {
        // Melee: spread evenly around the boss at melee range (~8y), using
        // GUID-based angle slots (pattern from ICC Festergut spore spread).
        uint32 slot = GetSpreadOrdinal(bot, botAI, true);
        uint32 const kMeleeSlots = 8;
        float angle = static_cast<float>(slot % kMeleeSlots) * ((2.0f * kPi) / static_cast<float>(kMeleeSlots));
        float radius = 8.0f;
        Position assigned(boss->GetPositionX() + std::cos(angle) * radius,
                          boss->GetPositionY() + std::sin(angle) * radius,
                          boss->GetPositionZ());
        sCthunSpreadPositions[botGuid] = assigned;
        return assigned;
    }

    // Ranged/heals: spread at 28y around the boss in 12 slots.
    uint32 slot = GetSpreadOrdinal(bot, botAI, false);
    float angle = static_cast<float>(slot % 12) * ((2.0f * kPi) / 12.0f);
    float radius = 28.0f;
    Position assigned(boss->GetPositionX() + std::cos(angle) * radius,
                      boss->GetPositionY() + std::sin(angle) * radius,
                      boss->GetPositionZ());
    sCthunSpreadPositions[botGuid] = assigned;
    return assigned;
}

}  // namespace

bool Aq40CthunChooseTargetAction::Execute(Event /*event*/)
{
    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, context->GetValue<GuidVector>("attackers")->Get());
    if (encounterUnits.empty())
        return false;

    bool const isPrimaryTank = Aq40BossHelper::IsEncounterPrimaryTank(bot, bot);
    Unit* target = nullptr;

    if (Aq40Helpers::IsCthunInStomach(bot, botAI))
        target = Aq40BossActions::FindUnitByAnyName(botAI, encounterUnits, { "flesh tentacle" });

    if (!target)
        target = isPrimaryTank ? FindTankPriorityCthunAdd(botAI, encounterUnits) : FindHighestPriorityCthunAdd(botAI, encounterUnits);

    if (!target)
    {
        Unit* body = FindCthunBody(botAI, encounterUnits);
        Unit* eye = FindCthunEye(botAI, encounterUnits);
        if (body && Aq40Helpers::IsCthunVulnerableNow(botAI, encounterUnits))
            target = body;
        else
            target = eye ? eye : body;
    }

    if (!target || AI_VALUE(Unit*, "current target") == target)
        return false;

    return Attack(target);
}

bool Aq40CthunMaintainSpreadAction::Execute(Event /*event*/)
{
    // Tanks hold position; everyone else must spread to prevent Eye Beam
    // chain bouncing.
    if (Aq40BossHelper::IsEncounterTank(bot, bot))
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, context->GetValue<GuidVector>("attackers")->Get());
    Unit* boss = FindCthunEye(botAI, encounterUnits);
    if (!boss)
        boss = FindCthunBody(botAI, encounterUnits);
    if (!boss)
        return false;

    bool const isMelee = !botAI->IsRanged(bot) && !botAI->IsHeal(bot);
    Position assigned = GetAssignedCthunSpreadPosition(bot, botAI, boss, isMelee);
    float moveX = assigned.GetPositionX();
    float moveY = assigned.GetPositionY();

    float const tolerance = isMelee ? 3.0f : 5.0f;
    if (bot->GetDistance2d(moveX, moveY) < tolerance)
        return false;

    return MoveTo(bot->GetMapId(), moveX, moveY, boss->GetPositionZ(), false, false, false, true,
                  MovementPriority::MOVEMENT_COMBAT, true, false);
}

bool Aq40CthunAvoidDarkGlareAction::Execute(Event /*event*/)
{
    if (Aq40BossHelper::IsEncounterTank(bot, bot))
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, context->GetValue<GuidVector>("attackers")->Get());
    Unit* boss = FindCthunEye(botAI, encounterUnits);
    if (!boss)
        boss = FindCthunBody(botAI, encounterUnits);
    if (!boss)
        return false;

    bot->AttackStop();
    bot->InterruptNonMeleeSpells(true);

    float dx = bot->GetPositionX() - boss->GetPositionX();
    float dy = bot->GetPositionY() - boss->GetPositionY();
    float radius = std::sqrt(dx * dx + dy * dy);
    if (radius < 10.0f)
        return false;

    float currentAngle = std::atan2(dy, dx);

    // Determine beam direction from boss orientation (Dark Glare fires
    // in the direction the Eye is facing and rotates).
    float beamAngle = boss->GetOrientation();
    // Vector from boss along beam direction
    float beamDx = std::cos(beamAngle);
    float beamDy = std::sin(beamAngle);
    // Normalize bot vector from boss
    float botDx = dx / radius;
    float botDy = dy / radius;
    // Cross product determines which side of the beam the bot is on
    // (pattern used by ICC Putricideoid directional avoidance).
    float cross = beamDx * botDy - beamDy * botDx;
    // Move in the direction that takes us away from the beam sweep.
    // If cross > 0, bot is counter-clockwise from beam → rotate further CCW (+angle).
    // If cross < 0, bot is clockwise from beam → rotate further CW (-angle).
    // If cross ~0, bot is right in the beam path → pick either direction.
    float step = kPi / 5.0f;  // 36 degrees
    float nextAngle = currentAngle + (cross >= 0.0f ? step : -step);
    float moveX = boss->GetPositionX() + std::cos(nextAngle) * radius;
    float moveY = boss->GetPositionY() + std::sin(nextAngle) * radius;

    return MoveTo(bot->GetMapId(), moveX, moveY, bot->GetPositionZ(), false, false, false, false,
                  MovementPriority::MOVEMENT_COMBAT);
}

bool Aq40CthunStomachDpsAction::Execute(Event /*event*/)
{
    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, context->GetValue<GuidVector>("attackers")->Get());
    Unit* fleshTentacle = Aq40BossActions::FindUnitByAnyName(botAI, encounterUnits, { "flesh tentacle" });
    if (!fleshTentacle)
        return false;

    if (AI_VALUE(Unit*, "current target") == fleshTentacle)
        return false;

    return Attack(fleshTentacle);
}

bool Aq40CthunStomachExitAction::Execute(Event /*event*/)
{
    Aura* acid = Aq40SpellIds::GetAnyAura(bot, { Aq40SpellIds::CthunDigestiveAcid });
    if (!acid)
        acid = botAI->GetAura("digestive acid", bot, false, true);
    if (!acid)
        return false;

    uint32 exitStacks = 10;
    if (Aq40BossHelper::IsEncounterPrimaryTank(bot, bot))
        exitStacks = 1;
    else if (botAI->IsHeal(bot))
        exitStacks = 5;

    if (acid->GetStackAmount() < exitStacks)
        return false;

    bot->AttackStop();
    bot->InterruptNonMeleeSpells(true);

    GameObject* portal = Aq40Helpers::FindLikelyStomachExitPortal(bot, botAI);
    if (portal)
    {
        return MoveTo(bot->GetMapId(), portal->GetPositionX(), portal->GetPositionY(), portal->GetPositionZ(), false, false,
                      false, false, MovementPriority::MOVEMENT_COMBAT);
    }

    // Fallback: deterministic radial sweep to avoid straight-line dead ends.
    uint32 direction = (getMSTime() / 2000) % 8;
    float angle = (2.0f * kPi) * (static_cast<float>(direction) / 8.0f);
    float moveDistance = 14.0f;
    float moveX = bot->GetPositionX() + std::cos(angle) * moveDistance;
    float moveY = bot->GetPositionY() + std::sin(angle) * moveDistance;

    return MoveTo(bot->GetMapId(), moveX, moveY, bot->GetPositionZ(), false, false, false, false,
                  MovementPriority::MOVEMENT_COMBAT);
}

bool Aq40CthunPhase2AddPriorityAction::Execute(Event /*event*/)
{
    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, context->GetValue<GuidVector>("attackers")->Get());
    Unit* target = Aq40BossHelper::IsEncounterPrimaryTank(bot, bot) ?
        FindTankPriorityCthunAdd(botAI, encounterUnits) : FindHighestPriorityCthunAdd(botAI, encounterUnits);
    if (!target || AI_VALUE(Unit*, "current target") == target)
        return false;

    return Attack(target);
}

bool Aq40CthunVulnerableBurstAction::Execute(Event /*event*/)
{
    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, context->GetValue<GuidVector>("attackers")->Get());
    if (!Aq40Helpers::IsCthunVulnerableNow(botAI, encounterUnits))
        return false;

    // Keep eye-tentacle cleanup priority even inside weakened windows.
    if (Aq40BossActions::FindUnitByAnyName(botAI, encounterUnits, { "flesh tentacle", "eye tentacle", "giant eye tentacle" }))
        return false;

    Unit* body = FindCthunBody(botAI, encounterUnits);
    if (!body || AI_VALUE(Unit*, "current target") == body)
        return false;

    return Attack(body);
}

bool Aq40CthunInterruptEyeAction::Execute(Event /*event*/)
{
    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, context->GetValue<GuidVector>("attackers")->Get());
    std::vector<Unit*> eyes = Aq40BossActions::FindUnitsByAnyName(botAI, encounterUnits, { "eye tentacle", "giant eye tentacle" });
    for (Unit* eye : eyes)
    {
        if (!eye || !eye->GetCurrentSpell(CURRENT_GENERIC_SPELL))
            continue;

        // Switch target this tick; interrupt will fire on the next cycle
        // once the bot is facing/in range. Avoids failing interrupts from
        // same-tick target switch (Naxxramas uses separate ticks too).
        if (AI_VALUE(Unit*, "current target") != eye)
            return Attack(eye);

        return botAI->DoSpecificAction("interrupt spell", Event(), true);
    }

    return false;
}
