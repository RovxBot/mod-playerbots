/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_PROFESSIONACTIONS_H
#define PLAYERBOTS_PROFESSIONACTIONS_H

#include "NewRpgBaseAction.h"
#include "ProfessionMgr.h"

class Creature;

class ProfessionAction : public NewRpgBaseAction
{
public:
    ProfessionAction(PlayerbotAI* botAI, std::string name, ProfessionPhase phase)
        : NewRpgBaseAction(botAI, name), _phase(phase)
    {
    }
    bool isUseful() override;
    bool isPossible() override;

protected:
    bool Fail();
    bool Travel(bool& arrived);
    Creature* GetDestinationCreature() const;
    bool PrepareSpell(ProfessionRecipe const& recipe, bool& ready);
    bool CastStep(ProfessionStep& step, bool product);
    void ChangePhase(ProfessionPhase phase);

private:
    ProfessionPhase _phase;
};

class ProfessionPlanAction : public ProfessionAction
{
public:
    ProfessionPlanAction(PlayerbotAI* botAI) : ProfessionAction(botAI, "profession plan", ProfessionPhase::Plan) {}
    bool Execute(Event event) override;
};

class ProfessionRefineAction : public ProfessionAction
{
public:
    ProfessionRefineAction(PlayerbotAI* botAI) : ProfessionAction(botAI, "profession refine", ProfessionPhase::Refine)
    {
    }
    bool Execute(Event event) override;
};

class ProfessionSupplyAction : public ProfessionAction
{
public:
    ProfessionSupplyAction(PlayerbotAI* botAI)
        : ProfessionAction(botAI, "profession supplies", ProfessionPhase::Supplies)
    {
    }
    bool Execute(Event event) override;
};

class ProfessionCraftAction : public ProfessionAction
{
public:
    ProfessionCraftAction(PlayerbotAI* botAI) : ProfessionAction(botAI, "profession craft", ProfessionPhase::Craft) {}
    bool Execute(Event event) override;
};

class ProfessionAuctionAction : public ProfessionAction
{
public:
    ProfessionAuctionAction(PlayerbotAI* botAI)
        : ProfessionAction(botAI, "profession auction", ProfessionPhase::Auction)
    {
    }
    bool Execute(Event event) override;
};

#endif
