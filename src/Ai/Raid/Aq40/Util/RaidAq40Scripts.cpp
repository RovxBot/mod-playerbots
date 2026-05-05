#include "RaidAq40TwinEmperors.h"

#include <string>

#include "ScriptMgr.h"
#include "Spell.h"
#include "../RaidAq40BossHelper.h"
#include "../RaidAq40SpellIds.h"

namespace
{
bool IsTwinTeleportCaster(Unit* caster)
{
    if (!caster || caster->GetMapId() != Aq40BossHelper::MAP_ID)
        return false;

    std::string const casterName = caster->GetName();
    return casterName == "Emperor Vek'nilash" || casterName == "Emperor Vek'lor";
}
}  // namespace

class Aq40TwinEmperorsListenerScript : public AllSpellScript
{
public:
    Aq40TwinEmperorsListenerScript() : AllSpellScript("Aq40TwinEmperorsListenerScript") { }

    void OnSpellCast(Spell* /*spell*/, Unit* caster, SpellInfo const* spellInfo, bool /*skipCheck*/) override
    {
        if (!spellInfo || spellInfo->Id != Aq40SpellIds::TwinTeleport)
            return;

        if (!IsTwinTeleportCaster(caster))
            return;

        Aq40TwinEmperors::NoteTwinTeleportCast(caster);
    }
};

void AddSC_Aq40BotScripts()
{
    new Aq40TwinEmperorsListenerScript();
}
