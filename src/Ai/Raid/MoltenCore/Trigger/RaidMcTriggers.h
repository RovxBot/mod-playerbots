#ifndef _PLAYERBOT_RAIDMCTRIGGERS_H
#define _PLAYERBOT_RAIDMCTRIGGERS_H

#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "Trigger.h"

class McLivingBombDebuffTrigger : public Trigger
{
public:
    McLivingBombDebuffTrigger(PlayerbotAI* botAI) : Trigger(botAI, "mc living bomb debuff") {}
    bool IsActive() override;
};

class McBaronGeddonInfernoTrigger : public Trigger
{
public:
    McBaronGeddonInfernoTrigger(PlayerbotAI* botAI) : Trigger(botAI, "mc baron geddon inferno") {}
    bool IsActive() override;
};

class McShazzrahRangedTrigger : public Trigger
{
public:
    McShazzrahRangedTrigger(PlayerbotAI* botAI) : Trigger(botAI, "mc shazzrah ranged") {}
    bool IsActive() override;
};

class McGolemaggMarkBossTrigger : public Trigger
{
public:
    McGolemaggMarkBossTrigger(PlayerbotAI* botAI) : Trigger(botAI, "mc golemagg mark boss") {}
    bool IsActive() override;
};

class McGolemaggIsMainTankTrigger : public Trigger
{
public:
    McGolemaggIsMainTankTrigger(PlayerbotAI* botAI) : Trigger(botAI, "mc golemagg is main tank") {}
    bool IsActive() override;
};

class McGolemaggIsAssistTankTrigger : public Trigger
{
public:
    McGolemaggIsAssistTankTrigger(PlayerbotAI* botAI) : Trigger(botAI, "mc golemagg is assist tank") {}
    bool IsActive() override;
};

class McRagnarosPositioningTrigger : public Trigger
{
public:
    McRagnarosPositioningTrigger(PlayerbotAI* botAI) : Trigger(botAI, "mc ragnaros positioning") {}
    bool IsActive() override;
};

class McRagnarosWrathWindowTrigger : public Trigger
{
public:
    McRagnarosWrathWindowTrigger(PlayerbotAI* botAI) : Trigger(botAI, "mc ragnaros wrath window") {}
    bool IsActive() override;
};

class McRagnarosSonsTrigger : public Trigger
{
public:
    McRagnarosSonsTrigger(PlayerbotAI* botAI) : Trigger(botAI, "mc ragnaros sons") {}
    bool IsActive() override;
};

class McDisableHunterPetGrowlTrigger : public Trigger
{
public:
    McDisableHunterPetGrowlTrigger(PlayerbotAI* botAI) : Trigger(botAI, "mc disable hunter pet growl", 5) {}
    bool IsActive() override;
};

#endif
