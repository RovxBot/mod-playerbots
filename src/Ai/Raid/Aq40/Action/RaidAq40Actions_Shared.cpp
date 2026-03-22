#include "RaidAq40Actions.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "ObjectGuid.h"
#include "../RaidAq40BossHelper.h"
#include "../RaidAq40SpellIds.h"

namespace
{
bool IsSarturaMob(PlayerbotAI* botAI, Unit* unit)
{
    return unit && (botAI->EqualLowercaseName(unit->GetName(), "battleguard sartura") ||
                    botAI->EqualLowercaseName(unit->GetName(), "sartura's royal guard"));
}

bool IsSarturaSpinning(PlayerbotAI* botAI, Unit* unit)
{
    if (!IsSarturaMob(botAI, unit))
        return false;

    Spell* spell = unit->GetCurrentSpell(CURRENT_GENERIC_SPELL);
    return (spell && Aq40SpellIds::MatchesAnySpellId(spell->GetSpellInfo(),
                { Aq40SpellIds::SarturaWhirlwind, Aq40SpellIds::SarturaGuardWhirlwind })) ||
           Aq40SpellIds::HasAnyAura(botAI, unit,
               { Aq40SpellIds::SarturaWhirlwind, Aq40SpellIds::SarturaGuardWhirlwind }) ||
           botAI->HasAura("whirlwind", unit);
}
}  // namespace

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
        { "qiraji mindslayer" },
        { "obsidian nullifier" },
        { "obsidian eradicator" },
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
        if (!member || member == bot || !member->IsAlive() || !Aq40BossHelper::IsSameInstance(bot, member))
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

    GuidVector const attackers = context->GetValue<GuidVector>("attackers")->Get();
    GuidVector activeUnits = Aq40BossHelper::GetActiveCombatUnits(botAI, attackers);
    if (activeUnits.empty())
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, attackers);
    Unit* target = nullptr;

    if (Aq40BossHelper::HasAnyNamedUnit(botAI, activeUnits, { "princess yauj", "vem", "lord kri", "yauj brood" }))
        target = Aq40BossActions::FindBugTrioTarget(botAI, encounterUnits);

    // Trash pulls should stay on trash logic unless a boss is actually engaged.
    if (!target && !Aq40BossHelper::IsBossEncounterActive(botAI, activeUnits))
    {
        target = Aq40BossActions::FindTrashTarget(botAI, activeUnits);
        if (!target)
            target = Aq40BossHelper::FindLowestHealthUnitByAnyName(botAI, activeUnits,
                { "vekniss stinger", "vekniss guardian", "vekniss warrior", "vekniss drone", "vekniss soldier",
                  "vekniss wasp", "obsidian nullifier", "obsidian eradicator", "qiraji mindslayer",
                  "qiraji champion", "qiraji slayer", "anubisath warder", "anubisath defender",
                  "anubisath sentinel", "qiraji lasher", "qiraji brainwasher", "qiraji battleguard",
                  "qiraji scarab", "scarab", "scorpion", "spitting scarab" });

        if (!target)
        {
            for (ObjectGuid const guid : activeUnits)
            {
                target = botAI->GetUnit(guid);
                if (target)
                    break;
            }
        }
    }

    // Favor fight-ending or high-impact targets first.
    if (!target)
        target = Aq40BossActions::FindCthunTarget(botAI, encounterUnits);
    if (!target)
        target = Aq40BossActions::FindTwinEmperorsTarget(botAI, encounterUnits);
    if (!target)
        target = Aq40BossActions::FindHuhuranTarget(botAI, encounterUnits);
    if (!target)
    {
        std::vector<Unit*> fankrissSpawns = Aq40BossActions::FindFankrissSpawns(botAI, encounterUnits);
        if (!fankrissSpawns.empty())
        {
            target = Aq40BossHelper::FindLowestHealthUnitByAnyName(botAI, encounterUnits, { "spawn of fankriss" });
        }
        else
        {
            target = Aq40BossActions::FindFankrissTarget(botAI, encounterUnits);
        }
    }
    if (!target)
    {
        std::vector<Unit*> sarturaGuards = Aq40BossActions::FindSarturaGuards(botAI, encounterUnits);
        if (!sarturaGuards.empty())
        {
            target = Aq40BossHelper::FindLowestHealthUnitByAnyName(botAI, encounterUnits, { "sartura's royal guard" });
        }
        else
        {
            target = Aq40BossActions::FindSarturaTarget(botAI, encounterUnits);
        }
    }
    if (!target)
        target = Aq40BossActions::FindSkeramTarget(botAI, encounterUnits);
    if (!target)
        target = Aq40BossActions::FindOuroTarget(botAI, encounterUnits);
    if (!target)
        target = Aq40BossActions::FindViscidusTarget(botAI, encounterUnits);

    if (!target)
    {
        for (ObjectGuid const guid : activeUnits)
        {
            target = botAI->GetUnit(guid);
            if (target)
                break;
        }
    }

    if (!target)
        return false;

    if (AI_VALUE(Unit*, "current target") == target && bot->GetVictim() == target)
        return false;

    return Attack(target);
}

bool Aq40SkeramAcquirePlatformTargetAction::Execute(Event /*event*/)
{
    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, context->GetValue<GuidVector>("attackers")->Get());
    Unit* target = Aq40BossActions::FindSkeramTarget(botAI, encounterUnits);
    if (!target)
        return false;

    if (AI_VALUE(Unit*, "current target") == target && bot->GetVictim() == target)
        return false;

    return Attack(target);
}

bool Aq40SkeramInterruptAction::Execute(Event /*event*/)
{
    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, context->GetValue<GuidVector>("attackers")->Get());
    std::vector<Unit*> skerams =
        Aq40BossActions::FindUnitsByAnyName(botAI, encounterUnits, { "the prophet skeram" });

    if (skerams.empty())
        return false;

    // If we are already targeting a casting Skeram, fire the interrupt directly.
    Unit* currentTarget = AI_VALUE(Unit*, "current target");
    if (currentTarget)
    {
        for (Unit* skeram : skerams)
        {
            if (skeram == currentTarget && skeram->GetCurrentSpell(CURRENT_GENERIC_SPELL))
                return botAI->DoSpecificAction("interrupt spell", Event(), true);
        }
    }

    // Otherwise switch to a casting Skeram; the interrupt fires next tick.
    Unit* target = nullptr;
    for (Unit* skeram : skerams)
    {
        if (!skeram)
            continue;

        if (skeram->GetCurrentSpell(CURRENT_GENERIC_SPELL))
        {
            target = skeram;
            break;
        }
    }

    if (!target)
        return false;

    if (AI_VALUE(Unit*, "current target") == target && bot->GetVictim() == target)
        return false;

    return Attack(target);
}

bool Aq40SkeramFocusRealBossAction::Execute(Event /*event*/)
{
    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, context->GetValue<GuidVector>("attackers")->Get());
    std::vector<Unit*> skerams =
        Aq40BossActions::FindUnitsByAnyName(botAI, encounterUnits, { "the prophet skeram" });

    if (skerams.empty())
        return false;

    Unit* target = skerams.front();
    for (Unit* skeram : skerams)
    {
        if (skeram && target && skeram->GetHealthPct() < target->GetHealthPct())
            target = skeram;
    }

    if (!target || (AI_VALUE(Unit*, "current target") == target && bot->GetVictim() == target))
        return false;

    return Attack(target);
}

bool Aq40SkeramControlMindControlAction::Execute(Event /*event*/)
{
    // Pattern lifted from BWL BwlPolymorphMindControlledTargetAction and
    // TempestKeep KaelthasSunstriderBreakMindControlAction:
    // Detect charmed raid members and apply CC (sheep/fear) to neutralize them.
    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, context->GetValue<GuidVector>("attackers")->Get());

    // First: try to CC an MC'd player (mages sheep, priests fear, etc.)
    Unit* mcTarget = nullptr;
    float closestDist = std::numeric_limits<float>::max();
    for (ObjectGuid const guid : encounterUnits)
    {
        Unit* unit = botAI->GetUnit(guid);
        Player* player = unit ? unit->ToPlayer() : nullptr;
        if (!player || !player->IsAlive() || player == bot)
            continue;

        // BWL pattern: IsCharmed() + !IsPolymorphed() to avoid double-CC.
        if (!player->IsCharmed() || player->IsPolymorphed())
            continue;

        float const dist = bot->GetDistance2d(player);
        if (dist < closestDist)
        {
            closestDist = dist;
            mcTarget = player;
        }
    }

    if (mcTarget)
    {
        // TempestKeep pattern: try multiple CC spells by class.
        static std::initializer_list<char const*> ccSpells = {
            "polymorph", "fear", "hibernate", "freezing trap", "repentance"
        };
        for (char const* spell : ccSpells)
        {
            if (botAI->CanCastSpell(spell, mcTarget) && botAI->CastSpell(spell, mcTarget))
                return true;
        }
    }

    // Fallback: force target back to Skeram.
    Unit* target = Aq40BossActions::FindSkeramTarget(botAI, encounterUnits);
    if (!target || (AI_VALUE(Unit*, "current target") == target && bot->GetVictim() == target))
        return false;

    return Attack(target);
}

bool Aq40SarturaChooseTargetAction::Execute(Event /*event*/)
{
    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, context->GetValue<GuidVector>("attackers")->Get());
    if (encounterUnits.empty())
        return false;

    Unit* target = nullptr;
    std::vector<Unit*> guards = Aq40BossActions::FindSarturaGuards(botAI, encounterUnits);
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
        target = Aq40BossActions::FindSarturaTarget(botAI, encounterUnits);
    }

    if (!target || (AI_VALUE(Unit*, "current target") == target && bot->GetVictim() == target))
        return false;

    return Attack(target);
}

bool Aq40SarturaAvoidWhirlwindAction::Execute(Event /*event*/)
{
    if (Aq40BossHelper::IsEncounterTank(bot, bot))
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, context->GetValue<GuidVector>("attackers")->Get());
    Unit* threat = nullptr;
    float closestDistance = std::numeric_limits<float>::max();
    for (ObjectGuid const guid : encounterUnits)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!IsSarturaSpinning(botAI, unit))
            continue;

        float const distance = bot->GetDistance2d(unit);
        bool const isCloser = distance < closestDistance;
        bool const isChasingBot = unit->GetVictim() == bot || unit->GetTarget() == bot->GetGUID();
        bool const currentThreatIsChasing = threat && (threat->GetVictim() == bot || threat->GetTarget() == bot->GetGUID());
        if (!threat || (isChasingBot && !currentThreatIsChasing) || (isChasingBot == currentThreatIsChasing && isCloser))
        {
            threat = unit;
            closestDistance = distance;
        }
    }
    if (!threat)
        return false;

    bool const isBackline = botAI->IsRanged(bot) || botAI->IsHeal(bot);
    bool const isChasingBot = threat->GetVictim() == bot || threat->GetTarget() == bot->GetGUID();
    float currentDistance = bot->GetDistance2d(threat);
    float desiredDistance = (isBackline && isChasingBot) ? 24.0f : 18.0f;
    if (currentDistance >= desiredDistance)
        return false;

    bot->AttackStop();
    bot->InterruptNonMeleeSpells(true);
    return MoveAway(threat, desiredDistance - currentDistance);
}

bool Aq40FankrissChooseTargetAction::Execute(Event /*event*/)
{
    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, context->GetValue<GuidVector>("attackers")->Get());
    if (encounterUnits.empty())
        return false;

    Unit* target = nullptr;
    std::vector<Unit*> spawns = Aq40BossActions::FindFankrissSpawns(botAI, encounterUnits);
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
        target = Aq40BossActions::FindFankrissTarget(botAI, encounterUnits);
    }

    if (!target || (AI_VALUE(Unit*, "current target") == target && bot->GetVictim() == target))
        return false;

    return Attack(target);
}

bool Aq40TrashChooseTargetAction::Execute(Event /*event*/)
{
    GuidVector const& attackers = context->GetValue<GuidVector>("attackers")->Get();
    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, attackers);
    if (encounterUnits.empty())
        return false;

    // Collect all Mindslayers/Nullifiers currently casting dangerous spells.
    // Distribute bots across them so each caster gets coverage instead of
    // everyone dog-piling the same one.
    std::vector<Unit*> castingDanger;
    for (ObjectGuid const guid : encounterUnits)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit)
            continue;

        // Check both regular casts and channels (Mind Flay is channeled)
        Spell* spell = unit->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        Spell* channel = unit->GetCurrentSpell(CURRENT_CHANNELED_SPELL);

        bool const isMindBlast = botAI->EqualLowercaseName(unit->GetName(), "qiraji mindslayer") &&
            (spell && Aq40SpellIds::MatchesAnySpellId(spell->GetSpellInfo(), { Aq40SpellIds::Aq40MindslayerMindBlast }));
        bool const isMindFlay = botAI->EqualLowercaseName(unit->GetName(), "qiraji mindslayer") &&
            (channel && Aq40SpellIds::MatchesAnySpellId(channel->GetSpellInfo(), { Aq40SpellIds::Aq40MindslayerMindFlay }));
        bool const isNullify = botAI->EqualLowercaseName(unit->GetName(), "obsidian nullifier") &&
            (spell && Aq40SpellIds::MatchesAnySpellId(spell->GetSpellInfo(), { Aq40SpellIds::Aq40NullifierNullify }));

        if (isMindBlast || isMindFlay || isNullify)
            castingDanger.push_back(unit);
    }

    if (!castingDanger.empty())
    {
        // Distribute: assign each bot to a caster based on GUID modulo count,
        // so different bots cover different Mindslayers.
        uint32 const idx = bot->GetGUID().GetCounter() % castingDanger.size();
        Unit* assigned = castingDanger[idx];

        if (!assigned || (AI_VALUE(Unit*, "current target") == assigned && bot->GetVictim() == assigned))
            return false;

        return Attack(assigned);
    }

    // Use combat-filtered units for general targeting so passive mobs
    // (e.g. idle scarabs/scorpions) are ignored until they actually aggro.
    GuidVector activeUnits = Aq40BossHelper::GetActiveCombatUnits(botAI, attackers);
    Unit* target = Aq40BossActions::FindTrashTarget(botAI, activeUnits);
    if (!target || (AI_VALUE(Unit*, "current target") == target && bot->GetVictim() == target))
        return false;

    return Attack(target);
}

bool Aq40TrashInterruptMindBlastAction::Execute(Event /*event*/)
{
    // If we are already targeting a Mindslayer or Nullifier that is casting,
    // fire our interrupt spell directly (Counterspell, Kick, Wind Shear, etc.).
    Unit* currentTarget = AI_VALUE(Unit*, "current target");
    if (currentTarget)
    {
        bool shouldInterrupt = false;

        if (botAI->EqualLowercaseName(currentTarget->GetName(), "qiraji mindslayer"))
        {
            Spell* spell = currentTarget->GetCurrentSpell(CURRENT_GENERIC_SPELL);
            Spell* channel = currentTarget->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
            shouldInterrupt =
                (spell && Aq40SpellIds::MatchesAnySpellId(spell->GetSpellInfo(), { Aq40SpellIds::Aq40MindslayerMindBlast })) ||
                (channel && Aq40SpellIds::MatchesAnySpellId(channel->GetSpellInfo(), { Aq40SpellIds::Aq40MindslayerMindFlay }));
        }
        else if (botAI->EqualLowercaseName(currentTarget->GetName(), "obsidian nullifier"))
        {
            Spell* spell = currentTarget->GetCurrentSpell(CURRENT_GENERIC_SPELL);
            shouldInterrupt = spell &&
                Aq40SpellIds::MatchesAnySpellId(spell->GetSpellInfo(), { Aq40SpellIds::Aq40NullifierNullify });
        }

        if (shouldInterrupt)
            return botAI->DoSpecificAction("interrupt spell", Event(), true);
    }

    // Not yet targeting a casting dangerous trash mob – find one and switch.
    // The actual interrupt fires next tick once we're facing/in range
    // (same two-tick pattern used in C'Thun eye tentacle interrupts).
    GuidVector const& attackers = context->GetValue<GuidVector>("attackers")->Get();
    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, attackers);

    std::vector<Unit*> castingTargets;
    for (ObjectGuid const guid : encounterUnits)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit)
            continue;

        if (botAI->EqualLowercaseName(unit->GetName(), "qiraji mindslayer"))
        {
            Spell* spell = unit->GetCurrentSpell(CURRENT_GENERIC_SPELL);
            Spell* channel = unit->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
            bool const castingMindBlast = spell &&
                Aq40SpellIds::MatchesAnySpellId(spell->GetSpellInfo(), { Aq40SpellIds::Aq40MindslayerMindBlast });
            bool const channelingMindFlay = channel &&
                Aq40SpellIds::MatchesAnySpellId(channel->GetSpellInfo(), { Aq40SpellIds::Aq40MindslayerMindFlay });

            if (castingMindBlast || channelingMindFlay)
                castingTargets.push_back(unit);
        }
        else if (botAI->EqualLowercaseName(unit->GetName(), "obsidian nullifier"))
        {
            Spell* spell = unit->GetCurrentSpell(CURRENT_GENERIC_SPELL);
            if (spell && Aq40SpellIds::MatchesAnySpellId(spell->GetSpellInfo(), { Aq40SpellIds::Aq40NullifierNullify }))
                castingTargets.push_back(unit);
        }
    }

    if (castingTargets.empty())
        return false;

    // Distribute bots across casting targets so each one gets an interrupter.
    uint32 const idx = bot->GetGUID().GetCounter() % castingTargets.size();
    Unit* assigned = castingTargets[idx];

    if (!assigned || (AI_VALUE(Unit*, "current target") == assigned && bot->GetVictim() == assigned))
        return false;

    return Attack(assigned);
}

bool Aq40TrashAvoidDangerousAoeAction::Execute(Event /*event*/)
{
    if (Aq40BossHelper::IsEncounterTank(bot, bot))
        return false;

    if (Aq40SpellIds::HasAnyAura(botAI, bot, { Aq40SpellIds::Aq40DefenderPlague }))
    {
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

    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, context->GetValue<GuidVector>("attackers")->Get());
    Unit* danger = nullptr;
    float dangerRange = 0.0f;

    for (ObjectGuid const guid : encounterUnits)
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

bool Aq40TrashControlMindControlAction::Execute(Event /*event*/)
{
    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, context->GetValue<GuidVector>("attackers")->Get());

    // Try to CC a mind-controlled player (polymorph, fear, etc.)
    Unit* mcTarget = nullptr;
    float closestDist = std::numeric_limits<float>::max();
    for (ObjectGuid const guid : encounterUnits)
    {
        Unit* unit = botAI->GetUnit(guid);
        Player* player = unit ? unit->ToPlayer() : nullptr;
        if (!player || !player->IsAlive() || player == bot)
            continue;

        if (!player->IsCharmed() || player->IsPolymorphed())
            continue;

        float const dist = bot->GetDistance2d(player);
        if (dist < closestDist)
        {
            closestDist = dist;
            mcTarget = player;
        }
    }

    if (mcTarget)
    {
        static std::initializer_list<char const*> ccSpells = {
            "polymorph", "fear", "hibernate", "freezing trap", "repentance", "blind"
        };
        for (char const* spell : ccSpells)
        {
            if (botAI->CanCastSpell(spell, mcTarget) && botAI->CastSpell(spell, mcTarget))
                return true;
        }
    }

    // Fallback: resume normal trash targeting using combat-filtered units
    // so passive mobs (idle scarabs/scorpions) are ignored.
    GuidVector activeUnits = Aq40BossHelper::GetActiveCombatUnits(botAI, context->GetValue<GuidVector>("attackers")->Get());
    Unit* target = Aq40BossActions::FindTrashTarget(botAI, activeUnits);
    if (!target || (AI_VALUE(Unit*, "current target") == target && bot->GetVictim() == target))
        return false;

    return Attack(target);
}

bool Aq40TrashTranqEnrageAction::Execute(Event /*event*/)
{
    if (bot->getClass() != CLASS_HUNTER)
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, context->GetValue<GuidVector>("attackers")->Get());
    for (ObjectGuid const guid : encounterUnits)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit || !botAI->EqualLowercaseName(unit->GetName(), "qiraji slayer"))
            continue;

        if (botAI->HasAura(Aq40SpellIds::Aq40SlayerEnrage, unit))
        {
            if (botAI->CanCastSpell("tranquilizing shot", unit))
                return botAI->CastSpell("tranquilizing shot", unit);
        }
    }

    return false;
}

bool Aq40TrashDispelVengeanceAction::Execute(Event /*event*/)
{
    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, context->GetValue<GuidVector>("attackers")->Get());

    Unit* vengeanceTarget = nullptr;
    for (ObjectGuid const guid : encounterUnits)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit || !botAI->EqualLowercaseName(unit->GetName(), "qiraji champion"))
            continue;

        if (botAI->HasAura(Aq40SpellIds::Aq40ChampionVengeance, unit))
        {
            vengeanceTarget = unit;
            break;
        }
    }

    if (!vengeanceTarget)
        return false;

    // Mage spellsteal, shaman purge, hunter tranq shot can remove this buff
    static std::initializer_list<char const*> dispelSpells = {
        "spellsteal", "purge", "tranquilizing shot"
    };
    for (char const* spell : dispelSpells)
    {
        if (botAI->CanCastSpell(spell, vengeanceTarget))
            return botAI->CastSpell(spell, vengeanceTarget);
    }

    return false;
}

bool Aq40TrashFearWardAction::Execute(Event /*event*/)
{
    // Shamans: drop tremor totem (handles the fear after it lands)
    if (bot->getClass() == CLASS_SHAMAN)
    {
        if (botAI->CanCastSpell("tremor totem", bot))
            return botAI->CastSpell("tremor totem", bot);
        return false;
    }

    // Priests: pre-cast Fear Ward on the main tank
    if (bot->getClass() == CLASS_PRIEST)
    {
        Player* mainTank = Aq40BossHelper::GetEncounterPrimaryTank(bot);
        if (mainTank && botAI->CanCastSpell("fear ward", mainTank))
            return botAI->CastSpell("fear ward", mainTank);
    }

    return false;
}
