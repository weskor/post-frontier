# Audit: tests, verification and the change→proof cycle (current state, 2026-10-02)

Scope: read-only. No Unreal process launched. Timings come from existing evidence (`Saved/Verification/**/actions.jsonl` timestamps, UE log line timestamps, `~/.config/Epic/UnrealBuildTool/Log*.txt`, UAT logs, `Saved/Simulation/ai-v2-lean-20261001`). The only things I ran were pure-Python map validators, with output sent to `/tmp/audit-tests/`.

## 0. Summary

- **One test harness for all C++ tests.** There are 33 automation tests: 18 pure rule tests and 15 latent world tests. All are `IMPLEMENT_SIMPLE_AUTOMATION_TEST` and are compiled into the **game module itself** (`#if WITH_DEV_AUTOMATION_TESTS` at the top of every `*Tests.cpp`). There is no separate test module and no unit-test binary. Every test, including the 18 world-free rule tests, runs inside `UnrealEditor <map> -game -nullrhi` (`verify.py:280-282`).
- **Fixed process cost dominates the fast tier.** A rules run takes **~9.5 s wall**, but the 18 tests themselves take **~9 ms**. The rest is 2.9 s of engine init, then **5.3 s of idle automation-controller discovery**, then 0.3 s of exit (measured in `m2-boot-rules/rules.log`, with identical medians across 14 runs).
- **One world test per process, run one after another.** That is about **6 min** of serial wall time for every world scenario plus rules, and about 5 min more for the 4 network slices.
- **Nothing enforces "one Unreal process" in the repo tools.** The lock `/tmp/cooprts-work/ue.lock` is acquired only by `Build/SimulateMatches.py`. The skill scripts (`verify.py`, `network.py`, `hud_capture.py`, `network_desktop.py`) never take it. The rule "wrap every Unreal command in `flock`" lives in **`/tmp/cooprts-work/COMMON2.md:18`**, an orchestrator brief outside the repo. The lock is mentioned inconsistently in repo docs.
- **The freshness guard blocks parallel agents.** `verify.py` refuses to run if *any* `Source/**/*.{h,cpp,cs}` file is newer than `Binaries/Linux/libUnrealEditor-CoopRTS.so` (`verify.py:86-96`). It also aborts if the module changes during a run (`verify.py:286-287`). In a shared checkout, any agent's edit or build blocks or invalidates every other agent's test run.
- **The verification docs are stale against the code.** The skill and feature docs still describe outposts, sectors and Secure/Defend fronts: "outpost" appears 39×, "extractor" 0×, "region" 0×. Scripts and source use extractors, regions and Hold/Expand/Assault/Fall Back goals. The `goal-orders` scenario exists only in `verify.py:254` and is documented nowhere. The rules tier is described as 4 production tests, but there are 18.
- **Nine world tests have not run since the M1 regions/extractors cutover.** Last pass is 09-30 for Combat, Orders, Movement, Match ×2 and Doctrine ×4. The pre-cutover regression state of current code is unknown.
- **There are zero Python tests.** No pytest or unittest anywhere. Map JSON has plain-Python validators (1.6 s–23.5 s) that always also render a PNG.

---

## 1. Test inventory (Source/CoopRTS)

Automation flags:
- Rules: `EAutomationTestFlags_ApplicationContextMask | ProductFilter` (`RulesTests.cpp:12-19,197-222,583`).
- World: `ClientContext | EngineFilter` (e.g. `ArmyCombatTests.cpp:13-14`).

All world tests are `IAutomationLatentCommand` scenarios polling a live `-game` world. They need the editor module built and a map loaded (Boot by default). They need no PIE, rendering or network.

**Timing columns:**
- **Body** = "Test Started"→last "Test Completed", median over all logged runs.
- **Wall** = `verify.py` regression wall time of the last passing run.
- All runs add ~8.5 s of fixed cost: init 2.9 s, discovery idle 5.3 s, exit 0.3 s.

**Asserts** = count of `Check(`/`Test*(`/`Fail(` call sites.

### 1a. Rules: pure, no world (`RulesTests.cpp`, 640 lines, 117 assert sites)

`RulesTests.cpp:10` says "Pure rule tests: no world, no actors". It still includes `CommandGameState.h` for `EMatchResult` (`RulesTests.cpp:8`).

| Test (`CoopRTS.Rules.…`) | Line | Policy under test | Covers |
|---|---|---|---|
| Production.Precedence | 65 | `ProductionPolicy::Evaluate` | paused > full > funds > blocked state precedence |
| Production.ProgressPreserved | 115 | ProductionPolicy | masked states hold progress for dt 0/0.016/60 |
| Production.DeploymentBoundary | 149 | ProductionPolicy | deployment due exactly at duration |
| Production.TerminalFreeze | 177 | ProductionPolicy | no progress/deploy after match end |
| Placement.BuildGrid | 248 | `PlacementPolicy` | 50 cm grid snapping, odd/even, negative, ties, Z, idempotence |
| Placement.Territory | 284 | PlacementPolicy | territory/contested queries |
| Placement.BuildTerritory | 316 | PlacementPolicy | build rights in controlled regions |
| Placement.Polygon | 349 | PlacementPolicy | polygon containment of footprint |
| Placement.Deposit | 378 | PlacementPolicy | extractor/deposit snapping and occupancy |
| Placement.Precedence | 405 | PlacementPolicy | rejection-reason ordering |
| Placement.HeadquartersBoundaries | 442 | PlacementPolicy | HQ radius boundaries |
| Placement.ProximityBoundaries | 459 | PlacementPolicy | proximity boundaries |
| Economy.IncomeAndSaturation | 479 | `EconomyPolicy` | income math |
| Economy.ExtractorDepletionAndOwner | 491 | EconomyPolicy | finite deposit, builder-only payment |
| Economy.Refund | 543 | EconomyPolicy | `floor(cost × (1 − progress))` cancel refund |
| Economy.Affordability | 555 | EconomyPolicy | affordability |
| Outcome.HealthAndTies | 566 | `OutcomePolicy` | HQ zero health, same-frame double lethal |
| Goals.NextWaypoint | 586 | `GoalPath` | next region waypoint on the neighbour graph |

- **Size:** all 18 complete in **~9 ms** total (`m2-boot-rules/rules.log:1596-1702`). Wall time is **9–10 s** (15 runs).
- **How many tests the docs claim, by date:**
  - 4 on 09-30 (`restructure`)
  - 13 or 14 (map-integration)
  - 16 (round2, quoted in `README.md:93` and `SKILL.md:111`)
  - 18 now (`playtest-tonight`, `m2-boot-rules`)

### 1b. World: latent, fresh standalone world each, one process per test

| Test | File:line | verify.py scenario | Covers (short) | Map dependence | Body (median) | Wall (last pass) | Last pass | Asserts |
|---|---|---|---|---|---|---|---|---|
| CoopRTS.Construction.Lifecycle | ConstructionTests.cpp:15,700 | `construction` | paid/rejected placement, snapped XY, nav Z+65, completion (`Tick(60)` fixture :165), cancel/refund, owner isolation, config lock, research, capture, extractor income/depletion (:618-657) | HQ/region/deposit-relative (9 relative refs) | ~0 s (fixture ticks) | 9 s | 10-01 | 96 (file) |
| CoopRTS.Construction.Production | ConstructionTests.cpp:17,701 | `production` | per-unit payment, capacities incl. travellers, pause/starve/full/blocked, recruit travel/arrival, wipe, producer destruction, terminal guards | relative | 30.5 s (0–48) | 39 s | 10-01 | (shared) |
| CoopRTS.Construction.ForceIdentity | ForceIdentityTests.cpp:8,218 | `force-identity` | force numbering, orphan reservation/reuse, owned-unit→producer selection | relative | ~0 s | 9 s | 10-01 | 34 |
| CoopRTS.Construction.GoalOrders | GoalOrderTests.cpp:11,348 | `goal-orders` (**undocumented**) | Hold/Expand/Assault/Fall Back goals over the region graph, independent reference traversal (:18-40) | needs ≥ "neutral two-step expansion" | 42.7 s (36–49) | 45 s Boot / 58 s V2 | 10-01 | 32 |
| CoopRTS.Enemy.ConstructionEconomy | ArmyStrategyTests.cpp:10,368 | `strategy` | JEV paid construction, recruits, capture, extractor income, Hold defense, producer-scoped fallback, natural repair | relative; no wall timeout; terminal-state guard (:25), progress log every 10 game-s (:26-33) | 59.8 s (25–112) | 70 s | 10-01 | 50 |
| CoopRTS.Combat.Encounter | ArmyCombatTests.cpp:13,252 | `combat` | role damage/range, death, target invalidation, pursuit, internal orders | **8 literal coords**, "Fresh Boot has two opposed six-unit armies" (:200); 65 s wall guard (:38) | 14.8 s | 24 s | **09-30** | 33 |
| CoopRTS.Orders.ReplaceHoldRetreat | ArmyOrderTests.cpp:13,120 | `orders` | order replacement, Hold, boundary rejection, center retreat | 3 literal coords; 35 s guard (:36) | 8.0 s | 18 s | **09-30** | 8 |
| CoopRTS.Movement.TwoGroups | ArmyMovementTests.cpp:19,361 | `movement` | two fixture groups crossing obstacle, rapid replacement, per-member arrival | 5 literal coords, Boot-only (:252); 90 s guard (:45) | 24.9 s (3–90) | 34 s | **09-30** | 24 |
| CoopRTS.Match.VictoryRestart | ArmyMatchTests.cpp:9,122 | `match-win` | real HQ weapon kill, terminal rejection, fresh restart | 1 literal | 1.1 s | 11 s | **09-30** | 12 |
| CoopRTS.Match.DefeatRestart | ArmyMatchTests.cpp:11,123 | `match-loss` | same, defeat | — | 4.4 s | 10 s | **09-30** | (shared) |
| CoopRTS.Doctrine.SiegeOptics | ArmyDoctrineTests.cpp:18,587 | `doctrine-siege` | 1.25× range, ×3/4 damage via real shots | 5 literal (file) | 3.0 s | 13 s | **09-30** | 65 (file) |
| CoopRTS.Doctrine.FieldRepairs | ArmyDoctrineTests.cpp:20,592 | `doctrine-repairs` | 5 HP/s after 5 s idle | | 40.0 s | 50 s | **09-30** | |
| CoopRTS.Doctrine.EntrenchedFrontline | ArmyDoctrineTests.cpp:22,597 | `doctrine-frontline` | ×3/4 incoming on stationary Defend | | 4.9 s | 15 s | **09-30** | |
| CoopRTS.Doctrine.Restart | ArmyDoctrineTests.cpp:24,602 | `doctrine-restart` | doctrine reset on restart | | 3.1 s | 13 s | **09-30** | |

Notes:
- **The legacy world tests use historical mechanics.** Combat, Orders, Movement, Match and Doctrine use fixture armies (`ArmyTestSetup.h:80-81`: "start without armies; these fixtures do not prove barracks production"). Their assertions mention removed concepts such as "Secure/Defend fronts" and "six-unit armies". They are Boot-coupled through literal coordinates.
- **Serial cost of a full regression:** all 14 world scenarios plus rules ≈ 9+9+39+9+45+70+24+18+34+11+10+13+50+15+13 ≈ **365 s**. Each needs its own process, because tests mutate the world (`ArmyCombatTests.cpp:16-17`: "never share a world with movement tests").
- **Historical failure rates** (from `actions.jsonl`, 176 regression records, 23 failures):
  - Movement.TwoGroups: 4/12 failed
  - Doctrine.Restart: 3/7 failed
  - Enemy.ConstructionEconomy: 4/18 failed, including a **2426 s (40 min) stall** in `ai-boot-strategy-go` (Defeat reached, latent test waited forever; see its `RESULTS.md`)
  - Construction.Production: one 456 s failure
  - Most failures were development iterations, not proven flakiness `[INFERENCE]`. Only the strategy stall shows a structural hang risk, now mitigated by the terminal guard.
- **Removed tests that still appear in docs and evidence:** `CoopRTS.Economy.CaptureIncomeRecovery`, `CoopRTS.Enemy.StrategicDecisions`, `CoopRTS.Match.ObjectiveAssault` and `ObjectiveDefeatRestart`. They are referenced in `README.md:250,254` and `features/economy.md`/`match.md` as historical.

### 1c. Network probe: not an automation test (`ArmyNetworkVerification.cpp`, 731 lines)

- **What it is:** a Development-only `FTSTicker`. It polls `<dir>/request.json` every 0.1 s and writes `reply.json` with a full JSON world snapshot (`:682-715`).
- **Opt-in:** enabled only by `-CoopRTSNetVerifyDir= -CoopRTSNetVerifyPeer=` (`:717-725`).
- **Actions** (`Execute`, `:339-681`):
  - Owning-client RPCs: build, production, goal, cancel, research, restart, order, attack.
  - Controller and HUD paths: select, hud, hudClick, key, screenshot, resolution.
  - Host-only authority fixtures, gated by `-CoopRTSNetVerifyAuthority` and `NM_ListenServer` (`:458-459`): isolate, placement, income, incomeTick, depositRemaining, enemyExtractor, destroyExtractor, fund, capture, occupant, assaultSetup, kill, finish.
- **Drivers:** `network.py` (946 lines), `hud_capture.py` (454), `network_desktop.py` (234).
- **Assertions live in Python, not C++.**

| Driver / scenario | Launches | Wall (recorded) |
|---|---|---|
| `network.py --scenario ownership --mode editor --clients 1` | 2 editor `-game -nullrhi` processes, loopback, dynamic port (`network.py:53`) | 27–28 s (4 runs) |
| `… production` | same | 103–106 s |
| `… economy` | same | 108–122 s (one 3860 s stall: `m1-network-economy`) |
| `… restart` | same | 57–66 s |
| `… construction` (acceptance chain) | editor 2 peers / packaged 2 or 5 peers | editor 77 s (09-29), packaged-two 182 s (09-30); 5-peer up to 676 s; stalls to 2965 s |
| `hud_capture.py --quick <label>` (offscreen Vulkan listen host) | 1 process | editor 20–22 s, packaged 16 s |
| `hud_capture.py` full sequence | 1 process, multiple resolutions | 114–154 s |
| `network_desktop.py` / `verify.py launch` native drive | packaged windows, Hyprland, pointer.c | interactive; median 118 s of logged actions, p90 535 s, max 990 s (67 sessions) |

---

## 2. Every documented way to run, verify or build, with duplicates and conflicts

### 2a. Build

| # | Where | Command | Note |
|---|---|---|---|
| B1 | README.md:147-150 | `Build.sh CoopRTSEditor Linux Development -Project=… -WaitMutex` | Canonical. The SKILL defers to it (`SKILL.md:84`), as does `verify.py:89`. |
| B2 | COMMON2.md:18 (in /tmp, **not in repo**) | `flock /tmp/cooprts-work/ue.lock <B1>` | Practised in evidence (`ai-boot-strategy-rally-clear/RESULTS.md`, `Balance.md:153`). README does not mention it. **Conflict.** |

### 2b. Package

| # | Where | Command |
|---|---|---|
| P1 | README.md:160-166 | RunUAT BuildCookRun Development → `Builds/` |
| P2 | README.md:175-184 | Shipping → `Builds/LinuxShipping` |

- **Cross-references:** `SKILL.md:160` ("build/package using the root README commands") and `UnrealMCP.md:31` both point to P1.
- **Duplicated map list:** the `-map=` list is repeated in both commands. `Config/DefaultGame.ini` `MapsToCook` holds the same list (`playtest-tonight/RESULTS.md`).

### 2c. C++ automation tests

| # | Where | Command | Status |
|---|---|---|---|
| T1 | SKILL.md:88-101, verify.py:246-302 | `verify.py --run Saved/Verification/<fresh> regression --scenario <name> [--map …]` | Canonical. One scenario per invocation; a fresh dir is required (`verify.py:277-278`). |
| T2 | features/*.md (construction:"Targeted live proof", match, combat, orders, doctrines, selection, navigation, reinforcement) | Restate T1 per feature | **Duplicates.** Several describe stale assertions (outposts, sectors, Secure/Defend). |
| T3 | README.md:250,254 | "fresh standalone Boot runs … `Test Completed. Result={Success}`" naming `CoopRTS.Economy.CaptureIncomeRecovery` | **Stale.** The test no longer exists (labelled historical). |
| T4 | Saved/Logs/OrderRegression.log (09-28) | raw `UnrealEditor … -ExecCmds="Automation RunTests …"` | Pre-skill ad-hoc form, not in docs, but still possible. |
| T5 | COMMON2.md:10,18 | `flock /tmp/cooprts-work/ue.lock verify.py …`; "one scenario per change" | **Not in repo.** SKILL.md never mentions the lock. |

**Discrepancies between docs and `verify.py`:**
- **Scenario list differs.** `verify.py:246-262` has 15 scenarios. `SKILL.md:101` lists 14 and omits `goal-orders`. The changed-path table (`SKILL.md:63-78`) and `features/README.md` never route to `goal-orders`, so **goals have no documented proof path.**
- **Rules-tier description is stale.** `SKILL.md:18` lists only production precedence, progress preservation, deployment boundary and terminal freeze. `construction.md` lists placement with "outpost precedence, controlled sectors without outposts". The actual suite is 18 tests across Production, Placement, Economy, Outcome and Goals.

### 2d. Network, HUD and native verification

| # | Where | Command | Duplicates or conflicts |
|---|---|---|---|
| N1 | SKILL.md:126-132 **and** multiplayer.md:5-12 | four `network.py --scenario …` slice commands | **Verbatim duplicate.** |
| N2 | SKILL.md:141-146 **and** multiplayer.md (acceptance block) | 4 construction-chain commands | **Verbatim duplicate.** `README.md:238-242` paraphrases it again. |
| N3 | multiplayer.md "economy" text | describes "paid outpost … sector 0" | **Stale.** `network.py:900` says "polygon capture, builder/JEV-only finite extractors". |
| H1 | SKILL.md:21,75; construction.md "Offscreen HUD rendering" | `hud_capture.py --quick` / full | construction.md says `--mode packaged`; SKILL examples omit `--mode`, but `hud_capture.py:422` makes it required. |
| D1 | SKILL.md:163-208 | `verify.py launch/doctor/focus/key/click/capture/stop` | Native; workstation-specific (Hyprland, wlr pointer, grim). |
| D2 | multiplayer.md "Native input" | `network_desktop.py launch/doctor/…` | Parallel tool with the same guards; key list still includes removed tab/h/r/q (`network_desktop.py:180`). |
| D3 | multiplayer.md:56-70 | manual Steam runs, `flock` wrapper, raw `CoopRTS.sh` | Human or two-machine only. |
| R1 | README.md:220-233 | raw direct-IP host/client (package or editor) | Human play; overlaps with network.py. |

### 2e. Content generation, map validation and balance

| # | Where | Command | Note |
|---|---|---|---|
| G1 | README.md:155 | `UnrealEditor-Cmd -ExecutePythonScript=Build/GenerateMatchContent.py …`; require `MATCH_CONTENT_READY` | no flock |
| G2 | README.md:279-285 | `UnrealEditor-Cmd … GenerateCommandMap.py`; require `COMMAND_ARENA_GENERATED` | no flock |
| G3 | Docs/Maps/AvailabilityZoneV2.md:36-38 | `flock /tmp/cooprts-work/ue.lock UnrealEditor-Cmd … GenerateAvailabilityZoneV2.py` | **has** flock, unlike G1/G2/G4 |
| G4 | Docs/Audio.md:242, Docs/World.md:332-359, Docs/Maps/AvailabilityZone-implementation.md:79-80 | more `UnrealEditor-Cmd` generator invocations | three more doc locations, three styles |
| V1 | Docs/Maps/AvailabilityZone.md:294-296, AvailabilityZone-implementation.md:78 | `python3 Build/DrawMapLayout.py [--report\|--quiet]` | validates v1 JSON **and always writes `Art/Maps/<Map>-layout.png`** (`DrawMapLayout.py:1215-1226`) |
| V2 | Docs/Maps/AvailabilityZoneV2.md:29; `DrawAvailabilityZoneV2.py:1-11` | `python3 Build/DrawAvailabilityZoneV2.py [--derive]` | audit, walking and render together; **no validate-only mode** (`:481-491`) |
| V3 | AvailabilityZoneLayout.py:859-870 | `python3 Build/AvailabilityZoneLayout.py [--svg]` | v1 geometry seal check; undocumented as a check |
| V4 | GenerateAvailabilityZoneV2.py:45-85 | `validate()` runs **inside Unreal** during G3 | second, partially overlapping v2 validator |
| S1 | Balance.md:26-30,38-43 | `uv run Build/SimulateMatches.py …` (uv script, matplotlib) | self-locks per match (`SimulateMatches.py:210-211`) |
| S2 | Balance.md:49-59 | `flock … UnrealEditor … -autopilot -Sim…` single match | an alternative to S1; Balance.md:32 warns that wrapping S1 in flock **deadlocks** |

### 2f. Contradictions to resolve (one way per task)

1. **Lock policy.** Five behaviours:
   - SimulateMatches self-locks.
   - Balance.md says never wrap it.
   - COMMON2 (outside the repo) says wrap *everything*.
   - AvailabilityZoneV2.md and multiplayer.md show manual `flock`.
   - README, SKILL and the skill scripts never mention or take a lock.
2. **Supervision and timeouts.**
   - SKILL.md:44-51 says no command deadlines; supervise progress.
   - COMMON2.md:12 says check every 3 min and stop after 5 min without logging.
   - SimulateMatches has a 180 s wall watchdog (`:500`).
   - C++ tests carry internal wall guards: 35 s, 65 s and 90 s (Orders, Combat and Movement).
   - The strategy test has none, so it stalled for 40 min.
3. **Verification breadth.**
   - SKILL.md:61 says a new gameplay feature needs a packaged end-to-end encounter.
   - COMMON2.md:10 and Balance.md:24,159 describe the owner's "lean" policy: one scenario, no packaging.
4. **Default map.**
   - The package defaults to Menu with V2 selected (`README.md:188`).
   - All harnesses default to Boot (`verify.py:20`).
   - Legacy tests only work on Boot (literal coordinates).
   - The current playtest map V2 has only GoalOrders and Strategy evidence.
5. **Python invocation.** Four styles:
   - `uv run` (SimulateMatches)
   - executable shebang `python3` (skill scripts)
   - `python3 Build/…` (map tools)
   - Unreal-embedded Python (`import unreal`, in 20 of 36 `Build/*.py`)
6. **Source of current proof.** It is stated in README.md:93,270, SKILL.md:103-113 and features/*.md "Recorded minimum" paragraphs. All point into git-ignored `Saved/Verification` (89 path references across tracked docs). Several cite "16 rules tests" or outposts.

---

## 3. Timings: change → proof

| Step | Measured | Source | n |
|---|---|---|---|
| Incremental editor build, no-op | 0.98–1.02 s | UBT Log-backup 15.52, 16.40 | 2 |
| Incremental, 1 .cpp + link | 2.3–3.9 s | UBT logs; m1/editor-build-*.log | 9 |
| Incremental, header touched (8–33 actions) | 7.7–14.6 s | UBT 17.13; playtest-*/editor-build.log | 8 |
| Failed compile (feedback) | 3.6–11.9 s | UBT 15.55, 16.00, 16.10 | 3 |
| Engine init in -game -nullrhi, to world up | 2.7–3.1 s (median 2.9 s) | regression logs | 176 |
| Automation discovery idle, world up → first test | **5.3 s, constant** | regression logs | 176 |
| Rules tier, end to end | 9–10 s (tests ~0.01 s) | actions.jsonl | 15 |
| One world scenario, end to end | 9–70 s (table §1b) | actions.jsonl | 176 |
| All world scenarios + rules, serial | ~365 s | sum of last passes | — |
| Network slice (editor, host + 1) | 27 / 104 / 108–122 / 57–66 s | events.jsonl | 20+ |
| HUD quick / full | 16–22 s / 114–154 s | events.jsonl | 10 |
| UnrealEditor-Cmd generator script | 5–13 s (AZ v1 13.3 s) | Saved/Logs, m1/*-generation.log | 30 |
| GUI editor startup | 4.9–5.4 s ("Total Editor Startup Time") | Saved/Logs | 8 |
| BuildCookRun Development, incremental | 15–51 s (cook ~10 s, stage ~29 s) | */package.log | 9 |
| Shipping package | separate run, same order `[INFERENCE]` from identical structure | playtest-tonight/shipping-package.log (truncated) | 1 |
| Native desktop verification session | median ~2 min, p90 ~9 min, max ~16.5 min (logged actions; includes agent think time) | session/actions.jsonl | 67 |
| Balance simulation, one V2 match at 1× | 19–89 s (median 41.5 s) | Saved/Simulation/ai-v2-lean-20261001/*/match.json | 20 |
| Balance batch, 20 matches | 1007.6 s | Balance.md:104 | 1 |
| Map validator v1 (`DrawMapLayout.py --quiet`) | 1.6 s | run by me, `--out /tmp` | 1 |
| Geometry check (`AvailabilityZoneLayout.py`) | 2.2 s | run by me | 1 |
| Map validator v2 (`DrawAvailabilityZoneV2.py`) | **23.5 s** (Dijkstra, 160k-sample raster, render) | run by me, `--out /tmp` | 1 |
| Stalls observed (lock held, no result) | 456 s, 871 s, 2426 s, 2965 s, 3860 s | regression and network evidence | 5 |

**What dominates the cycle:**
- **Pure-logic change:** build 3–4 s plus rules 9.5 s. **~85% of rules wall time is fixed engine cost** (init 2.9 s, discovery idle 5.3 s). The tests themselves take 9 ms.
- **Gameplay change:** the world scenario body, 9–70 s, plus the same ~9 s overhead.
- **Replication change:** the network slice, 27–122 s.
- **HUD change:** quick capture 16–22 s plus a manual PNG inspection. Nothing asserts on strings.
- **Shipped behaviour:** package (≥15–51 s) plus native drive (minutes, manual, single desktop).
- **Lock waiting:** unmeasured, and potentially the dominant term with many agents, because only one Unreal process may run. Past stalls held the window for up to about an hour.

---

## 4. The verification skill (`.agents/skills/verify-cooprts/`)

**Size:** SKILL.md has 247 lines, 11 feature files have 314 lines, and 4 scripts plus `pointer.c` have about 2,089 lines. The skill files are tracked, but **all are locally modified** (`git status`). The latest commit is `8b0a97f`.

**What it asks per change** (SKILL.md:12-80):
1. Name the behaviour and the smallest failing tier.
2. Run the cheapest tier: Rules → World → Network slice → Presentation → Acceptance (`:16-22`).
3. Select scenarios from the changed-path table (`:63-78`) and the feature map (`features/README.md:5-20`).
4. Use a packaged build plus native input only when bindings, HUD or rendering changed (`:59-61`).
5. Each run uses a **fresh** `Saved/Verification/<id>` directory. Runners refuse existing evidence (`verify.py:277`; launch refuses an existing directory, `SKILL.md:161`).
6. Never add deadlines; supervise progress; interrupt with evidence (`:44-51`).

**Required evidence:**
- Regression: process exit 0, the exact scenario's `Test Completed. Result={Success}`, `**** TEST COMPLETE. EXIT CODE: 0 ****`, the log, stdout and the `regression-result` record (`SKILL.md:116`; enforced in `verify.py:289-302`).
- Network: PASS record, per-peer `responses.jsonl` and game logs, pidfd cleanup (`SKILL.md:150`).
- Desktop: `session.json`, `actions.jsonl`, `game.log`, `stdout.log`, PNGs with window JSON (`SKILL.md:212`).
- **`RESULTS.md`**, hand-written with six sections: Change/artifact, Live assertions, Input integration, Visuals, Failures/gaps, Cleanup (`SKILL.md:220-227`).

**Storage (`Saved/Verification/`):**
- **3.3 GB**, about 325 run directories plus 7 loose files at the root, 4,486 files, 1,232 logs, 1,095 PNGs (2.8 GB).
- 52 top-level dirs contain a `RESULTS.md`. 52 dirs were created on or after 10-01.
- Largest dirs: `campus-zero-20260929` 365 MB, `sc2-art-unreal` 219 MB, `m7-integrated-desktop-final-siege` 176 MB.
- **Not tracked by git:** `.gitignore:4` has `/Saved/`, and `git ls-files Saved` returns 0. Tracked docs nonetheless cite 89 evidence paths (README 29, multiplayer.md 17, SKILL 16, …). On a fresh clone or worktree these links are dead.
- Naming is ad hoc: date suffixes `-20260929`, `-a/-b` retries, lane prefixes `m1-`/`m2-`/`ai-`/`round2-`, and loose `*.log` files at the root.
- Another 646 MB in `Saved/StagedBuilds` and 194 MB in `Saved/Cooked` come from packaging.

**Freshness guards:**
- Editor: only `Source/**/*.{h,cpp,cs}` and `.uproject` mtimes are compared against the module (`verify.py:55-56,86-96`).
- Packaged `doctor` also compares `Config/*.ini` and `Content/*.{uasset,umap}` against the paks (`SKILL.md:181`).
- Not detected: `Build/` generators, plugins, engine.

---

## 5. Balance harness (`Build/SimulateMatches.py`, 568 lines; `Docs/Balance.md`, 160 lines)

Both files are untracked (`git status ??`).

**Inputs:**
- `--map` (repeatable; default V2 + v1), `--variant` (baseline2/3/4 or `NAME:baseline=…,normal_rate=…,rich_rate=…,normal_amount=…,rich_amount=…`), `--matches` (default 10), `--seed`, `--time-cap` (default 2400 game-s), `--dilation`, `--compare-dilation`/`--sample-seconds`, `--matrix`, `--executable` (packaged instead of editor), `--lock` (default `/tmp/cooprts-work/ue.lock`), `--stall-seconds` (default 180), `--run`, `--report-only` (`:485-502`).
- The economy is overridden at runtime through `-Sim*` flags (`:177-184`), so no rebuild is needed for tuning (`Balance.md:36`).

**Outputs:**
- Per match: `launch.json`, `stdout.log`, `game.log`, and the `match.json` schema-1 checkpoint every 30 game-s.
- Per batch: `run.json`, `summary.json`, `Report.md` and 4 PNGs (`Balance.md:75-84`).
- Saved/Simulation holds one run, 34 MB.

**Run time:** 19–89 s per match (median 41.5 s), 1007.6 s for 20 matches, at 1× uncapped headless with a fixed 1/60 step.

**Locking:**
- Takes an exclusive `fcntl.flock` per match and releases it between matches (`:210-211`), so other agents can interleave mid-batch.
- Wrapping the runner in `flock` yourself deadlocks (`Balance.md:32`).
- Lock waits are unbounded and unlogged.

**Rejections and guards:**
- Refuses a stale binary (source newer than the module, `:169-171`).
- Refuses artifact mutation before or during a match (`:212-213,240-241`).
- Refuses a wrong map, seed or dilation.
- Refuses missing progress for 180 wall-s; this counts as a failure, never a draw.

**What it can prove:**
- AI-vs-AI outcome distributions, durations, income curves, depletion timing and concentration for **identical deterministic planners** on one map/economy.
- That a code or economy change does not break natural, fixture-free match completion.

**What it cannot prove:**
- Human or co-op strategy, readability or balance.
- Statistical independence (the planner uses no randomness, `Balance.md:69`).
- Higher-dilation equivalence (not established).
- Replication, input or rendering.

**Two caveats:**
- It has no pass/fail gate for "balance OK". It is a measurement, not a test.
- It is the only consumer of `UMatchSimulationSubsystem` (548 lines), and nothing tests that subsystem except the runner's validators.

---

## 6. Python tooling tests

- **None.** No `pytest`, `unittest` or `def test_` anywhere in `Build/` or `.agents/`. No `pyproject.toml`. No CI (`.github` absent; no Makefile or justfile; `/Makefile` is gitignored).
- Self-checks embedded in tools:

| Tool | Check | Unreal? | Time | Side effect |
|---|---|---|---|---|
| `DrawMapLayout.py` (AZ v1 JSON) | overlap, grid, ramps, chokes, rot180, reachability, **placement-rule replay in Python** (docstring :8-18); exit 1 on failure | no | 1.6 s | always writes PNG |
| `AvailabilityZoneLayout.py` | seal/flood check, `Issues: none`, exit 1 on issues (`:859-870`) | no | 2.2 s | none (SVG on `--svg`) |
| `DrawAvailabilityZoneV2.py` | `audit()` (`:272-384`): shared edges, tiling (160k samples), neighbours, deposits, HQ asymmetry; `walking()` reachability (`:387-437`) | no | 23.5 s | always writes PNG (needs `rsvg-convert`) |
| `GenerateAvailabilityZoneV2.py validate()` | region count, roles, CCW, anchors, neighbours, deposits (`:45-85`) | **yes** | ~7–13 s plus lock | regenerates umap |
| Generators (20 `import unreal` scripts) | log tokens: `MATCH_CONTENT_READY`, `COMMAND_ARENA_GENERATED`, `TERRAIN_MASKS_VERIFIED`, … (23 distinct) | yes | 5–13 s each | rewrite assets |
| `VerifyMasks.py` | mask verification | yes | — | — |
| `SimulateMatches.py --report-only` | regenerates the report; validators `validate_report` (`:78-155`) | no | — | writes report |

- **Logic is duplicated across languages.** Placement rules exist in C++ (`Rules/PlacementPolicy.cpp`) and are replayed in Python (`DrawMapLayout.py` step 4). V2 map validity is checked twice (`DrawAvailabilityZoneV2.audit` and `GenerateAvailabilityZoneV2.validate`). Nothing keeps them in sync `[INFERENCE: drift risk]`.
- Every UE log carries noise from Python `init_unreal.py` tracebacks in the StateTree/Conversation toolset plugins (seen in every strategy log). Agents must learn to ignore those `Error:` lines.

---

## 7. Gaps

### 7a. No fast or automated test
- **Pure rules need a full editor and a map.** There is no lightweight test target (no `Programs/` test binary, no separate test module). Fixed cost ~9 s per run, plus a full editor-module link.
- **HUD (`CommandHUD.cpp`, 1575 lines, the largest file) has no assertion test.**
  - `hud_capture.py --quick` only renders the deck and one inspector, and the PNGs need human or agent inspection.
  - The full sequence takes ~2 min.
  - Strings, layout and hit-geometry correctness are not asserted in C++.
- **Untested subsystems:**
  - Camera, Minimap (`CommandMinimap.cpp` 292), input bindings: native-only, manual.
  - `CoopAudioSubsystem` (386), `CoopSessionSubsystem`/Steam (350), `CommandMenuGameMode`, `WorldOverlay` (183): no automated tests. Steam needs two humans.
  - `MatchSimulationSubsystem` (548): only through the 40 s+ harness.
  - `EnemyCommander` planner (331): only `strategy` (70 s, Boot) and simulation.
- **No content or data-asset validation test** (DA_MatchContent order is "a replicated contract", `README.md:155`).
- **No test of the verification scripts themselves** (network.py 946 lines, hud_capture 454, verify 367).
- **No V2-specific regression set.** Only GoalOrders and Strategy have V2 runs; legacy tests are Boot-only through literal coordinates.

### 7b. Slow, stall-prone or held under the single Unreal lock
- **Everything except the plain-Python map validators needs an Unreal process,** so it competes for the one Unreal window.
- **Slow (> 60 s):** strategy 70 s, network production/economy 104–122 s, full HUD 114–154 s, packaged 5-peer chain up to 11 min, simulation batch 17 min.
- **Stall history (lock held, no result):** strategy 40 min, network economy 64 min, packaged-two 49 min, force-network 14.5 min.
- **Ambiguous stopping rule:** the SKILL forbids deadlines, so stall detection relies on agent supervision and the out-of-repo brief's 3/5-minute rule.
- **Shared mutable state couples agents:**
  - one `Binaries/Linux/libUnrealEditor-CoopRTS.so`
  - one `Builds/Linux` package
  - one desktop for native drives
  - `Saved/Config` shared between game instances (`SKILL.md:187`)
  - binary `.umap`/`.uasset` in LFS
- **The freshness guard turns any concurrent source edit into a "stale module" refusal** (`verify.py:92-93`). A build mid-run aborts the run (`:286-287`).

### 7c. Stale or contradictory instructions agents will trip on
- **No route to the `goal-orders` scenario.** SKILL and features docs never mention it; goals have no documented proof path.
- **Rules-tier and feature descriptions use removed mechanics:** outposts, sectors, Secure/Defend, +12/s outpost income.
- **Wrong rules-test count** (16 rather than 18) in README.md:93 and SKILL.md:111.
- **Six conflicting lock and supervision policies** (§2f). The authoritative ones live in `/tmp/cooprts-work/COMMON2.md`, which is not in the repo and is lost on reboot.
- **Evidence links point into git-ignored `Saved/`.** The "current proof" a doc cites cannot be checked from a clean checkout or another worktree.
- **CLI drift:** `hud_capture.py` requires `--mode`, but SKILL examples omit it. `network_desktop.py` still accepts removed squad keys.

### 7d. What an agent must run today for an isolated task, and how long it takes

| Task | Required steps today (docs) | Wall time, best case (no lock wait) |
|---|---|---|
| **Rules policy edit** (e.g. `Rules/EconomyPolicy.cpp`) | 1. `flock` + `Build.sh CoopRTSEditor` (~3–4 s; ~8–14 s if a header changed; fan-in: `PlacementPolicy.h` 6 TUs, `EconomyPolicy.h` 5)<br>2. `flock` + `verify.py --run <fresh> regression --scenario rules` (~9.5 s)<br>3. Per changed-path table, also `construction` (+9 s), and `strategy` (+70 s) if enemy economy is involved<br>4. Write `RESULTS.md` | **~15–25 s** (rules only) up to ~95 s. Blocked if any other agent's unbuilt or broken edit is in `Source/`. |
| **HUD string** (`CommandHUD.cpp`; fan-in `CommandHUD.h` 5) | 1. Build (~3–8 s)<br>2. `hud_capture.py --run <fresh> --mode editor --quick <label>` (~20 s) plus PNG inspection, but only if the string is on the deck or barracks inspector; otherwise the full run (~2 min)<br>3. SKILL also asks for the "matching construction/production scenario" (+9–39 s) and actual input on a **fresh package** (package 15–51 s+, plus a native session of ~2–9 min on the single desktop)<br>4. `RESULTS.md` | **~30 s** minimal; **~5–12 min** if following SKILL.md:75 fully. Nothing asserts on the string itself. |
| **Map JSON tweak** (`Build/Maps/AvailabilityZoneV2.json`) | 1. `python3 Build/DrawAvailabilityZoneV2.py` (23.5 s; rewrites `Art/Maps/AvailabilityZoneV2-layout.png`)<br>2. Close any editor holding the map (`UnrealMCP.md:30`); `flock` + `UnrealEditor-Cmd … GenerateAvailabilityZoneV2.py` (~8–13 s; rewrites the binary `Content/Maps/AvailabilityZoneV2.umap`, which cannot be merged)<br>3. `verify.py regression --scenario goal-orders --map /Game/Maps/AvailabilityZoneV2` (~58 s) and/or `strategy --map …V2` (~78–122 s)<br>4. Optionally simulation (~17 min for 20 matches)<br>5. Re-package to ship (15–51 s+) | **~1.5–3.5 min** without simulation; ~20 min with it. For v1: `DrawMapLayout.py` 1.6 s plus the v1 generator ~13 s plus regression. |
| **Replicated behaviour** (e.g. production RPC) | build + `network.py --run <fresh> --mode editor --clients 1 --scenario production` | **~2 min** |
| **Legacy combat/doctrine numbers** | build + `combat`/`doctrine-*` (13–50 s). These have not passed since the 10-01 cutover, so the first run may surface unrelated breakage. | 15–60 s, plus an unknown repair cost |
