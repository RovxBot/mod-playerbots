#include "RaidBwlTriggers.h"

#include "RaidBwlSpellIds.h"
#include "Spell.h"
#include "SharedDefines.h"

namespace
{
enum class ChromaggusBreathCast
{
    None,
    OtherBreath,
    TimeLapse
};

ChromaggusBreathCast GetChromaggusBreathCast(PlayerbotAI* botAI, Unit* chromaggus)
{
    if (!botAI || !chromaggus || !chromaggus->HasUnitState(UNIT_STATE_CASTING))
    {
        return ChromaggusBreathCast::None;
    }

    Spell* spell = chromaggus->GetCurrentSpell(CURRENT_GENERIC_SPELL);
    if (!spell)
    {
        spell = chromaggus->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
    }
    if (!spell || !spell->GetSpellInfo())
    {
        return ChromaggusBreathCast::None;
    }

    SpellInfo const* spellInfo = spell->GetSpellInfo();
    if (BwlSpellIds::MatchesAnySpellId(spellInfo, {BwlSpellIds::ChromaggusTimeLapse}))
    {
        return ChromaggusBreathCast::TimeLapse;
    }

    if (BwlSpellIds::MatchesAnySpellId(
            spellInfo, {BwlSpellIds::ChromaggusIncinerate, BwlSpellIds::ChromaggusFrostBurn, BwlSpellIds::ChromaggusCorrosiveAcid,
                        BwlSpellIds::ChromaggusIgniteFlesh}))
    {
        return ChromaggusBreathCast::OtherBreath;
    }

    return ChromaggusBreathCast::None;
}
}  // namespace

bool BwlChromaggusEncounterTrigger::IsActive()
{
    if (!helper.IsInBwl() || !bot->IsInCombat())
    {
        return false;
    }

    if (Unit* chromaggus = AI_VALUE2(Unit*, "find target", "chromaggus"))
    {
        return chromaggus->IsAlive();
    }

    return false;
}

bool BwlChromaggusPositioningTrigger::IsActive()
{
    if (!helper.IsInBwl() || !bot->IsInCombat())
    {
        return false;
    }

    if (Unit* chromaggus = AI_VALUE2(Unit*, "find target", "chromaggus"))
    {
        return chromaggus->IsAlive();
    }

    return false;
}

bool BwlChromaggusFrenzyTrigger::IsActive()
{
    if (!helper.IsInBwl() || !bot->IsInCombat())
    {
        return false;
    }

    if (bot->getClass() != CLASS_HUNTER)
    {
        return false;
    }

    Unit* chromaggus = AI_VALUE2(Unit*, "find target", "chromaggus");
    if (!chromaggus || !chromaggus->IsAlive())
    {
        return false;
    }

    return BwlSpellIds::GetAnyAura(chromaggus, {BwlSpellIds::ChromaggusFrenzy}) != nullptr;
}

bool BwlChromaggusBreathLosTrigger::IsActive()
{
    if (!helper.IsInBwl() || !bot->IsInCombat() || botAI->IsMainTank(bot))
    {
        return false;
    }

    Unit* chromaggus = AI_VALUE2(Unit*, "find target", "chromaggus");
    if (!chromaggus || !chromaggus->IsAlive())
    {
        return false;
    }

    ChromaggusBreathCast const breathCast = GetChromaggusBreathCast(botAI, chromaggus);
    if (breathCast == ChromaggusBreathCast::None)
    {
        return false;
    }

    // Standard breath: all non-main tanks break LoS.
    // Time Lapse: off-tank/healers break LoS so OT can pick up while DPS keeps pressure.
    if (breathCast == ChromaggusBreathCast::OtherBreath)
    {
        return true;
    }

    return botAI->IsAssistTank(bot) || botAI->IsHeal(bot);
}

bool BwlChromaggusMainTankTimeLapseTrigger::IsActive()
{
    if (!helper.IsInBwl() || !bot->IsInCombat() || !botAI->IsAssistTank(bot))
    {
        return false;
    }

    Unit* chromaggus = AI_VALUE2(Unit*, "find target", "chromaggus");
    if (!chromaggus || !chromaggus->IsAlive())
    {
        return false;
    }

    Unit* mainTank = AI_VALUE(Unit*, "main tank");
    if (!mainTank || mainTank == bot)
    {
        return false;
    }

    if (BwlSpellIds::HasAnyAura(botAI, mainTank, {BwlSpellIds::ChromaggusTimeLapse}))
    {
        return true;
    }

    return false;
}

bool BwlAfflictionBronzeTrigger::IsActive()
{
    if (!helper.IsInBwl())
    {
        return false;
    }

    return helper.HasBronzeAffliction();
}
