#ifndef _PLAYERBOT_RAIDBWLTRIGGERS_H
#define _PLAYERBOT_RAIDBWLTRIGGERS_H

#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "RaidBwlBossHelper.h"
#include "Trigger.h"

class BwlSuppressionDeviceTrigger : public Trigger
{
public:
    BwlSuppressionDeviceTrigger(PlayerbotAI* botAI) : Trigger(botAI, "bwl suppression device"), helper(botAI) {}
    bool IsActive() override;

private:
    BwlBossHelper helper;
};

class BwlAfflictionBronzeTrigger : public Trigger
{
public:
    BwlAfflictionBronzeTrigger(PlayerbotAI* botAI) : Trigger(botAI, "bwl affliction bronze"), helper(botAI) {}
    bool IsActive() override;

private:
    BwlBossHelper helper;
};

class BwlMissingOnyxiaScaleCloakTrigger : public Trigger
{
public:
    BwlMissingOnyxiaScaleCloakTrigger(PlayerbotAI* botAI)
        : Trigger(botAI, "bwl missing onyxia scale cloak", 30), helper(botAI)
    {
    }
    bool IsActive() override;

private:
    BwlBossHelper helper;
};

class BwlRazorgoreEncounterTrigger : public Trigger
{
public:
    BwlRazorgoreEncounterTrigger(PlayerbotAI* botAI) : Trigger(botAI, "bwl razorgore encounter"), helper(botAI) {}
    bool IsActive() override;

private:
    BwlBossHelper helper;
};

class BwlVaelastraszEncounterTrigger : public Trigger
{
public:
    BwlVaelastraszEncounterTrigger(PlayerbotAI* botAI) : Trigger(botAI, "bwl vaelastrasz encounter"), helper(botAI) {}
    bool IsActive() override;

private:
    BwlBossHelper helper;
};

class BwlVaelastraszBurningAdrenalineSelfTrigger : public Trigger
{
public:
    BwlVaelastraszBurningAdrenalineSelfTrigger(PlayerbotAI* botAI)
        : Trigger(botAI, "bwl vaelastrasz burning adrenaline self"), helper(botAI)
    {
    }
    bool IsActive() override;

private:
    BwlBossHelper helper;
};

class BwlVaelastraszMainTankBurningAdrenalineTrigger : public Trigger
{
public:
    BwlVaelastraszMainTankBurningAdrenalineTrigger(PlayerbotAI* botAI)
        : Trigger(botAI, "bwl vaelastrasz main tank burning adrenaline"), helper(botAI)
    {
    }
    bool IsActive() override;

private:
    BwlBossHelper helper;
};

class BwlVaelastraszPositioningTrigger : public Trigger
{
public:
    BwlVaelastraszPositioningTrigger(PlayerbotAI* botAI)
        : Trigger(botAI, "bwl vaelastrasz positioning"), helper(botAI)
    {
    }
    bool IsActive() override;

private:
    BwlBossHelper helper;
};

class BwlBroodlordEncounterTrigger : public Trigger
{
public:
    BwlBroodlordEncounterTrigger(PlayerbotAI* botAI) : Trigger(botAI, "bwl broodlord encounter"), helper(botAI) {}
    bool IsActive() override;

private:
    BwlBossHelper helper;
};

class BwlBroodlordPositioningTrigger : public Trigger
{
public:
    BwlBroodlordPositioningTrigger(PlayerbotAI* botAI) : Trigger(botAI, "bwl broodlord positioning"), helper(botAI) {}
    bool IsActive() override;

private:
    BwlBossHelper helper;
};

class BwlBroodlordMainTankMortalStrikeTrigger : public Trigger
{
public:
    BwlBroodlordMainTankMortalStrikeTrigger(PlayerbotAI* botAI)
        : Trigger(botAI, "bwl broodlord main tank mortal strike"), helper(botAI)
    {
    }
    bool IsActive() override;

private:
    BwlBossHelper helper;
};

class BwlFiremawEncounterTrigger : public Trigger
{
public:
    BwlFiremawEncounterTrigger(PlayerbotAI* botAI) : Trigger(botAI, "bwl firemaw encounter"), helper(botAI) {}
    bool IsActive() override;

private:
    BwlBossHelper helper;
};

class BwlFiremawPositioningTrigger : public Trigger
{
public:
    BwlFiremawPositioningTrigger(PlayerbotAI* botAI) : Trigger(botAI, "bwl firemaw positioning"), helper(botAI) {}
    bool IsActive() override;

private:
    BwlBossHelper helper;
};

class BwlFiremawHighFlameBuffetTrigger : public Trigger
{
public:
    BwlFiremawHighFlameBuffetTrigger(PlayerbotAI* botAI) : Trigger(botAI, "bwl firemaw high flame buffet"), helper(botAI) {}
    bool IsActive() override;

private:
    BwlBossHelper helper;
};

class BwlFiremawMainTankHighFlameBuffetTrigger : public Trigger
{
public:
    BwlFiremawMainTankHighFlameBuffetTrigger(PlayerbotAI* botAI)
        : Trigger(botAI, "bwl firemaw main tank high flame buffet"), helper(botAI)
    {
    }
    bool IsActive() override;

private:
    BwlBossHelper helper;
};

class BwlEbonrocEncounterTrigger : public Trigger
{
public:
    BwlEbonrocEncounterTrigger(PlayerbotAI* botAI) : Trigger(botAI, "bwl ebonroc encounter"), helper(botAI) {}
    bool IsActive() override;

private:
    BwlBossHelper helper;
};

class BwlEbonrocPositioningTrigger : public Trigger
{
public:
    BwlEbonrocPositioningTrigger(PlayerbotAI* botAI) : Trigger(botAI, "bwl ebonroc positioning"), helper(botAI) {}
    bool IsActive() override;

private:
    BwlBossHelper helper;
};

class BwlEbonrocMainTankShadowTrigger : public Trigger
{
public:
    BwlEbonrocMainTankShadowTrigger(PlayerbotAI* botAI) : Trigger(botAI, "bwl ebonroc main tank shadow"), helper(botAI) {}
    bool IsActive() override;

private:
    BwlBossHelper helper;
};

class BwlFlamegorEncounterTrigger : public Trigger
{
public:
    BwlFlamegorEncounterTrigger(PlayerbotAI* botAI) : Trigger(botAI, "bwl flamegor encounter"), helper(botAI) {}
    bool IsActive() override;

private:
    BwlBossHelper helper;
};

class BwlFlamegorPositioningTrigger : public Trigger
{
public:
    BwlFlamegorPositioningTrigger(PlayerbotAI* botAI) : Trigger(botAI, "bwl flamegor positioning"), helper(botAI) {}
    bool IsActive() override;

private:
    BwlBossHelper helper;
};

class BwlFlamegorFrenzyTrigger : public Trigger
{
public:
    BwlFlamegorFrenzyTrigger(PlayerbotAI* botAI) : Trigger(botAI, "bwl flamegor frenzy"), helper(botAI) {}
    bool IsActive() override;

private:
    BwlBossHelper helper;
};

class BwlChromaggusEncounterTrigger : public Trigger
{
public:
    BwlChromaggusEncounterTrigger(PlayerbotAI* botAI) : Trigger(botAI, "bwl chromaggus encounter"), helper(botAI) {}
    bool IsActive() override;

private:
    BwlBossHelper helper;
};

class BwlChromaggusPositioningTrigger : public Trigger
{
public:
    BwlChromaggusPositioningTrigger(PlayerbotAI* botAI)
        : Trigger(botAI, "bwl chromaggus positioning"), helper(botAI)
    {
    }
    bool IsActive() override;

private:
    BwlBossHelper helper;
};

class BwlChromaggusFrenzyTrigger : public Trigger
{
public:
    BwlChromaggusFrenzyTrigger(PlayerbotAI* botAI) : Trigger(botAI, "bwl chromaggus frenzy"), helper(botAI) {}
    bool IsActive() override;

private:
    BwlBossHelper helper;
};

class BwlChromaggusBreathLosTrigger : public Trigger
{
public:
    BwlChromaggusBreathLosTrigger(PlayerbotAI* botAI) : Trigger(botAI, "bwl chromaggus breath los"), helper(botAI) {}
    bool IsActive() override;

private:
    BwlBossHelper helper;
};

class BwlChromaggusMainTankTimeLapseTrigger : public Trigger
{
public:
    BwlChromaggusMainTankTimeLapseTrigger(PlayerbotAI* botAI)
        : Trigger(botAI, "bwl chromaggus main tank time lapse"), helper(botAI)
    {
    }
    bool IsActive() override;

private:
    BwlBossHelper helper;
};

class BwlNefarianPhaseOneTrigger : public Trigger
{
public:
    BwlNefarianPhaseOneTrigger(PlayerbotAI* botAI) : Trigger(botAI, "bwl nefarian phase one"), helper(botAI) {}
    bool IsActive() override;

private:
    BwlBossHelper helper;
};

class BwlNefarianPhaseOneTunnelPositioningTrigger : public Trigger
{
public:
    BwlNefarianPhaseOneTunnelPositioningTrigger(PlayerbotAI* botAI)
        : Trigger(botAI, "bwl nefarian phase one tunnel positioning"), helper(botAI)
    {
    }
    bool IsActive() override;

private:
    BwlBossHelper helper;
};

class BwlNefarianPhaseTwoTrigger : public Trigger
{
public:
    BwlNefarianPhaseTwoTrigger(PlayerbotAI* botAI) : Trigger(botAI, "bwl nefarian phase two"), helper(botAI) {}
    bool IsActive() override;

private:
    BwlBossHelper helper;
};

class BwlNefarianPhaseTwoPositioningTrigger : public Trigger
{
public:
    BwlNefarianPhaseTwoPositioningTrigger(PlayerbotAI* botAI)
        : Trigger(botAI, "bwl nefarian phase two positioning"), helper(botAI)
    {
    }
    bool IsActive() override;

private:
    BwlBossHelper helper;
};

class BwlTrashDangerousEncounterTrigger : public Trigger
{
public:
    BwlTrashDangerousEncounterTrigger(PlayerbotAI* botAI) : Trigger(botAI, "bwl trash dangerous encounter"), helper(botAI) {}
    bool IsActive() override;

private:
    BwlBossHelper helper;
};

class BwlTrashSafePositioningTrigger : public Trigger
{
public:
    BwlTrashSafePositioningTrigger(PlayerbotAI* botAI) : Trigger(botAI, "bwl trash safe positioning"), helper(botAI) {}
    bool IsActive() override;

private:
    BwlBossHelper helper;
};

class BwlDeathTalonSeetherEnrageTrigger : public Trigger
{
public:
    BwlDeathTalonSeetherEnrageTrigger(PlayerbotAI* botAI)
        : Trigger(botAI, "bwl death talon seether enrage"), helper(botAI)
    {
    }
    bool IsActive() override;

private:
    BwlBossHelper helper;
};

class BwlDeathTalonDetectMagicTrigger : public Trigger
{
public:
    BwlDeathTalonDetectMagicTrigger(PlayerbotAI* botAI) : Trigger(botAI, "bwl death talon detect magic"), helper(botAI) {}
    bool IsActive() override;

private:
    BwlBossHelper helper;
};

#endif
