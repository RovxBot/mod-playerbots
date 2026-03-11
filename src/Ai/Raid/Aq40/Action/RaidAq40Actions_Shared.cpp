#include "RaidAq40Actions.h"

#include <algorithm>
#include <cmath>

#include "ObjectGuid.h"
#include "../RaidAq40BossHelper.h"
#include "../RaidAq40SpellIds.h"

namespace Aq40BossActions
{
Unit* FindUnitByAnyName(PlayerbotAI* botAI, GuidVector const& attackers, std::initializer_list<char const*> names)
{
    return Aq40BossHelper::FindUnitByAnyName(botAI, attackers, names);
}

std::vector<Unit*> FindUnitsByAnyName(PlayerbotAI* botAI, GuidVector const& attackers,
                                      std::initializer_list<char const*> names)
{
    return Aq40BossHelper::FindUnitsByAnyName(botAI, attackers, names);
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
        Unit* chosen = Aq40BossHelper::FindLowestHealthUnitByAnyName(botAI, attackers, names);
        if (chosen)
            return chosen;
    }

    return nullptr;
}
}  // namespace Aq40BossActions

namespace
{
Unit* FindClosestAq40PlagueSeparationRisk(Player* bot, PlayerbotAI* botAI, float& distanceToCreate)
{
    distanceToCreate = 0.0f;
    if (!bot || !botAI)
        return nullptr;

    Group* group = bot->GetGroup();
    if (!group)
        return nullptr;

    Unit* riskiestMember = nullptr;
    float largestDeficit = 0.0f;

    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || member == bot || !member->IsAlive() || member->GetMapId() != bot->GetMapId())
            continue;

        float const currentDistance = bot->GetDistance2d(member);
        float const requiredDistance =
            Aq40SpellIds::HasAnyAura(botAI, member, { Aq40SpellIds::Aq40DefenderPlague }) ? 28.0f : 20.0f;
        float const deficit = requiredDistance - currentDistance;
        if (deficit <= 0.0f || deficit <= largestDeficit)
            continue;

        largestDeficit = deficit;
        riskiestMember = member;
    }

    distanceToCreate = largestDeficit;
    return riskiestMember;
}
}  // namespace

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
            target = Aq40BossHelper::FindLowestHealthUnitByAnyName(botAI, attackers, { "spawn of fankriss" });
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
            target = Aq40BossHelper::FindLowestHealthUnitByAnyName(botAI, attackers, { "sartura's royal guard" });
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
    if (Aq40BossHelper::IsEncounterTank(bot, bot))
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

    float currentDistance = bot->GetDistance2d(threat);
    float desiredDistance = 18.0f;
    if (currentDistance >= desiredDistance)
        return false;

    bot->AttackStop();
    bot->InterruptNonMeleeSpells(true);
    return MoveAway(threat, desiredDistance - currentDistance);
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
    if (Aq40BossHelper::IsEncounterTank(bot, bot))
        return false;

    if (Aq40SpellIds::HasAnyAura(botAI, bot, { Aq40SpellIds::Aq40DefenderPlague }))
    {
        botAI->Reset();
        bot->AttackStop();
        bot->InterruptNonMeleeSpells(true);
        context->GetValue<Unit*>("current target")->Set(nullptr);
        bot->SetTarget(ObjectGuid::Empty);
        bot->SetSelection(ObjectGuid());

        float separationNeeded = 0.0f;
        if (Unit* separationRisk = FindClosestAq40PlagueSeparationRisk(bot, botAI, separationNeeded))
            return MoveAway(separationRisk, separationNeeded);

        return false;
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
                { Aq40SpellIds::Aq40WarderFireNova, Aq40SpellIds::Aq40DefenderThunderclap }))
        {
            danger = unit;
            dangerRange = 16.0f;
            break;
        }
    }

    if (!danger)
        return false;

    float currentDistance = bot->GetDistance2d(danger);
    float desiredDistance = dangerRange + 8.0f;
    if (currentDistance >= desiredDistance)
        return false;

    return MoveAway(danger, desiredDistance - currentDistance);
}
