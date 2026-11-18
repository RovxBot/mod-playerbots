# Optional routine-maintenance veto v1

An optional process-lifetime provider can veto generated/destructive maintenance
by low character GUID and action. Missing provider returns false: all existing
permission, LFG, safety and human-control checks remain authoritative. False is
no objection, never a grant. No autonomy implementation or database dependency
is imported here. Normal XP, loot, quests, paid trainers, professions, auctions,
RDF, explicit group commands and dungeon execution are not globally intercepted.

`RoutineMaintenancePolicy.h` is engine-independent C++17. Register a static
function with `RegisterProvider`, verify ownership with `IsProvider`, and remove
only that owner with `UnregisterProvider`. The provider's code and storage must
outlive every map thread; dynamic unload is unsupported. Register before bot
factories/login maintenance, keep unknown candidates protected until durable
state is loaded, and remove the provider when its feature is disabled. The
provider must do no SQL, config reads or engine-object mutations on map threads.
Both modules must be statically linked into the same worldserver.

Actions distinguish Randomize, LevelReset, Refresh, RandomTeleport,
GroupTeardown, PopulationTrim, LegacyGuildAssignment and AccountDeletion.
GUID **0** is reserved for a bulk startup AccountDeletion or legacy guild-deletion
query; an active persistence provider must veto these realm-wide resets until a
separately audited retirement/deletion procedure exists. LegacyGuildAssignment
also gates `InitGuild` before auto-membership or tabard generation. The provider
must opt in to suppressing it only with a usable explicit enrollment lifecycle.

`RoutineMaintenance.h` adapts Player* to a single cached GUID query. It never
enumerates group members from factory/map-thread calls. Routine group leaves use
the existing world-thread processor: retain bot/group GUIDs, expire after 30
seconds, revalidate the party and human controller, then check all member-slot
GUIDs (including offline members) before using the original packet/permission
path. Stale, disconnected or disabled-provider jobs are dropped; restart simply
drops these ephemeral maintenance jobs. Login validation and routine population
pauses also check parties on the world thread. Work is bounded by party/raid size
at those operations; there is no realm/guild scan. The
missing-provider fast path is an atomic pointer load. Aggregate atomic veto
counts are available through `GetVetoCount(Action)`; no per-tick logs are added.
Explicit shutdown logout and existing authorized group commands retain their
original path. Routine no-human population pauses use `LogoutAllBots(true)`.

## Enforced boundaries

| Boundary | Enforcement |
| --- | --- |
| Manager Randomize/First/Min, IncreaseLevel, Clear | Entry guards before RNG, event, group or character effects |
| Factory Prepare/Randomize/Refresh/ClearEverything | Entry guards, including alternate console/player-manager callers |
| Generated factory resources, skills, talents, glyphs, equipment, quests, inventory cleanup and instance unbind | Underlying helper guards, including maintenance/trainer convenience commands |
| Bracket census/flagging, surplus redistribution, deferred execution | Exclude protected candidates; erase newly protected queued resets before execution |
| ResetBot/SkipBotLevel, played-time, login and natural level-up reset | Both trigger-level and direct mutation guards; natural XP is untouched |
| Manager Refresh/revive/strategy changes, idle teleports and all teleport overloads | Guard before effects; ProcessBot gates individual destructive branches |
| Random bot-led group dissolution and login group validation | Veto for every party member, including offline GUIDs |
| Expired add events, routine logout/removal | Recheck before reclaiming population slots |
| Startup account deletion, random-guild deletion, automatic InitGuild | Optional veto before bulk SQL, disband, membership or free tabard |
| Login and active-provider manager XP policy | Protected bots do not retain the filler no-XP flag after online enrollment/cache readiness |

`rndbot reset` still clears only the playerbots event store. It does not touch
module-owned enrollment/audit/receipts. Subsequent factory/manager calls remain
guarded. Existing core paid respec/trainer and dedicated player-command talent
routines remain outside routine maintenance; this is not a blanket progression
freeze. Factory-generated free training, attunement rewards, consumables and
repairs are suppressed for protected GUIDs.

## Branch compatibility and rollout

PR #21 now targets **master** directly at base
`59e5f26d417fca173d969cd6a1064ec9638b1afa`. Its original
`test-staging` maintenance changes have been replayed against the exact
master source files, using the independently reviewed master compatibility
patch with strict hunk/context validation. The head retains the original
feature branch history via a merge commit whose other parent is master;
the PR diff contains only the maintenance implementation, standalone tests
and this documentation (not the 184 commits already present on master).
The newer master profession and guild-bank deposit functionality is retained.

**Policy exception still needed:** this repository's
`.github/workflows/check_pr_source.yml` requires every PR into master to
come from a branch literally named `test-staging`. The owner explicitly
requested retargeting this PR directly to master; the workflow was not
bypassed or modified by the port and will reject PR #21 until the
maintainer approves a specific exception or changes the workflow.

Core previously inspected for source syntax checks: playerbots AzerothCore **test-staging**
`0554bcdd664ff6d85291ca2a10fd31d3e37e89e6`. This is the owner's stated production
core branch. The documented normal pairing says module master/core Playerbot;
that discrepancy remains a full integration gate. Neither syntax success nor
source ancestry demonstrates production compatibility.

Deploy only after the exact combined sources are linked, startup/disabled mode
are checked, and a controlled filler/Pending/Persistent pilot records progression
through repeated cycles and restart. Roll back by removing the provider/feature
and reverting the change; preserve durable state owned by the provider module.
Before rolling back an enabled pilot, pause destructive maintenance or stop the
realm so protected characters cannot be reset when the provider disappears.

## Verification scope

Standalone callback tests cover missing/disabled provider, ownership conflicts,
registration/removal, every action, concurrent readers and aggregate counts.
Source-extracted entry guards execute with fake Player/Group types under C++17
and C++20. Static contracts cover deferred/reclaim/bulk-delete boundaries and the
master deposit ordering. These tests do **not** execute full engine methods.
The original implementation and the master compatibility patch were
syntax-checked against pinned core headers by Codex. The replayed master
port must be retested by the PR's lightweight CI and later a complete
linked worldserver build. No full compilation/link,
worldserver startup, game-server pilot or pmon baseline was performed. Local
callback timing is a microbenchmark only; measure full realm overhead separately.
No deployment or production config/database was changed.

## Enrollment handoff API (release candidate)

`RandomPlayerbotMgr::IsDedicatedRandomAccount(account)` reads an O(1) world-thread
snapshot populated exclusively by read-back of persisted explicit type-1 assignments.
A failed read leaves it empty, even if prefix-based assignment was attempted. It is false until
assignment completes. `RandomBotAccounts` is a factory-populated broad pool (also
AddClass/unassigned and possibly empty when creation is skipped); neither it nor
an account prefix establishes autonomy eligibility. `IsAccountType()` performs
synchronous SQL and is unsuitable for autonomy observation/tick paths.

`AutonomyEnrollment.h` v1 installs one process-lifetime per-character login veto.
`AddPlayerBot` checks before query-holder initialization; the asynchronous callback
checks again before `HandlePlayerLoginFromDB`. A rejected callback follows normal
`AbandonPendingLogin` and deferred session cleanup. Missing provider preserves
existing permissions and behavior. `PlayerbotHolder::HasPendingBotLogin` is a
world-thread observation, not a login bypass. The caller must hold its reservation
until outstanding login cleanup and normal `LogoutPlayerBot`/save have completed;
then persist Pending and use the ordinary login path. No `characters.online`
updates, forced account classification, bot AI or guild orchestration live here.

Exact baseline: master `08b6bd127513d7273b1fcb765e537c5a3cd813ec`.
The release owner explicitly requires PRs to their master fork. Existing source
promotion governance may reject that branch name; no workflow exemption is made.
Keep the companion PR draft: full-build workflows skip drafts. Cheap tests cover
callback ownership/per-character scope and both source barriers; real asynchronous
engine login/logout, map-thread races and combined linking remain pilot gates.
