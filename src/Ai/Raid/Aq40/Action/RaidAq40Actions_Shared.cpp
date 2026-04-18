#include "RaidAq40Actions.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

#include "ObjectGuid.h"
#include "../RaidAq40BossHelper.h"
#include "../RaidAq40SpellIds.h"
#include "../Util/RaidAq40Helpers.h"
#include "../../RaidBossHelpers.h"

namespace
{
enum class Aq40TrashPackArchetype
{
    Mixed,
    Anubisath,
    CasterControl,
    SwarmMelee
};

struct Aq40ManagedResistanceState
{
    bool natureCombatEnabled = false;
    bool natureNonCombatEnabled = false;
    bool shamanNatureCombatEnabled = false;
    bool shadowCombatEnabled = false;
    bool shadowNonCombatEnabled = false;
    bool warlockShadowBuffApplied = false;
};

std::unordered_map<uint64, Aq40ManagedResistanceState> sManagedResistanceStateByBot;
std::unordered_map<uint64, bool> sAq40CleanupReportedDirtyByBot;

bool ClearManagedAq40ResistanceStrategies(Player* bot, PlayerbotAI* botAI)
{
    if (!bot || !botAI)
        return false;

    bool cleaned = false;

    if (botAI->HasStrategy("rnature", BotState::BOT_STATE_COMBAT))
    {
        botAI->ChangeStrategy("-rnature", BotState::BOT_STATE_COMBAT);
        cleaned = true;
    }

    if (botAI->HasStrategy("rnature", BotState::BOT_STATE_NON_COMBAT))
    {
        botAI->ChangeStrategy("-rnature", BotState::BOT_STATE_NON_COMBAT);
        cleaned = true;
    }

    if (botAI->HasStrategy("nature resistance", BotState::BOT_STATE_COMBAT))
    {
        botAI->ChangeStrategy("-nature resistance", BotState::BOT_STATE_COMBAT);
        cleaned = true;
    }

    if (botAI->HasStrategy("rshadow", BotState::BOT_STATE_COMBAT))
    {
        botAI->ChangeStrategy("-rshadow", BotState::BOT_STATE_COMBAT);
        cleaned = true;
    }

    if (botAI->HasStrategy("rshadow", BotState::BOT_STATE_NON_COMBAT))
    {
        botAI->ChangeStrategy("-rshadow", BotState::BOT_STATE_NON_COMBAT);
        cleaned = true;
    }

    if (bot->HasAura(Aq40SpellIds::TwinWarlockShadowResistBuff))
    {
        bot->RemoveAurasDueToSpell(Aq40SpellIds::TwinWarlockShadowResistBuff);
        cleaned = true;
    }

    cleaned = sManagedResistanceStateByBot.erase(bot->GetGUID().GetRawValue()) > 0 || cleaned;
    return cleaned;
}

void LogAq40CleanupTransition(Player* bot, bool wasDirty)
{
    if (!bot)
        return;

    uint64 const botGuid = bot->GetGUID().GetRawValue();
    auto itr = sAq40CleanupReportedDirtyByBot.find(botGuid);
    bool const previousDirty = itr != sAq40CleanupReportedDirtyByBot.end() && itr->second;

    if (wasDirty)
    {
        if (!previousDirty)
            LOG_INFO("playerbots", "AQ40 cleanup: bot={} cleaned stale recovery state and can resume follow", bot->GetName());

        sAq40CleanupReportedDirtyByBot[botGuid] = true;
        return;
    }

    if (previousDirty)
        LOG_INFO("playerbots", "AQ40 cleanup: bot={} recovery state already clean", bot->GetName());

    sAq40CleanupReportedDirtyByBot[botGuid] = false;
}

// IsSarturaMob / IsSarturaSpinning now live in Aq40BossHelper.

bool IsAq40TrashUnit(PlayerbotAI* botAI, Unit* unit)
{
    return Aq40BossHelper::IsUnitNamedAny(botAI, unit,
        { "anubisath warder", "anubisath defender", "obsidian eradicator", "obsidian nullifier",
          "vekniss stinger", "qiraji slayer", "qiraji champion", "qiraji mindslayer",
          "qiraji brainwasher", "qiraji battleguard", "anubisath sentinel", "qiraji lasher",
          "vekniss warrior", "vekniss guardian", "vekniss drone", "vekniss soldier",
          "vekniss wasp", "scarab", "qiraji scarab", "spitting scarab", "scorpion" });
}

bool IsAq40AnubisathTrashUnit(PlayerbotAI* botAI, Unit* unit)
{
    return Aq40BossHelper::IsUnitNamedAny(botAI, unit,
        { "anubisath warder", "anubisath defender", "anubisath sentinel" });
}

bool IsAq40CasterControlTrashUnit(PlayerbotAI* botAI, Unit* unit)
{
    return Aq40BossHelper::IsUnitNamedAny(botAI, unit,
        { "qiraji mindslayer", "obsidian nullifier", "obsidian eradicator", "qiraji brainwasher" });
}

bool IsAq40SwarmMeleeTrashUnit(PlayerbotAI* botAI, Unit* unit)
{
    return Aq40BossHelper::IsUnitNamedAny(botAI, unit,
        { "qiraji champion", "qiraji slayer", "qiraji battleguard", "vekniss stinger",
          "qiraji lasher", "vekniss guardian", "vekniss warrior", "vekniss drone",
          "vekniss soldier", "vekniss wasp", "scarab", "qiraji scarab", "spitting scarab", "scorpion" });
}

Aq40TrashPackArchetype DetectAq40TrashPackArchetype(PlayerbotAI* botAI, GuidVector const& units)
{
    if (!botAI)
        return Aq40TrashPackArchetype::Mixed;

    uint32 anubisathCount = 0;
    uint32 casterControlCount = 0;
    uint32 swarmMeleeCount = 0;

    for (ObjectGuid const guid : units)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!IsAq40TrashUnit(botAI, unit))
            continue;

        if (IsAq40AnubisathTrashUnit(botAI, unit))
            ++anubisathCount;
        else if (IsAq40CasterControlTrashUnit(botAI, unit))
            ++casterControlCount;
        else if (IsAq40SwarmMeleeTrashUnit(botAI, unit))
            ++swarmMeleeCount;
    }

    if (anubisathCount > 0)
        return Aq40TrashPackArchetype::Anubisath;
    if (casterControlCount > 0)
        return Aq40TrashPackArchetype::CasterControl;
    if (swarmMeleeCount > 0)
        return Aq40TrashPackArchetype::SwarmMelee;
    return Aq40TrashPackArchetype::Mixed;
}

uint32 GetAq40TrashTankControlPriority(PlayerbotAI* botAI, Unit* unit, Aq40TrashPackArchetype archetype)
{
    if (!botAI || !unit)
        return std::numeric_limits<uint32>::max();

    switch (archetype)
    {
        case Aq40TrashPackArchetype::Anubisath:
            if (Aq40BossHelper::IsUnitNamedAny(botAI, unit, { "anubisath warder" }))
                return 0;
            if (Aq40BossHelper::IsUnitNamedAny(botAI, unit, { "anubisath defender" }))
                return 1;
            if (Aq40BossHelper::IsUnitNamedAny(botAI, unit, { "anubisath sentinel" }))
                return 2;
            if (Aq40BossHelper::IsUnitNamedAny(botAI, unit, { "qiraji champion", "qiraji slayer", "qiraji battleguard", "vekniss stinger" }))
                return 3;
            if (Aq40BossHelper::IsUnitNamedAny(botAI, unit, { "obsidian nullifier", "qiraji mindslayer", "obsidian eradicator", "qiraji brainwasher" }))
                return 4;
            break;
        case Aq40TrashPackArchetype::CasterControl:
            if (Aq40BossHelper::IsUnitNamedAny(botAI, unit, { "obsidian nullifier", "qiraji mindslayer" }))
                return 0;
            if (Aq40BossHelper::IsUnitNamedAny(botAI, unit, { "obsidian eradicator", "qiraji brainwasher" }))
                return 1;
            if (Aq40BossHelper::IsUnitNamedAny(botAI, unit, { "qiraji champion", "qiraji slayer", "qiraji battleguard", "vekniss stinger" }))
                return 2;
            if (Aq40BossHelper::IsUnitNamedAny(botAI, unit, { "anubisath warder", "anubisath defender", "anubisath sentinel" }))
                return 3;
            break;
        case Aq40TrashPackArchetype::SwarmMelee:
            if (Aq40BossHelper::IsUnitNamedAny(botAI, unit, { "qiraji champion", "qiraji slayer", "qiraji battleguard", "vekniss stinger" }))
                return 0;
            if (Aq40BossHelper::IsUnitNamedAny(botAI, unit, { "qiraji lasher", "vekniss guardian", "vekniss warrior" }))
                return 1;
            if (Aq40BossHelper::IsUnitNamedAny(botAI, unit, { "vekniss drone", "vekniss soldier", "vekniss wasp" }))
                return 2;
            break;
        case Aq40TrashPackArchetype::Mixed:
            break;
    }

    if (IsAq40AnubisathTrashUnit(botAI, unit))
        return 0;
    if (Aq40BossHelper::IsUnitNamedAny(botAI, unit, { "qiraji champion", "qiraji slayer", "qiraji battleguard", "vekniss stinger" }))
        return 1;
    if (IsAq40CasterControlTrashUnit(botAI, unit))
        return 2;
    if (Aq40BossHelper::IsUnitNamedAny(botAI, unit, { "qiraji lasher", "vekniss guardian", "vekniss warrior" }))
        return 3;
    if (Aq40BossHelper::IsUnitNamedAny(botAI, unit, { "vekniss drone", "vekniss soldier", "vekniss wasp" }))
        return 4;
    return 5;
}

uint32 GetAq40TrashAssistPriority(PlayerbotAI* botAI, Unit* unit)
{
    if (!botAI || !unit)
        return std::numeric_limits<uint32>::max();

    if (Aq40BossHelper::IsUnitNamedAny(botAI, unit, { "qiraji mindslayer" }))
        return 0;
    if (Aq40BossHelper::IsUnitNamedAny(botAI, unit, { "obsidian nullifier" }))
        return 1;
    if (Aq40BossHelper::IsUnitNamedAny(botAI, unit, { "obsidian eradicator" }))
        return 2;
    if (Aq40BossHelper::IsUnitNamedAny(botAI, unit, { "qiraji champion" }))
        return 3;
    if (Aq40BossHelper::IsUnitNamedAny(botAI, unit, { "qiraji slayer" }))
        return 4;
    if (Aq40BossHelper::IsUnitNamedAny(botAI, unit, { "anubisath warder" }))
        return 5;
    if (Aq40BossHelper::IsUnitNamedAny(botAI, unit, { "anubisath defender" }))
        return 6;
    if (Aq40BossHelper::IsUnitNamedAny(botAI, unit, { "anubisath sentinel" }))
        return 7;
    if (Aq40BossHelper::IsUnitNamedAny(botAI, unit, { "qiraji lasher" }))
        return 8;
    if (Aq40BossHelper::IsUnitNamedAny(botAI, unit, { "vekniss stinger" }))
        return 9;
    if (Aq40BossHelper::IsUnitNamedAny(botAI, unit, { "qiraji brainwasher", "qiraji battleguard" }))
        return 10;
    if (Aq40BossHelper::IsUnitNamedAny(botAI, unit, { "vekniss guardian", "vekniss warrior", "vekniss drone", "vekniss soldier", "vekniss wasp" }))
        return 11;
    return 12;
}

std::vector<Unit*> GetSortedAq40TrashUnits(PlayerbotAI* botAI, GuidVector const& units, bool tankPriority,
                                          Aq40TrashPackArchetype archetype = Aq40TrashPackArchetype::Mixed)
{
    std::vector<Unit*> trashUnits;
    if (!botAI)
        return trashUnits;

    for (ObjectGuid const guid : units)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!IsAq40TrashUnit(botAI, unit))
            continue;

        trashUnits.push_back(unit);
    }

    std::sort(trashUnits.begin(), trashUnits.end(), [botAI, tankPriority, archetype](Unit* left, Unit* right)
    {
        if (!left || !right)
            return left != nullptr;

        uint32 const leftPriority = tankPriority ? GetAq40TrashTankControlPriority(botAI, left, archetype)
                                                 : GetAq40TrashAssistPriority(botAI, left);
        uint32 const rightPriority = tankPriority ? GetAq40TrashTankControlPriority(botAI, right, archetype)
                                                  : GetAq40TrashAssistPriority(botAI, right);
        if (leftPriority != rightPriority)
            return leftPriority < rightPriority;

        if (left->GetHealthPct() != right->GetHealthPct())
            return left->GetHealthPct() < right->GetHealthPct();

        return left->GetGUID().GetRawValue() < right->GetGUID().GetRawValue();
    });

    return trashUnits;
}

Unit* FindAssignedAq40TrashTankTarget(Player* bot, PlayerbotAI* botAI, std::vector<Unit*> const& controlTargets)
{
    if (!bot || !botAI || controlTargets.empty())
        return nullptr;

    uint32 assignedIndex = 0;
    if (Aq40BossHelper::IsEncounterBackupTank(bot, bot, 0))
        assignedIndex = 1;
    else if (Aq40BossHelper::IsEncounterBackupTank(bot, bot, 1))
        assignedIndex = 2;

    if (assignedIndex < controlTargets.size())
        return controlTargets[assignedIndex];

    return controlTargets.front();
}

Unit* FindBestHeldAq40TrashTarget(Player* bot, PlayerbotAI* botAI, std::vector<Unit*> const& assistTargets)
{
    if (!bot || !botAI)
        return nullptr;

    for (Unit* target : assistTargets)
    {
        if (Aq40BossHelper::IsUnitHeldByEncounterTank(bot, target))
            return target;
    }

    return nullptr;
}

bool HasAnyHeldAq40TrashTarget(Player* bot, PlayerbotAI* botAI, std::vector<Unit*> const& targets)
{
    return FindBestHeldAq40TrashTarget(bot, botAI, targets) != nullptr;
}

std::vector<Unit*> FindCastingAq40TrashDangerUnits(PlayerbotAI* botAI, GuidVector const& encounterUnits)
{
    std::vector<Unit*> castingDanger;
    if (!botAI)
        return castingDanger;

    for (ObjectGuid const guid : encounterUnits)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit)
            continue;

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

    std::sort(castingDanger.begin(), castingDanger.end(), [botAI](Unit* left, Unit* right)
    {
        if (!left || !right)
            return left != nullptr;

        uint32 const leftPriority = GetAq40TrashAssistPriority(botAI, left);
        uint32 const rightPriority = GetAq40TrashAssistPriority(botAI, right);
        if (leftPriority != rightPriority)
            return leftPriority < rightPriority;

        return left->GetGUID().GetRawValue() < right->GetGUID().GetRawValue();
    });

    return castingDanger;
}
}    // namespace

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
    Player* bot = botAI ? botAI->GetBot() : nullptr;

    // Pre-filter attackers by distance so bots don't run past nearby mobs to
    // chase a distant priority target.  Fall back to closest unit if nothing
    // from the priority list is within range.
    static float constexpr kTrashTargetMaxRange = 45.0f;
    GuidVector nearbyAttackers;
    Unit* closestUnit = nullptr;
    float closestDist = std::numeric_limits<float>::max();
    for (ObjectGuid const guid : attackers)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit || !unit->IsAlive() || !unit->IsCreature())
            continue;

        float const dist = bot ? bot->GetDistance2d(unit) : 0.0f;
        if (!bot || dist <= kTrashTargetMaxRange)
            nearbyAttackers.push_back(guid);

        if (dist < closestDist)
        {
            closestDist = dist;
            closestUnit = unit;
        }
    }

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
        Unit* chosen = Aq40BossHelper::FindLowestHealthUnitByAnyName(botAI, nearbyAttackers, names);
        if (chosen)
            return chosen;
    }

    // Nothing from the priority list in range — fall back to the closest active
    // combat unit so the bot doesn't stand idle while mobs hit them.
    return closestUnit;
}
}    // namespace Aq40BossActions

namespace
{
Unit* FindClosestAq40PlagueSeparationRisk(Player* bot, PlayerbotAI* botAI, float& distanceToCreate)
{
    distanceToCreate = 0.0f;
    if (!bot || !botAI)
        return nullptr;

    Group const* group = bot->GetGroup();
    if (!group)
        return nullptr;

    Unit* riskiestMember = nullptr;
    float largestDeficit = 0.0f;

    for (GroupReference const* ref = group->GetFirstMember(); ref; ref = ref->next())
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

struct Aq40TankRetreatResult
{
    bool valid = false;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

// Compute a short retreat position toward the encounter tank, away from a danger source.
// Returns invalid if no tank is available or the candidate would push the bot farther from
// the tank (deeper into the room).  Callers should hold position when invalid.
Aq40TankRetreatResult ComputeTankRetreatPosition(Player* bot, Unit* danger, float clearDistance)
{
    Aq40TankRetreatResult result;
    if (!bot || !danger)
        return result;

    Player* tank = Aq40BossHelper::GetEncounterPrimaryTank(bot);
    if (!tank)
        tank = Aq40BossHelper::GetEncounterBackupTank(bot, 0);
    if (!tank)
        return result;

    // Direction from danger toward the tank (retreat direction).
    float dx = tank->GetPositionX() - danger->GetPositionX();
    float dy = tank->GetPositionY() - danger->GetPositionY();
    float mag = std::sqrt(dx * dx + dy * dy);
    if (mag < 0.001f)
        return result;

    dx /= mag;
    dy /= mag;

    // Short corrective step, capped at 8y to avoid large scatter.
    float const step = std::min(clearDistance, 8.0f);
    float candidateX = bot->GetPositionX() + dx * step;
    float candidateY = bot->GetPositionY() + dy * step;
    float candidateZ = bot->GetPositionZ();

    // Safety: reject if the candidate is farther from the tank than we currently are.
    float const currentDistToTank = bot->GetDistance2d(tank);
    float const candidateDistToTank = std::sqrt(
        (candidateX - tank->GetPositionX()) * (candidateX - tank->GetPositionX()) +
        (candidateY - tank->GetPositionY()) * (candidateY - tank->GetPositionY()));
    if (candidateDistToTank > currentDistToTank + 1.0f)
        return result;

    // Validate collision.
    if (!bot->GetMap()->CheckCollisionAndGetValidCoords(bot, bot->GetPositionX(), bot->GetPositionY(),
                                                        bot->GetPositionZ(), candidateX, candidateY, candidateZ))
        return result;

    result.valid = true;
    result.x = candidateX;
    result.y = candidateY;
    result.z = candidateZ;
    return result;
}
}    // namespace

bool Aq40ManageResistanceStrategiesAction::Execute(Event /*event*/)
{
    if (!bot)
        return false;

    Aq40ManagedResistanceState& managedState = sManagedResistanceStateByBot[bot->GetGUID().GetRawValue()];

    GuidVector const attackers = context->GetValue<GuidVector>("attackers")->Get();
    GuidVector const activeUnits = Aq40BossHelper::GetActiveCombatUnits(botAI, attackers);
    bool const inAq40 = Aq40BossHelper::IsInAq40(bot);
    bool const needNatureResistance =
        inAq40 && Aq40BossHelper::HasAnyNamedUnit(botAI, activeUnits,
            { "princess huhuran", "viscidus", "glob of viscidus", "toxic slime" });
    bool const needShadowResistance =
        inAq40 && Aq40BossHelper::HasAnyNamedUnit(botAI, activeUnits,
            { "emperor vek'nilash", "emperor vek'lor" });

    bool acted = false;

    if (bot->getClass() == CLASS_HUNTER)
    {
        bool const hasNatureStrategyCombat = botAI->HasStrategy("rnature", BotState::BOT_STATE_COMBAT);
        bool const hasNatureStrategyNonCombat = botAI->HasStrategy("rnature", BotState::BOT_STATE_NON_COMBAT);

        if (needNatureResistance)
        {
            if (!hasNatureStrategyCombat)
            {
                botAI->ChangeStrategy("+rnature", BotState::BOT_STATE_COMBAT);
                managedState.natureCombatEnabled = true;
                acted = true;
            }
            if (!hasNatureStrategyNonCombat)
            {
                botAI->ChangeStrategy("+rnature", BotState::BOT_STATE_NON_COMBAT);
                managedState.natureNonCombatEnabled = true;
                acted = true;
            }

            if (!botAI->HasAura("aspect of the wild", bot))
                acted = botAI->DoSpecificAction("aspect of the wild", Event(), true) || acted;
        }
        else if (managedState.natureCombatEnabled || managedState.natureNonCombatEnabled)
        {
            if (managedState.natureCombatEnabled && hasNatureStrategyCombat)
            {
                botAI->ChangeStrategy("-rnature", BotState::BOT_STATE_COMBAT);
                managedState.natureCombatEnabled = false;
                acted = true;
            }
            if (managedState.natureNonCombatEnabled && hasNatureStrategyNonCombat)
            {
                botAI->ChangeStrategy("-rnature", BotState::BOT_STATE_NON_COMBAT);
                managedState.natureNonCombatEnabled = false;
                acted = true;
            }
        }
    }

    if (bot->getClass() == CLASS_SHAMAN)
    {
        bool const hasNatureTotemStrategyCombat = botAI->HasStrategy("nature resistance", BotState::BOT_STATE_COMBAT);

        if (needNatureResistance)
        {
            if (!hasNatureTotemStrategyCombat)
            {
                botAI->ChangeStrategy("+nature resistance", BotState::BOT_STATE_COMBAT);
                managedState.shamanNatureCombatEnabled = true;
                acted = true;
            }

            if (!botAI->HasAura("nature resistance totem", bot))
                acted = botAI->DoSpecificAction("nature resistance totem", Event(), true) || acted;
        }
        else if (managedState.shamanNatureCombatEnabled)
        {
            if (hasNatureTotemStrategyCombat)
            {
                botAI->ChangeStrategy("-nature resistance", BotState::BOT_STATE_COMBAT);
                acted = true;
            }
            managedState.shamanNatureCombatEnabled = false;
        }
    }

    if (bot->getClass() == CLASS_PRIEST || bot->getClass() == CLASS_PALADIN)
    {
        bool const hasShadowStrategyCombat = botAI->HasStrategy("rshadow", BotState::BOT_STATE_COMBAT);
        bool const hasShadowStrategyNonCombat = botAI->HasStrategy("rshadow", BotState::BOT_STATE_NON_COMBAT);

        if (needShadowResistance)
        {
            if (!hasShadowStrategyCombat)
            {
                botAI->ChangeStrategy("+rshadow", BotState::BOT_STATE_COMBAT);
                managedState.shadowCombatEnabled = true;
                acted = true;
            }
            if (!hasShadowStrategyNonCombat)
            {
                botAI->ChangeStrategy("+rshadow", BotState::BOT_STATE_NON_COMBAT);
                managedState.shadowNonCombatEnabled = true;
                acted = true;
            }

            if (bot->getClass() == CLASS_PRIEST)
            {
                if (!botAI->HasAura("shadow protection", bot) &&
                    !botAI->HasAura("prayer of shadow protection", bot))
                    acted = botAI->DoSpecificAction("shadow protection on party", Event(), true) || acted;
            }
            else if (bot->getClass() == CLASS_PALADIN)
            {
                acted = botAI->DoSpecificAction("shadow resistance aura", Event(), true) || acted;
            }
        }
        else if (managedState.shadowCombatEnabled || managedState.shadowNonCombatEnabled)
        {
            if (managedState.shadowCombatEnabled && hasShadowStrategyCombat)
            {
                botAI->ChangeStrategy("-rshadow", BotState::BOT_STATE_COMBAT);
                managedState.shadowCombatEnabled = false;
                acted = true;
            }
            if (managedState.shadowNonCombatEnabled && hasShadowStrategyNonCombat)
            {
                botAI->ChangeStrategy("-rshadow", BotState::BOT_STATE_NON_COMBAT);
                managedState.shadowNonCombatEnabled = false;
                acted = true;
            }
        }
    }

    if (bot->getClass() == CLASS_WARLOCK)
    {
        if (needShadowResistance)
        {
            if (!managedState.warlockShadowBuffApplied &&
                !botAI->HasAura(Aq40SpellIds::TwinWarlockShadowResistBuff, bot))
            {
                Aura* const aura = bot->AddAura(Aq40SpellIds::TwinWarlockShadowResistBuff, bot);
                if (aura)
                {
                    managedState.warlockShadowBuffApplied = true;
                    acted = true;
                }
            }
        }
        else if (managedState.warlockShadowBuffApplied)
        {
            bot->RemoveAurasDueToSpell(Aq40SpellIds::TwinWarlockShadowResistBuff);
            managedState.warlockShadowBuffApplied = false;
            acted = true;
        }
    }

    if (!managedState.natureCombatEnabled && !managedState.natureNonCombatEnabled &&
        !managedState.shamanNatureCombatEnabled &&
        !managedState.shadowCombatEnabled && !managedState.shadowNonCombatEnabled &&
        !managedState.warlockShadowBuffApplied)
        sManagedResistanceStateByBot.erase(bot->GetGUID().GetRawValue());

    return acted;
}

bool Aq40ManageResistanceStrategiesAction::isUseful()
{
    if (!bot)
        return false;

    GuidVector const attackers = context->GetValue<GuidVector>("attackers")->Get();
    GuidVector const activeUnits = Aq40BossHelper::GetActiveCombatUnits(botAI, attackers);
    bool const inAq40 = Aq40BossHelper::IsInAq40(bot);
    bool const needNatureResistance =
        inAq40 && Aq40BossHelper::HasAnyNamedUnit(botAI, activeUnits,
            { "princess huhuran", "viscidus", "glob of viscidus", "toxic slime" });
    bool const needShadowResistance =
        inAq40 && Aq40BossHelper::HasAnyNamedUnit(botAI, activeUnits,
            { "emperor vek'nilash", "emperor vek'lor" });

    if (bot->getClass() == CLASS_HUNTER)
    {
        bool const hasNatureStrategyCombat = botAI->HasStrategy("rnature", BotState::BOT_STATE_COMBAT);
        bool const hasNatureStrategyNonCombat = botAI->HasStrategy("rnature", BotState::BOT_STATE_NON_COMBAT);
        return (needNatureResistance &&
                (!hasNatureStrategyCombat || !hasNatureStrategyNonCombat || !botAI->HasAura("aspect of the wild", bot))) ||
               (!needNatureResistance && (hasNatureStrategyCombat || hasNatureStrategyNonCombat));
    }

    if (bot->getClass() == CLASS_SHAMAN)
    {
        bool const hasNatureTotemStrategyCombat = botAI->HasStrategy("nature resistance", BotState::BOT_STATE_COMBAT);
        return (needNatureResistance &&
                (!hasNatureTotemStrategyCombat || !botAI->HasAura("nature resistance totem", bot))) ||
               (!needNatureResistance && hasNatureTotemStrategyCombat);
    }

    if (bot->getClass() == CLASS_PRIEST || bot->getClass() == CLASS_PALADIN)
    {
        bool const hasShadowStrategyCombat = botAI->HasStrategy("rshadow", BotState::BOT_STATE_COMBAT);
        bool const hasShadowStrategyNonCombat = botAI->HasStrategy("rshadow", BotState::BOT_STATE_NON_COMBAT);
        bool const missingShadowBuff =
            (bot->getClass() == CLASS_PRIEST) ?
                (!botAI->HasAura("shadow protection", bot) && !botAI->HasAura("prayer of shadow protection", bot)) :
                !botAI->HasAura("shadow resistance aura", bot);

        return (needShadowResistance &&
                (!hasShadowStrategyCombat || !hasShadowStrategyNonCombat || missingShadowBuff)) ||
               (!needShadowResistance && (hasShadowStrategyCombat || hasShadowStrategyNonCombat));
    }

    if (bot->getClass() == CLASS_WARLOCK)
    {
        bool const hasBuff = botAI->HasAura(Aq40SpellIds::TwinWarlockShadowResistBuff, bot);
        return (needShadowResistance && !hasBuff) || (!needShadowResistance && hasBuff);
    }

    return false;
}

bool Aq40EraseTimersAndTrackersAction::isUseful()
{
    return bot && bot->IsAlive() && Aq40BossHelper::IsInAq40(bot) &&
           Aq40Helpers::ShouldRunOutOfCombatMaintenance(bot, botAI);
}

bool Aq40EraseTimersAndTrackersAction::Execute(Event /*event*/)
{
    if (!bot || !Aq40BossHelper::IsInAq40(bot))
        return false;

    if (!Aq40Helpers::ShouldRunOutOfCombatMaintenance(bot, botAI))
        return false;

    bool const hadManagedResistance = ClearManagedAq40ResistanceStrategies(bot, botAI);
    // Only wipe instance-level encounter caches when no group member is inside
    // the Twin Emperors room.  Bots outside the room running cleanup must not
    // destroy assignments that bots inside are actively using for pre-pull staging.
    bool const hadPersistentEncounterState =
        !Aq40Helpers::IsAnyGroupMemberInTwinRoom(bot) && Aq40Helpers::ResetEncounterState(bot);
    bool const recoveredDirtyState = hadManagedResistance || hadPersistentEncounterState;

    LogAq40CleanupTransition(bot, recoveredDirtyState);
    return true;
}

bool Aq40SkeramAcquirePlatformTargetAction::Execute(Event /*event*/)
{
    GuidVector const attackers = context->GetValue<GuidVector>("attackers")->Get();
    GuidVector encounterUnits = Aq40Helpers::GetObservedSkeramEncounterUnits(bot, botAI, attackers);
    Unit* target = Aq40BossActions::FindSkeramTarget(botAI, encounterUnits);
    if (!target)
        return false;

    if (!Aq40BossHelper::IsEncounterTank(bot, bot) && !Aq40BossActions::HasSkeramSkullTarget(botAI))
    {
        if (Aq40Helpers::IsSkeramPostBlinkHoldActive(bot, botAI, attackers))
            return false;

        if (!Aq40BossHelper::HasAnyNamedUnitHeldByEncounterTank(botAI, bot, encounterUnits, { "the prophet skeram" }, true))
            return false;
    }

    if (AI_VALUE(Unit*, "current target") == target && bot->GetVictim() == target)
        return false;

    float const desiredRange = (botAI->IsRanged(bot) || botAI->IsHeal(bot)) ? 24.0f : 4.0f;
    float const engageSlack = (botAI->IsRanged(bot) || botAI->IsHeal(bot)) ? 4.0f : 2.0f;
    if (!bot->IsWithinLOSInMap(target) || bot->GetDistance2d(target) > (desiredRange + engageSlack))
        return MoveNear(target, desiredRange, MovementPriority::MOVEMENT_COMBAT);

    // Encounter tank marks the real Skeram with skull so the raid can follow
    // through blinks without relying solely on tank aggro detection.
    if (Aq40BossHelper::IsEncounterTank(bot, bot))
        MarkTargetWithSkull(bot, target);

    return Attack(target);
}

bool Aq40SkeramInterruptAction::Execute(Event /*event*/)
{
    GuidVector const attackers = context->GetValue<GuidVector>("attackers")->Get();
    GuidVector encounterUnits = Aq40Helpers::GetObservedSkeramEncounterUnits(bot, botAI, attackers);
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

    if (!bot->IsWithinLOSInMap(target) || bot->GetDistance2d(target) > 22.0f)
        return MoveNear(target, 18.0f, MovementPriority::MOVEMENT_COMBAT);

    return Attack(target);
}

bool Aq40SkeramFocusRealBossAction::Execute(Event /*event*/)
{
    GuidVector const attackers = context->GetValue<GuidVector>("attackers")->Get();
    GuidVector encounterUnits = Aq40Helpers::GetObservedSkeramEncounterUnits(bot, botAI, attackers);
    Unit* target = Aq40BossActions::FindSkeramTarget(botAI, encounterUnits, true);

    if (!target || (AI_VALUE(Unit*, "current target") == target && bot->GetVictim() == target))
        return false;

    // When a skull marker is present the target has already been validated as
    // the real boss — skip the tank-aggro prerequisite that frequently fails
    // after blinks.  Only apply the old guards when no skull exists.
    bool const hasSkullTarget = Aq40BossActions::HasSkeramSkullTarget(botAI);
    if (!Aq40BossHelper::IsEncounterTank(bot, bot) && !hasSkullTarget)
    {
        if (Aq40Helpers::IsSkeramPostBlinkHoldActive(bot, botAI, attackers))
            return false;

        if (!Aq40BossHelper::HasAnyNamedUnitHeldByEncounterTank(botAI, bot, encounterUnits, { "the prophet skeram" }, true))
            return false;
    }

    float const desiredRange = (botAI->IsRanged(bot) || botAI->IsHeal(bot)) ? 24.0f : 4.0f;
    float const engageSlack = (botAI->IsRanged(bot) || botAI->IsHeal(bot)) ? 4.0f : 2.0f;
    if (!bot->IsWithinLOSInMap(target) || bot->GetDistance2d(target) > (desiredRange + engageSlack))
        return MoveNear(target, desiredRange, MovementPriority::MOVEMENT_COMBAT);

    return Attack(target);
}

bool Aq40SkeramControlMindControlAction::Execute(Event /*event*/)
{
    GuidVector const attackers = context->GetValue<GuidVector>("attackers")->Get();
    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, attackers);

    if (Aq40BossHelper::TryCrowdControlCharmedPlayer(bot, botAI, encounterUnits))
        return true;

    // Fallback: force target back to Skeram.
    GuidVector skeramUnits = Aq40Helpers::GetObservedSkeramEncounterUnits(bot, botAI, attackers);
    Unit* target = Aq40BossActions::FindSkeramTarget(botAI, skeramUnits);
    if (!target || (AI_VALUE(Unit*, "current target") == target && bot->GetVictim() == target))
        return false;

    float const desiredRange = (botAI->IsRanged(bot) || botAI->IsHeal(bot)) ? 24.0f : 4.0f;
    float const engageSlack = (botAI->IsRanged(bot) || botAI->IsHeal(bot)) ? 4.0f : 2.0f;
    if (!bot->IsWithinLOSInMap(target) || bot->GetDistance2d(target) > (desiredRange + engageSlack))
        return MoveNear(target, desiredRange, MovementPriority::MOVEMENT_COMBAT);

    return Attack(target);
}

bool Aq40SarturaChooseTargetAction::Execute(Event /*event*/)
{
    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, context->GetValue<GuidVector>("attackers")->Get());
    if (encounterUnits.empty())
        return false;

    Unit* sartura = Aq40BossActions::FindSarturaTarget(botAI, encounterUnits);
    std::vector<Unit*> guards = Aq40BossActions::FindSarturaGuards(botAI, encounterUnits);
    std::sort(guards.begin(), guards.end(), [](Unit* left, Unit* right)
    {
        if (!left || !right)
            return left != nullptr;
        return left->GetGUID().GetRawValue() < right->GetGUID().GetRawValue();
    });

    Unit* target = nullptr;
    if (Aq40BossHelper::IsEncounterTank(bot, bot))
    {
        if (Aq40BossHelper::IsEncounterPrimaryTank(bot, bot))
            target = sartura;
        else if (Aq40BossHelper::IsEncounterBackupTank(bot, bot, 0) && !guards.empty())
            target = guards[0];
        else if (Aq40BossHelper::IsEncounterBackupTank(bot, bot, 1) && guards.size() >= 2)
            target = guards[1];

        if (!target && !guards.empty())
        {
            target = guards.front();
            for (Unit* guard : guards)
            {
                if (guard && target && guard->GetHealthPct() < target->GetHealthPct())
                    target = guard;
            }
        }

        if (!target)
            target = sartura;
    }
    else
    {
        for (Unit* guard : guards)
        {
            if (Aq40BossHelper::IsUnitHeldByEncounterTank(bot, guard))
            {
                target = guard;
                break;
            }
        }

        if (!target && guards.empty() && sartura &&
            Aq40BossHelper::IsUnitHeldByEncounterTank(bot, sartura, true))
            target = sartura;
    }

    bool const targetIsGuard = target && botAI->EqualLowercaseName(target->GetName(), "sartura's royal guard");
    if (Aq40BossHelper::ShouldWaitForEncounterTankAggro(bot, bot, target, !targetIsGuard))
        return false;

    if (!target || AI_VALUE(Unit*, "current target") == target)
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
        if (!Aq40BossHelper::IsSarturaSpinning(botAI, unit))
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
    Unit* fankriss = Aq40BossActions::FindFankrissTarget(botAI, encounterUnits);
    std::vector<Unit*> spawns = Aq40BossActions::FindFankrissSpawns(botAI, encounterUnits);
    if (!spawns.empty())
    {
        std::sort(spawns.begin(), spawns.end(), [](Unit* left, Unit* right)
        {
            if (!left || !right)
                return left != nullptr;
            return left->GetGUID().GetRawValue() < right->GetGUID().GetRawValue();
        });

        if (Aq40BossHelper::IsEncounterTank(bot, bot))
        {
            bool const hasBossAggro = fankriss && Aq40BossHelper::IsUnitFocusedOnPlayer(fankriss, bot);
            if (hasBossAggro)
                target = fankriss;
            else
            {
                uint32 assignedIndex = 0;
                if (Aq40BossHelper::IsEncounterBackupTank(bot, bot, 0))
                    assignedIndex = 1;
                else if (Aq40BossHelper::IsEncounterBackupTank(bot, bot, 1))
                    assignedIndex = 2;

                if (assignedIndex < spawns.size())
                    target = spawns[assignedIndex];
                else if (!spawns.empty())
                    target = spawns.back();
            }
        }
        else if (!botAI->IsRanged(bot) && !botAI->IsHeal(bot))
        {
            target = fankriss;
        }
        else
        {
            // Ranged/healers prefer a tank-held spawn, but fall back to any spawn
            // to avoid the deadlock where nobody attacks spawns because no tank
            // has picked one up yet.
            for (Unit* spawn : spawns)
            {
                if (Aq40BossHelper::IsUnitHeldByEncounterTank(bot, spawn))
                {
                    target = spawn;
                    break;
                }
            }

            if (!target)
                target = spawns.front();
        }
    }
    else
    {
        target = fankriss;
    }

    // Fall back to Fankriss if no target was resolved (e.g. tank index overshoot).
    if (!target)
        target = fankriss;

    bool const targetIsSpawn = target && botAI->EqualLowercaseName(target->GetName(), "spawn of fankriss");
    if (Aq40BossHelper::ShouldWaitForEncounterTankAggro(bot, bot, target, !targetIsSpawn))
        return false;

    if (!target || AI_VALUE(Unit*, "current target") == target)
        return false;

    return Attack(target);
}

bool Aq40TrashChooseTargetAction::Execute(Event /*event*/)
{
    GuidVector const& attackers = context->GetValue<GuidVector>("attackers")->Get();
    GuidVector activeUnits = Aq40BossHelper::GetActiveCombatUnits(botAI, attackers);
    if (activeUnits.empty())
        return false;

    Aq40TrashPackArchetype const archetype = DetectAq40TrashPackArchetype(botAI, activeUnits);
    std::vector<Unit*> const controlTargets = GetSortedAq40TrashUnits(botAI, activeUnits, true, archetype);
    std::vector<Unit*> const assistTargets = GetSortedAq40TrashUnits(botAI, activeUnits, false);
    if (controlTargets.empty() || assistTargets.empty())
        return false;

    std::vector<Unit*> const castingDanger = FindCastingAq40TrashDangerUnits(botAI, activeUnits);

    if (!castingDanger.empty())
    {
        Unit* assigned = nullptr;
        if (Aq40BossHelper::IsEncounterTank(bot, bot))
        {
            Unit* tankAssignment = FindAssignedAq40TrashTankTarget(bot, botAI, controlTargets);
            if (tankAssignment &&
                std::find(castingDanger.begin(), castingDanger.end(), tankAssignment) != castingDanger.end())
                assigned = tankAssignment;
        }
        else
        {
            for (Unit* caster : castingDanger)
            {
                if (Aq40BossHelper::IsUnitHeldByEncounterTank(bot, caster))
                {
                    assigned = caster;
                    break;
                }
            }

            if (!assigned && !HasAnyHeldAq40TrashTarget(bot, botAI, assistTargets))
                assigned = castingDanger.front();
        }

        if (!assigned || AI_VALUE(Unit*, "current target") == assigned)
            assigned = nullptr;

        if (assigned)
            return Attack(assigned);
    }

    Unit* target = nullptr;
    if (Aq40BossHelper::IsEncounterTank(bot, bot))
        target = FindAssignedAq40TrashTankTarget(bot, botAI, controlTargets);

    if (!target)
        target = Aq40BossActions::FindTrashTarget(botAI, activeUnits);

    // Only call Attack() when we actually need to switch targets.  Requiring
    // GetVictim() == target caused Attack() to re-fire every tick for melee
    // bots that hadn't reached the mob yet — consuming the engine's single
    // action slot and permanently blocking reach-melee from running.
    if (!target || AI_VALUE(Unit*, "current target") == target)
        return false;

    float desiredRange = (botAI->IsRanged(bot) || botAI->IsHeal(bot)) ? 24.0f : 4.0f;
    float engageSlack = (botAI->IsRanged(bot) || botAI->IsHeal(bot)) ? 4.0f : 2.0f;
    if (!bot->IsWithinLOSInMap(target) || bot->GetDistance2d(target) > (desiredRange + engageSlack))
        return MoveNear(target, desiredRange, MovementPriority::MOVEMENT_COMBAT);

    return Attack(target);
}

bool Aq40TrashChooseTargetAction::isUseful()
{
    GuidVector const& attackers = context->GetValue<GuidVector>("attackers")->Get();
    GuidVector activeUnits = Aq40BossHelper::GetActiveCombatUnits(botAI, attackers);
    if (activeUnits.empty())
        return false;

    Unit* currentTarget = AI_VALUE(Unit*, "current target");
    if (!currentTarget || !currentTarget->IsAlive())
        return true;

    if (!Aq40BossHelper::IsUnitNamedAny(botAI, currentTarget,
            { "anubisath warder", "anubisath defender", "obsidian eradicator", "obsidian nullifier",
              "vekniss stinger", "qiraji slayer", "qiraji champion", "qiraji mindslayer",
              "qiraji brainwasher", "qiraji battleguard", "anubisath sentinel", "qiraji lasher",
              "vekniss warrior", "vekniss guardian", "vekniss drone", "vekniss soldier",
              "vekniss wasp", "scarab", "qiraji scarab", "spitting scarab", "scorpion" }))
        return true;

    for (ObjectGuid const guid : activeUnits)
    {
        if (guid == currentTarget->GetGUID())
            return false;
    }

    return true;
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

    std::vector<Unit*> const castingTargets = FindCastingAq40TrashDangerUnits(botAI, encounterUnits);

    if (castingTargets.empty())
        return false;

    Unit* assigned = nullptr;
    if (Aq40BossHelper::IsEncounterTank(bot, bot))
    {
        Aq40TrashPackArchetype const archetype = DetectAq40TrashPackArchetype(botAI, encounterUnits);
        std::vector<Unit*> const controlTargets = GetSortedAq40TrashUnits(botAI, encounterUnits, true, archetype);
        Unit* tankAssignment = FindAssignedAq40TrashTankTarget(bot, botAI, controlTargets);
        if (tankAssignment &&
            std::find(castingTargets.begin(), castingTargets.end(), tankAssignment) != castingTargets.end())
            assigned = tankAssignment;
    }
    else
    {
        for (Unit* target : castingTargets)
        {
            if (Aq40BossHelper::IsUnitHeldByEncounterTank(bot, target))
            {
                assigned = target;
                break;
            }
        }

        if (!assigned)
        {
            std::vector<Unit*> const assistTargets = GetSortedAq40TrashUnits(botAI, encounterUnits, false);
            if (!HasAnyHeldAq40TrashTarget(bot, botAI, assistTargets))
                assigned = castingTargets.front();
        }
    }

    if (!assigned && Aq40BossHelper::IsEncounterTank(bot, bot))
        assigned = castingTargets.front();

    if (!assigned)
        return false;

    if (!assigned || (AI_VALUE(Unit*, "current target") == assigned && bot->GetVictim() == assigned))
        return false;

    return Attack(assigned);
}

bool Aq40TrashAvoidDangerousAoeAction::Execute(Event /*event*/)
{
    if (Aq40BossHelper::IsEncounterTank(bot, bot))
        return false;

    // Plague separation path — applies to all non-tank roles.
    // Only stop attacks and move when separation is actually needed;
    // ranged/healers keep casting/healing when already at safe distance.
    if (Aq40SpellIds::HasAnyAura(botAI, bot, { Aq40SpellIds::Aq40DefenderPlague }))
    {
        float separationNeeded = 0.0f;
        Unit* separationRisk = FindClosestAq40PlagueSeparationRisk(bot, botAI, separationNeeded);
        if (!separationRisk || separationNeeded <= 0.0f)
            return false;

        bot->AttackStop();
        bot->InterruptNonMeleeSpells(true);
        context->GetValue<Unit*>("current target")->Set(nullptr);
        bot->SetTarget(ObjectGuid::Empty);
        bot->SetSelection(ObjectGuid());

        return MoveAway(separationRisk, separationNeeded);
    }

    // Only ranged and healers reposition for trash AoE; melee stay on target.
    if (!PlayerbotAI::IsRanged(bot) && !botAI->IsHeal(bot))
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetActiveCombatUnits(botAI, context->GetValue<GuidVector>("attackers")->Get());
    Unit* danger = nullptr;
    float highestThreatGap = 0.0f;

    for (ObjectGuid const guid : encounterUnits)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit)
            continue;

        // Defender Thunderclap: 24y danger radius
        Spell* spell = unit->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        if (spell &&
            Aq40SpellIds::MatchesAnySpellId(spell->GetSpellInfo(), { Aq40SpellIds::Aq40DefenderThunderclap }))
        {
            float const gap = 24.0f - bot->GetDistance2d(unit);
            if (gap > highestThreatGap)
            {
                highestThreatGap = gap;
                danger = unit;
            }
        }

        // Mindslayer Mind Flay: 30y danger radius
        Spell* channel = unit->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
        if (channel &&
            Aq40SpellIds::MatchesAnySpellId(channel->GetSpellInfo(), { Aq40SpellIds::Aq40MindslayerMindFlay }))
        {
            float const gap = 30.0f - bot->GetDistance2d(unit);
            if (gap > highestThreatGap)
            {
                highestThreatGap = gap;
                danger = unit;
            }
        }
    }

    if (!danger || highestThreatGap <= 0.0f)
        return false;

    bot->AttackStop();
    bot->InterruptNonMeleeSpells(true);

    // Retreat toward the encounter tank instead of directly away from the mob,
    // preventing bots from running deeper into uncleared rooms.
    Aq40TankRetreatResult retreat = ComputeTankRetreatPosition(bot, danger, highestThreatGap + 2.0f);
    if (retreat.valid)
        return MoveTo(bot->GetMapId(), retreat.x, retreat.y, retreat.z,
                      false, false, false, true, MovementPriority::MOVEMENT_COMBAT);

    // No safe tank-relative position — hold current position rather than scattering.
    return false;
}

bool Aq40TrashAvoidDangerousAoeAction::isUseful()
{
    if (Aq40BossHelper::IsEncounterTank(bot, bot))
        return false;

    if (Aq40SpellIds::HasAnyAura(botAI, bot, { Aq40SpellIds::Aq40DefenderPlague }))
    {
        float separationNeeded = 0.0f;
        return FindClosestAq40PlagueSeparationRisk(bot, botAI, separationNeeded) != nullptr &&
               separationNeeded > 0.0f;
    }

    // Only ranged and healers reposition for trash AoE.
    if (!PlayerbotAI::IsRanged(bot) && !botAI->IsHeal(bot))
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetActiveCombatUnits(botAI, context->GetValue<GuidVector>("attackers")->Get());
    for (ObjectGuid const guid : encounterUnits)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit)
            continue;

        Spell* spell = unit->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        if (spell &&
            Aq40SpellIds::MatchesAnySpellId(spell->GetSpellInfo(), { Aq40SpellIds::Aq40DefenderThunderclap }) &&
            bot->GetDistance2d(unit) < 24.0f)
            return true;

        Spell* channel = unit->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
        if (channel &&
            Aq40SpellIds::MatchesAnySpellId(channel->GetSpellInfo(), { Aq40SpellIds::Aq40MindslayerMindFlay }) &&
            bot->GetDistance2d(unit) < 30.0f)
            return true;
    }

    return false;
}

bool Aq40TrashControlMindControlAction::Execute(Event /*event*/)
{
    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, context->GetValue<GuidVector>("attackers")->Get());

    if (Aq40BossHelper::TryCrowdControlCharmedPlayer(bot, botAI, encounterUnits))
        return true;

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
