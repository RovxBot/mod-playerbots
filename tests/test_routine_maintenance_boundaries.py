"""Source-extracted guard execution and coverage contracts, not a game-server test."""

from pathlib import Path
import re
import os
import subprocess
import tempfile
import unittest

ROOT = Path(os.getenv("PLAYERBOTS_MAINTENANCE_ROOT", Path(__file__).resolve().parents[1]))
MANIFEST = {
    "src/Bot/RandomPlayerbotMgr.cpp": (
        "RandomPlayerbotMgr",
        "Revive RandomTeleport RandomTeleportForLevel RandomTeleportGrindForLevel Randomize IncreaseLevel RandomizeFirst RandomizeMin Clear Refresh ChangeStrategy ChangeStrategyOnce RandomTeleportForRpg Remove",
    ),
    "src/Bot/Factory/PlayerbotFactory.cpp": (
        "PlayerbotFactory",
        "Prepare Randomize Refresh InitConsumables InitPetTalents InitPet ClearSkills ClearEverything ClearSpells ResetQuests InitTalentsTree InitEquipment InitBags InitTradeSkills InitTradeSpecializations UpdateTradeSkills InitSkills InitAvailableSpells InitClassSpells InitSpecialSpells InitQuests InitInstanceQuests ClearInventory ClearAllItems InitAmmo DestroyEquippedGear AutoGear InitMounts UnbindInstance InitPotions InitFood InitReagents CleanupConsumables InitGlyphs InitInventory InitGuild ApplyEnchantAndGemsNew InitKeyring InitReputation InitAttunementQuests",
    ),
    "src/Bot/RandomBotLevelMgr.cpp": (
        "RandomBotLevelMgr",
        "AdjustBotToRange GetOrFlagPlayerBracket ResetBot SkipBotLevel OnBotLogin OnBotLevelChanged",
    ),
    "src/Ai/Base/Actions/TrainerAction.cpp": ("MaintenanceAction", "Execute"),
}


class RoutineMaintenanceBoundaryTests(unittest.TestCase):
    def test_each_named_ingress_executes_its_first_statement_guard(self):
        functions = []
        for path, (cls, names) in MANIFEST.items():
            source = (ROOT / path).read_text()
            if cls == "RandomPlayerbotMgr" and "bool RandomPlayerbotMgr::IsRemovableBot(" in source:
                names += " IsRemovableBot"
            for name in names.split():
                signature = re.compile(
                    r"(void|bool|uint32|int) " + cls + "::" + name + r"\([^;{]*\)(?:\s*//[^\n]*)?\s*\{"
                )
                matches = list(signature.finditer(source))
                # ProcessBot(uint32) handles logins. Its conditional reclaim gate
                # is tested separately; Player* is the maintenance ingress.
                if name == "ProcessBot":
                    matches = [m for m in matches if "Player*" in m.group()]
                self.assertTrue(matches, (path, name))
                for match in matches:
                    guard = re.match(
                        r"\s*(if \(Playerbots::RoutineMaintenance::ShouldBlockRoutineMaintenance\(.*?\)\)\s*return(?: [^;]+)?;)",
                        source[match.end() :],
                        re.S,
                    )
                    self.assertIsNotNone(guard, (path, name))
                    returns = "" if match[1] == "void" else " 1"
                    functions.append(
                        f"{match[1]} gate{len(functions)}(Player* bot) {{\n    Player* player=bot; (void)player;\n    {guard[1]}\n    ++effects;\n    return{returns};\n}}"
                    )
        with tempfile.TemporaryDirectory() as directory:
            scratch = Path(directory)
            (scratch / "Group.h").write_text("""#pragma once
#include <vector>
#include <cstdint>
struct Guid { std::uint32_t Value; std::uint32_t GetCounter() const { return Value; } };
struct Group { struct Member { Guid guid; }; std::vector<Member> Members; auto const& GetMemberSlots() const { return Members; } };
""")
            (scratch / "Player.h").write_text("""#pragma once
#include "Group.h"
using uint32 = std::uint32_t;
struct Player { Guid GuidValue; Group* Party=nullptr; Guid GetGUID() const { return GuidValue; } Group* GetGroup() const { return Party; } };
""")
            calls = "\n".join(f"gate{i}(&bot);" for i in range(len(functions)))
            (scratch / "guards.cpp").write_text(
                """#include "RoutineMaintenance.h"
#include <cassert>
#include <iostream>
using namespace Playerbots::RoutineMaintenance;
bool enabled=false;
bool policy(std::uint32_t guid, Action) { return enabled && guid == 42; }
int effects=0;
"""
                + "\n".join(functions)
                + f"""
int main() {{
 Player bot{{{{42}}}};
 RegisterProvider(&policy);
 {calls}
 assert(effects=={len(functions)});
 enabled=true; effects=0;
 {calls}
 assert(effects==0);
 Player filler{{{{7}}}}; Group group; group.Members.push_back({{{{42}}}}); filler.Party=&group;
 assert(!ShouldBlockRoutineMaintenance(&filler, Action::GroupTeardown));
 assert(ShouldBlockGroupTeardown(&group));
 assert(!ShouldBlockRoutineMaintenance(&filler, Action::Refresh));
 assert(!ShouldBlockRoutineMaintenance(&filler, Action::RandomTeleport));
 enabled=false; assert(!ShouldBlockGroupTeardown(&group));
 UnregisterProvider(&policy); assert(!ShouldBlockGroupTeardown(&group));
 std::cout << "{len(functions)} source-extracted entry guards passed; fake player/group boundary\\n";
}}
"""
            )
            for standard in ("c++17", "c++20"):
                subprocess.run(
                    [
                        "c++",
                        f"-std={standard}",
                        "-Wall",
                        "-Wextra",
                        "-Werror",
                        "-pedantic",
                        "-I" + str(scratch),
                        "-I" + str(ROOT / "src/Bot"),
                        str(scratch / "guards.cpp"),
                        "-o",
                        str(scratch / "guards"),
                    ],
                    check=True,
                )
                subprocess.run([str(scratch / "guards")], check=True)

    def test_deferred_reset_and_reclaim_boundaries(self):
        source = (ROOT / "src/Bot/RandomBotLevelMgr.cpp").read_text()
        pending = source.split("void RandomBotLevelMgr::ProcessPendingLevelResets()", 1)[1]
        self.assertLess(pending.index("ShouldBlockRoutineMaintenance"), pending.index("ObjectAccessor::FindPlayer"))
        self.assertIn("_pendingLevelResets.erase(it)", pending)
        self.assertIn(
            "Action::LevelReset",
            source.split("void RandomBotLevelMgr::RedistributeSurplusBots", 1)[1].split(
                "void RandomBotLevelMgr::ProcessFactionDistribution", 1
            )[0],
        )
        manager = (ROOT / "src/Bot/RandomPlayerbotMgr.cpp").read_text()
        self.assertIn(
            "Action::PopulationTrim",
            manager.split("bool RandomPlayerbotMgr::ProcessBot(uint32 bot)", 1)[1].split(
                "bool RandomPlayerbotMgr::ProcessBot(Player* bot)", 1
            )[0],
        )
        process = manager.split("bool RandomPlayerbotMgr::ProcessBot(Player* bot)", 1)[1].split(
            "void RandomPlayerbotMgr::Revive", 1
        )[0]
        if "ProcessPendingGuildBankDeposit(bot)" in process:
            self.assertLess(process.index("ProcessPendingGuildBankDeposit(bot)"), process.index("// if death revive"))
        for action in ("Refresh", "Randomize", "RandomTeleport", "GroupTeardown"):
            self.assertIn("Action::" + action, process)
        self.assertIn("LogoutAllBots(true)", manager)
        if "uint32 RandomPlayerbotMgr::RemoveRandomBots()" in manager:
            trim = manager.split("uint32 RandomPlayerbotMgr::RemoveRandomBots()", 1)[1].split(
                "uint32 RandomPlayerbotMgr::GetScheduledBotCount()", 1
            )[0]
            self.assertIn("IsRemovableBot(bot)", trim)
            self.assertIn("IsRemovableBot(GetPlayerBot(guid))", trim)
        operation = (ROOT / "src/Script/WorldThr/RoutineGroupLeaveOperation.h").read_text()
        self.assertLess(operation.index("ShouldBlockGroupTeardown"), operation.index("ai->LeaveOrDisbandGroup()"))
        self.assertIn("_groupGuid", operation)
        self.assertIn("seconds(30)", operation)
        self.assertIn("botAI->LeaveOrDisbandGroup(true)", manager)
        holder = (ROOT / "src/Bot/PlayerbotMgr.cpp").read_text()
        self.assertIn("!groupValid && !Playerbots::RoutineMaintenance::ShouldBlockGroupTeardown(group)", holder)
        factory = (ROOT / "src/Bot/Factory/RandomPlayerbotFactory.cpp").read_text()
        self.assertLess(factory.index("Action::AccountDeletion"), factory.index('"DELETE FROM characters'))
        guild = (
            (ROOT / "src/Mgr/Guild/PlayerbotGuildMgr.cpp")
            .read_text()
            .split("void PlayerbotGuildMgr::DeleteRandomBotGuilds()", 1)[1]
        )
        self.assertLess(guild.index("Action::LegacyGuildAssignment"), guild.index("guild->Disband()"))


class RoutineGroupOperationTests(unittest.TestCase):
    def test_actual_world_thread_operation_revalidates_party_and_controller(self):
        with tempfile.TemporaryDirectory() as directory:
            scratch = Path(directory)
            (scratch / "CoreFakes.h").write_text(r"""#pragma once
#include <cstdint>
#include <string>
#include <vector>
using uint32 = std::uint32_t;
struct ObjectGuid { uint32 Value=0; uint32 GetCounter() const { return Value; }
 bool operator==(ObjectGuid other) const { return Value==other.Value; }
 bool operator!=(ObjectGuid other) const { return Value!=other.Value; }
 static ObjectGuid const Empty;
};
inline ObjectGuid const ObjectGuid::Empty{};
struct Group { struct Member { ObjectGuid guid; }; ObjectGuid Guid{100}; std::vector<Member> Members;
 auto const& GetMemberSlots() const { return Members; } ObjectGuid GetGUID() const { return Guid; } };
struct PlayerbotAI { bool HumanMaster=false; unsigned Calls=0;
 bool HasGameClientMaster() const { return HumanMaster; } void LeaveOrDisbandGroup() { ++Calls; } };
struct Player { ObjectGuid Guid{7}; Group* Party=nullptr; bool Self=false; bool HasAI=true; PlayerbotAI AI;
 ObjectGuid GetGUID() const { return Guid; } Group* GetGroup() const { return Party; } };
inline bool IsSelfBot(Player* player) { return player->Self; }
#define GET_PLAYERBOT_AI(player) ((player)->HasAI ? &(player)->AI : nullptr)
namespace ObjectAccessor { inline Player* Connected=nullptr;
 inline Player* FindPlayer(ObjectGuid guid) { return Connected && Connected->GetGUID()==guid ? Connected : nullptr; } }
""")
            for name in ("Common", "ObjectGuid", "Group", "Player", "PlayerbotAI", "Playerbots", "ObjectAccessor"):
                (scratch / (name + ".h")).write_text('#include "CoreFakes.h"\n')
            (scratch / "operation.cpp").write_text(r"""#include "RoutineGroupLeaveOperation.h"
#include <cassert>
using namespace Playerbots::RoutineMaintenance;
bool enabled=true;
bool policy(std::uint32_t guid, Action) { return enabled && guid==42; }
int main() {
 RegisterProvider(&policy);
 Player bot; Group group; bot.Party=&group; ObjectAccessor::Connected=&bot;
 RoutineGroupLeaveOperation operation(bot.GetGUID(),group.GetGUID());
 assert(operation.IsValid());
 group.Members.push_back({{42}});
 assert(!operation.Execute()); assert(bot.AI.Calls==0);
 group.Members.clear(); bot.Guid.Value=42;
 RoutineGroupLeaveOperation protectedBot(bot.GetGUID(),group.GetGUID());
 assert(!protectedBot.Execute()); bot.Guid.Value=7;
 bot.AI.HumanMaster=true; assert(!operation.Execute()); bot.AI.HumanMaster=false;
 bot.Self=true; assert(!operation.Execute()); bot.Self=false;
 bot.HasAI=false; assert(!operation.Execute()); bot.HasAI=true;
 group.Guid.Value=101; assert(!operation.Execute()); group.Guid.Value=100;
 assert(operation.Execute()); assert(bot.AI.Calls==1);
 ObjectAccessor::Connected=nullptr; assert(!operation.Execute()); ObjectAccessor::Connected=&bot;
 UnregisterProvider(&policy); assert(!operation.Execute()); assert(bot.AI.Calls==1);
}
""")
            for standard in ("c++17", "c++20"):
                subprocess.run(
                    [
                        "c++",
                        f"-std={standard}",
                        "-Wall",
                        "-Wextra",
                        "-Werror",
                        "-pedantic",
                        "-I" + str(scratch),
                        "-I" + str(ROOT / "src/Bot"),
                        "-I" + str(ROOT / "src/Script/WorldThr"),
                        str(scratch / "operation.cpp"),
                        "-o",
                        str(scratch / "operation"),
                    ],
                    check=True,
                )
                subprocess.run([str(scratch / "operation")], check=True)


if __name__ == "__main__":
    unittest.main()
