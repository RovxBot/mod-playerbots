#include "RaidAq40Triggers.h"

#include <initializer_list>

#include "ObjectGuid.h"
#include "RaidAq40SpellIds.h"

namespace
{
bool IsAnyNamedUnit(PlayerbotAI* botAI, GuidVector const& attackers, std::initializer_list<char const*> names)
{
    for (ObjectGuid const guid : attackers)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit)
            continue;

        for (char const* name : names)
        {
            if (botAI->EqualLowercaseName(unit->GetName(), name))
                return true;
        }
    }

    return false;
}

bool IsAq40BossEncounterActive(PlayerbotAI* botAI, GuidVector const& attackers)
{
    return IsAnyNamedUnit(botAI, attackers, { "the prophet skeram", "battleguard sartura", "sartura's royal guard",
                                               "lord kri", "princess yauj", "vem", "yauj brood",
                                               "fankriss the unyielding", "spawn of fankriss", "princess huhuran",
                                               "emperor vek'nilash", "emperor vek'lor", "ouro", "dirt mound",
                                               "viscidus", "glob of viscidus", "toxic slime", "c'thun",
                                               "eye of c'thun", "eye tentacle", "claw tentacle",
                                               "giant eye tentacle", "giant claw tentacle", "flesh tentacle" });
}

}  // namespace

bool Aq40EngageTrigger::IsActive()
{
    if (!Aq40BossHelper::IsInAq40(bot))
        return false;

    if (!bot->IsInCombat())
        return false;

    return !AI_VALUE(GuidVector, "attackers").empty();
}

bool Aq40SkeramActiveTrigger::IsActive()
{
    if (!Aq40BossHelper::IsInAq40(bot) || !bot->IsInCombat())
        return false;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    for (ObjectGuid const guid : attackers)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (unit && botAI->EqualLowercaseName(unit->GetName(), "the prophet skeram"))
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
    if (!Aq40SkeramActiveTrigger::IsActive())
        return false;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    for (ObjectGuid const guid : attackers)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit || !botAI->EqualLowercaseName(unit->GetName(), "the prophet skeram"))
            continue;

        Spell* spell = unit->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        if (spell &&
            Aq40SpellIds::MatchesAnySpellId(spell->GetSpellInfo(), { Aq40SpellIds::SkeramArcaneExplosion }))
            return true;

        if (spell)
            return true;
    }

    return false;
}

bool Aq40SkeramMindControlTrigger::IsActive()
{
    if (!Aq40SkeramActiveTrigger::IsActive())
        return false;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    for (ObjectGuid const guid : attackers)
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
    if (!Aq40SkeramActiveTrigger::IsActive())
        return false;

    uint32 skeramCount = 0;
    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    for (ObjectGuid const guid : attackers)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (unit && botAI->EqualLowercaseName(unit->GetName(), "the prophet skeram"))
            ++skeramCount;
    }

    return skeramCount >= 2;
}

bool Aq40SkeramExecutePhaseTrigger::IsActive()
{
    if (!Aq40SkeramActiveTrigger::IsActive())
        return false;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    for (ObjectGuid const guid : attackers)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (unit && botAI->EqualLowercaseName(unit->GetName(), "the prophet skeram") && unit->GetHealthPct() <= 25.0f)
            return true;
    }

    return false;
}

bool Aq40SarturaActiveTrigger::IsActive()
{
    if (!Aq40BossHelper::IsInAq40(bot) || !bot->IsInCombat())
        return false;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    for (ObjectGuid const guid : attackers)
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
    if (!Aq40SarturaActiveTrigger::IsActive() || botAI->IsTank(bot))
        return false;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    for (ObjectGuid const guid : attackers)
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
        bool spinning = unit->GetCurrentSpell(CURRENT_GENERIC_SPELL) ||
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
    if (!Aq40BossHelper::IsInAq40(bot) || !bot->IsInCombat())
        return false;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    return IsAnyNamedUnit(botAI, attackers, { "lord kri", "princess yauj", "vem", "yauj brood" });
}

bool Aq40BugTrioHealCastTrigger::IsActive()
{
    if (!Aq40BugTrioActiveTrigger::IsActive())
        return false;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    for (ObjectGuid const guid : attackers)
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
    if (!Aq40BugTrioActiveTrigger::IsActive() || botAI->IsTank(bot))
        return false;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    for (ObjectGuid const guid : attackers)
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
    if (!Aq40BossHelper::IsInAq40(bot) || !bot->IsInCombat())
        return false;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    for (ObjectGuid const guid : attackers)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (unit && botAI->EqualLowercaseName(unit->GetName(), "fankriss the unyielding"))
            return true;
    }

    return false;
}

bool Aq40FankrissSpawnedTrigger::IsActive()
{
    if (!Aq40FankrissActiveTrigger::IsActive())
        return false;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    for (ObjectGuid const guid : attackers)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (unit && botAI->EqualLowercaseName(unit->GetName(), "spawn of fankriss"))
            return true;
    }

    return false;
}

bool Aq40TrashActiveTrigger::IsActive()
{
    if (!Aq40BossHelper::IsInAq40(bot) || !bot->IsInCombat())
        return false;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    if (attackers.empty() || IsAq40BossEncounterActive(botAI, attackers))
        return false;

    return IsAnyNamedUnit(botAI, attackers,
                          { "anubisath warder", "anubisath defender", "obsidian eradicator", "obsidian nullifier",
                              "vekniss stinger", "qiraji slayer", "qiraji champion", "qiraji mindslayer",
                              "qiraji brainwasher", "qiraji battleguard", "anubisath sentinel", "qiraji lasher",
                              "vekniss warrior", "vekniss guardian", "vekniss drone", "vekniss soldier",
                              "vekniss wasp", "scarab", "qiraji scarab", "spitting scarab", "scorpion" });
}

bool Aq40TrashDangerousAoeTrigger::IsActive()
{
    if (!Aq40TrashActiveTrigger(botAI).IsActive() || botAI->IsTank(bot))
        return false;

    if (Aq40SpellIds::HasAnyAura(botAI, bot, { Aq40SpellIds::Aq40DefenderPlague }))
        return true;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    for (ObjectGuid const guid : attackers)
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
                { Aq40SpellIds::Aq40WarderFireNova, Aq40SpellIds::Aq40DefenderThunderclap,
                    Aq40SpellIds::Aq40DefenderShadowStorm }) &&
            bot->GetDistance2d(unit) <= 18.0f)
            return true;
    }

    return false;
}

bool Aq40HuhuranActiveTrigger::IsActive()
{
    if (!Aq40BossHelper::IsInAq40(bot) || !bot->IsInCombat())
        return false;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    for (ObjectGuid const guid : attackers)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (unit && botAI->EqualLowercaseName(unit->GetName(), "princess huhuran"))
            return true;
    }

    return false;
}

bool Aq40HuhuranPoisonPhaseTrigger::IsActive()
{
    if (!Aq40HuhuranActiveTrigger::IsActive())
        return false;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    for (ObjectGuid const guid : attackers)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit || !botAI->EqualLowercaseName(unit->GetName(), "princess huhuran"))
            continue;

        // Phase transition baseline:
        // spread ranged during the dangerous poison volley/enrage window.
        if (unit->GetHealthPct() <= 32.0f)
            return true;

        if (botAI->HasAura("frenzy", unit) || botAI->HasAura("berserk", unit))
            return true;
    }

    return false;
}

bool Aq40TwinEmperorsActiveTrigger::IsActive()
{
    if (!Aq40BossHelper::IsInAq40(bot) || !bot->IsInCombat())
        return false;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    for (ObjectGuid const guid : attackers)
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
    if (!Aq40TwinEmperorsActiveTrigger::IsActive())
        return false;

    Unit* currentTarget = AI_VALUE(Unit*, "current target");
    if (!currentTarget)
        return true;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    bool hasVeknilash = false;
    bool hasVeklor = false;
    for (ObjectGuid const guid : attackers)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit)
            continue;

        if (botAI->EqualLowercaseName(unit->GetName(), "emperor vek'nilash"))
            hasVeknilash = true;
        else if (botAI->EqualLowercaseName(unit->GetName(), "emperor vek'lor"))
            hasVeklor = true;
    }

    bool preferVeknilash = botAI->IsTank(bot) || !botAI->IsRanged(bot);
    if (Aq40BossHelper::IsDesignatedTwinWarlockTank(bot))
        preferVeknilash = false;

    if (preferVeknilash &&
        botAI->EqualLowercaseName(currentTarget->GetName(), "emperor vek'lor") && hasVeknilash)
        return true;

    if (!preferVeknilash &&
        botAI->EqualLowercaseName(currentTarget->GetName(), "emperor vek'nilash") && hasVeklor)
        return true;

    bool onEmperor = botAI->EqualLowercaseName(currentTarget->GetName(), "emperor vek'nilash") ||
                     botAI->EqualLowercaseName(currentTarget->GetName(), "emperor vek'lor");
    return !onEmperor;
}

bool Aq40TwinEmperorsArcaneBurstRiskTrigger::IsActive()
{
    if (!Aq40TwinEmperorsActiveTrigger::IsActive())
        return false;

    if (Aq40BossHelper::IsDesignatedTwinWarlockTank(bot))
        return false;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    for (ObjectGuid const guid : attackers)
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
    if (!Aq40TwinEmperorsActiveTrigger::IsActive())
        return false;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    Unit* veklor = nullptr;
    Unit* veknilash = nullptr;
    for (ObjectGuid const guid : attackers)
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
    if (!Aq40BossHelper::IsInAq40(bot) || !bot->IsInCombat())
        return false;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    return IsAnyNamedUnit(botAI, attackers, { "ouro", "dirt mound", "qiraji scarab", "scarab" });
}

bool Aq40OuroScarabsTrigger::IsActive()
{
    if (!Aq40OuroActiveTrigger::IsActive())
        return false;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    return IsAnyNamedUnit(botAI, attackers, { "qiraji scarab", "scarab" });
}

bool Aq40OuroSweepTrigger::IsActive()
{
    if (!Aq40OuroActiveTrigger::IsActive() || botAI->IsTank(bot))
        return false;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    for (ObjectGuid const guid : attackers)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit || !botAI->EqualLowercaseName(unit->GetName(), "ouro"))
            continue;

        if (bot->GetDistance2d(unit) <= 10.0f)
            return true;
    }

    return false;
}

bool Aq40OuroSubmergeTrigger::IsActive()
{
    if (!Aq40OuroActiveTrigger::IsActive())
        return false;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    for (ObjectGuid const guid : attackers)
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
    if (!Aq40BossHelper::IsInAq40(bot) || !bot->IsInCombat())
        return false;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    return IsAnyNamedUnit(botAI, attackers, { "viscidus", "glob of viscidus", "toxic slime" });
}

bool Aq40ViscidusFrozenTrigger::IsActive()
{
    if (!Aq40ViscidusActiveTrigger::IsActive())
        return false;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    for (ObjectGuid const guid : attackers)
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
    if (!Aq40ViscidusActiveTrigger::IsActive())
        return false;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    return IsAnyNamedUnit(botAI, attackers, { "glob of viscidus" });
}

bool Aq40CthunActiveTrigger::IsActive()
{
    if (!Aq40BossHelper::IsInAq40(bot) || !bot->IsInCombat())
        return false;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    return IsAnyNamedUnit(botAI, attackers,
                          { "c'thun", "eye of c'thun", "eye tentacle", "claw tentacle",
                            "giant eye tentacle", "giant claw tentacle", "flesh tentacle" });
}

bool Aq40CthunPhase2Trigger::IsActive()
{
    if (!Aq40CthunActiveTrigger::IsActive())
        return false;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    return IsAnyNamedUnit(botAI, attackers,
                          { "c'thun", "giant eye tentacle", "giant claw tentacle", "flesh tentacle" });
}

bool Aq40CthunAddsPresentTrigger::IsActive()
{
    if (!Aq40CthunActiveTrigger::IsActive())
        return false;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    return IsAnyNamedUnit(botAI, attackers,
                          { "eye tentacle", "claw tentacle", "giant eye tentacle", "giant claw tentacle",
                            "flesh tentacle" });
}

bool Aq40CthunDarkGlareTrigger::IsActive()
{
    if (!Aq40CthunActiveTrigger::IsActive())
        return false;

    if (Aq40CthunInStomachTrigger(botAI).IsActive())
        return false;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    for (ObjectGuid const guid : attackers)
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
    return Aq40SpellIds::GetAnyAura(bot, { Aq40SpellIds::CthunDigestiveAcid }) ||
           botAI->GetAura("digestive acid", bot, false, false) ||
           botAI->GetAura("digestive acid", bot, false, true, 1);
}

bool Aq40CthunVulnerableTrigger::IsActive()
{
    if (!Aq40CthunPhase2Trigger::IsActive())
        return false;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    for (ObjectGuid const guid : attackers)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit)
            continue;

        if (botAI->EqualLowercaseName(unit->GetName(), "c'thun") && botAI->HasAura("weakened", unit))
            return true;
    }

    return false;
}

bool Aq40CthunEyeCastTrigger::IsActive()
{
    if (!Aq40CthunActiveTrigger::IsActive() || Aq40CthunInStomachTrigger(botAI).IsActive())
        return false;

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    for (ObjectGuid const guid : attackers)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit)
            continue;

        bool isEyeTentacle = botAI->EqualLowercaseName(unit->GetName(), "eye tentacle") ||
                             botAI->EqualLowercaseName(unit->GetName(), "giant eye tentacle");
        Spell* spell = unit->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        bool eyeCast = spell && Aq40SpellIds::MatchesAnySpellId(spell->GetSpellInfo(), { Aq40SpellIds::CthunMindFlay });
        if (isEyeTentacle && (eyeCast || spell))
            return true;
    }

    return false;
}
