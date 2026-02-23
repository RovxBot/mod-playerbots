#include "RaidAq40Actions.h"

#include <algorithm>
#include <cmath>

#include "ObjectGuid.h"
#include "RaidAq40BossHelper.h"
#include "RaidAq40SpellIds.h"

namespace Aq40BossActions
{
Unit* FindUnitByAnyName(PlayerbotAI* botAI, GuidVector const& attackers, std::initializer_list<char const*> names)
{
    for (ObjectGuid const guid : attackers)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit)
            continue;

        for (char const* name : names)
        {
            if (botAI->EqualLowercaseName(unit->GetName(), name))
                return unit;
        }
    }

    return nullptr;
}

std::vector<Unit*> FindUnitsByAnyName(PlayerbotAI* botAI, GuidVector const& attackers,
                                      std::initializer_list<char const*> names)
{
    std::vector<Unit*> found;
    for (ObjectGuid const guid : attackers)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit)
            continue;

        for (char const* name : names)
        {
            if (botAI->EqualLowercaseName(unit->GetName(), name))
            {
                found.push_back(unit);
                break;
            }
        }
    }

    return found;
}

Unit* FindTrashTarget(PlayerbotAI* botAI, GuidVector const& attackers)
{
    std::initializer_list<std::initializer_list<char const*>> priority = {
        { "obsidian nullifier" },
        { "obsidian eradicator" },
        { "qiraji mindslayer" },
        { "qiraji champion" },
        { "qiraji slayer" },
        { "anubisath warder" },
        { "anubisath defender" },
        { "anubisath sentinel" },
        { "qiraji lasher" },
        { "vekniss stinger" },
        { "qiraji brainwasher", "qiraji battleguard" },
        { "vekniss guardian", "vekniss warrior", "vekniss drone", "vekniss soldier", "vekniss wasp" },
        { "qiraji scarab", "scarab", "scorpion", "spitting scarab" },
    };

    for (std::initializer_list<char const*> names : priority)
    {
        std::vector<Unit*> matches = FindUnitsByAnyName(botAI, attackers, names);
        Unit* chosen = nullptr;
        for (Unit* unit : matches)
        {
            if (!unit)
                continue;

            if (!chosen || unit->GetHealthPct() < chosen->GetHealthPct())
                chosen = unit;
        }

        if (chosen)
            return chosen;
    }

    return nullptr;
}
}  // namespace Aq40BossActions

bool Aq40ChooseTargetAction::Execute(Event /*event*/)
{
    if (!Aq40BossHelper::IsInAq40(bot))
        return false;

    GuidVector attackers = context->GetValue<GuidVector>("attackers")->Get();
    if (attackers.empty())
        return false;

    Unit* target = nullptr;

    // Favor fight-ending or high-impact targets first.
    target = Aq40BossActions::FindCthunTarget(botAI, attackers);
    if (!target)
        target = Aq40BossActions::FindTwinEmperorsTarget(botAI, attackers);
    if (!target)
        target = Aq40BossActions::FindHuhuranTarget(botAI, attackers);
    if (!target)
    {
        std::vector<Unit*> fankrissSpawns = Aq40BossActions::FindFankrissSpawns(botAI, attackers);
        if (!fankrissSpawns.empty())
        {
            target = fankrissSpawns.front();
            for (Unit* spawn : fankrissSpawns)
            {
                if (spawn && target && spawn->GetHealthPct() < target->GetHealthPct())
                    target = spawn;
            }
        }
        else
        {
            target = Aq40BossActions::FindFankrissTarget(botAI, attackers);
        }
    }
    if (!target)
    {
        std::vector<Unit*> sarturaGuards = Aq40BossActions::FindSarturaGuards(botAI, attackers);
        if (!sarturaGuards.empty())
        {
            target = sarturaGuards.front();
            for (Unit* guard : sarturaGuards)
            {
                if (guard && target && guard->GetHealthPct() < target->GetHealthPct())
                    target = guard;
            }
        }
        else
        {
            target = Aq40BossActions::FindSarturaTarget(botAI, attackers);
        }
    }
    if (!target)
        target = Aq40BossActions::FindBugTrioTarget(botAI, attackers);
    if (!target)
        target = Aq40BossActions::FindSkeramTarget(botAI, attackers);
    if (!target)
        target = Aq40BossActions::FindOuroTarget(botAI, attackers);
    if (!target)
        target = Aq40BossActions::FindViscidusTarget(botAI, attackers);

    if (!target)
    {
        for (ObjectGuid const guid : attackers)
        {
            target = botAI->GetUnit(guid);
            if (target)
                break;
        }
    }

    if (!target || AI_VALUE(Unit*, "current target") == target)
        return false;

    return Attack(target);
}

bool Aq40SkeramAcquirePlatformTargetAction::Execute(Event /*event*/)
{
    GuidVector attackers = context->GetValue<GuidVector>("attackers")->Get();
    Unit* target = Aq40BossActions::FindSkeramTarget(botAI, attackers);
    if (!target)
        return false;

    if (AI_VALUE(Unit*, "current target") == target)
        return false;

    return Attack(target);
}

bool Aq40SkeramInterruptAction::Execute(Event /*event*/)
{
    GuidVector attackers = context->GetValue<GuidVector>("attackers")->Get();
    std::vector<Unit*> skerams =
        Aq40BossActions::FindUnitsByAnyName(botAI, attackers, { "the prophet skeram" });

    if (skerams.empty())
        return false;

    Unit* target = nullptr;
    for (Unit* skeram : skerams)
    {
        if (!skeram)
            continue;

        // Prefer whichever visible Skeram is currently casting.
        if (skeram->GetCurrentSpell(CURRENT_GENERIC_SPELL))
        {
            target = skeram;
            break;
        }
    }

    if (!target)
        target = skerams.front();

    if (!target || AI_VALUE(Unit*, "current target") == target)
        return false;

    return Attack(target);
}

bool Aq40SkeramFocusRealBossAction::Execute(Event /*event*/)
{
    GuidVector attackers = context->GetValue<GuidVector>("attackers")->Get();
    std::vector<Unit*> skerams =
        Aq40BossActions::FindUnitsByAnyName(botAI, attackers, { "the prophet skeram" });

    if (skerams.empty())
        return false;

    Unit* target = skerams.front();
    for (Unit* skeram : skerams)
    {
        if (skeram && target && skeram->GetHealthPct() < target->GetHealthPct())
            target = skeram;
    }

    if (!target || AI_VALUE(Unit*, "current target") == target)
        return false;

    return Attack(target);
}

bool Aq40SkeramControlMindControlAction::Execute(Event /*event*/)
{
    // Keep this baseline cheap and safe:
    // when MC pressure appears, force target normalization on Skeram.
    GuidVector attackers = context->GetValue<GuidVector>("attackers")->Get();
    Unit* target = Aq40BossActions::FindSkeramTarget(botAI, attackers);
    if (!target || AI_VALUE(Unit*, "current target") == target)
        return false;

    return Attack(target);
}

bool Aq40SarturaChooseTargetAction::Execute(Event /*event*/)
{
    GuidVector attackers = context->GetValue<GuidVector>("attackers")->Get();
    if (attackers.empty())
        return false;

    Unit* target = nullptr;
    std::vector<Unit*> guards = Aq40BossActions::FindSarturaGuards(botAI, attackers);
    if (!guards.empty())
    {
        // Strategy baseline: kill the royal guards before Sartura.
        target = guards.front();
        for (Unit* guard : guards)
        {
            if (guard && target && guard->GetHealthPct() < target->GetHealthPct())
                target = guard;
        }
    }
    else
    {
        target = Aq40BossActions::FindSarturaTarget(botAI, attackers);
    }

    if (!target || AI_VALUE(Unit*, "current target") == target)
        return false;

    return Attack(target);
}

bool Aq40SarturaAvoidWhirlwindAction::Execute(Event /*event*/)
{
    if (botAI->IsTank(bot))
        return false;

    GuidVector attackers = context->GetValue<GuidVector>("attackers")->Get();
    Unit* threat = Aq40BossActions::FindSarturaTarget(botAI, attackers);
    if (!threat)
    {
        std::vector<Unit*> guards = Aq40BossActions::FindSarturaGuards(botAI, attackers);
        if (!guards.empty())
            threat = guards.front();
    }
    if (!threat)
        return false;

    float d = bot->GetDistance2d(threat);
    if (d > 14.0f)
        return false;

    float dx = bot->GetPositionX() - threat->GetPositionX();
    float dy = bot->GetPositionY() - threat->GetPositionY();
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.1f)
    {
        dx = std::cos(bot->GetOrientation());
        dy = std::sin(bot->GetOrientation());
        len = 1.0f;
    }

    float escape = 18.0f;
    float moveX = threat->GetPositionX() + (dx / len) * escape;
    float moveY = threat->GetPositionY() + (dy / len) * escape;

    return MoveTo(bot->GetMapId(), moveX, moveY, bot->GetPositionZ(), false, false, false, false,
                  MovementPriority::MOVEMENT_COMBAT);
}

bool Aq40FankrissChooseTargetAction::Execute(Event /*event*/)
{
    GuidVector attackers = context->GetValue<GuidVector>("attackers")->Get();
    if (attackers.empty())
        return false;

    Unit* target = nullptr;
    std::vector<Unit*> spawns = Aq40BossActions::FindFankrissSpawns(botAI, attackers);
    if (!spawns.empty())
    {
        // Fankriss baseline: quickly remove Spawn adds before returning to boss.
        target = spawns.front();
        for (Unit* spawn : spawns)
        {
            if (spawn && target && spawn->GetHealthPct() < target->GetHealthPct())
                target = spawn;
        }
    }
    else
    {
        target = Aq40BossActions::FindFankrissTarget(botAI, attackers);
    }

    if (!target || AI_VALUE(Unit*, "current target") == target)
        return false;

    return Attack(target);
}

bool Aq40TrashChooseTargetAction::Execute(Event /*event*/)
{
    GuidVector attackers = context->GetValue<GuidVector>("attackers")->Get();
    if (attackers.empty())
        return false;

    for (ObjectGuid const guid : attackers)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit || !botAI->EqualLowercaseName(unit->GetName(), "obsidian nullifier"))
            continue;

        Spell* spell = unit->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        if (!spell)
            continue;

        if (Aq40SpellIds::MatchesAnySpellId(spell->GetSpellInfo(), { Aq40SpellIds::Aq40NullifierNullify }))
        {
            if (AI_VALUE(Unit*, "current target") == unit)
                return false;

            return Attack(unit);
        }
    }

    Unit* target = Aq40BossActions::FindTrashTarget(botAI, attackers);
    if (!target || AI_VALUE(Unit*, "current target") == target)
        return false;

    return Attack(target);
}

bool Aq40TrashAvoidDangerousAoeAction::Execute(Event /*event*/)
{
    if (botAI->IsTank(bot))
        return false;

    if (Aq40SpellIds::HasAnyAura(botAI, bot, { Aq40SpellIds::Aq40DefenderPlague }))
    {
        Unit* threat = AI_VALUE(Unit*, "current target");
        if (!threat)
            threat = AI_VALUE(Unit*, "enemy player target");

        if (threat)
        {
            float dx = bot->GetPositionX() - threat->GetPositionX();
            float dy = bot->GetPositionY() - threat->GetPositionY();
            float len = std::sqrt(dx * dx + dy * dy);
            if (len < 0.1f)
            {
                dx = std::cos(bot->GetOrientation());
                dy = std::sin(bot->GetOrientation());
                len = 1.0f;
            }

            float desired = 22.0f;
            float moveX = threat->GetPositionX() + (dx / len) * desired;
            float moveY = threat->GetPositionY() + (dy / len) * desired;
            return MoveTo(bot->GetMapId(), moveX, moveY, bot->GetPositionZ(), false, false, false, false,
                          MovementPriority::MOVEMENT_COMBAT);
        }
    }

    GuidVector attackers = context->GetValue<GuidVector>("attackers")->Get();
    Unit* danger = nullptr;
    float dangerRange = 0.0f;

    for (ObjectGuid const guid : attackers)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit)
            continue;

        Spell* spell = unit->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        if (!spell)
            continue;

        if (Aq40SpellIds::MatchesAnySpellId(spell->GetSpellInfo(), { Aq40SpellIds::Aq40EradicatorShockBlast }))
        {
            danger = unit;
            dangerRange = 12.0f;
            break;
        }

        if (Aq40SpellIds::MatchesAnySpellId(spell->GetSpellInfo(),
                { Aq40SpellIds::Aq40WarderFireNova, Aq40SpellIds::Aq40DefenderThunderclap,
                    Aq40SpellIds::Aq40DefenderShadowStorm }))
        {
            danger = unit;
            dangerRange = 16.0f;
            break;
        }
    }

    if (!danger)
        return false;

    float d = bot->GetDistance2d(danger);
    if (d > dangerRange + 2.0f)
        return false;

    float dx = bot->GetPositionX() - danger->GetPositionX();
    float dy = bot->GetPositionY() - danger->GetPositionY();
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.1f)
    {
        dx = std::cos(bot->GetOrientation());
        dy = std::sin(bot->GetOrientation());
        len = 1.0f;
    }

    float desired = dangerRange + 8.0f;
    float moveX = danger->GetPositionX() + (dx / len) * desired;
    float moveY = danger->GetPositionY() + (dy / len) * desired;
    return MoveTo(bot->GetMapId(), moveX, moveY, bot->GetPositionZ(), false, false, false, false,
                  MovementPriority::MOVEMENT_COMBAT);
}
