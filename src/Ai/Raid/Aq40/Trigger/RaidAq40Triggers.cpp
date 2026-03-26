#include "RaidAq40Triggers.h"

#include <initializer_list>

#include "ObjectGuid.h"
#include "SharedDefines.h"
#include "Spell.h"
#include "../RaidAq40SpellIds.h"
#include "../Util/RaidAq40Helpers.h"

namespace
{
bool Aq40EncounterEngaged(PlayerbotAI* botAI, Player* bot)
{
    if (!Aq40BossHelper::IsInAq40(bot))
        return false;

    // Primary check: the bot's own attackers list.
    if (!botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get().empty())
        return true;

    // Fallback: a cluster of nearby group members is in combat, so the
    // encounter is active even though this bot briefly dropped threat.
    // Uses the same cluster check as shared state cleanup so both agree.
    return Aq40BossHelper::IsEncounterCombatActive(bot);
}

Unit* FindBurrowedOuro(PlayerbotAI* botAI, GuidVector const& attackers)
{
    Unit* ouro = Aq40BossHelper::FindUnitByAnyName(botAI, attackers, { "ouro" });
    if (!ouro || (ouro->GetUnitFlags() & UNIT_FLAG_NOT_SELECTABLE) != UNIT_FLAG_NOT_SELECTABLE)
        return nullptr;

    return ouro;
}

Unit* FindSelectableCthunBody(PlayerbotAI* botAI, GuidVector const& attackers)
{
    Unit* cthun = Aq40BossHelper::FindUnitByAnyName(botAI, attackers, { "c'thun" });
    if (!cthun || (cthun->GetUnitFlags() & UNIT_FLAG_NOT_SELECTABLE) == UNIT_FLAG_NOT_SELECTABLE)
        return nullptr;

    return cthun;
}

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

Unit* FindTwinSideBugTarget(PlayerbotAI* botAI, GuidVector const& encounterUnits,
                            Aq40Helpers::TwinAssignments const& assignment)
{
    Unit* sideBug = nullptr;
    for (ObjectGuid const guid : encounterUnits)
    {
        Unit* bug = botAI->GetUnit(guid);
        if (!bug || !bug->IsAlive() ||
            !Aq40BossHelper::IsUnitNamedAny(botAI, bug, { "mutate bug", "qiraji scarab", "qiraji scorpion", "scarab", "scorpion" }) ||
            !Aq40Helpers::IsLikelyOnSameTwinSide(bug, assignment.sideEmperor, assignment.oppositeEmperor))
            continue;

        bool const isMutateBug = botAI->EqualLowercaseName(bug->GetName(), "mutate bug");
        if (!sideBug)
        {
            sideBug = bug;
            continue;
        }

        bool const chosenIsMutate = botAI->EqualLowercaseName(sideBug->GetName(), "mutate bug");
        if (isMutateBug != chosenIsMutate)
        {
            if (isMutateBug)
                sideBug = bug;
            continue;
        }

        if (bug->GetHealthPct() < sideBug->GetHealthPct())
            sideBug = bug;
    }

    return sideBug;
}
}    // namespace

bool Aq40BotIsNotInCombatTrigger::IsActive()
{
    return !bot->IsInCombat() && !Aq40BossHelper::IsEncounterCombatActive(bot);
}

bool Aq40ResistanceStrategyTrigger::IsActive()
{
    return true;
}

bool Aq40SkeramActiveTrigger::IsActive()
{
    if (!Aq40EncounterEngaged(botAI, bot))
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetActiveCombatUnits(botAI, AI_VALUE(GuidVector, "attackers"));
    for (ObjectGuid const guid : encounterUnits)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (Aq40BossHelper::IsUnitNamedAny(botAI, unit, { "the prophet skeram" }))
            return true;
    }

    return false;
}

bool Aq40SkeramBlinkTrigger::IsActive()
{
    Unit* currentTarget = AI_VALUE(Unit*, "current target");
    if (!currentTarget || !botAI->EqualLowercaseName(currentTarget->GetName(), "the prophet skeram"))
        return false;

    return !AI_VALUE2(bool, "has aggro", "current target");
}

bool Aq40SkeramArcaneExplosionTrigger::IsActive()
{
    if (!Aq40SkeramActiveTrigger(botAI).IsActive())
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, AI_VALUE(GuidVector, "attackers"));
    for (ObjectGuid const guid : encounterUnits)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit || !botAI->EqualLowercaseName(unit->GetName(), "the prophet skeram"))
            continue;

        Spell* spell = unit->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        if (spell &&
            Aq40SpellIds::MatchesAnySpellId(spell->GetSpellInfo(), { Aq40SpellIds::SkeramArcaneExplosion }))
            return true;
    }

    return false;
}

bool Aq40SkeramMindControlTrigger::IsActive()
{
    if (!Aq40SkeramActiveTrigger(botAI).IsActive())
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, AI_VALUE(GuidVector, "attackers"));
    for (ObjectGuid const guid : encounterUnits)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit)
            continue;

    // "True Fulfillment" can force players/bots into hostile behavior.
        if (unit->IsPlayer() &&
            (unit->IsCharmed() ||
             Aq40SpellIds::HasAnyAura(botAI, unit, { Aq40SpellIds::SkeramTrueFulfillment }) ||
             botAI->HasAura("true fulfillment", unit)))
            return true;
    }

    return false;
}

bool Aq40SkeramExecutePhaseTrigger::IsActive()
{
    if (!Aq40SkeramActiveTrigger(botAI).IsActive())
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, AI_VALUE(GuidVector, "attackers"));
    for (ObjectGuid const guid : encounterUnits)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (Aq40BossHelper::IsUnitNamedAny(botAI, unit, { "the prophet skeram" }) && unit->GetHealthPct() <= 25.0f)
            return true;
    }

    return false;
}

bool Aq40SarturaActiveTrigger::IsActive()
{
    if (!Aq40EncounterEngaged(botAI, bot))
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetActiveCombatUnits(botAI, AI_VALUE(GuidVector, "attackers"));
    for (ObjectGuid const guid : encounterUnits)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit)
            continue;

        if (botAI->EqualLowercaseName(unit->GetName(), "battleguard sartura") ||
            botAI->EqualLowercaseName(unit->GetName(), "sartura's royal guard"))
            return true;
    }

    return false;
}

bool Aq40SarturaWhirlwindTrigger::IsActive()
{
    if (!Aq40SarturaActiveTrigger(botAI).IsActive() || Aq40BossHelper::IsEncounterTank(bot, bot))
        return false;

    bool const isBackline = botAI->IsRanged(bot) || botAI->IsHeal(bot);
    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, AI_VALUE(GuidVector, "attackers"));
    for (ObjectGuid const guid : encounterUnits)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!IsSarturaSpinning(botAI, unit))
            continue;

        float const distance = bot->GetDistance2d(unit);
        bool const isClosingOnBot = unit->GetVictim() == bot || unit->GetTarget() == bot->GetGUID();
        if (distance <= 18.0f || (isBackline && isClosingOnBot && distance <= 24.0f))
            return true;
    }

    return false;
}

bool Aq40BugTrioActiveTrigger::IsActive()
{
    if (!Aq40EncounterEngaged(botAI, bot))
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetActiveCombatUnits(botAI, AI_VALUE(GuidVector, "attackers"));
    return Aq40BossHelper::HasAnyNamedUnit(botAI, encounterUnits, { "princess yauj", "vem", "lord kri", "yauj brood" });
}

bool Aq40BugTrioHealCastTrigger::IsActive()
{
    if (!Aq40BugTrioActiveTrigger(botAI).IsActive())
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, AI_VALUE(GuidVector, "attackers"));
    Unit* yauj = Aq40BossHelper::FindUnitByAnyName(botAI, encounterUnits, { "princess yauj" });
    if (!yauj)
        return false;

    Spell* spell = yauj->GetCurrentSpell(CURRENT_GENERIC_SPELL);
    return spell && Aq40SpellIds::MatchesAnySpellId(spell->GetSpellInfo(), { Aq40SpellIds::BugTrioYaujHeal });
}

bool Aq40BugTrioFearTrigger::IsActive()
{
    if (!Aq40BugTrioActiveTrigger(botAI).IsActive())
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, AI_VALUE(GuidVector, "attackers"));
    Unit* yauj = Aq40BossHelper::FindUnitByAnyName(botAI, encounterUnits, { "princess yauj" });
    if (!yauj)
        return false;

    Spell* spell = yauj->GetCurrentSpell(CURRENT_GENERIC_SPELL);
    if (spell && Aq40SpellIds::MatchesAnySpellId(spell->GetSpellInfo(), { Aq40SpellIds::BugTrioYaujFear }))
        return true;

    Group const* group = bot->GetGroup();
    if (!group)
        return false;

    for (GroupReference const* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || !member->IsAlive() || !Aq40BossHelper::IsSameInstance(bot, member))
            continue;

        if (bot->GetDistance2d(member) > 30.0f)
            continue;

        if (Aq40SpellIds::HasAnyAura(botAI, member, { Aq40SpellIds::BugTrioYaujFear }) || member->HasFearAura())
            return true;
    }

    return false;
}

bool Aq40BugTrioPoisonCloudTrigger::IsActive()
{
    if (!Aq40BugTrioActiveTrigger(botAI).IsActive() || Aq40BossHelper::IsEncounterTank(bot, bot))
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, AI_VALUE(GuidVector, "attackers"));
    Unit* kri = Aq40BossHelper::FindUnitByAnyName(botAI, encounterUnits, { "lord kri" });
    if (!kri)
        return false;

    bool poisonCloudWindow = kri->GetHealthPct() <= 5.0f ||
                             Aq40SpellIds::HasAnyAura(botAI, kri, { Aq40SpellIds::BugTrioPoisonCloud });
    return poisonCloudWindow && bot->GetDistance2d(kri) <= 12.0f;
}

bool Aq40FankrissActiveTrigger::IsActive()
{
    if (!Aq40EncounterEngaged(botAI, bot))
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetActiveCombatUnits(botAI, AI_VALUE(GuidVector, "attackers"));
    for (ObjectGuid const guid : encounterUnits)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (unit && botAI->EqualLowercaseName(unit->GetName(), "fankriss the unyielding"))
            return true;
    }

    return false;
}

bool Aq40FankrissSpawnedTrigger::IsActive()
{
    if (!Aq40FankrissActiveTrigger(botAI).IsActive())
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, AI_VALUE(GuidVector, "attackers"));
    for (ObjectGuid const guid : encounterUnits)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (unit && botAI->EqualLowercaseName(unit->GetName(), "spawn of fankriss"))
            return true;
    }

    return false;
}

bool Aq40TrashActiveTrigger::IsActive()
{
    if (!Aq40EncounterEngaged(botAI, bot))
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetActiveCombatUnits(botAI, AI_VALUE(GuidVector, "attackers"));
    if (encounterUnits.empty() || Aq40BossHelper::IsBossEncounterActive(botAI, encounterUnits))
        return false;

    return Aq40BossHelper::IsTrashEncounterActive(botAI, encounterUnits);
}

bool Aq40TrashDangerousAoeTrigger::IsActive()
{
    if (!Aq40TrashActiveTrigger(botAI).IsActive() || Aq40BossHelper::IsEncounterTank(bot, bot))
        return false;

    if (Aq40SpellIds::HasAnyAura(botAI, bot, { Aq40SpellIds::Aq40DefenderPlague }))
        return true;

    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, AI_VALUE(GuidVector, "attackers"));
    for (ObjectGuid const guid : encounterUnits)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit)
            continue;

        Spell* spell = unit->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        if (!spell)
            continue;

        if (Aq40SpellIds::MatchesAnySpellId(spell->GetSpellInfo(), { Aq40SpellIds::Aq40EradicatorShockBlast }) &&
            bot->GetDistance2d(unit) <= 20.0f)
            return true;

        if (Aq40SpellIds::MatchesAnySpellId(spell->GetSpellInfo(),
                { Aq40SpellIds::Aq40WarderFireNova, Aq40SpellIds::Aq40DefenderThunderclap }) &&
            bot->GetDistance2d(unit) <= 24.0f)
            return true;
    }

    return false;
}

bool Aq40TrashMindslayerCastTrigger::IsActive()
{
    if (!Aq40TrashActiveTrigger(botAI).IsActive())
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, AI_VALUE(GuidVector, "attackers"));
    for (ObjectGuid const guid : encounterUnits)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit)
            continue;

        if (botAI->EqualLowercaseName(unit->GetName(), "qiraji mindslayer"))
        {
            Spell* spell = unit->GetCurrentSpell(CURRENT_GENERIC_SPELL);
            if (spell && Aq40SpellIds::MatchesAnySpellId(spell->GetSpellInfo(), { Aq40SpellIds::Aq40MindslayerMindBlast }))
                return true;

            Spell* channel = unit->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
            if (channel && Aq40SpellIds::MatchesAnySpellId(channel->GetSpellInfo(), { Aq40SpellIds::Aq40MindslayerMindFlay }))
                return true;
        }

        if (botAI->EqualLowercaseName(unit->GetName(), "obsidian nullifier"))
        {
            Spell* spell = unit->GetCurrentSpell(CURRENT_GENERIC_SPELL);
            if (spell && Aq40SpellIds::MatchesAnySpellId(spell->GetSpellInfo(), { Aq40SpellIds::Aq40NullifierNullify }))
                return true;
        }
    }

    return false;
}

bool Aq40TrashMindControlTrigger::IsActive()
{
    if (!Aq40TrashActiveTrigger(botAI).IsActive())
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, AI_VALUE(GuidVector, "attackers"));
    for (ObjectGuid const guid : encounterUnits)
    {
        Unit* unit = botAI->GetUnit(guid);
        Player* player = unit ? unit->ToPlayer() : nullptr;
        if (!player || !player->IsAlive() || player == bot)
            continue;

        if (player->IsCharmed() && !player->IsPolymorphed())
            return true;
    }

    return false;
}

bool Aq40TrashSlayerEnrageTrigger::IsActive()
{
    if (!Aq40TrashActiveTrigger(botAI).IsActive())
        return false;

    if (bot->getClass() != CLASS_HUNTER)
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, AI_VALUE(GuidVector, "attackers"));
    for (ObjectGuid const guid : encounterUnits)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit || !botAI->EqualLowercaseName(unit->GetName(), "qiraji slayer"))
            continue;

        if (botAI->HasAura(Aq40SpellIds::Aq40SlayerEnrage, unit))
            return true;
    }

    return false;
}

bool Aq40TrashChampionFearTrigger::IsActive()
{
    if (!Aq40TrashActiveTrigger(botAI).IsActive())
        return false;

    if (bot->getClass() != CLASS_SHAMAN && bot->getClass() != CLASS_PRIEST)
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, AI_VALUE(GuidVector, "attackers"));

    // Trigger if a Champion is casting Frightening Shout
    for (ObjectGuid const guid : encounterUnits)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit || !botAI->EqualLowercaseName(unit->GetName(), "qiraji champion"))
            continue;

        Spell* spell = unit->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        if (spell && Aq40SpellIds::MatchesAnySpellId(spell->GetSpellInfo(), { Aq40SpellIds::Aq40ChampionFrighteningShout }))
            return true;
    }

    // Also trigger if nearby group members are feared
    Group const* group = bot->GetGroup();
    if (!group)
        return false;

    for (GroupReference const* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || !member->IsAlive() || !Aq40BossHelper::IsSameInstance(bot, member))
            continue;

        if (bot->GetDistance2d(member) > 30.0f)
            continue;

        if (Aq40SpellIds::HasAnyAura(botAI, member, { Aq40SpellIds::Aq40ChampionFrighteningShout }) || member->HasFearAura())
            return true;
    }

    return false;
}

bool Aq40TrashChampionVengeanceTrigger::IsActive()
{
    if (!Aq40TrashActiveTrigger(botAI).IsActive())
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, AI_VALUE(GuidVector, "attackers"));
    for (ObjectGuid const guid : encounterUnits)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit || !botAI->EqualLowercaseName(unit->GetName(), "qiraji champion"))
            continue;

        if (botAI->HasAura(Aq40SpellIds::Aq40ChampionVengeance, unit))
            return true;
    }

    return false;
}

bool Aq40HuhuranActiveTrigger::IsActive()
{
    if (!Aq40EncounterEngaged(botAI, bot))
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetActiveCombatUnits(botAI, AI_VALUE(GuidVector, "attackers"));
    for (ObjectGuid const guid : encounterUnits)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (unit && botAI->EqualLowercaseName(unit->GetName(), "princess huhuran"))
            return true;
    }

    return false;
}

bool Aq40HuhuranPoisonPhaseTrigger::IsActive()
{
    if (!Aq40HuhuranActiveTrigger(botAI).IsActive())
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, AI_VALUE(GuidVector, "attackers"));
    for (ObjectGuid const guid : encounterUnits)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit || !botAI->EqualLowercaseName(unit->GetName(), "princess huhuran"))
            continue;

    // Phase transition baseline:
    // spread ranged during the dangerous poison volley/enrage window.
        if (unit->GetHealthPct() <= 32.0f)
            return true;

        if (Aq40SpellIds::HasAnyAura(botAI, unit, { Aq40SpellIds::HuhuranFrenzy }))
            return true;
    }

    return false;
}

bool Aq40TwinEmperorsActiveTrigger::IsActive()
{
    if (!Aq40EncounterEngaged(botAI, bot))
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, AI_VALUE(GuidVector, "attackers"));
    return Aq40BossHelper::HasAnyNamedUnit(botAI, encounterUnits, { "emperor vek'nilash", "emperor vek'lor" });
}

bool Aq40TwinEmperorsRoleMismatchTrigger::IsActive()
{
    if (!Aq40TwinEmperorsActiveTrigger(botAI).IsActive())
        return false;

    if (botAI->IsHeal(bot))
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, AI_VALUE(GuidVector, "attackers"));
    Aq40Helpers::TwinAssignments assignment = Aq40Helpers::GetTwinAssignments(bot, botAI, encounterUnits);
    if (!assignment.sideEmperor)
        return false;

    bool const isWarlockTank = Aq40BossHelper::IsDesignatedTwinWarlockTank(bot);
    bool const isMeleeTank = PlayerbotAI::IsTank(bot) && !PlayerbotAI::IsRanged(bot);
    bool const inRecoveryWindow = Aq40Helpers::IsTwinTeleportRecoveryWindow(bot, botAI, encounterUnits);
    Unit* currentTarget = AI_VALUE(Unit*, "current target");
    if (!currentTarget)
    {
        if (!isWarlockTank && !isMeleeTank &&
            inRecoveryWindow && !Aq40Helpers::IsTwinAssignedTankReady(bot, botAI, assignment))
            return false;

        return true;
    }

    if (isWarlockTank)
        return assignment.sideEmperor == assignment.veklor ? currentTarget != assignment.veklor : currentTarget != nullptr;

    if (isMeleeTank)
        return assignment.sideEmperor == assignment.veknilash ? currentTarget != assignment.veknilash : currentTarget != nullptr;

    if (inRecoveryWindow && !Aq40Helpers::IsTwinAssignedTankReady(bot, botAI, assignment))
    {
        Unit* sideBug = FindTwinSideBugTarget(botAI, encounterUnits, assignment);
        if (!sideBug)
            return false;

        return currentTarget != sideBug;
    }

    Unit* sideBug = FindTwinSideBugTarget(botAI, encounterUnits, assignment);

    if (botAI->IsRanged(bot))
    {
        if (assignment.sideEmperor != assignment.veklor)
            return currentTarget != nullptr;

        if (sideBug)
            return currentTarget != sideBug;

        return currentTarget != assignment.veklor;
    }

    Unit* mutateBug = Aq40BossHelper::FindUnitByAnyName(botAI, encounterUnits, { "mutate bug" });
    if (assignment.sideEmperor == assignment.veknilash && mutateBug &&
        Aq40Helpers::IsLikelyOnSameTwinSide(mutateBug, assignment.sideEmperor, assignment.oppositeEmperor))
        return currentTarget != mutateBug;

    return assignment.sideEmperor == assignment.veknilash ? currentTarget != assignment.veknilash : currentTarget != nullptr;
}

bool Aq40TwinEmperorsPreTeleportTrigger::IsActive()
{
    if (!Aq40TwinEmperorsActiveTrigger(botAI).IsActive())
        return false;

    if (Aq40BossHelper::IsDesignatedTwinWarlockTank(bot))
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, AI_VALUE(GuidVector, "attackers"));
    return Aq40Helpers::IsTwinPreTeleportWindow(bot, botAI, encounterUnits);
}

bool Aq40TwinEmperorsArcaneBurstRiskTrigger::IsActive()
{
    if (!Aq40TwinEmperorsActiveTrigger(botAI).IsActive())
        return false;

    if (Aq40BossHelper::IsDesignatedTwinWarlockTank(bot))
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, AI_VALUE(GuidVector, "attackers"));
    for (ObjectGuid const guid : encounterUnits)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit || !botAI->EqualLowercaseName(unit->GetName(), "emperor vek'lor"))
            continue;

        Spell* spell = unit->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        bool castingArcaneBurst =
            spell && Aq40SpellIds::MatchesAnySpellId(spell->GetSpellInfo(), { Aq40SpellIds::TwinArcaneBurst });
        if (bot->GetDistance2d(unit) <= 18.0f || (castingArcaneBurst && bot->GetDistance2d(unit) <= 22.0f))
            return true;
    }

    return false;
}

bool Aq40TwinEmperorsHasOppositeAggroTrigger::IsActive()
{
    if (!Aq40TwinEmperorsActiveTrigger(botAI).IsActive())
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, AI_VALUE(GuidVector, "attackers"));
    Aq40Helpers::TwinAssignments assignment = Aq40Helpers::GetTwinAssignments(bot, botAI, encounterUnits);
    if (!assignment.veklor || !assignment.veknilash || !assignment.sideEmperor || !assignment.oppositeEmperor)
        return false;

    ObjectGuid const botGuid = bot->GetGUID();
    ObjectGuid petGuid = ObjectGuid::Empty;
    if (Pet* pet = bot->GetPet())
        petGuid = pet->GetGUID();

    Unit* sideBoss = assignment.sideEmperor;
    Unit* oppositeBoss = assignment.oppositeEmperor;
    if (!sideBoss || !oppositeBoss)
        return false;

    bool const hasSideAggro = sideBoss->GetTarget() == botGuid || (petGuid && sideBoss->GetTarget() == petGuid);
    bool const hasOppositeAggro = oppositeBoss->GetTarget() == botGuid || (petGuid && oppositeBoss->GetTarget() == petGuid);

    // This trigger is only for cross-room aggro mistakes. Holding threat on the
    // assigned boss is the expected steady-state and must not trigger the
    // emergency separation action.
    return (hasOppositeAggro && bot->GetDistance2d(sideBoss) < 90.0f) ||
           (hasSideAggro && bot->GetDistance2d(oppositeBoss) < 20.0f);
}

bool Aq40TwinEmperorsBlizzardRiskTrigger::IsActive()
{
    if (!Aq40TwinEmperorsActiveTrigger(botAI).IsActive())
        return false;

    if ((!botAI->IsRanged(bot) && !botAI->IsHeal(bot)) || Aq40BossHelper::IsDesignatedTwinWarlockTank(bot))
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, AI_VALUE(GuidVector, "attackers"));
    Aq40Helpers::TwinAssignments assignment = Aq40Helpers::GetTwinAssignments(bot, botAI, encounterUnits);
    if (!assignment.veklor || assignment.sideEmperor != assignment.veklor)
        return false;

    if (Aq40SpellIds::HasAnyAura(botAI, bot, { Aq40SpellIds::TwinBlizzard }))
        return true;

    Spell* spell = assignment.veklor->GetCurrentSpell(CURRENT_GENERIC_SPELL);
    return spell &&
           Aq40SpellIds::MatchesAnySpellId(spell->GetSpellInfo(), { Aq40SpellIds::TwinBlizzard }) &&
           bot->GetDistance2d(assignment.veklor) <= 36.0f;
}

bool Aq40TwinEmperorsHealBrotherTrigger::IsActive()
{
    if (!Aq40TwinEmperorsActiveTrigger(botAI).IsActive())
        return false;

    bool const isWarlockTank = Aq40BossHelper::IsDesignatedTwinWarlockTank(bot);
    bool const isMeleeTank = PlayerbotAI::IsTank(bot) && !PlayerbotAI::IsRanged(bot);
    if (!isWarlockTank && !isMeleeTank)
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, AI_VALUE(GuidVector, "attackers"));
    Aq40Helpers::TwinAssignments assignment = Aq40Helpers::GetTwinAssignments(bot, botAI, encounterUnits);
    if (!assignment.veklor || !assignment.veknilash)
        return false;

    Spell* veklorSpell = assignment.veklor->GetCurrentSpell(CURRENT_GENERIC_SPELL);
    Spell* veknilashSpell = assignment.veknilash->GetCurrentSpell(CURRENT_GENERIC_SPELL);
    bool const healBrotherCast =
        (veklorSpell && Aq40SpellIds::MatchesAnySpellId(veklorSpell->GetSpellInfo(), { Aq40SpellIds::TwinHealBrother })) ||
        (veknilashSpell && Aq40SpellIds::MatchesAnySpellId(veknilashSpell->GetSpellInfo(), { Aq40SpellIds::TwinHealBrother }));
    if (healBrotherCast)
        return true;

    return assignment.veklor->GetDistance2d(assignment.veknilash) < 60.0f;
}

bool Aq40TwinEmperorsNeedSeparationTrigger::IsActive()
{
    if (!Aq40TwinEmperorsActiveTrigger(botAI).IsActive())
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, AI_VALUE(GuidVector, "attackers"));
    Unit* veklor = nullptr;
    Unit* veknilash = nullptr;
    for (ObjectGuid const guid : encounterUnits)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit)
            continue;

        if (botAI->EqualLowercaseName(unit->GetName(), "emperor vek'lor"))
            veklor = unit;
        else if (botAI->EqualLowercaseName(unit->GetName(), "emperor vek'nilash"))
            veknilash = unit;
    }

    if (!veklor || !veknilash)
        return false;

    return veklor->GetDistance2d(veknilash) < 60.0f;
}

bool Aq40OuroActiveTrigger::IsActive()
{
    if (!Aq40EncounterEngaged(botAI, bot))
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetActiveCombatUnits(botAI, AI_VALUE(GuidVector, "attackers"));
    return Aq40BossHelper::HasAnyNamedUnit(botAI, encounterUnits, { "ouro", "qiraji scarab", "scarab" });
}

bool Aq40OuroScarabsTrigger::IsActive()
{
    if (!Aq40OuroActiveTrigger(botAI).IsActive())
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, AI_VALUE(GuidVector, "attackers"));
    return Aq40BossHelper::HasAnyNamedUnit(botAI, encounterUnits, { "qiraji scarab", "scarab" });
}

bool Aq40OuroSweepTrigger::IsActive()
{
    if (!Aq40OuroActiveTrigger(botAI).IsActive() || Aq40BossHelper::IsEncounterTank(bot, bot))
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, AI_VALUE(GuidVector, "attackers"));
    for (ObjectGuid const guid : encounterUnits)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit || !botAI->EqualLowercaseName(unit->GetName(), "ouro"))
            continue;

    // Detect actual Sweep cast or aura, matching the pattern used by
    // Sartura whirlwind detection (spell + aura + name fallback).
        Spell* spell = unit->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        bool const sweeping =
            (spell && Aq40SpellIds::MatchesAnySpellId(spell->GetSpellInfo(), { Aq40SpellIds::OuroSweep })) ||
            Aq40SpellIds::HasAnyAura(botAI, unit, { Aq40SpellIds::OuroSweep }) ||
            botAI->HasAura("sweep", unit);
        if (!sweeping)
            continue;

        if (bot->GetDistance2d(unit) <= 10.0f)
            return true;
    }

    return false;
}

bool Aq40OuroSandBlastRiskTrigger::IsActive()
{
    if (!Aq40OuroActiveTrigger(botAI).IsActive() || Aq40BossHelper::IsEncounterTank(bot, bot))
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, AI_VALUE(GuidVector, "attackers"));
    for (ObjectGuid const guid : encounterUnits)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit || !botAI->EqualLowercaseName(unit->GetName(), "ouro"))
            continue;

    // Non-tanks in Ouro's frontal arc are at Sand Blast risk
    // (pattern from ICC Marrowgar: boss->isInFront(bot)).
        if (unit->isInFront(bot, 10.0f) && bot->GetDistance2d(unit) <= 15.0f)
            return true;
    }

    return false;
}

bool Aq40OuroSubmergeTrigger::IsActive()
{
    if (!Aq40EncounterEngaged(botAI, bot))
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, AI_VALUE(GuidVector, "attackers"));
    if (Unit* ouro = FindBurrowedOuro(botAI, encounterUnits))
        return bot->GetDistance2d(ouro) <= 16.0f;

    for (ObjectGuid const guid : encounterUnits)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit || !botAI->EqualLowercaseName(unit->GetName(), "dirt mound"))
            continue;

        if (bot->GetDistance2d(unit) <= 16.0f)
            return true;
    }

    return false;
}

bool Aq40ViscidusActiveTrigger::IsActive()
{
    if (!Aq40EncounterEngaged(botAI, bot))
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetActiveCombatUnits(botAI, AI_VALUE(GuidVector, "attackers"));
    return Aq40BossHelper::HasAnyNamedUnit(botAI, encounterUnits, { "viscidus", "glob of viscidus", "toxic slime" });
}

bool Aq40ViscidusFrozenTrigger::IsActive()
{
    if (!Aq40ViscidusActiveTrigger(botAI).IsActive())
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, AI_VALUE(GuidVector, "attackers"));
    for (ObjectGuid const guid : encounterUnits)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit || !botAI->EqualLowercaseName(unit->GetName(), "viscidus"))
            continue;

        if (Aq40SpellIds::HasAnyAura(botAI, unit,
                { Aq40SpellIds::ViscidusFreeze }))
            return true;
    }

    return false;
}

bool Aq40ViscidusGlobTrigger::IsActive()
{
    if (!Aq40ViscidusActiveTrigger(botAI).IsActive())
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, AI_VALUE(GuidVector, "attackers"));
    return Aq40BossHelper::HasAnyNamedUnit(botAI, encounterUnits, { "glob of viscidus" });
}

bool Aq40CthunActiveTrigger::IsActive()
{
    if (!Aq40EncounterEngaged(botAI, bot))
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetActiveCombatUnits(botAI, AI_VALUE(GuidVector, "attackers"));
    return Aq40BossHelper::HasAnyNamedUnit(botAI, encounterUnits,
                                           { "c'thun", "eye of c'thun", "eye tentacle", "claw tentacle",
                                             "giant eye tentacle", "giant claw tentacle", "flesh tentacle" });
}

bool Aq40CthunPhase2Trigger::IsActive()
{
    if (!Aq40CthunActiveTrigger(botAI).IsActive())
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, AI_VALUE(GuidVector, "attackers"));
    if (FindSelectableCthunBody(botAI, encounterUnits))
        return true;

    return Aq40BossHelper::HasAnyNamedUnit(botAI, encounterUnits,
                                           { "giant eye tentacle", "giant claw tentacle", "flesh tentacle" });
}

bool Aq40CthunAddsPresentTrigger::IsActive()
{
    if (!Aq40CthunActiveTrigger(botAI).IsActive())
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, AI_VALUE(GuidVector, "attackers"));
    return Aq40BossHelper::HasAnyNamedUnit(botAI, encounterUnits,
                                           { "eye tentacle", "claw tentacle", "giant eye tentacle", "giant claw tentacle",
                                             "flesh tentacle" });
}

bool Aq40CthunDarkGlareTrigger::IsActive()
{
    if (!Aq40CthunActiveTrigger(botAI).IsActive())
        return false;

    if (Aq40CthunInStomachTrigger(botAI).IsActive())
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, AI_VALUE(GuidVector, "attackers"));
    for (ObjectGuid const guid : encounterUnits)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit)
            continue;

        if (!botAI->EqualLowercaseName(unit->GetName(), "eye of c'thun"))
            continue;

        Spell* spell = unit->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        bool castingDarkGlare =
            spell && Aq40SpellIds::MatchesAnySpellId(spell->GetSpellInfo(), { Aq40SpellIds::CthunDarkGlare });
        bool hasDarkGlare = Aq40SpellIds::HasAnyAura(botAI, unit, { Aq40SpellIds::CthunDarkGlare }) ||
                            botAI->HasAura("dark glare", unit);
        if (castingDarkGlare || hasDarkGlare)
            return true;
    }

    return false;
}

bool Aq40CthunInStomachTrigger::IsActive()
{
    return Aq40Helpers::IsCthunInStomach(bot, botAI);
}

bool Aq40CthunVulnerableTrigger::IsActive()
{
    if (!Aq40CthunPhase2Trigger(botAI).IsActive())
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, AI_VALUE(GuidVector, "attackers"));
    return Aq40Helpers::IsCthunVulnerableNow(botAI, encounterUnits);
}

bool Aq40CthunEyeCastTrigger::IsActive()
{
    if (!Aq40CthunActiveTrigger(botAI).IsActive() || Aq40CthunInStomachTrigger(botAI).IsActive())
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, AI_VALUE(GuidVector, "attackers"));
    for (ObjectGuid const guid : encounterUnits)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit)
            continue;

        bool isEyeTentacle = botAI->EqualLowercaseName(unit->GetName(), "eye tentacle") ||
                             botAI->EqualLowercaseName(unit->GetName(), "giant eye tentacle");
        Spell* spell = unit->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
        bool eyeCast = spell && Aq40SpellIds::MatchesAnySpellId(spell->GetSpellInfo(), { Aq40SpellIds::CthunMindFlay });
        if (isEyeTentacle && eyeCast)
            return true;
    }

    return false;
}
