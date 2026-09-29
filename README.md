# Co-op RTS Roguelite

Project concept, agreed design direction, and Unreal implementation plan.

Status: Milestone 7's first doctrine choice passed four fresh-editor live scenarios, seven existing gameplay regressions, and a packaged Linux desktop run; see `Saved/Verification/m7-integrated-20260929/RESULTS.md`. Milestone 8's real-socket editor two-peer, packaged two-peer, packaged five-peer, and packaged five-peer 120 ms lag/8% loss runs passed on the current Development artifacts with all headless peers capped at 60 FPS. Inspected native host/client input and fifth-client roster captures accompany the numeric runs. An earlier uncapped nonseamless restart lost a client; capping eliminated multi-second tick stalls in the focused comparison, but does not prove the production restart race impossible. Human five-player readability/coordination/deathball and doctrine strategy playtests remain outstanding. Evidence: `Saved/Verification/m8-package-five-loss-final-a-20260929/RESULTS.md`.

An independent fresh-session run repeated the packaged five-player 120 ms lag/8% loss scenario successfully, inspected each peer's actual replicated state, and exercised separate native client selection/pan/refocus while the host camera remained unchanged. It made no gameplay changes and does not close the uncapped restart or human-playtest limits above. Evidence: `Saved/Verification/m8-independent-five-loss-20260929/RESULTS.md`.

## Core idea

A 1–5 player cooperative real-time strategy roguelite with matches lasting approximately 12–18 minutes, targeting 15 minutes.

The central promise:

> An RTS night with friends where coordination and builds matter more than mechanical execution.

Players command separate armies on one shared battlefield, draft distinct unit rosters and upgrades, coordinate objectives, and defeat an enemy strategic commander. Players should feel like commanders rather than operators of individual soldiers.

Inspirations:

- StarCraft 2: composition, scouting, positioning, economy and map control.
- Slay the Spire: drafting, consequential choices and run variation.
- Diablo: interacting builds and occasionally spectacular power.
- Heroes of the Storm: shared objectives that create coordinated action.

This is not an RTS with cards added on top. The goal is a cooperative command game where a few understandable orders produce battles worth coordinating, and drafted builds change the plans players can execute.

## Design principles

### Strategy rather than unit micro

Keep composition, positioning, economy, scouting, counters, expansion, territory, timing, objectives, risk/reward and cooperation.

Do not require individual unit control, kiting, stutter stepping, worker management, individual production-building selection, or abilities on many separate soldiers.

The primary controllable object is an army group. Initially support no more than two groups per player. The broader concept allows approximately one to three, but additional groups require evidence that the control burden remains manageable.

Automatic behavior must be trustworthy:

- Artillery uses its range.
- Support units do not lead charges.
- Units do not chase scouts away from assigned objectives.
- Retreat overrides combat behavior.
- Reinforcement behavior must not repeatedly feed units into avoidable losses.

Losses should feel like bad commitments, not unintelligible unit behavior.

### Consequential choices, not an input quota

The original concept proposed a small decision every 5–10 seconds. The agreed adjustment is to avoid enforcing that cadence.

Players should regularly have consequential alternatives and enough time to understand them. Quiet time is acceptable when it reflects a meaningful commitment, but long periods of empty transit are not.

### Cooperative ownership

Players share the battlefield, vision, territory, objectives and victory/loss condition.

Each player owns their army groups, roster, production decisions, doctrines and limited commander abilities.

Specializations can include heavy armor, swarm, artillery, air, scouting, support and mobile raiding. These are build directions, not mandatory classes.

Five players choosing the same specialization should remain viable. Counters should usually change efficiency and approach rather than determine whether an interaction is possible at all. Every roster needs a functional baseline.

### Offensive victory

Players win by taking ground and creating an opening to destroy the enemy HQ, not by surviving predictable waves.

Enemy forces should expand, scout, defend, raid, flank, retreat and contest objectives. Static defenses support armies rather than replacing them.

## Match arc

Approximate pacing, to be tested rather than enforced rigidly:

| Time | Phase | Intended experience |
| --- | --- | --- |
| 0–3 minutes | Establish | One small army, nearby capture, first income and scouting |
| 3–6 minutes | Expand | First build direction, specialization and pressure across fronts |
| 6–10 minutes | Adapt | Distinct builds, enemy responses and coordinated objectives |
| 10–13 minutes | Escalate | Strong synergies, larger battles and harder territory decisions |
| 13–15 minutes | Finale | Earn an opening and assault the enemy HQ |

The finale should feel earned through battlefield decisions, not merely unlocked by waiting for a timer.

## Drafting and run progression

### Starting roster

Longer-term direction:

- A quick pre-match draft, approximately 60–90 seconds.
- One always-available basic unit.
- Approximately three additional unit types per player.
- No access to every unit in a match.
- Optional single commander trait, not a large pre-match talent tree.

Example units: heavy infantry, siege walkers, missile drones, assault bikes, artillery, repair drones, scouts, aircraft and shield units.

Example traits: improved scouting, faster army movement, or stronger early production.

Drafting should prompt useful conversation without allowing an unlucky roster to make an enemy type untouchable.

### In-match doctrines

Use choose-one-of-three decisions.

The original proposal was a choice every 60–90 seconds. The agreed starting adjustment is to test approximately four to six major choices per match:

1. Establish a direction.
2. Deepen or branch that direction.
3. Enable a dramatic late payoff.

Descriptions should be short. Players should be able to defer a choice until safe rather than lose an army while reading.

Prefer interacting mechanics over prescribed upgrade chains or small generic stat increases.

Example interactions:

- Vehicle regeneration produces nearby shields.
- Shields change attack behavior.
- Marked or poisoned enemies spread an effect on death.
- Spawned units inherit a limited effect.
- Siege units gain a new movement/combat interaction.

Bound recursion and stacking from the beginning. Multiplayer healing, spawning and on-death chains must not create unlimited simulation work.

Let powerful builds feel powerful. Enemy adaptation should be delayed, scoutable and costly, with tradeoffs rather than immediate perfect counters.

## Army control

An army tracks composition, location, destination, current order, combat state and relevant effects.

Start with four orders:

| Order | Behavioral contract |
| --- | --- |
| Move | Reach the destination without pursuing enemies away from the route |
| Attack | Advance on a target or location and engage within a bounded pursuit area |
| Hold | Stay near the assigned position and do not chase beyond its boundary |
| Retreat | Break engagement and return to a valid friendly reinforcement location |

Separate command semantics from future stance features. The original aggressive, defensive, raid, skirmish and siege proposals mix pursuit policy, target selection, retreat policy and combat mode. Do not implement all of them initially.

Smarter autonomous orders such as Raid are future possibilities, not prototype requirements.

## Economy and territory

Use one primary resource initially.

Starting economy:

- Baseline passive income to prevent permanent economic lockout.
- Extra income from controlled resource sites.
- Shared team territory ownership.
- Separate player wallets.
- Territory income benefits players without requiring personal last-capture credit.

This is a starting rule to test for passivity and snowballing, not a proven final balance model.

Territory can eventually provide economy, vision, mobility, strategic bonuses and forward reinforcement.

Examples: mines, radar, factories, power stations, bridges and research sites.

### Reinforcement first, production automation later

For the initial playable slice, use fixed army compositions and an explicit reinforcement purchase at the base or a controlled reinforcement site. Show the replacement cost before purchase.

Reinforcement restores casualties up to the configured composition. Destroyed groups can be rebuilt at the base for their replacement cost.

Later, test desired compositions or production weights rather than repeated factory management. An example is infantry 40%, armor 40%, support 20%.

### Supply networks are deferred

The original concept suggested connected supply routes, with isolation reducing reinforcement, healing, vision or efficiency without instantly destroying armies.

First test whether economy, vision, objectives and reinforcement locations already create useful fronts. Add explicit supply only if cutting connections solves an observed strategic problem.

## Battlefield and camera

Use a continuous traversable RTS battlefield, not a board-game layout or tower-defense lane system.

Support camera panning, mouse dragging, zoom, army-jump controls and eventually a minimap. The normal gameplay view should show a portion of the battlefield rather than the entire map.

Longer-term camera levels:

- Close: combat spectacle.
- Normal: local tactics and nearby threats.
- Strategic: fronts, objectives and simplified army markers.

The original proposal suggested 5–8 camera screens across and full crossing times of 45–60 seconds for fast armies, about 90 seconds for normal armies and up to two minutes for heavy armies. These are not locked targets.

Agreed adjustment: tune travel time between contested locations first. A two-minute crossing is a large commitment in a 15-minute match. Test whether teammates can respond before fights are decided and whether slow armies can participate in successive objectives.

Start with broad routes, few obstacles and simple elevation. Avoid narrow bridges and complex formation constraints in the first prototype.

## Objectives and cooperation

Possible shared objectives:

- Orbital cannon controlled through uplinks.
- Production-boosting reactor.
- Intelligence-providing radar array.
- Capturable neutral war machine.

Initial scope is one meaningful shared objective, not the complete catalog.

Prioritize battlefield cooperation over proximity-stat bonuses:

- Screen an ally's siege army.
- Cut reinforcement routes before a heavy assault.
- Reveal an enemy commitment so another player can attack elsewhere.
- Trade responsibility for a threatened location.

Potential later build interactions include target painting, combined-arms bonuses and roster sharing. Resource gifting is optional and uncommitted.

Watch for:

- Deathballing: all armies move together because concentration always wins.
- Quarterbacking: one experienced player makes every decision.
- Mandatory support roles.
- Cooperation that requires constant voice communication.

Multiple valuable fronts and distinct local responsibilities should help, but must be validated in playtests.

## Enemy commander and intelligence

Use two layers:

1. Strategic planner selects goals.
2. Ordinary army and unit logic executes orders.

Initial strategic actions:

- Capture a resource site.
- Contest enemy territory.
- Defend a threatened HQ.
- Retreat and reinforce.
- Attack when a useful advantage exists.

A periodic scoring system is sufficient. Consider target value, travel cost, combat risk and urgency. Give plans commitment time and explicit emergency interruptions to avoid constant switching.

The enemy should issue the same army-order structure as player commands.

### Enemy intent

Scouting should reveal actionable information:

- Poor intelligence: heavy movement detected west.
- Better intelligence: mechanized assault likely west in approximately 45 seconds.
- Excellent intelligence: a specific armored force observed preparing near a refinery.

Distinguish observed facts from inferred plans. Feints should be understandable deception, not an interface that appears to lie.

### JEV / LLM commander is deferred

The longer-term idea is to allow JEV or another model to choose bounded strategic actions from a compact observation every approximately 10–20 seconds.

Do not depend on this for the prototype or initial release scope. Do not build a generic external-agent protocol yet.

Any future model commander should receive only information the enemy legitimately knows. The game validates actions and remains responsible for execution. Interesting, legible opposition matters more than planner sophistication.

## Commander abilities

Longer-term option: a small number of high-level abilities with meaningful costs or cooldowns.

Examples: orbital scan, airstrike, temporary shield, emergency reinforcement, rapid redeploy and production overclock.

Avoid MOBA-style ability spam. These are not required for the first playable slice.

## Visual direction and onboarding

Stylized sci-fi:

- Chunky readable silhouettes.
- Moderately stylized proportions.
- Dark environments with selective emissive accents.
- Some graphic-novel character.
- Restrained player-color accents.
- Strong readability for ownership, selection, threat and objective state.

Avoid photorealism, generic modern military styling, excessive neon and overly cartoonish presentation.

Effects can become more spectacular as builds develop: piercing shots, chain lightning, drones, shields, artillery and orbital attacks. Readability remains the priority.

Start players with one army, one obvious nearby objective and few controls. Introduce groups, information and build complexity gradually.

## Scope and non-goals

The original broader MVP proposed one map, 1–5 players, a listen server, approximately 12 unit types, 15 doctrines, one resource, two groups per player, a draft, automatic combat, simple production, a scripted commander and an HQ finale.

That remains a possible later playable scope, not the first implementation milestone. Do not build the full unit and doctrine inventory before validating command quality, build identity and five-player cooperation.

Not in initial scope:

- Competitive PvP.
- Matchmaking, accounts or dedicated servers.
- Host migration or reconnect support.
- Campaign or persistent progression.
- Cosmetics.
- Five separate factions.
- Large tech trees or elaborate base building.
- Procedural maps.
- Individual-unit micro.
- Sophisticated LLM integration.
- Large enemy or unit catalogs.

## Unreal technical direction

The project is pinned to Unreal Engine 5.8.3 and Linux x86_64, using the bundled Clang 20.1.8 toolchain and Vulkan SM6. The remaining gameplay choices below are the selected starting direction.

| Area | Choice |
| --- | --- |
| Engine | Unreal Engine 5.8.3 with the ShaderPrint patch documented below |
| Template | C++ Blank project |
| Gameplay implementation | C++ authoritative systems; Blueprints for presentation and tuning |
| View | Top-down 3D, initially mostly flat terrain |
| Network target | Listen server |
| Input | Enhanced Input |
| UI | UMG |
| Navigation | Unreal navigation mesh with built-in Detour crowd avoidance |
| Initial art | Primitive meshes, flat materials and team accents |
| Source control | Git with Git LFS for `.uasset` and `.umap` |
| Platform | Linux x86_64; initial verification on Arch/Omarchy with Radeon RX 6950 XT |

Keep this project separate from Draft.

Do not initially add Gameplay Ability System, Mass Entity, procedural terrain, online-service integration or a plugin architecture. Begin with one game module and clear folders.

### Authority and Unreal framework responsibilities

Solo play should use the same authority boundaries as multiplayer:

1. Local input creates an order request.
2. The owning PlayerController sends the server RPC.
3. The server validates ownership, target and order.
4. ArmyGroup records and executes intent.
5. Units navigate, acquire targets and attack.
6. Replicated state drives client UI and visuals.

| Class | Responsibility |
| --- | --- |
| GameMode | Server-only rules, spawning and win/loss conditions |
| GameState | Replicated match phase, objectives and shared territory state |
| PlayerState | Player identity, wallet and later roster/doctrines |
| PlayerController | Input, local selection and owned server RPC entry point |
| Camera Pawn | Local camera movement and zoom |
| ArmyGroup actor | Ownership, membership, order, destination and engagement bounds |
| Unit Pawn | Movement, health and weapon behavior |
| Capture-point actor | Ownership, progress and strategic benefit |
| Server commander actor | Enemy strategic decisions |

Keep camera and selection local. Resources, capture, damage, spawning and accepted orders are server-authoritative.

Do not assume clients can invoke server RPCs on arbitrary world actors. Route requests through the owning PlayerController and validate before forwarding.

### Movement and combat implementation

Army movement begins with one logical destination and simple per-unit formation slots. Put frontline units ahead of ranged and siege units. Allow loose transit formation and bounded regrouping.

Use a supported Unreal navigation-agent movement setup. Simple pawns with floating movement are a prototype candidate to verify, not a demonstrated solution in this project.

Do not write a custom pathfinder or demand rigid formations through obstacles.

Combat starts with explicit states such as Moving, Engaging, Retreating and Dead. A C++ state machine is sufficient; Behavior Trees are optional.

- Server applies damage.
- Clients show weapon effects.
- Periodic target acquisition avoids all-world scans every frame.
- Target selection is stable unless a target becomes invalid or leaves permitted engagement bounds.
- Movement uses the movement system continuously.
- Retreat cannot be overridden by individual combat decisions.

Conventional replicated actors are a low-count prototype choice. Profile before assuming they support the eventual army size.

Use Data Assets for unit and doctrine definitions, C++ for behavior and Blueprints for presentation/tuning. Do not build a general recursive effect framework for a handful of initial doctrines.

## First playable scenario

Build:

- One player base and one enemy HQ.
- Two player groups with fixed compositions.
- Three unit types: frontline, ranged and siege.
- Two resource sites and one forward reinforcement site.
- One resource plus baseline income.
- One enemy group that contests territory and retreats.
- Move, Attack, Hold and Retreat.
- Automatic combat.
- Explicit reinforcement purchases at valid locations.
- Victory by destroying the enemy HQ.
- Defeat by losing the player base.

The central dilemma:

> Send both armies to secure an attack route, or split them to protect income and prevent a flank?

Begin with short travel distances. Expand only if movement commitments are too cheap.

## Implementation sequence and acceptance checks

### 1. Project and packaging

Create the project, pin the engine version, configure source control and package an empty build.

Acceptance: the packaged build launches on the intended test machine. Do not defer toolchain and graphics validation.

### 2. Camera and orders

Implement keyboard pan, mouse drag, zoom, army selection, army-jump hotkeys, contextual orders and visible order/destination indicators.

Acceptance: an order can be interrupted and replaced without units continuing stale intent.

### 3. Group movement

Implement formation destinations and loose navigation.

Exercise:

- Two friendly groups crossing.
- Navigation around an obstacle.
- Destinations near map boundaries.
- Repeated replacement orders.
- A reinforcement joining a moving group.

Add debug overlays for destinations, slots, pursuit bounds and targets.

### 4. Combat and retreat

Implement the three unit roles, targeting, damage and death.

Acceptance: siege uses its range, engagement stays bounded and retreat reliably breaks engagement.

### 5. Capture, income and recovery

Implement capturable locations, baseline/territory income, reinforcement costs and rebuilding destroyed groups.

Acceptance: spending is authoritative and losing all territory does not leave the player permanently unable to act.

### 6. Enemy and complete match

Implement a small strategic scoring planner, plan commitment and emergency interruption. Expose current plan and rationale in debug UI.

Acceptance: the enemy contests territory, can retreat, defends its HQ and supports a complete win/loss loop without scripted survival waves.

### 7. First doctrine choice

The first prototype choice is free, once per player per ongoing match, and irreversible until a fresh restart. F1 / F2 / F3 select one of three mutually exclusive doctrines. The owning controller sends the server request; replicated PlayerState stores the choice, and each owned army (including paid replacements) resolves it dynamically. It is not a shared team upgrade or a mutation of unit Data Assets.

| Input | Doctrine | Battlefield tradeoff |
| --- | --- | --- |
| F1 | Siege Optics | Siege weapons reach 25% farther but deal 25% less outgoing damage to units and HQs. Integer damage is `base * 3 / 4`, truncated toward zero. Commit to ranged positioning rather than raw damage. |
| F2 | Field Repairs | Living units recover 5 HP per second, capped at max health, only after five uninterrupted seconds without moving, firing or taking damage. Leaving that quiet state resets the wait and unspent healing; choose when to disengage and hold a safe position. |
| F3 | Entrenched Frontline | Frontline units take 25% less incoming damage only while actually stationary under a Hold order. Integer damage is `incoming * 3 / 4`, truncated toward zero. Move, Attack and Retreat do not grant protection; defend a position rather than rely on mitigation during a push. |

These are finalized **prototype** choices, not a roster/upgrade-tree expansion. Mobile Siege was rejected because Move already permits firing in range, so it would not create a distinct action. Rapid Reinforcement was rejected because paid reinforcement has no production timer to accelerate. Balance values need playtesting; automated health and path assertions cannot establish the acceptance judgement that players can explain how a choice changed where or when they fought.

### 8. Early multiplayer

Test two players in editor, then separate packaged processes, then five players before expanding content.

Use LAN or private-network direct connection initially. Listen-server hosting alone does not solve Internet discovery or NAT traversal.

Exercise latency and packet loss using Unreal network emulation.

Verify:

- Players cannot order other players' groups.
- Clients cannot grant units or resources.
- Capture and death resolve once.
- Orders remain usable under latency.
- Ownership and match outcomes agree across peers.
- Five-player play remains readable and is not dominated by one deathball.

### 9. Match arc and content expansion

Only after the earlier evidence, develop the shared objective, offensive finale, roster drafting and additional doctrines/units.

One to five players cannot be balanced through enemy health alone. Test simultaneous threats, objective count, force distribution and possibly usable map area. Solo is the first mechanical test, but five-player co-op is a core design case that must be tested early.

## Playtest evidence

Positive signals:

- “I committed too far.”
- “You bought me enough time.”
- “That upgrade changed where I wanted to fight.”
- “We saw their attack coming and hit somewhere else.”
- “Next run, I want to try a different combination.”

Warning signs:

- Players always keep both groups together.
- Players mostly watch combat without meaningful intervention.
- Losses are blamed on targeting or navigation.
- Objectives interrupt rather than create interesting decisions.
- Losing an army makes its owner irrelevant for minutes.
- Drafting distracts from the battlefield without changing plans.
- Five-player play becomes a deathball or a single player's command exercise.

## Milestone boundary

Milestones 1–6 provide the verified solo match loop. Milestone 7's doctrine implementation passed four fresh-world doctrine scenarios and all seven existing gameplay regressions; packaged input exercised cards, F1/F2/F3, locked choices, combat, terminal rejection and fresh-match reselection. A fresh-session independent pass reran all four doctrine scenarios successfully without command deadlines. Evidence: `Saved/Verification/m7-integrated-20260929/RESULTS.md` and `Saved/Verification/m7-independent-20260929/RESULTS.md`. The human doctrine criterion still requires playtesting. Milestone 8's opt-in Development real-socket editor and package runs passed two-peer and five-peer scenarios including actual packet emulation; rendered two-peer input and a five-player client roster were inspected. `Saved/Verification/m8-package-five-loss-final-a-20260929/RESULTS.md` records artifacts, the intermittent uncapped restart risk, and limits. Human five-player coordination/readability and deathball judgment remain **unverified pending a five-human playtest**.

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

The three unit-role Data Assets live in `Content/Units/` and are cooked with the package. If missing on a new checkout, build `CoopRTSEditor`, then run `Build/GenerateCombatUnits.py` via `UnrealEditor-Cmd -EnablePlugins=PythonScriptPlugin -ExecutePythonScript="$PWD/Build/GenerateCombatUnits.py" -unattended -nullrhi -nosplash`; require `COMBAT_UNITS_READY` in the log. The script preserves existing assets on reruns.

### Package and run

```bash
"$UE_ROOT/Engine/Build/BatchFiles/RunUAT.sh" BuildCookRun \
  -project="$PWD/CoopRTS.uproject" -noP4 -platform=Linux \
  -clientconfig=Development -build -cook -map=/Game/Maps/Boot \
  -stage -pak -iostore -package -archive \
  -archivedirectory="$PWD/Builds" -unattended -utf8output
./Builds/Linux/CoopRTS.sh -windowed -ResX=1280 -ResY=720
```

Distribute the entire `Builds/Linux` directory, not just the executable. Generated builds, caches, and logs are ignored by Git.

The enabled ModelContextProtocol/AllToolsets editor plugins activate GameFeatures. UE 5.8.3's `GameFeaturesEditorModule.cpp::AddDefaultGameDataRule` requires a `GameFeatureData` primary-asset scan with `CookRule=AlwaysCook`; without it, the cook reports two Asset Manager errors and exits 1. `Config/DefaultGame.ini` includes that exact rule. The first milestone 8 cook failed on the missing rule; the subsequent full BuildCookRun completed with `BUILD SUCCESSFUL` after adding it. No plugin was disabled.

### Direct LAN/private-network multiplayer

The listen host owns server-authoritative combat, capture, wallets and match outcomes. Each player gets an independent commander index (0–4), two six-role armies and a separate wallet/doctrine; slot 6 is rejected. Run a built Linux Development package on **each** machine on the same LAN or reachable private network. The host permits inbound UDP port 7777; substitute its actual private IP below. From the repository root:

```bash
# Host (one terminal / one machine):
./Builds/Linux/CoopRTS.sh '/Game/Maps/Boot?listen' -port=7777 -windowed -ResX=1280 -ResY=720
# Remote player (a separate machine or process; use the host's LAN address):
./Builds/Linux/CoopRTS.sh 192.168.1.42:7777 -windowed -ResX=1280 -ResY=720
```

For two fresh editor game processes instead of a package, replace the executable with `"$UE_ROOT/Engine/Binaries/Linux/UnrealEditor" "$PWD/CoopRTS.uproject"`, then the same host Boot listen URL or client IP and `-game`. Do not launch a second client on top of the host process. Direct IP/LAN hosting is **not** matchmaking, Internet discovery, relaying, NAT traversal or a promise about public-network connectivity. Listen-host exit ends the match; terminal Enter requests a fresh Boot for connected players.

### Milestone 8 real-network verification (after integrated build)

The development-only `ArmyNetworkVerification.cpp` probe requires explicit `-CoopRTSNetVerifyDir` and `-CoopRTSNetVerifyPeer` **per process**; controlled server encounter fixtures additionally require `-CoopRTSNetVerifyAuthority` on the listen host. No normal launch enables it, and the code is excluded from Shipping. Remote clients use the existing owning-PlayerController RPCs and observe *their own* replicated actor worlds. The coordinator starts actual separate processes over a loopback UDP socket, checks binary/package freshness, records peer identities, logs/observations/results, and cleans up only its recorded PIDs. Do not use it against another player's public host.

After all C++ handoffs and the editor build above, run editor host+remote first; then build/package with the command above and run package host+remote, host+four, and active UE packet-lag/loss emulation:

```bash
N=.agents/skills/verify-cooprts/scripts/network.py
"$N" --run Saved/Verification/m8-editor-two-unique --mode editor --clients 1 --max-fps 60
"$N" --run Saved/Verification/m8-package-two-unique --mode packaged --clients 1 --max-fps 60
"$N" --run Saved/Verification/m8-package-five-unique --mode packaged --clients 4 --max-fps 60
"$N" --run Saved/Verification/m8-package-five-loss-unique --mode packaged --clients 4 --emulation --max-fps 60
```

Use new evidence directories per run; these are *commands to execute*, not recorded passes. `--max-fps 60` applies `t.MaxFPS 60` to every headless peer to avoid CPU-starved multi-second net-driver ticks on this workstation; it does not change gameplay travel rules. `--emulation` requires every UE net driver to report `PktLag=120` ms and `PktLoss=8` percent. Inspect `events.jsonl` PASS, each peer's `game.log`, `responses.jsonl`, `process.json`, and run artifact stamps; on failure retain logs and resolve the actual violation. The runner reports pending predicates and peer health without a command/stage deadline, and immediately rejects terminal network/travel failures. This checks foreign-army RPC rejection, own wallet/roster purchase limits under invalid/insufficient/spam requests, doctrine isolation/replacements, capture/death/payment agreement, remote movement under lag/loss, HQ outcome and connected restart, join/leave/late join, identity separation, sixth and terminal join rejection. Fixture teleports/health/wallet setup shorten encounters; real hostile weapon, capture tick, network RPC and replicated client observations determine outcomes.

For short rendered **host plus client** and **five-player roster** proof, use [the multiplayer skill recipe](.agents/skills/verify-cooprts/features/multiplayer.md) and its `network_desktop.py` focused-window driver after the package is fresh. Inspect actual native-window captures and correlate client-focused 1/2/F keys, orders and HUD with peer logs; headless state assertions alone do not prove visuals. Five-player readability, coordination, and whether a deathball dominates **remain unverified until a human five-player playtest**.

### Verification status

**Milestone 7:** Four doctrine scenarios and the seven existing gameplay scenarios passed in fresh editor worlds; doctrine selection and restart were exercised in the packaged game. The independent pass confirmed the four doctrine results and inspected existing package evidence. Numerical healing and damage comparisons are live-world assertions, not screenshot claims. An unrelated edit to `Content/Maps/CampusZero.umap` subsequently made the then-existing archive fail the conservative package-freshness guard; that user change was preserved. Milestone 8 rebuilt the package and passed fresh guarded desktop runs. The milestone 7 evidence remains tied to its original tested artifacts: `Saved/Verification/m7-independent-20260929/RESULTS.md`.

**Milestone 5 verified on its prior build (2026-09-29):** `CoopRTSEditor Linux Development` and Linux `BuildCookRun` completed successfully. Four fresh standalone Boot runs each exited zero with exact `Test Completed. Result={Success}`: `CoopRTS.Economy.CaptureIncomeRecovery` (`Saved/Verification/m5-economy-logic/economy.log:1452`) asserted contested/neutral/team/enemy capture, baseline and territory payments to two separate player-state wallets, atomic invalid-source/insufficient/full-roster rejection, an owned RPC charging exactly the quoted missing siege price (80), forward-source eligibility and a 360-resource six-role base rebuild after a wipe. `CoopRTS.Movement.TwoGroups` (`m5-movement-logic/movement.log:1467`) asserted paid moving casualty joining, two-army crossing, rejected-order preservation and all-member arrival. `CoopRTS.Orders.ReplaceHoldRetreat` (`m5-orders-logic/regression.log:1444`) and `CoopRTS.Combat.Encounter` (`m5-combat-logic/combat.log:1451`) passed independently. All evidence directories are beneath `Saved/Verification/`; their `actions.jsonl` files record exit codes and the then-current editor-module stamp. This is historical evidence, not a milestone 6 run.

**Milestone 5 packaged input:** The owned Linux run `Saved/Verification/m5-desktop-20260929/` used right-click to capture Resource 1 (neutral progress 86% to friendly 100%), observed income increase from +10/s to +16/s, rejected N at full composition and again when a depleted army was away from a valid source, then pressed R and N at base to restore four casualties for the quoted 200 resources. Its game log records `restored army=0 cost=200 balance=4556 units=6` at line 1329; `paid-restoration.png` shows all roles, RETREAT #3 preserved and purchase feedback. In a fresh owned run `Saved/Verification/m5-rebuild-flank-20260929/`, a second army took a flank, retreated under hostile fire, then lost all six units in a follow-up Attack. `wiped-group.png` shows the same selectable Army 2 with zero units, base source and 360 quote; real 2/Space then N rebuilt six roles at base, switched its order to HOLD #5 and charged 360 (`game.log:1331`, `rebuilt-at-base.png`). Both owned processes were stopped; original screenshots, action histories, logs and inspected local crops remain under their evidence directories. These solo runs do not prove two-client replication, hostile-client RPC validation, or long-session balance.

**VERIFIED in the milestone 4 integration run (2026-09-29):** The `CoopRTSEditor` Development build succeeded; the editor asset script logged `COMBAT_UNITS_READY`, creating the three role Data Assets; Linux `BuildCookRun` completed with `BUILD SUCCESSFUL` and exit code 0. Three fresh standalone Boot automation runs passed with explicit `Test Completed. Result={Success}`: `CoopRTS.Combat.Encounter` (role-specific range, server damage/death, target invalidation, bounded Attack, Move/Hold/Retreat and stale-intent replacement), `CoopRTS.Orders.ReplaceHoldRetreat`, and `CoopRTS.Movement.TwoGroups`. Logs: `Saved/Verification/combat-integration-20260929/{combat,regression,movement}.log`.

**Packaged desktop evidence:** Owned Linux game sessions under `Saved/Verification/combat-desktop-20260929/`, `combat-targeted-20260929/`, and `combat-effects-20260929/` exercised actual 1/2 selection, WASD/Space camera, right-click Move, Q on a living hostile (`ATTACK TARGET`), Q on floor (`ATTACK AREA`), H and R. Screenshots show per-role/health/target HUD, red attack indicators, enemy HP falling, unit casualties, cyan ranged and purple siege attack flashes, position-bound Hold and surviving units retreating with target cleared. The last effects run waited too long before R: all selected units died and the order was rejected, so that attempt is **not** a Retreat pass; the earlier two live runs and the combat automation cover accepted Retreat. See each run's `RESULTS.md`, `actions.jsonl` and `game.log`. Desktop images cannot prove exact radius, authority under a second client or long-term stability; automation cannot prove desktop Q hit-testing/rendering.

**Earlier movement/cursor runs:** The C++ editor/game build and Linux package completed successfully before combat changes. The packaged arena was visually inspected and exercised with keyboard pan, mouse drag, wheel zoom, selection, army focus, and ground orders. The six-unit army reached a commanded destination on the opposite side of the central obstacle and retreated home. Mouse dragging was corrected to use viewport-position deltas and visually rechecked against native pointer motion.

The pre-economy group-movement pass verified two independently selectable armies exchanging positions, navigating around the obstacle, preserving movement after rejected orders, and admitting the former free reinforcement from home. Both earlier live assertion scenarios passed, including every-unit successful path completion, eight rapid order replacements per army, boundary rejection, and immediate Hold. The earlier packaged UI returned seven-unit armies home. That behavior is superseded by paid six-role casualty replacement and is **not** milestone 5 proof. Historical evidence is under `Saved/Verification/two-groups-20260929-grounded/` and `Saved/Verification/two-groups-20260929-grounded-ui/`; the latter's `RESULTS.md` identifies screenshots and coverage limits.

Crossing exposed a Character RVO final-approach stall; units now use Unreal's built-in Detour crowd controller and its path-query flags, without a second RVO layer. Reinforcement exposed normal character grounding during initialization; its route is now prepared from the actual post-spawn feet position before admitting the new member.

The Linux cursor fix removes forced viewport confinement (`EMouseLockMode::DoNotLock`). Packaged verification confirmed a visible native arrow, twenty unwarped left/right motion segments, ground clicks at both sides, and middle-drag followed by free pointer motion; the drag preserved the held order. Cursor-inclusive captures and action logs are under `Saved/Verification/cursor-unconfined-20260929/`. The pointer is intentionally allowed to leave the game window.

The original empty-map packaging milestone passed an earlier 60-second launch, which was startup smoke evidence only. Current checks use explicit live assertions and owned-process launch/stop recipes, not timed exit as a pass. Runtime logs are under `Builds/Linux/CoopRTS/Saved/Logs/`. Milestone 8 now adds independently observed real remote worlds on this Linux workstation; other hardware, Internet traversal and long-session stability remain unverified.

## Playing the command prototype

| Input | Action |
| --- | --- |
| WASD | Pan camera; diagonal speed is normalized |
| Middle mouse + drag | Drag the ground beneath the pointer |
| Mouse wheel | Zoom in/out within fixed limits |
| Left-click a unit | Select its army |
| Left-click empty ground | Clear selection |
| 1 / 2 | Select your first / second army without changing either army's orders |
| Space | Center the camera on the selected army |
| Right-click ground | Replace the selected army's movement destination |
| Q over a live hostile unit or enemy HQ | Request targeted Attack with the selected friendly army |
| Q over reachable ground | Request attack-location; A remains WASD pan-left |
| H | Hold immediately, cancelling all current unit movement |
| R | Retreat to the army's home marker |
| N | Request paid restoration of missing roles for the selected owned army at its base or an owned forward site; rebuild a wiped army at its base |
| F1 / F2 / F3 | Choose Siege Optics / Field Repairs / Entrenched Frontline once during this match; cannot change the choice until a fresh restart |
| Enter after Victory or Defeat | Request a fresh match; no effect during an ongoing match |

Cyan rings show selected units; a white outline marks the selected army's home. Move has a green destination ring, Retreat orange, Hold cyan, and Attack a red destination/target indicator. The HUD distinguishes `ATTACK TARGET` from `ATTACK AREA`, shows a 1050-unit pursuit limit from the accepted Attack destination, target health/distance and per-unit role/health/target, both headquarters' health, the enemy's current plan and rationale, and a Victory/Defeat overlay. Rejection feedback is yellow. Wallet, baseline plus owned-resource income, site ownership/progress, full replacement quote and purchase-source eligibility remain visible. Orders remain named Move/Retreat after arrival until replaced.

In multiplayer, the HUD presents your commander slot, owned army and wallet plus the `PlayerArray` teammate roster, doctrine and commander colors; client selection is local and only resolves groups whose replicated owning PlayerState is yours. Native two-window proof inspected client-focused 1/Space, right-click Move with host acceptance, F2 doctrine and H Hold, including the host's roster update. An expanded fifth-client view showed all five commander rows and its locally selected second army. This does not establish human readability or coordination during combat.

Only the owning controller may submit orders for an army. The server validates map bounds, navigation projection and complete paths for every formation slot before replacing intent. Targeted Attack validates a living hostile unit or enemy HQ in the same world and uses the authoritative target location; null target retains attack-location behavior. Rejected requests preserve the previous order. Hold cancels all AI path requests and stops character velocity; Retreat clears combat intent and returns home. After a terminal result, Q/right-click/H/R/N cannot mutate gameplay; camera and group selection remain available until Enter requests a fresh Boot world.

The arena starts with two owned six-unit armies, a six-unit autonomous enemy and distinct friendly/enemy damageable headquarters; each army has two frontline, two ranged and two siege roles. The second owned army's home is 1000 Unreal units farther along negative X. Two resource sites and one forward reinforcement site begin neutral. A friendly-controlled resource site contributes to **each** teammate's separate wallet; baseline income remains without any territory. Capture progress pauses while both teams are present; ownership changes through neutral when a rival takes control. N purchases only missing fixed-composition roles at their quoted total cost; a full army cannot buy a seventh member. A living army must be within 450 Unreal units of its base or the team-owned forward site to purchase. Its missing roles spawn at that source, and a wiped owned group remains selectable/rebuildable at its home. Destroying the hostile HQ with real weapon hits wins; losing the friendly HQ loses. Start another match with Enter after either result.

### Live movement regression

`CoopRTS.Orders.ReplaceHoldRetreat` exercises real characters and navmesh movement in a fresh Boot world: initial movement, replacement toward a different target, every unit remaining stationary after Hold, rejection of an out-of-bounds order, and return toward home.

After building the editor target, run from the project root:

```bash
"$UE_ROOT/Engine/Binaries/Linux/UnrealEditor" "$PWD/CoopRTS.uproject" \
  /Game/Maps/Boot -game -nullrhi -nosound -unattended \
  -ExecCmds="Automation RunTests CoopRTS.Orders.ReplaceHoldRetreat; SoftQuit" \
  -abslog="$PWD/Saved/Logs/OrderRegression.log" -stdout
```

Require process exit zero and the exact scenario's `Test Completed. Result={Success}`; a timed exit or acceptance log does not establish a pass. This scenario changes the local army's state; run it in a fresh standalone game rather than an active play session.

`CoopRTS.Movement.TwoGroups` runs separately in a fresh standalone Boot world. It buys a casualty replacement during a short Move near base, then exchanges the two groups' home positions, rapidly replaces both groups' orders, rejects out-of-bounds/obstacle/near-wall destinations without interrupting movement, checks immediate Hold and requires all twelve members to cross around the obstacle and settle at their latest path endpoints with successful path completion. Distant free home-spawn reinforcement during the central crossing is no longer permitted: paid recovery requires the living group's center near a valid source. An idle but failed path is not arrival. The scenario has a 90-second deadline and must not share an altered world with another test.

### Reusable verification skill

The project-local [verify-cooprts skill](.agents/skills/verify-cooprts/SKILL.md) maps camera, selection, orders, crossing/navigation, [paid recovery](.agents/skills/verify-cooprts/features/reinforcement.md), [combat](.agents/skills/verify-cooprts/features/combat.md), [economy/capture](.agents/skills/verify-cooprts/features/economy.md), [enemy strategy/match](.agents/skills/verify-cooprts/features/match.md), and [doctrines](.agents/skills/verify-cooprts/features/doctrines.md) to specific assertions and targeted packaged-input recipes. OMP discovers `.agents/skills/` at session startup; use `/skill:verify-cooprts` in a fresh session, or read the file directly in an existing session.

The workflow is assertion-first: select the matching legacy or `doctrine-siege`, `doctrine-repairs`, `doctrine-frontline`, `doctrine-restart` scenario for changed rules, then use short packaged-input/visual checks for affected controls or presentation. Earlier runs established the milestone 6 behavior on their then-current build; they are not proof of the milestone 7 changes. The doctrine scenarios require real live weapon, health, owner, navigation and restart observations; no packaged input or client/server correctness is inferred from standalone tests. The driver checks editor-module freshness before regressions and package freshness before desktop input, records artifact/process identity, refuses input unless its own window is focused, and terminates only its owned desktop instance.

```bash
V=.agents/skills/verify-cooprts/scripts/verify.py
LOGIC_RUN=Saved/Verification/my-unique-strategy-logic
"$V" --run "$LOGIC_RUN" regression --scenario strategy
# Each outcome and each legacy scenario needs its own new directory/fresh Boot world:
"$V" --run Saved/Verification/my-unique-match-win regression --scenario match-win
"$V" --run Saved/Verification/my-unique-match-loss regression --scenario match-loss
# Doctrine scenarios each require a separate fresh Boot world; these use engine
# Automation SoftQuit to exit naturally after completion, without -seconds
# or a subprocess deadline. Do not drive a user's existing game instance.
for scenario in doctrine-siege doctrine-repairs doctrine-frontline doctrine-restart; do
  "$V" --run "Saved/Verification/my-unique-${scenario}" regression --scenario "$scenario"
done
# When packaged input/rendering proof is needed, use a distinct new directory:
RUN=Saved/Verification/my-unique-desktop-run
"$V" --run "$RUN" launch
"$V" --run "$RUN" doctor
"$V" --run "$RUN" capture baseline
# Exercise only the affected desktop probes and inspect their visual evidence.
"$V" --run "$RUN" stop
```

Build the editor target when C++/tests change; the regression wrapper rejects a missing/stale editor module, requires exit zero and the named test's explicit Success, and records the module stamp. Build/package when a required desktop check would otherwise use stale gameplay inputs. Reuse unchanged visual evidence only with its artifact/run identified and exclusions explained. Evidence remains under ignored `Saved/Verification/`; keep full-desktop captures local. The skill separates live assertions, input integration, visuals, failures and unverified paths. Milestone 7 integration and independent evidence are linked above; the new two-client/five-player replication commands remain unverified until integration.

### Regenerating the arena

The map and material assets are already present; normal builds do not need regeneration. `Build/GenerateCommandMap.py` reconstructs the test arena through Unreal's editor APIs.

**This replaces the actors in Boot. Do not run it over hand-edited map work you want to keep.** Close the editor, build `CoopRTSEditor`, then run:

```bash
"$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" "$PWD/CoopRTS.uproject" \
  -EnablePlugins=PythonScriptPlugin \
  -ExecutePythonScript="$PWD/Build/GenerateCommandMap.py" \
  -unattended -nullrhi -nosplash
```

Require `COMMAND_ARENA_GENERATED` in the log before packaging. The script uses a factory-built navigation brush and enables dynamic Recast generation, so navigation also initializes in cooked games. Existing unit material graphs are preserved on reruns.
