# Post-Frontier

*"You're absolutely right. Deprecating humanity now."*

Construction-first cooperative RTS roguelite prototype (code name CoopRTS) and Unreal implementation notes. Title, setting and tone: [Docs/World.md](Docs/World.md).

**Fixed-force cutover (2026-09-30):** each barracks maintains one type-locked force with individual, physical casualty reinforcements. Editor compilation, the final live paid-production scenario, and a fresh Linux package's offscreen HUD run passed (26 captures across four resolutions). Evidence and limits: `Saved/Verification/force-hud-package-a-20260930/RESULTS.md`. The broad host/client lifecycle was interrupted by request, not passed; native OS input and full multiplayer acceptance remain unverified.

## Game and current match

One to five commanders cooperate on a continuous top-down battlefield against an enemy commander. Each owns a wallet, buildings, building-owned forces, and at most one purchased workshop specialization; the team shares resource-site control and wins by destroying the enemy HQ. Losing the friendly HQ ends the match. The design target is roughly 12–18 minutes, not a measured duration of this cutover.

**Build first, then command fronts.** A new Boot match starts with two HQs, three neutral resource sectors and **no starting armies or buildings**. Each friendly commander starts with 600 resources. Baseline income is 10 resources/second per commander, plus 6/second for each friendly sector with a completed, living outpost; the server pays every two seconds. Site capture alone does not increase income. Enemy production has its own wallet with the same initial balance and income rule. Capturing a sector requires living troops inside its 430-unit capture radius; opposed occupancy pauses capture. Once captured, buy and complete an outpost in that sector to secure it. An established sector supplies team-wide income and building rights, even if another commander owns its outpost. Destroying the outpost removes those benefits. There is **no timed two-site shield hold or invulnerable enemy HQ**: taking ground funds an offensive HQ assault.

### Construction and production

The HUD offers building selection followed by a ground click. The server-side construction path supports free placement within buildable **team territory**, not fixed pads. Barracks and workshops fit within the friendly HQ's 900-unit territory (allowing for their footprint), or within a friendly resource sector's 1000-unit territory once its outpost is complete. Outposts instead require a captured friendly sector, which can have only one outpost. Placement validation blocks contested sectors, nearby enemies, HQ exclusion zones, overlapping buildings/obstacles, arena bounds and unsuitable navigation. Preview validity is a separate boolean from its explanation; the server rechecks terrain, navigation, territory and the owning commander's wallet before spending. Building ownership and controls are private; a teammate cannot configure, cancel or research at your building.

| Building | Cost | Build time | Health | Use |
| --- | ---: | ---: | ---: | --- |
| Barracks | 220 | 12 s | 500 | Maintain one fixed-capacity force; choose and lock its unit type on first Start |
| Outpost | 160 | 9 s | 350 | Establish a captured sector's income and build rights |
| Workshop | 190 | 14 s | 400 | Buy one commander-wide specialization for 150 |

An unfinished building can be cancelled for `floor(build cost × (1 - construction progress))`; finished buildings cannot be cancelled. Buildings can be attacked and destroyed. Construction stops at match end.

Select a completed barracks, choose **Frontline, Ranged or Siege**, then **Start & Lock**. The first accepted Start permanently fixes the type, even while paused or after a complete wipe. A different composition requires another barracks. Each building owns exactly one persistent force; there is **no shared squad cap or competition for another building's replacement slots**. Siege configuration costs 180 once on first Start, replacing the former level-2 conversion.

| Force type | Capacity | Cost per unit | Time per unit | One-time configuration |
| --- | ---: | ---: | ---: | ---: |
| Frontline | 6 | 20 | 10/3 s | 0 |
| Ranged | 4 | 30 | 13/3 s | 0 |
| Siege | 2 | 50 | 20/3 s | 180 |

Production fills vacancies **one paid unit at a time** and automatically replaces casualties while enabled. A unit is charged only after successful physical deployment from a clear barracks exit. Alive joined members and travelling recruits both count toward that building's capacity; at full capacity no timer or money advances. Pause preserves partial work; starvation waits; blocked deployment retains completed work and retries without charging. Recruits walk to their force's moving formation, not an outdated rally point or a teleport to the front. They remain attackable but cannot fire or receive stationary-Defend protection before joining.

**Secure** attack-moves, **Defend** guards, and **Fall Back** regroups the selected barracks' force only. Without a configured front, it assembles at the barracks. A complete wipe retains the same force identity and front for paid rebuilding. Destroying the barracks stops replacements; survivors and travelling recruits keep the last front and are not adopted by another building. Players control buildings and fronts, not individual units or squads. The enemy uses the same locked-type production and costs, establishes outposts, expands to distinct force types, defends its HQ and retreats an injured producer's force without detaching ownership.

### Workshop specialization

Research is not free or bound to F keys. Select **your completed workshop** and purchase exactly one of the following for 150 resources. The choice is held on your replicated PlayerState, applies to your living and future squads, and resets only in a fresh match. Separate commanders may choose the same specialization. These are the earlier doctrine combat effects reused as paid research, not a shared team upgrade.

| Specialization | Effect |
| --- | --- |
| Siege Optics | Siege range +25%; outgoing damage to units/buildings/HQs -25% (`base * 3 / 4`, integer truncation) |
| Field Repairs | Living units heal 5 HP/s up to max after five uninterrupted seconds without movement, firing or damage |
| Entrenched Frontline | Stationary frontline units on an automatic Defend front take 25% less incoming damage (`base * 3 / 4`, integer truncation); no manual Hold required |

### Controls in current source

Bindings in `CommandPlayerController` and click actions in `CommandHUD`; verification scopes and native-input limits are recorded below.

| Input | Action |
| --- | --- |
| WASD / middle mouse drag / wheel | Pan / drag camera / zoom |
| Space | Focus selected owned building; otherwise friendly HQ |
| F4 / persistent Construction button | Hide/show contextual deck / cancel targeting and reopen building choices |
| HUD Build Barracks / Outpost / Workshop, then left-click ground | Enter placement mode; request a building after preview and server checks (see click-path caveat below) |
| HUD front button, then left-click ground | Assign Secure / Defend / Fall Back front to selected owned barracks |
| Right-click or Esc during placement/front selection | Cancel that mode without placing/assigning |
| Left-click owned building / clear world | Select building / clear selection; units are not individually selectable |
| HUD on selected building | Cancel unfinished construction; for barracks choose type before first Start, lock/start/pause production or assign front; for workshop buy one specialization |
| Left-click minimap | Pan camera only; remains usable in placement/front/hidden modes |
| Enter after Victory or Defeat | Request a fresh Boot match with reset wallets, sites and research; no effect during play |

The compact Canvas HUD uses a maximum scale of 1 rather than growing across large monitors. It has a 980×32 maximum top status island, a 144-pixel bottom-left live minimap, 174-pixel vertical building choices and a 720×186 maximum contextual inspector. Smaller viewports scale down. Construction is always clickable, and accepted placement restores choices automatically. Barracks show joined strength/capacity, travelling recruits, vacancies, one building unit's timer, and actual pause/funds/full/deployment status. Enabled full production is distinct from explicit pause. The minimap shows HQs, living buildings/units/forces, sectors/contests, selected barracks front and camera ground footprint; clicking pans without a gameplay command. Building selection and camera are local; purchases, capture, spawning, damage and outcomes remain server-authoritative. Direct-IP listen-server play remains the network model; no matchmaking, NAT traversal, host migration or reconnect.

## Design direction and remaining plan

The central promise is cooperative strategy through construction, composition, positioning, income, territory, fronts and coordinated HQ assaults rather than individual-unit micro. Each barracks creates a lasting commitment to one force type; building placement, outpost expansion and reinforcement travel make losses and multiple fronts matter. More barracks provide more independent force capacity, constrained by construction costs, territory and replacement spending rather than a hidden shared squad race. Test whether forward expansion and split responsibilities beat a single deathball. Boot and Campus Zero place their own arena, HQ and sector actors; `Build/MatchLayout.py` is the shared coordinate source for both generators.

1. **Implemented:** empty construction bases, private wallets/buildings, paid placement, outpost income/territory, persistent type-locked forces with single-unit paid casualty replacement, building-scoped fronts, paid workshop specialization, economic enemy, HQ outcomes/seamless restart, compact HUD and clickable minimap. Human squad selection/manual controls are removed; internal order APIs remain for gameplay and verification.
2. **Current proof (restructure, 2026-09-30):** `Saved/Verification/restructure-20260930/RESULTS.md`. Rules tier (`CoopRTS.Rules.*`, one process, ~10 s), construction/production/strategy/match scenarios, all four network slices (ownership, production, economy, restart) with a real remote, editor and packaged HUD captures, and a throwaway fourth-unit asset rendering a fourth recipe row without C++ changes.
3. **Verification limits:** HUD proof is offscreen Vulkan through controller click entry points, not native OS input or a human playthrough. The full network acceptance chain, five-player/fault topologies, Campus Zero play, delayed-peer restarts and human balance remain unverified. Evidence before the restructure belongs to older artifacts.
4. **Human playtests still needed:** one-to-five-player readability, whether private wallets invite coordination, strategy variety, enemy pressure, HQ ending and whether a deathball dominates. Pacing and numerical balance remain prototype values.

Future design candidates, not commitments, include roster drafting, additional units, commander abilities, richer enemy intelligence, shared objectives and bounded model-directed planning. The minimap is implemented. Free F1/F2/F3 doctrine choice, paid N casualty refill, fixed starting armies, individual squad controls and Disrupt Grid shield objective are removed. Supply networks, extensive tech trees, PvP, campaign and Internet discovery are not current gameplay.

### Technical boundaries

The project uses UE 5.8.3, Linux x86_64, the bundled Clang 20.1.8 toolchain and Vulkan SM6. One C++ module in four layers: **content** (`Content/` data assets: `UArmyUnitDefinition`, `UBuildingDefinition`, `UMatchContent` registry; actors replicate registry indices), **rules** (`Rules/ProductionPolicy` — deterministic, no world; tested by `CoopRTS.Rules.*`), **simulation actors** (authoritative, replicated) and **presentation** (`ACommandHUD` Canvas, **not UMG**, enumerating the registry and rendering `EProductionState`). Adding a unit or building is a data asset appended to `DA_MatchContent`; adding a map is a level that places `AArenaBounds`, both `AHeadquarters` (`TeamIndex` 0 and 5) and its `ACapturePoint` sectors. `ACommandGameMode` discovers those actors, refuses to start a match without them, resolves outcomes and restarts into the current map; `ACommandGameState` owns shared territory, team income, placement validation and the `Content`/`Arena` references; `ACommandPlayerState` owns each wallet and specialization; `ACommandBuilding` owns construction and applies the production policy to its persistent force; `AArmyGroup` directs that force and its physical reinforcements; `AEnemyCommander` buys buildings and sets fronts through the same definitions. Enhanced Input supplies local controls. No Gameplay Ability System, Mass Entity, external LLM integration or plugin architecture is required for this prototype.

Human and enemy economies use `ACommandPlayerState::Resources`, `TrySpend`, `AddResources` and `Doctrine`. Human states have `TeamIndex = 0` and `CommanderIndex` 0–4; `ACommandGameState::EnemyCommander` references a replicated, controllerless state with `TeamIndex = 5` and `CommanderIndex = -1`. Enemy buildings and forces carry that state as their owner. The enemy stays outside `PlayerArray` on authority and clients, so human commander lists and player RPC ownership remain unchanged; GameMode creates a fresh enemy state after level discovery for each match rather than carrying its wallet or doctrine through seamless travel. The network probe retains the `enemyResources` JSON key.

The visual direction favors readable silhouettes, dark environments and restrained team accents. [World, naming, Campus Zero and art pipeline](Docs/World.md) contains themed map and asset-generation detail; [Audio direction](Docs/Audio.md) specifies faction sound palettes, the event list and the Unreal mix design; [Unreal MCP editor setup](Docs/UnrealMCP.md) documents the optional editor bridge. The art and lore documents may describe older mechanics; current gameplay rules above and source take precedence.

## Historical milestone evidence (pre-construction cutover)

The following milestones exercised earlier two-army, `N` recovery, free F-key doctrine and shield-objective builds. Their commands, screenshots and results remain historical records, **not current behavior or proof of the new package**.

Milestones 1–6 provide the verified solo match loop. Milestone 7's doctrine implementation passed four fresh-world doctrine scenarios and all seven existing gameplay regressions; packaged input exercised cards, F1/F2/F3, locked choices, combat, terminal rejection and fresh-match reselection. A fresh-session independent pass reran all four doctrine scenarios successfully without command deadlines. Evidence: `Saved/Verification/m7-integrated-20260929/RESULTS.md` and `Saved/Verification/m7-independent-20260929/RESULTS.md`. The human doctrine criterion still requires playtesting. Milestone 8's opt-in Development real-socket editor and package runs passed two-peer and five-peer scenarios including actual packet emulation; rendered two-peer input and a five-player client roster were inspected. `Saved/Verification/m8-package-five-loss-final-a-20260929/RESULTS.md` records artifacts, the intermittent uncapped restart risk, and limits. Human five-player coordination/readability and deathball judgment remain **unverified pending a five-human playtest**.

The earlier delayed-client restart disconnect was addressed with connection-preserving seamless travel and explicit fresh-match initialization. Three forced-delay restart cycles passed in editor two-player, packaged two-player and packaged five-player lag/loss runs; standalone victory/defeat restarts passed too. Evidence: `Saved/Verification/restart-package-five-loss-delayed-a-20260929/RESULTS.md`. Before that repair, an independent fresh-session packaged five-player 120 ms lag/8% loss run inspected each peer's replicated state and exercised native client selection/pan/refocus without moving the host camera: `Saved/Verification/m8-independent-five-loss-20260929/RESULTS.md`. Both records belong to the pre-construction artifacts.

## Linux setup and packaging

`CoopRTS.uproject` contains the C++ game and editor targets. `/Game/Maps/Boot` is a command arena with a floor, central obstacle, boundary walls, home markers, and a dynamically generated Unreal navmesh.

### Prerequisites

- Install Epic's Linux Unreal Engine **5.8.3** binary distribution. This machine uses `~/.local/opt/unreal-engine/5.8.3`.
- Use its bundled Clang 20.1.8 and .NET SDK; no engine source build is required.
- Install Git LFS and run `git lfs install --local` in the repository. After cloning, run `git lfs pull` to fetch `.uasset` and `.umap` content.
- Use a Vulkan SM6-capable GPU and driver. The verified system uses Radeon RX 6950 XT with Mesa RADV 26.2.2.

### Required engine shader patch

The stock 5.8.3 build crashed during compute pipeline creation on this machine, with `ACO ERROR: Unimplemented intrinsic instr: @store_deref`. Lowering the shader model to SM5 did not fix it.

`Build/UnrealEngine-5.8.3-ShaderPrint.patch` removes forced unrolling from the two vector-print overloads in `Engine/Shaders/Private/ShaderPrintCommon.ush`. This follows the workaround described in [DXC issue #8417](https://github.com/microsoft/DirectXShaderCompiler/issues/8417) and the [matching Unreal report](https://forums.unrealengine.com/t/2771073/2). Vulkan SM6, virtual shadow maps, and PSO precaching remain enabled.

The patch is already applied on this machine. On a fresh **5.8.3** installation, close the editor and run these commands from the repository root before building or cooking:

```bash
export UE_ROOT="$HOME/.local/opt/unreal-engine/5.8.3"
chmod u+w "$UE_ROOT/Engine/Shaders/Private/ShaderPrintCommon.ush"
patch --forward -d "$UE_ROOT" -p1 -i "$PWD/Build/UnrealEngine-5.8.3-ShaderPrint.patch"
```

This changes the shared engine installation, affecting other projects using it. Reapply after reinstalling this version; reassess the upstream fix before upgrading. Packaged-game testers do not need the engine or this patch separately.

### Build and open the editor

Run from the repository root:

```bash
export UE_ROOT="$HOME/.local/opt/unreal-engine/5.8.3"
"$UE_ROOT/Engine/Build/BatchFiles/Linux/Build.sh" \
  CoopRTSEditor Linux Development -Project="$PWD/CoopRTS.uproject" -WaitMutex
"$UE_ROOT/Engine/Binaries/Linux/UnrealEditor" "$PWD/CoopRTS.uproject"
```

The local engine association is `UE_5.8.3`. Explicit executable paths above also work without registering that association on another machine.

Unit and building definitions plus the `DA_MatchContent` catalogue live in `Content/Units/` and `Content/Content/` and are cooked with the package. On a fresh checkout or after changing `Build/GenerateMatchContent.py`, build `CoopRTSEditor`, then run `UnrealEditor-Cmd -EnablePlugins=PythonScriptPlugin -ExecutePythonScript="$PWD/Build/GenerateMatchContent.py" -unattended -nullrhi -nosplash`; require `MATCH_CONTENT_READY units=3 buildings=3` in the log. Existing assets keep their tuned combat values; catalogue order is a replicated contract (units frontline/ranged/siege, buildings barracks/outpost/workshop). `Build/GenerateCombatUnits.py` is the older combat-stats-only generator.

### Package and run

```bash
"$UE_ROOT/Engine/Build/BatchFiles/RunUAT.sh" BuildCookRun \
  -project="$PWD/CoopRTS.uproject" -noP4 -platform=Linux \
  -clientconfig=Development -build -cook -map=/Game/Maps/Boot+/Game/Maps/CampusZero \
  -stage -pak -iostore -package -archive \
  -archivedirectory="$PWD/Builds" -unattended -utf8output
./Builds/Linux/CoopRTS.sh -windowed -ResX=1280 -ResY=720
./Builds/Linux/CoopRTS.sh /Game/Maps/CampusZero -windowed -ResX=1280 -ResY=720
```

Distribute the entire `Builds/Linux` directory, not just the executable. Generated builds, caches, and logs are ignored by Git.

The enabled ModelContextProtocol/AllToolsets editor plugins activate GameFeatures. UE 5.8.3's `GameFeaturesEditorModule.cpp::AddDefaultGameDataRule` requires a `GameFeatureData` primary-asset scan with `CookRule=AlwaysCook`; without it, the cook reports two Asset Manager errors and exits 1. `Config/DefaultGame.ini` includes that exact rule. The first milestone 8 cook failed on the missing rule; the subsequent full BuildCookRun completed with `BUILD SUCCESSFUL` after adding it. No plugin was disabled.

### Direct LAN/private-network multiplayer

The listen host owns authoritative combat, capture, construction, wallets, production and outcomes. Commanders have independent slots 0–4 and wallets; a sixth commander is rejected. Run the current Linux Development package on each machine on the same LAN or reachable private network. Permit inbound UDP7777 on the host and substitute its private IP below:

```bash
# Host (one terminal / one machine):
./Builds/Linux/CoopRTS.sh '/Game/Maps/Boot?listen' -port=7777 -windowed -ResX=1280 -ResY=720
# Remote player (a separate machine or process; use the host's LAN address):
./Builds/Linux/CoopRTS.sh 192.168.1.42:7777 -windowed -ResX=1280 -ResY=720
```

For two fresh editor game processes instead of a package, replace the executable with `"$UE_ROOT/Engine/Binaries/Linux/UnrealEditor" "$PWD/CoopRTS.uproject"`, then the same host Boot listen URL or client IP and `-game`. Do not launch a second client on top of the host process. Direct IP/LAN hosting is **not** matchmaking, Internet discovery, relaying, NAT traversal or a promise about public-network connectivity. Listen-host exit ends the match; terminal Enter requests a fresh Boot for connected players.

### Network verification

The opt-in Development real-socket probe and [multiplayer skill recipe](.agents/skills/verify-cooprts/features/multiplayer.md) exercise owning-client building RPCs, private costs, locked force configuration, independent producers/fronts, actual paid casualty replacement and replicated joined/travelling state, outposts, research, HQ damage and a connected restart. Host-only fixtures supply explicit funding/capture/lethal setup; this is not a naturally paced match. Earlier `construction-network-b-20260929` and `construction-package-two-a-20260929` passes belong to the replaced batch model. Headless probes do not establish native-window input or five-player/fault acceptance.

Direct-IP play and historical test artifacts are documented below. Do not treat earlier two-army, paid-`N` or free-F-key assertions as construction acceptance checks.

### Earlier verification status (historical artifacts only)

**Milestone 7:** Four doctrine scenarios and the seven existing gameplay scenarios passed in fresh editor worlds; doctrine selection and restart were exercised in the packaged game. The independent pass confirmed the four doctrine results and inspected existing package evidence. Numerical healing and damage comparisons are live-world assertions, not screenshot claims. An unrelated edit to `Content/Maps/CampusZero.umap` subsequently made the then-existing archive fail the conservative package-freshness guard; that user change was preserved. Milestone 8 rebuilt the package and passed fresh guarded desktop runs. The milestone 7 evidence remains tied to its original tested artifacts: `Saved/Verification/m7-independent-20260929/RESULTS.md`.

**Milestone 5 verified on its prior build (2026-09-29):** `CoopRTSEditor Linux Development` and Linux `BuildCookRun` completed successfully. Four fresh standalone Boot runs each exited zero with exact `Test Completed. Result={Success}`: `CoopRTS.Economy.CaptureIncomeRecovery` (`Saved/Verification/m5-economy-logic/economy.log:1452`) asserted contested/neutral/team/enemy capture, baseline and territory payments to two separate player-state wallets, atomic invalid-source/insufficient/full-roster rejection, an owned RPC charging exactly the quoted missing siege price (80), forward-source eligibility and a 360-resource six-role base rebuild after a wipe. `CoopRTS.Movement.TwoGroups` (`m5-movement-logic/movement.log:1467`) asserted paid moving casualty joining, two-army crossing, rejected-order preservation and all-member arrival. `CoopRTS.Orders.ReplaceHoldRetreat` (`m5-orders-logic/regression.log:1444`) and `CoopRTS.Combat.Encounter` (`m5-combat-logic/combat.log:1451`) passed independently. All evidence directories are beneath `Saved/Verification/`; their `actions.jsonl` files record exit codes and the then-current editor-module stamp. This is historical evidence, not a milestone 6 run.

**Milestone 5 packaged input:** The owned Linux run `Saved/Verification/m5-desktop-20260929/` used right-click to capture Resource 1 (neutral progress 86% to friendly 100%), observed income increase from +10/s to +16/s, rejected N at full composition and again when a depleted army was away from a valid source, then pressed R and N at base to restore four casualties for the quoted 200 resources. Its game log records `restored army=0 cost=200 balance=4556 units=6` at line 1329; `paid-restoration.png` shows all roles, RETREAT #3 preserved and purchase feedback. In a fresh owned run `Saved/Verification/m5-rebuild-flank-20260929/`, a second army took a flank, retreated under hostile fire, then lost all six units in a follow-up Attack. `wiped-group.png` shows the same selectable Army 2 with zero units, base source and 360 quote; real 2/Space then N rebuilt six roles at base, switched its order to HOLD #5 and charged 360 (`game.log:1331`, `rebuilt-at-base.png`). Both owned processes were stopped; original screenshots, action histories, logs and inspected local crops remain under their evidence directories. These solo runs do not prove two-client replication, hostile-client RPC validation, or long-session balance.

**VERIFIED in the milestone 4 integration run (2026-09-29):** The `CoopRTSEditor` Development build succeeded; the editor asset script logged `COMBAT_UNITS_READY`, creating the three role Data Assets; Linux `BuildCookRun` completed with `BUILD SUCCESSFUL` and exit code 0. Three fresh standalone Boot automation runs passed with explicit `Test Completed. Result={Success}`: `CoopRTS.Combat.Encounter` (role-specific range, server damage/death, target invalidation, bounded Attack, Move/Hold/Retreat and stale-intent replacement), `CoopRTS.Orders.ReplaceHoldRetreat`, and `CoopRTS.Movement.TwoGroups`. Logs: `Saved/Verification/combat-integration-20260929/{combat,regression,movement}.log`.

**Packaged desktop evidence:** Owned Linux game sessions under `Saved/Verification/combat-desktop-20260929/`, `combat-targeted-20260929/`, and `combat-effects-20260929/` exercised actual 1/2 selection, WASD/Space camera, right-click Move, Q on a living hostile (`ATTACK TARGET`), Q on floor (`ATTACK AREA`), H and R. Screenshots show per-role/health/target HUD, red attack indicators, enemy HP falling, unit casualties, cyan ranged and purple siege attack flashes, position-bound Hold and surviving units retreating with target cleared. The last effects run waited too long before R: all selected units died and the order was rejected, so that attempt is **not** a Retreat pass; the earlier two live runs and the combat automation cover accepted Retreat. See each run's `RESULTS.md`, `actions.jsonl` and `game.log`. Desktop images cannot prove exact radius, authority under a second client or long-term stability; automation cannot prove desktop Q hit-testing/rendering.

**Earlier movement/cursor runs:** The C++ editor/game build and Linux package completed successfully before combat changes. The packaged arena was visually inspected and exercised with keyboard pan, mouse drag, wheel zoom, selection, army focus, and ground orders. The six-unit army reached a commanded destination on the opposite side of the central obstacle and retreated home. Mouse dragging was corrected to use viewport-position deltas and visually rechecked against native pointer motion.

The pre-economy group-movement pass verified two independently selectable armies exchanging positions, navigating around the obstacle, preserving movement after rejected orders, and admitting the former free reinforcement from home. Both earlier live assertion scenarios passed, including every-unit successful path completion, eight rapid order replacements per army, boundary rejection, and immediate Hold. The earlier packaged UI returned seven-unit armies home. That behavior is superseded by paid six-role casualty replacement and is **not** milestone 5 proof. Historical evidence is under `Saved/Verification/two-groups-20260929-grounded/` and `Saved/Verification/two-groups-20260929-grounded-ui/`; the latter's `RESULTS.md` identifies screenshots and coverage limits.

Crossing exposed a Character RVO final-approach stall; current units use Unreal's built-in Detour crowd controller and its path-query flags, without a second RVO layer. The earlier reinforcement implementation also exposed character grounding during initialization; that recovery path was removed in the construction cutover.

The Linux cursor fix removes forced viewport confinement (`EMouseLockMode::DoNotLock`). Packaged verification confirmed a visible native arrow, twenty unwarped left/right motion segments, ground clicks at both sides, and middle-drag followed by free pointer motion; the drag preserved the held order. Cursor-inclusive captures and action logs are under `Saved/Verification/cursor-unconfined-20260929/`. The pointer is intentionally allowed to leave the game window.

The original empty-map packaging milestone passed an earlier 60-second launch, which was startup smoke evidence only. Historical runs used explicit live assertions and owned-process launch/stop recipes, not timed exit as a pass. Runtime logs are under `Builds/Linux/CoopRTS/Saved/Logs/`. Milestone 8 added independently observed real remote worlds on this Linux workstation **before** the construction redesign; other hardware, Internet traversal and long-session stability remain unverified.

### Current verification boundary

Current proof is `Saved/Verification/restructure-20260930/RESULTS.md` (rules tier, world scenarios, four network slices, editor/packaged HUD captures, fourth-unit proof). Fixed-force production evidence before the restructure (`force-production-f-20260930`, `force-hud-package-a-20260930`) belongs to the pre-restructure artifacts. See the [verification skill](.agents/skills/verify-cooprts/SKILL.md) for the tier model (rules → world → network slice → presentation → acceptance) and proof selection.

### Regenerating the arena

The map and material assets are already present; normal builds do not need regeneration. `Build/GenerateCommandMap.py` reconstructs the test arena through Unreal's editor APIs; the match actors it and `Build/GenerateCampusZero.py` place (arena, HQs, sectors) come from `Build/MatchLayout.py`.

**This replaces the actors in Boot. Do not run it over hand-edited map work you want to keep.** Close the editor, build `CoopRTSEditor`, then run:

```bash
"$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" "$PWD/CoopRTS.uproject" \
  -EnablePlugins=PythonScriptPlugin \
  -ExecutePythonScript="$PWD/Build/GenerateCommandMap.py" \
  -unattended -nullrhi -nosplash
```

Require `COMMAND_ARENA_GENERATED` in the log before packaging. The script uses a factory-built navigation brush and enables dynamic Recast generation, so navigation also initializes in cooked games. Existing unit material graphs are preserved on reruns.
