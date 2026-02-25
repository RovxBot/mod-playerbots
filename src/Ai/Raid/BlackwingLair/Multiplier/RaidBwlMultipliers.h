#ifndef _PLAYERBOT_RAIDBWLMULTIPLIERS_H
#define _PLAYERBOT_RAIDBWLMULTIPLIERS_H

#include "Multiplier.h"
#include "RaidBwlBossHelper.h"

class BwlEncounterTargetingMultiplier : public Multiplier
{
public:
    BwlEncounterTargetingMultiplier(PlayerbotAI* ai) : Multiplier(ai, "bwl encounter targeting"), helper(ai) {}
    float GetValue(Action* action) override;

private:
    void RefreshStateCache() const;

    BwlBossHelper helper;
    mutable uint32 cacheMSTime = 0;
    mutable bool cacheNefarianP1 = false;
    mutable bool cacheNefarianP2 = false;
    mutable bool cacheAnyBossEncounter = false;
    mutable bool cacheDangerousTrashEncounter = false;
    mutable bool cacheSeetherEnraged = false;
    mutable bool cacheDeathTalonUndetected = false;
};

class BwlEncounterPositioningMultiplier : public Multiplier
{
public:
    BwlEncounterPositioningMultiplier(PlayerbotAI* ai) : Multiplier(ai, "bwl encounter positioning"), helper(ai) {}
    float GetValue(Action* action) override;

private:
    void RefreshStateCache() const;

    BwlBossHelper helper;
    mutable uint32 cacheMSTime = 0;
    mutable bool cacheAnyBossEncounter = false;
    mutable bool cacheChromaggusTimeLapseCast = false;
};

#endif
