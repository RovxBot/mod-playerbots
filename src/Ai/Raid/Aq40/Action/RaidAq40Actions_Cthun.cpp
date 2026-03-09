#include "RaidAq40Actions.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_map>

#include "GameObject.h"
#include "ObjectGuid.h"
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

uint32 GetRangedSpreadOrdinal(Player* bot, PlayerbotAI* botAI)
{
    Group* group = bot->GetGroup();
    if (!group)
        return static_cast<uint32>(bot->GetGUID().GetCounter() % 12);

    uint32 index = 0;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || !member->IsAlive() || member->GetMapId() != bot->GetMapId())
            continue;

        PlayerbotAI* memberAI = GET_PLAYERBOT_AI(member);
        bool isRangedSlot = memberAI && !memberAI->IsTank(member) && (memberAI->IsRanged(member) || memberAI->IsHeal(member));
        if (!isRangedSlot)
            continue;

        if (member->GetGUID() == bot->GetGUID())
            return index;

        ++index;
    }

    return static_cast<uint32>(bot->GetGUID().GetCounter() % 12);
}

}  // namespace

bool Aq40CthunChooseTargetAction::Execute(Event /*event*/)
{
    GuidVector attackers = context->GetValue<GuidVector>("attackers")->Get();
    if (attackers.empty())
        return false;

    Unit* target = nullptr;

    if (Aq40Helpers::IsCthunInStomach(bot, botAI))
        target = Aq40BossActions::FindUnitByAnyName(botAI, attackers, { "flesh tentacle" });

    if (!target)
        target = botAI->IsTank(bot) ? FindTankPriorityCthunAdd(botAI, attackers) : FindHighestPriorityCthunAdd(botAI, attackers);

    if (!target)
    {
        Unit* body = FindCthunBody(botAI, attackers);
        Unit* eye = FindCthunEye(botAI, attackers);
        if (body && Aq40Helpers::IsCthunVulnerableNow(botAI, attackers))
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
    if (botAI->IsTank(bot) || !(botAI->IsRanged(bot) || botAI->IsHeal(bot)))
        return false;

    GuidVector attackers = context->GetValue<GuidVector>("attackers")->Get();
    Unit* boss = FindCthunEye(botAI, attackers);
    if (!boss)
        boss = FindCthunBody(botAI, attackers);
    if (!boss)
        return false;

    uint32 slot = GetRangedSpreadOrdinal(bot, botAI);
    float baseAngle = static_cast<float>(slot % 12) * ((2.0f * kPi) / 12.0f);
    float radius = 28.0f;
    float moveX = boss->GetPositionX() + std::cos(baseAngle) * radius;
    float moveY = boss->GetPositionY() + std::sin(baseAngle) * radius;

    if (bot->GetDistance2d(moveX, moveY) < 5.0f)
        return false;

    return MoveTo(bot->GetMapId(), moveX, moveY, bot->GetPositionZ(), false, false, false, false,
                  MovementPriority::MOVEMENT_COMBAT);
}

bool Aq40CthunAvoidDarkGlareAction::Execute(Event /*event*/)
{
    if (botAI->IsTank(bot))
        return false;

    GuidVector attackers = context->GetValue<GuidVector>("attackers")->Get();
    Unit* boss = FindCthunEye(botAI, attackers);
    if (!boss)
        boss = FindCthunBody(botAI, attackers);
    if (!boss)
        return false;

    float dx = bot->GetPositionX() - boss->GetPositionX();
    float dy = bot->GetPositionY() - boss->GetPositionY();
    float radius = std::sqrt(dx * dx + dy * dy);
    if (radius < 10.0f)
        return false;

    float currentAngle = std::atan2(dy, dx);
    float nextAngle = currentAngle + (kPi / 5.0f);
    float moveX = boss->GetPositionX() + std::cos(nextAngle) * radius;
    float moveY = boss->GetPositionY() + std::sin(nextAngle) * radius;

    return MoveTo(bot->GetMapId(), moveX, moveY, bot->GetPositionZ(), false, false, false, false,
                  MovementPriority::MOVEMENT_COMBAT);
}

bool Aq40CthunStomachDpsAction::Execute(Event /*event*/)
{
    GuidVector attackers = context->GetValue<GuidVector>("attackers")->Get();
    Unit* fleshTentacle = Aq40BossActions::FindUnitByAnyName(botAI, attackers, { "flesh tentacle" });
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
    if (botAI->IsTank(bot))
        exitStacks = 1;
    else if (botAI->IsHeal(bot))
        exitStacks = 5;

    if (acid->GetStackAmount() < exitStacks)
        return false;

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
    GuidVector attackers = context->GetValue<GuidVector>("attackers")->Get();
    Unit* target = botAI->IsTank(bot) ? FindTankPriorityCthunAdd(botAI, attackers) : FindHighestPriorityCthunAdd(botAI, attackers);
    if (!target || AI_VALUE(Unit*, "current target") == target)
        return false;

    return Attack(target);
}

bool Aq40CthunVulnerableBurstAction::Execute(Event /*event*/)
{
    GuidVector attackers = context->GetValue<GuidVector>("attackers")->Get();
    if (!Aq40Helpers::IsCthunVulnerableNow(botAI, attackers))
        return false;

    // Keep eye-tentacle cleanup priority even inside weakened windows.
    if (Aq40BossActions::FindUnitByAnyName(botAI, attackers, { "flesh tentacle", "eye tentacle", "giant eye tentacle" }))
        return false;

    Unit* body = FindCthunBody(botAI, attackers);
    if (!body || AI_VALUE(Unit*, "current target") == body)
        return false;

    return Attack(body);
}

bool Aq40CthunInterruptEyeAction::Execute(Event /*event*/)
{
    GuidVector attackers = context->GetValue<GuidVector>("attackers")->Get();
    std::vector<Unit*> eyes = Aq40BossActions::FindUnitsByAnyName(botAI, attackers, { "eye tentacle", "giant eye tentacle" });
    for (Unit* eye : eyes)
    {
        if (!eye || !eye->GetCurrentSpell(CURRENT_GENERIC_SPELL))
            continue;

        if (AI_VALUE(Unit*, "current target") != eye && !Attack(eye))
            continue;

        return botAI->DoSpecificAction("interrupt spell", Event(), true);
    }

    return false;
}
