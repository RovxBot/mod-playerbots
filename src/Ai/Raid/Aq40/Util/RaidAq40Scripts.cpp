#include <algorithm>
#include <cmath>
#include <sstream>
#include <vector>

#include "ObjectAccessor.h"
#include "Playerbots.h"
#include "ScriptMgr.h"
#include "Spell.h"
#include "Timer.h"
#include "../RaidAq40BossHelper.h"
#include "../RaidAq40SpellIds.h"
#include "RaidAq40Helpers_Shared.h"
#include "RaidAq40TwinEncounter.h"

namespace
{
float constexpr kTwinRoomBotRadius = 180.0f;
float constexpr kTwinRoomExtendedBotRadius = 220.0f;
float constexpr kTwinExplodeBugInterruptRadius = 15.0f;
uint32 constexpr kTwinTeleportThreatHoldMs = 8000;
uint32 constexpr kTwinPickupAnchorDurationMs = 6000;
uint32 constexpr kTwinTeleportDebounceMs = 1000;

size_t ToSideIndex(Aq40TwinEncounter::TwinSide side)
{
	return side == Aq40TwinEncounter::TwinSide::Side1 ? 1u : 0u;
}

bool HasActiveAq40CombatStrategy(PlayerbotAI* botAI)
{
	return botAI && botAI->HasStrategy("aq40", BOT_STATE_COMBAT);
}

bool TryGetTwinBoss(Unit const* unit, Aq40TwinEncounter::TwinBoss& outBoss)
{
	if (!unit)
		return false;

	switch (unit->GetEntry())
	{
		case Aq40SpellIds::TwinVeklorNpcEntry:
			outBoss = Aq40TwinEncounter::TwinBoss::Veklor;
			return true;
		case Aq40SpellIds::TwinVeknilashNpcEntry:
			outBoss = Aq40TwinEncounter::TwinBoss::Veknilash;
			return true;
		default:
			return false;
	}
}

bool IsTwinRelevantCaster(Unit* caster)
{
	return caster && caster->GetMap() && caster->GetMapId() == Aq40BossHelper::MAP_ID &&
		   Aq40SpellIds::IsTwinEncounterNpcEntry(caster->GetEntry());
}

float GetDistance2d(float leftX, float leftY, float rightX, float rightY)
{
	float const dx = leftX - rightX;
	float const dy = leftY - rightY;
	return std::sqrt(dx * dx + dy * dy);
}

Aq40TwinEncounter::TwinSide GetTwinSideForPosition(float x, float y)
{
	Aq40TwinEncounter::TwinEncounterGeometry const& geometry = Aq40TwinEncounter::GetGeometry();
	float const side0Distance = GetDistance2d(x, y,
		geometry.bossPark[0].position.GetPositionX(), geometry.bossPark[0].position.GetPositionY());
	float const side1Distance = GetDistance2d(x, y,
		geometry.bossPark[1].position.GetPositionX(), geometry.bossPark[1].position.GetPositionY());
	return side0Distance <= side1Distance ? Aq40TwinEncounter::TwinSide::Side0 : Aq40TwinEncounter::TwinSide::Side1;
}

bool IsTwinRelevantBot(Player* player, Unit* source)
{
	if (!player || !source || !player->IsAlive() || !player->IsInWorld() || !player->GetMap())
		return false;

	PlayerbotAI* botAI = GET_PLAYERBOT_AI(player);
	if (!HasActiveAq40CombatStrategy(botAI))
		return false;

	if (!Aq40BossHelper::IsInAq40(player) || !source->GetMap() ||
		player->GetMap()->GetInstanceId() != source->GetMap()->GetInstanceId())
	{
		return false;
	}

	Position const& center = Aq40TwinEncounter::GetGeometry().roomCenter.position;
	if (player->GetExactDist2d(center.GetPositionX(), center.GetPositionY()) <= kTwinRoomBotRadius)
		return true;

	if (source->GetDistance2d(player) <= kTwinRoomBotRadius)
		return true;

	return Aq40TwinEncounter::GetLockedPickupAnchor(player) &&
		   player->GetExactDist2d(center.GetPositionX(), center.GetPositionY()) <= kTwinRoomExtendedBotRadius;
}

std::vector<Player*> CollectTwinBots(Unit* source)
{
	std::vector<Player*> bots;
	if (!source || !source->GetMap())
		return bots;

	Map::PlayerList const& players = source->GetMap()->GetPlayers();
	for (Map::PlayerList::const_iterator itr = players.begin(); itr != players.end(); ++itr)
	{
		Player* player = itr->GetSource();
		if (IsTwinRelevantBot(player, source))
			bots.push_back(player);
	}

	return bots;
}

bool UpdateHazardTimestamp(uint32& hazardAtMs, uint32 nowMs, uint32 debounceMs = 0)
{
	if (hazardAtMs && debounceMs > 0 && getMSTimeDiff(hazardAtMs, nowMs) < debounceMs)
		return false;

	hazardAtMs = nowMs;
	return true;
}

Player* ResolvePrimarySpellTargetPlayer(Spell* spell, Unit* caster)
{
	if (!spell || !caster)
		return nullptr;

	std::list<TargetInfo> const* targets = spell->GetUniqueTargetInfo();
	if (!targets)
		return nullptr;

	for (TargetInfo const& targetInfo : *targets)
	{
		Unit* target = ObjectAccessor::GetUnit(*caster, targetInfo.targetGUID);
		if (!target)
			continue;

		if (Player* playerTarget = target->ToPlayer())
			return playerTarget;
	}

	return nullptr;
}

Player* ResolveBossOwnerForSpell(Spell* spell, Unit* caster, uint32 spellId)
{
	if (!caster)
		return nullptr;

	if (spellId == Aq40SpellIds::TwinShadowBolt || spellId == Aq40SpellIds::TwinUppercut ||
		spellId == Aq40SpellIds::TwinUnbalancingStrike)
	{
		if (Player* explicitTarget = ResolvePrimarySpellTargetPlayer(spell, caster))
			return explicitTarget;
	}

	if (Unit* victim = caster->GetVictim())
	{
		if (Player* victimPlayer = victim->ToPlayer())
			return victimPlayer;
	}

	if (ObjectGuid const targetGuid = caster->GetTarget())
	{
		if (Unit* target = ObjectAccessor::GetUnit(*caster, targetGuid))
		{
			if (Player* targetPlayer = target->ToPlayer())
				return targetPlayer;
		}
	}

	return ResolvePrimarySpellTargetPlayer(spell, caster);
}

void MarkEncounterCombat(Aq40TwinEncounter::TwinEncounterState& state, uint32 nowMs)
{
	if (state.phase == Aq40TwinEncounter::TwinEncounterPhase::PrePull &&
		(state.mode != Aq40TwinEncounter::TwinStrategyMode::StandardCompReady ||
		 !Aq40TwinEncounter::HasDeterministicAssignments(state)))
	{
		return;
	}

	Aq40TwinEncounter::SetMode(state, Aq40TwinEncounter::TwinStrategyMode::Combat, nowMs);
	if (state.phase == Aq40TwinEncounter::TwinEncounterPhase::PrePull)
		Aq40TwinEncounter::EnterDualPullWindow(state, nowMs);
}

void MaybeLockPickupAnchor(Aq40TwinEncounter::TwinEncounterState const& state, Player* owner,
						   Aq40TwinEncounter::TwinBoss boss, Aq40TwinEncounter::TwinSide side, uint32 nowMs)
{
	if (!owner || !GET_PLAYERBOT_AI(owner) || !Aq40TwinEncounter::IsKnownSide(side))
		return;

	if (state.phase != Aq40TwinEncounter::TwinEncounterPhase::TeleportWindow &&
		state.phase != Aq40TwinEncounter::TwinEncounterPhase::PickupRecovery &&
		!Aq40TwinEncounter::IsThreatHoldWindowActive(state, boss, nowMs))
	{
		return;
	}

	Aq40TwinEncounter::TwinEncounterGeometry const& geometry = Aq40TwinEncounter::GetGeometry();
	Aq40TwinEncounter::TwinAnchor const& anchor =
		boss == Aq40TwinEncounter::TwinBoss::Veklor
			? geometry.stableVeklorWarlock[ToSideIndex(side)]
			: geometry.bossPark[ToSideIndex(side)];

	uint32 const durationMs = std::max(
		Aq40TwinEncounter::GetThreatHoldRemainingMs(state, boss, nowMs), kTwinPickupAnchorDurationMs);
	Aq40TwinEncounter::SetLockedPickupAnchor(owner, boss, side, anchor, durationMs, nowMs);
}

void PromoteStablePhaseIfReady(Aq40TwinEncounter::TwinEncounterState& state, uint32 nowMs)
{
	if (!Aq40TwinEncounter::IsPickupEstablished(state, Aq40TwinEncounter::TwinBoss::Veklor) ||
		!Aq40TwinEncounter::IsPickupEstablished(state, Aq40TwinEncounter::TwinBoss::Veknilash))
	{
		return;
	}

	Aq40TwinEncounter::SetSplitBand(state, Aq40TwinEncounter::TwinSplitBand::Stable, nowMs);
	Aq40TwinEncounter::EnterStablePhase(state, nowMs);
}

void ConfirmBossOwner(Aq40TwinEncounter::TwinEncounterState& state, Spell* spell, Unit* caster, uint32 spellId,
					  Aq40TwinEncounter::TwinBoss boss, uint32 nowMs, Player* logBot)
{
	if (Aq40TwinEncounter::IsTerminalPhase(state.phase))
		return;

	Player* owner = ResolveBossOwnerForSpell(spell, caster, spellId);
	if (!owner)
		return;

	ObjectGuid const ownerGuid = owner->GetGUID();
	bool changed = false;
	changed |= Aq40TwinEncounter::ConfirmOwner(state, boss, ownerGuid, nowMs);
	changed |= Aq40TwinEncounter::SetStableOwner(state, boss, ownerGuid, nowMs);
	changed |= Aq40TwinEncounter::MarkPickupEstablished(state, boss, ownerGuid, nowMs);

	if (state.phase == Aq40TwinEncounter::TwinEncounterPhase::TeleportWindow)
		Aq40TwinEncounter::EnterPickupRecovery(state, nowMs);

	MaybeLockPickupAnchor(state, owner, boss, GetTwinSideForPosition(caster->GetPositionX(), caster->GetPositionY()),
						  nowMs);
	PromoteStablePhaseIfReady(state, nowMs);

	if (!changed || !logBot)
		return;

	std::ostringstream fields;
	fields << "boss=twin twin_boss=" << Aq40TwinEncounter::ToString(boss)
		   << " owner=" << Aq40Helpers::GetAq40LogUnit(owner)
		   << " source=" << Aq40Helpers::GetAq40LogUnit(caster)
		   << " phase=" << Aq40TwinEncounter::ToString(state.phase);
	Aq40Helpers::LogAq40Info(logBot, "twin_pickup_confirm",
		std::string(Aq40TwinEncounter::ToString(boss)) + ":" + std::to_string(ownerGuid.GetCounter()) + ":" +
			Aq40TwinEncounter::ToString(state.phase),
		fields.str(), 1000);
}

void RequestInterruptForTwinBots(std::vector<Player*> const& twinBots, Unit* source, Player* excludedBot = nullptr,
								 float maxDistance = 0.0f, bool excludeEncounterTanks = false)
{
	for (Player* bot : twinBots)
	{
		if (!bot || bot == excludedBot)
			continue;
		if (maxDistance > 0.0f && (!source || bot->GetDistance2d(source) > maxDistance))
			continue;
		if (excludeEncounterTanks && Aq40BossHelper::IsEncounterTank(bot, bot))
			continue;

		Aq40TwinEncounter::RequestImmediateMovementInterrupt(bot);
	}
}
}    // namespace

class Aq40TwinEmperorsListenerScript : public AllSpellScript
{
public:
	Aq40TwinEmperorsListenerScript() : AllSpellScript("Aq40TwinEmperorsListenerScript") { }

	void OnSpellCast(Spell* spell, Unit* caster, SpellInfo const* spellInfo, bool /*skipCheck*/) override
	{
		if (!spell || !caster || !spellInfo || !Aq40SpellIds::IsTwinEncounterSpell(spellInfo) || !IsTwinRelevantCaster(caster))
			return;

		std::vector<Player*> twinBots = CollectTwinBots(caster);
		if (twinBots.empty())
			return;

		Player* logBot = twinBots.front();
		Aq40TwinEncounter::TwinBoss boss;
		bool const hasBossCaster = TryGetTwinBoss(caster, boss);
		Aq40TwinEncounter::TwinEncounterState* statePtr = Aq40TwinEncounter::GetEncounterState(logBot);
		if (!hasBossCaster && (!statePtr || !Aq40TwinEncounter::IsActivePhase(statePtr->phase)))
			return;

		uint32 const nowMs = getMSTime();
		Aq40TwinEncounter::TwinEncounterState& state =
			statePtr ? *statePtr : Aq40TwinEncounter::EnsureEncounterState(logBot);
		Aq40TwinEncounter::TwinScriptedHazardWindows& hazards = state.scriptedHazards;

		if (hasBossCaster)
			MarkEncounterCombat(state, nowMs);

		if (hasBossCaster && spellInfo->Id != Aq40SpellIds::TwinTeleportPrimary &&
			spellInfo->Id != Aq40SpellIds::TwinTeleportSecondary && spellInfo->Id != Aq40SpellIds::TwinHealBrother)
		{
			ConfirmBossOwner(state, spell, caster, spellInfo->Id, boss, nowMs, logBot);
		}

		switch (spellInfo->Id)
		{
			case Aq40SpellIds::TwinTeleportPrimary:
			case Aq40SpellIds::TwinTeleportSecondary:
			{
				if (!UpdateHazardTimestamp(hazards.teleportAtMs, nowMs, kTwinTeleportDebounceMs))
					return;

				Aq40TwinEncounter::EnterTeleportWindow(state, kTwinTeleportThreatHoldMs, nowMs);
				RequestInterruptForTwinBots(twinBots, caster);

				std::ostringstream fields;
				fields << "boss=twin spell=" << spellInfo->Id
					   << " source=" << Aq40Helpers::GetAq40LogUnit(caster)
					   << " phase=" << Aq40TwinEncounter::ToString(state.phase);
				Aq40Helpers::LogAq40Info(logBot, "twin_script_window", "twin:teleport", fields.str(), 1000);
				return;
			}

			case Aq40SpellIds::TwinBlizzard:
			{
				UpdateHazardTimestamp(hazards.blizzardAtMs, nowMs);
				RequestInterruptForTwinBots(twinBots, caster);

				std::ostringstream fields;
				fields << "boss=twin spell=" << spellInfo->Id
					   << " hazard=blizzard source=" << Aq40Helpers::GetAq40LogUnit(caster)
					   << " phase=" << Aq40TwinEncounter::ToString(state.phase);
				Aq40Helpers::LogAq40Info(logBot, "twin_script_hazard", "twin:blizzard", fields.str(), 1000);
				return;
			}

			case Aq40SpellIds::TwinArcaneBurst:
			{
				UpdateHazardTimestamp(hazards.arcaneBurstAtMs, nowMs);
				Player* activeOwner = hasBossCaster ? ResolveBossOwnerForSpell(spell, caster, spellInfo->Id) : nullptr;
				RequestInterruptForTwinBots(twinBots, caster, activeOwner);

				std::ostringstream fields;
				fields << "boss=twin spell=" << spellInfo->Id
					   << " hazard=arcane_burst source=" << Aq40Helpers::GetAq40LogUnit(caster)
					   << " owner=" << Aq40Helpers::GetAq40LogUnit(activeOwner)
					   << " phase=" << Aq40TwinEncounter::ToString(state.phase);
				Aq40Helpers::LogAq40Info(logBot, "twin_script_hazard", "twin:arcane_burst", fields.str(), 1000);
				return;
			}

			case Aq40SpellIds::TwinHealBrother:
			{
				UpdateHazardTimestamp(hazards.healBrotherAtMs, nowMs);
				Aq40TwinEncounter::EnterTerminalFailure(state, nowMs);

				std::ostringstream fields;
				fields << "boss=twin spell=" << spellInfo->Id
					   << " hazard=heal_brother source=" << Aq40Helpers::GetAq40LogUnit(caster)
					   << " phase=" << Aq40TwinEncounter::ToString(state.phase);
				Aq40Helpers::LogAq40Warn(logBot, "twin_terminal_failure", "twin:heal_brother", fields.str(), 1000);
				return;
			}

			case Aq40SpellIds::TwinExplodeBug:
			{
				UpdateHazardTimestamp(hazards.explodeBugAtMs, nowMs);
				RequestInterruptForTwinBots(twinBots, caster, nullptr, kTwinExplodeBugInterruptRadius, true);

				std::ostringstream fields;
				fields << "boss=twin spell=" << spellInfo->Id
					   << " hazard=explode_bug source=" << Aq40Helpers::GetAq40LogUnit(caster);
				Aq40Helpers::LogAq40Info(logBot, "twin_script_hazard", "twin:explode_bug", fields.str(), 1000);
				return;
			}

			case Aq40SpellIds::TwinMutateBug:
				UpdateHazardTimestamp(hazards.mutateBugAtMs, nowMs);
				return;

			case Aq40SpellIds::TwinUppercut:
				UpdateHazardTimestamp(hazards.uppercutAtMs, nowMs);
				return;

			case Aq40SpellIds::TwinUnbalancingStrike:
				UpdateHazardTimestamp(hazards.unbalancingStrikeAtMs, nowMs);
				return;

			default:
				return;
		}
	}
};

void AddSC_Aq40BotScripts()
{
	new Aq40TwinEmperorsListenerScript();
}
