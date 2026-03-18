#include "RaidAq40Triggers.h"

#include <initializer_list>

#include "ObjectGuid.h"
#include "Spell.h"
#include "../RaidAq40SpellIds.h"
#include "../Util/RaidAq40Helpers.h"

namespace
{
bool Aq40EncounterEngaged(PlayerbotAI* botAI, Player* bot)
{
    return Aq40BossHelper::IsInAq40(bot) &&
           !botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get().empty();
}
}  // namespace

bool Aq40EngageTrigger::IsActive()
{
    return Aq40EncounterEngaged(botAI, bot);
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

bool Aq40SkeramSplitTrigger::IsActive()
{
    if (!Aq40SkeramActiveTrigger(botAI).IsActive())
        return false;

    uint32 skeramCount = 0;
    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, AI_VALUE(GuidVector, "attackers"));
    for (ObjectGuid const guid : encounterUnits)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (Aq40BossHelper::IsUnitNamedAny(botAI, unit, { "the prophet skeram" }))
            ++skeramCount;
    }

    return skeramCount >= 2;
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

    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, AI_VALUE(GuidVector, "attackers"));
    for (ObjectGuid const guid : encounterUnits)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit)
            continue;

        bool isSarturaMob = botAI->EqualLowercaseName(unit->GetName(), "battleguard sartura") ||
                            botAI->EqualLowercaseName(unit->GetName(), "sartura's royal guard");
        if (!isSarturaMob)
            continue;

        // Use broad detection to avoid core-script dependencies:
        // either active cast window or whirlwind aura.
        Spell* spell = unit->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        bool const spinning =
            (spell && Aq40SpellIds::MatchesAnySpellId(spell->GetSpellInfo(),
                { Aq40SpellIds::SarturaWhirlwind, Aq40SpellIds::SarturaGuardWhirlwind })) ||
            Aq40SpellIds::HasAnyAura(botAI, unit,
                                     { Aq40SpellIds::SarturaWhirlwind, Aq40SpellIds::SarturaGuardWhirlwind }) ||
            botAI->HasAura("whirlwind", unit);
        if (!spinning)
            continue;

        if (bot->GetDistance2d(unit) <= 14.0f)
            return true;
    }

    return false;
}

bool Aq40BugTrioActiveTrigger::IsActive()
{
    if (!Aq40EncounterEngaged(botAI, bot))
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetActiveCombatUnits(botAI, AI_VALUE(GuidVector, "attackers"));
    return Aq40BossHelper::HasAnyNamedUnit(botAI, encounterUnits, { "lord kri", "princess yauj", "vem", "yauj brood" });
}

bool Aq40BugTrioHealCastTrigger::IsActive()
{
    if (!Aq40BugTrioActiveTrigger(botAI).IsActive())
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, AI_VALUE(GuidVector, "attackers"));
    for (ObjectGuid const guid : encounterUnits)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit || !botAI->EqualLowercaseName(unit->GetName(), "princess yauj"))
            continue;

        Spell* spell = unit->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        if (!spell)
            continue;

        if (Aq40SpellIds::MatchesAnySpellId(spell->GetSpellInfo(), { Aq40SpellIds::BugTrioYaujHeal }))
            return true;
    }

    return false;
}

bool Aq40BugTrioPoisonCloudTrigger::IsActive()
{
    if (!Aq40BugTrioActiveTrigger(botAI).IsActive() || Aq40BossHelper::IsEncounterTank(bot, bot))
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, AI_VALUE(GuidVector, "attackers"));
    for (ObjectGuid const guid : encounterUnits)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit || !botAI->EqualLowercaseName(unit->GetName(), "lord kri"))
            continue;

        bool poisonCloudWindow = unit->GetHealthPct() <= 5.0f ||
                                 Aq40SpellIds::HasAnyAura(botAI, unit, { Aq40SpellIds::BugTrioPoisonCloud });
        if (poisonCloudWindow && bot->GetDistance2d(unit) <= 12.0f)
            return true;
    }

    return false;
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
            bot->GetDistance2d(unit) <= 14.0f)
            return true;

        if (Aq40SpellIds::MatchesAnySpellId(spell->GetSpellInfo(),
                { Aq40SpellIds::Aq40WarderFireNova, Aq40SpellIds::Aq40DefenderThunderclap }) &&
            bot->GetDistance2d(unit) <= 18.0f)
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

    GuidVector encounterUnits = Aq40BossHelper::GetActiveCombatUnits(botAI, AI_VALUE(GuidVector, "attackers"));
    for (ObjectGuid const guid : encounterUnits)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit)
            continue;

        if (botAI->EqualLowercaseName(unit->GetName(), "emperor vek'nilash") ||
            botAI->EqualLowercaseName(unit->GetName(), "emperor vek'lor"))
            return true;
    }

    return false;
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
        return assignment.sideEmperor == assignment.veklor && currentTarget != assignment.veklor;

    if (isMeleeTank)
        return assignment.sideEmperor == assignment.veknilash && currentTarget != assignment.veknilash;

    if (inRecoveryWindow && !Aq40Helpers::IsTwinAssignedTankReady(bot, botAI, assignment))
        return false;

    if (botAI->IsRanged(bot))
        return currentTarget != assignment.veklor;

    Unit* mutateBug = Aq40BossHelper::FindUnitByAnyName(botAI, encounterUnits, { "mutate bug" });
    if (mutateBug &&
        Aq40Helpers::IsLikelyOnSameTwinSide(mutateBug, assignment.sideEmperor, assignment.oppositeEmperor))
        return currentTarget != mutateBug;

    return currentTarget != assignment.veknilash;
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

    return veklor->GetDistance2d(veknilash) < 20.0f;
}

bool Aq40OuroActiveTrigger::IsActive()
{
    if (!Aq40EncounterEngaged(botAI, bot))
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetActiveCombatUnits(botAI, AI_VALUE(GuidVector, "attackers"));
    return Aq40BossHelper::HasAnyNamedUnit(botAI, encounterUnits, { "ouro", "dirt mound", "qiraji scarab", "scarab" });
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
    if (!Aq40OuroActiveTrigger(botAI).IsActive())
        return false;

    GuidVector encounterUnits = Aq40BossHelper::GetEncounterUnits(botAI, AI_VALUE(GuidVector, "attackers"));
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
                { Aq40SpellIds::ViscidusFreeze, Aq40SpellIds::ViscidusSlowedMore }))
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
    return Aq40BossHelper::HasAnyNamedUnit(botAI, encounterUnits,
                                           { "c'thun", "giant eye tentacle", "giant claw tentacle", "flesh tentacle" });
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
        Spell* spell = unit->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        bool eyeCast = spell && Aq40SpellIds::MatchesAnySpellId(spell->GetSpellInfo(), { Aq40SpellIds::CthunMindFlay });
        if (isEyeTentacle && eyeCast)
            return true;
    }

    return false;
}
