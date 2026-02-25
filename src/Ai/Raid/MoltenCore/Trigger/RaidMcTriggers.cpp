#include "RaidMcTriggers.h"

#include "SharedDefines.h"
#include "Spell.h"
#include "RaidMcHelpers.h"

using namespace MoltenCoreHelpers;

bool McLivingBombDebuffTrigger::IsActive()
{
    // No check for Baron Geddon, because bots may have the bomb even after Geddon died.
    return bot->HasAura(SPELL_LIVING_BOMB);
}

bool McBaronGeddonInfernoTrigger::IsActive()
{
    if (Unit* boss = AI_VALUE2(Unit*, "find target", "baron geddon"))
        return boss->HasAura(SPELL_INFERNO);
    return false;
}

bool McShazzrahRangedTrigger::IsActive()
{
    return AI_VALUE2(Unit*, "find target", "shazzrah") && PlayerbotAI::IsRanged(bot);
}

bool McGolemaggMarkBossTrigger::IsActive()
{
    // any tank may mark the boss
    return AI_VALUE2(Unit*, "find target", "golemagg the incinerator") && PlayerbotAI::IsTank(bot);
}

bool McGolemaggIsMainTankTrigger::IsActive()
{
    return AI_VALUE2(Unit*, "find target", "golemagg the incinerator") && PlayerbotAI::IsMainTank(bot);
}

bool McGolemaggIsAssistTankTrigger::IsActive()
{
    return AI_VALUE2(Unit*, "find target", "golemagg the incinerator") && PlayerbotAI::IsAssistTank(bot);
}

bool McRagnarosPositioningTrigger::IsActive()
{
    Unit* boss = AI_VALUE2(Unit*, "find target", "ragnaros");
    if (!boss || !boss->IsAlive() || boss->HasAura(SPELL_RAGSUBMERGE))
    {
        return false;
    }

    return bot->IsInCombat();
}

bool McRagnarosWrathWindowTrigger::IsActive()
{
    if (!(botAI->IsMelee(bot) || botAI->IsAssistTank(bot)))
    {
        return false;
    }

    Unit* boss = AI_VALUE2(Unit*, "find target", "ragnaros");
    if (!boss || !boss->IsAlive() || boss->HasAura(SPELL_RAGSUBMERGE))
    {
        return false;
    }

    if (!boss->HasUnitState(UNIT_STATE_CASTING))
    {
        return false;
    }

    Spell* spell = boss->GetCurrentSpell(CURRENT_GENERIC_SPELL);
    if (!spell)
    {
        spell = boss->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
    }
    if (!spell || !spell->GetSpellInfo())
    {
        return false;
    }

    uint32 const spellId = spell->GetSpellInfo()->Id;
    return spellId == SPELL_WRATH_OF_RAGNAROS || spellId == SPELL_HAND_OF_RAGNAROS;
}

bool McRagnarosSonsTrigger::IsActive()
{
    Unit* boss = AI_VALUE2(Unit*, "find target", "ragnaros");
    if (!boss || !boss->IsAlive())
    {
        return false;
    }

    if (!boss->HasAura(SPELL_RAGSUBMERGE))
    {
        return false;
    }

    GuidVector attackers = context->GetValue<GuidVector>("attackers")->Get();
    for (ObjectGuid const guid : attackers)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (unit && unit->IsAlive() && unit->GetEntry() == NPC_SON_OF_FLAME)
        {
            return true;
        }
    }

    GuidVector nearby = context->GetValue<GuidVector>("nearest npcs")->Get();
    for (ObjectGuid const guid : nearby)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (unit && unit->IsAlive() && unit->GetEntry() == NPC_SON_OF_FLAME)
        {
            return true;
        }
    }

    return false;
}
