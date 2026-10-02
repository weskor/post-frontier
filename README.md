# Post-Frontier

*"You're absolutely right. Deprecating humanity now."*

Construction-first cooperative RTS roguelite prototype (code name CoopRTS) and Unreal implementation notes. Title, setting and tone: [Docs/World.md](Docs/World.md). Target design: [Docs/Design.md](Docs/Design.md), an index of topic files under `Docs/Design/`, with research in [Docs/Research/](Docs/Research/). A source-verified table comparing the current build with the target is in [Docs/Design/status.md](Docs/Design/status.md). The mechanics sections below describe the current Extractor/region-goal build; Outposts, sector income, Secure/Defend fronts and the Barracks level-2 upgrade are removed. Agents: start at [AGENTS.md](AGENTS.md).

**Linux playtest builds:** the Development (`Builds/Linux`) and Shipping (`Builds/LinuxShipping`) packages open a main menu with **Availability Zone v2** selected by default and **Availability Zone** as the classic option. **Play vs JEV** starts offline; **Host co-op** uses the same selected map. Tonight's instructions and artifact-scoped evidence: `Saved/Verification/playtest-tonight/HOW-TO-PLAY.md` and `RESULTS.md`. Steam uses test **App ID 480**; distinct-account invitation/friends-list joining and Internet co-op remain pending. Earlier Steam proof and limitations: `Saved/Verification/steam/RESULTS.md`.

## Game and current match

One to five commanders cooperate on a continuous top-down battlefield against an enemy commander. Each owns a wallet, buildings, building-owned forces, and at most one purchased workshop specialization; the team shares region control and wins by destroying the enemy HQ. Losing the friendly HQ ends the match. The design target is 8–12 minutes ([Docs/Design/battle.md](Docs/Design/battle.md)), not a measured duration of this build.

**Build first, then give forces goals.** A new match starts with two HQs, a battlefield tiled into polygon regions and **no starting armies or buildings**. Each side's main region belongs to it while its HQ stands; every other region has one capture anchor and belongs to whichever team holds that anchor (5 regions on Boot and Campus Zero, 10 on classic Availability Zone, 15 with 13 anchors on Availability Zone v2). Finite Power deposits sit inside regions: two per main and one per other region on Boot, Campus Zero and classic Availability Zone, and 16 (8 rich) on v2. Each friendly commander starts with 600 resources (Power). Baseline income is a flat 2/s per commander, paid by the server every two seconds; region control alone pays nothing. A completed Extractor pays only the commander who built it: 4/s from a 2,400 deposit or 6/s from a 3,000 rich deposit, until the deposit is empty, even if the region is lost later. The enemy has its own wallet with the same 600 start, a flat 2/s baseline regardless of player count, and income only from its own Extractors. Capturing an anchor requires living troops inside its 430-unit capture radius; opposed occupancy pauses capture. Capture takes 8 seconds from neutral and 16 from enemy-held. Extractors never lock capture, so a region can be retaken while they stand. There is **no timed two-site shield hold or invulnerable enemy HQ**: taking ground funds an offensive HQ assault.

### Construction and production

The HUD offers building selection followed by a ground click on a **world-origin-aligned 50 cm build grid**, not fixed pads. Barracks and workshops need their whole footprint (centre and four corners) inside one region your team controls with no living enemy unit in it: your main while your HQ stands, or any region whose capture anchor your team holds. No Extractor is required for build rights. An Extractor needs a free deposit within 300 cm of the click in such a region; it snaps to that deposit, its footprint must fit the deposit's region, and each deposit takes one Extractor. Barracks occupy 5×5 preview cells, Extractors 4×4 and workshops 6×6. Odd footprints snap their centres to `25 + 50n`, even footprints to `50n`, with nearest ties toward positive infinity. Preview and server placement use the same snapped XY; navigation supplies height only (ground +65 cm). Dark territory-tinted cells and brighter green/red footprint cells replace the old placement circles; capture rings, selected-building square outlines, goal-region outlines and configured-front markers remain. The preview window extends eight cells beyond each footprint side, capped at 22×22 cells; territory tint is not full placement approval. Placement validation also blocks contested regions, enemy troops within 330 cm plus the footprint radius, HQ exclusion zones (1,000 cm plus the footprint radius from the enemy HQ; 210 cm plus the footprint radius from either HQ), overlapping buildings/obstacles, arena bounds and unsuitable navigation. Preview validity is a separate boolean from its explanation; the server rechecks terrain, navigation, territory and the owning commander's wallet before spending. Building ownership and controls are private; a teammate cannot configure, cancel or research at your building.

World-space placement cells/grid, selection/front squares, front poles, goal-region outlines, force links, capture rings and attack flashes use non-replicated pooled instanced meshes, not debug drawing. Their depth-tested unlit translucent material reads per-instance RGBA and is always cooked from `/Game/Materials/Overlay`; dedicated servers create no overlay actor. Capture rings retain 48 segments, owner green/red/neutral yellow, radius 430 cm and ground +9 cm. Attack beams and three eight-segment hit-sphere circles persist for 0.32 seconds: Frontline yellow and Ranged cyan use 3 cm beams/17 cm hit radius; Siege purple uses 6 cm beams/32 cm hit radius. Editor, Development and Shipping builds passed; representative native Shipping grid, selection, fronts/links, capture ring, combat flashes and Canvas labels/bars/badges were inspected (`Saved/Verification/round2/RESULTS.md`). All footprint sizes/attack colours, exact expiry, remote/lifecycle and resolution coverage remain unverified.

| Building | Cost | Build time | Health | Use |
| --- | ---: | ---: | ---: | --- |
| Barracks | 220 | 12 s | 500 | Maintain one fixed-capacity force; choose and lock its unit type on first Start |
| Extractor | 160 | 9 s | 350 | Mine one free, finite deposit in a controlled region; pays only its builder and does not lock capture |
| Workshop | 190 | 14 s | 400 | Buy one commander-wide specialization for 150 |

An unfinished building can be cancelled for `floor(build cost × (1 - construction progress))`; finished buildings cannot be cancelled. Buildings can be attacked and destroyed. Construction stops at match end.

Select a completed barracks, choose **Frontline, Ranged or Siege**, then **Start & Lock**. The first accepted Start permanently fixes the type, even while paused or after a complete wipe. A different composition requires another barracks. Each building owns exactly one persistent force; there is **no shared squad cap or competition for another building's replacement slots**. Siege configuration costs 180 once on first Start, replacing the former level-2 conversion.

Each producer receives the lowest unused positive **force number** within its commander's living producers and groups with at least one living unit; the enemy shares its own numbering scope. Building labels, the inspector and living units show that number. Destroying a producer does not free its number while orphan survivors live; reuse is allowed only once neither a living producer nor a living force holds it. Surviving groups retain their number and last front; numbers are not globally unique identities. Click an owned living unit to select its living barracks and change that force's production/goal without returning the camera to base. Orphans clear selection and report “Barracks destroyed; survivors keep their last front.” Selection draws links from barracks to force centre and from centre to a configured front, and outlines the goal region.

All friendly (team 0) living units and producers show their force number as a bold HUD canvas badge in their owning commander's colour with a dark backing, not debug text; enemies have no badges. Each unit has its own badge, including travelling recruits and orphan survivors. Badges project above the actor, skip behind-camera/off-screen positions, hide on modal screens and draw beneath HUD panels; the barracks badge and inspector use the same producer number.

World health and region feedback also uses the Shipping-safe HUD canvas: living units have 36×5 HUD-pixel health bars when damaged or when their force is selected (full-health unselected units stay uncluttered); friendly health is green and enemy health red. HQs show their name and numeric HP, buildings show their name and numeric HP plus a gold construction bar while unfinished, capture anchors show `REGION n` (anchor `SiteIndex` + 1) in their owner colour with a signed-team capture fill, and deposits show `POWER` or `RICH`, the remaining amount, the rate and `FREE`/`TAKEN`/`EMPTY`. Labels use bold 10-point text, dark backings and 4-pixel bars, scale with the HUD, skip behind-camera/off-screen positions and hide on modal screens. Bars draw beneath force badges and panels; placed buildings no longer draw an always-on debug footprint box.

HQ/building label backings are 96–220 HUD pixels wide with 4-pixel horizontal fills inset by 4 pixels; capture-anchor backings are 96 pixels with an 88×4 fill, and deposit backings 148 pixels. Unit badges share the health-bar projection anchor and leave a 6-pixel gap above the bar; producer badges leave at least 6 pixels above the complete label/bar stack, independent of camera zoom.

| Force type | Capacity | Cost per unit | Time per unit | One-time configuration |
| --- | ---: | ---: | ---: | ---: |
| Frontline | 6 | 20 | 10/3 s | 0 |
| Ranged | 4 | 30 | 13/3 s | 0 |
| Siege | 2 | 50 | 20/3 s | 180 |

Production fills vacancies **one paid unit at a time** and automatically replaces casualties while enabled. A unit is charged only after successful physical deployment from a clear barracks exit. Alive joined members and travelling recruits both count toward that building's capacity; at full capacity no timer or money advances. Pause preserves partial work; starvation waits; blocked deployment retains completed work and retries without charging. Recruits walk to their force's moving formation, not an outdated rally point or a teleport to the front. They remain attackable but cannot fire or receive stationary-Defend protection before joining.

Each barracks has one **goal**, set in its inspector. **Hold** and **Expand** then take a region click on the ground or minimap; **Assault** and **Fall Back** apply at once. **Hold** guards a region: the force walks to its capture anchor (the HQ in a main) on a Defend front and engages only enemies within 10.5 m of that point. **Expand** attack-moves region by region through neighbouring regions, capturing anchors on the way, and switches to Hold at the target. **Assault** attack-moves the same way to the enemy main; below 40% of capacity it regroups at the last friendly region on its route and resumes once refilled to full. **Fall Back** regroups at the barracks. Hold and Expand cannot target the enemy main, and the target must be reachable through neighbouring regions. A new force Holds its barracks' own region until given another goal. A complete wipe retains the same force identity and goal for paid rebuilding. Destroying the barracks stops replacements; survivors and travelling recruits keep their last front, are not adopted by another building and cannot be given new goals. Players control buildings and goals, not individual units or squads. The enemy uses the same locked-type production, costs and goals: it builds Extractors, locks its barracks to Frontline, Ranged, then Siege, Holds its regions where human units intrude, Expands, Assaults when it has the advantage or no region is left to take, and sends a force whose joined units average below 35% health to Fall Back until they recover to 80%.

### Workshop specialization

Research is not free or bound to F keys. Select **your completed workshop** and purchase exactly one of the following for 150 resources. The choice is held on your replicated PlayerState, applies to your living and future forces, and resets only in a fresh match. Separate commanders may choose the same specialization. These are the earlier doctrine combat effects reused as paid research, not a shared team upgrade.

| Specialization | Effect |
| --- | --- |
| Siege Optics | Siege range +25%; outgoing damage to units/buildings/HQs -25% (`base * 3 / 4`, integer truncation) |
| Field Repairs | Living units heal 5 HP/s up to max after five uninterrupted seconds without movement, firing or damage |
| Entrenched Frontline | Stationary, joined Frontline units on a Defend front (a Hold goal, or an Assault force regrouping) take 25% less incoming damage (`base * 3 / 4`, integer truncation) |

### Controls in current source

Bindings in `CommandPlayerController` and click actions in `CommandHUD`; verification scopes and native-input limits are recorded below.

| Input | Action |
| --- | --- |
| WASD / middle mouse drag / wheel | Pan / drag camera / zoom |
| Space | Focus selected owned building; otherwise friendly HQ |
| F4 / persistent Construction button | Hide/show contextual deck / cancel targeting and reopen building choices |
| HUD Build Barracks / Extractor / Workshop, then left-click ground | Enter grid placement mode (Extractor: near a free deposit); request a building at snapped XY after preview and server checks |
| HUD Hold / Expand goal button, then left-click a region on the ground or minimap | Assign that goal and target region to the selected owned barracks; the hovered region is outlined. Assault and Fall Back apply on click, without a target |
| Right-click or Esc during placement/goal targeting | Cancel that mode without placing/assigning |
| Esc outside targeting / Menu button | Open match menu; solo pauses simulation, co-op matches keep running; Resume returns to play |
| Left-click owned building / owned living unit / clear world | Select building / select unit's living producer / clear selection; enemy, foreign, dead and orphan units clear selection, not individual-unit control |
| HUD on selected building | Cancel unfinished construction; for barracks choose type before first Start, lock/start/pause/resume production or set its goal; for workshop buy one specialization |
| Left-click minimap | Pan camera; while choosing a Hold/Expand region it picks that region instead; usable in placement and hidden modes |
| Enter | Start solo from the main menu; request a fresh match only on the actual Victory/Defeat screen, never through another dialog |

The compact Canvas HUD uses a maximum scale of 1 rather than growing across large monitors. It has a 980×32 maximum top status island, a 144-pixel bottom-left live minimap, 174-pixel vertical building choices and a 720×186 maximum contextual inspector. Smaller viewports scale down. Construction is always clickable, and accepted placement restores choices automatically. Barracks show joined strength/capacity, travelling recruits, vacancies, one building unit's timer, and actual pause/funds/full/deployment status. Enabled full production is distinct from explicit pause. The minimap shows HQs, living buildings/units/forces, region outlines in their controller's colour, deposits, capture anchors/contests, selected barracks front and camera ground footprint; clicking pans without a gameplay command. Building selection and camera are local; purchases, capture, spawning, damage and outcomes remain server-authoritative. Direct-IP listen-server play remains the network model; no matchmaking, NAT traversal, host migration or reconnect.

### Solo entry, menus and audio

Launch without a map argument, read **How to Play / Controls**, choose **Availability Zone v2** or classic **Availability Zone**, then **Play vs JEV**. The `/Game/Maps/Menu` frontend uses a separate game mode with no command match state or enemy economy; JEV starts only when the gameplay level opens. Map selection is session-only and survives Leave to Menu. Leave and Quit require confirmation. **Play Again** resets the current map; a standalone retry remains standalone rather than silently becoming a listen host. There is no match save/load.

The centered screens and gameplay buttons share drawn/hit-test geometry. Overlays consume clicks instead of passing them to the world or minimap. The overview gives the next construction step; the selected-building inspector shows production progress and remedies.

**Audio** provides saved master volume in 10% steps; 0% mutes gameplay and UI sounds. Imported sounds cover unit combat, structure construction/deployment/research/destruction, HQ warnings, capture and results; events are replicated and late join does not replay old one-shots. The audio importer authors distance depth: a logarithmically scaled low-pass filter from 18,000 to 2,500 Hz and reverb send from 0.08 to 0.20 over 900–7,000 units, with 0.85-second reverb decay. Runtime code adds a match-only 32-second ventilation/wind/power-line ambience loop at class gain 0.25, fading in over 2 seconds and out over 1 second on outcome/leave; the menu stays silent and master mute applies. See [Docs/Audio.md](Docs/Audio.md) for assets, routing and scope. Earlier runtime output/mute was measured; the new ambience/filter/reverb import, packaged lifecycle and subjective balance need fresh verification.


## Design direction and remaining plan

The central promise is cooperative strategy through construction, composition, positioning, income, territory, goals and coordinated HQ assaults rather than individual-unit micro. Each barracks creates a lasting commitment to one force type; building placement, Extractor expansion and reinforcement travel make losses and multiple fronts matter. More barracks provide more independent force capacity, constrained by construction costs, territory and replacement spending rather than a hidden shared squad race. Test whether forward expansion and split responsibilities beat a single deathball. Every map places its own arena, HQ, capture-anchor, region and deposit actors: Boot and Campus Zero use `Build/MatchLayout.py`; Availability Zone uses `Build/Maps/AvailabilityZone.json` and `Build/AvailabilityZoneLayout.py`; Availability Zone v2 uses `Build/Maps/AvailabilityZoneV2.json` and `Build/GenerateAvailabilityZoneV2.py`.

1. **Implemented:** empty construction bases, private wallets/buildings, paid grid-snapped placement, controlled-region build rights, finite-deposit Extractor income paid to the builder, numbered persistent type-locked forces with single-unit paid casualty replacement, owned-unit-to-barracks selection and force links, per-barracks region goals (Hold/Expand/Assault/Fall Back), paid workshop specialization, economic enemy, HQ outcomes/seamless restart, compact HUD/clickable minimap, offline frontend and modal/result screens, event-driven audio with authored ambience/distance depth, Shipping-safe Canvas/world overlays, and Steam lobby host/invite/join using test App ID 480. Individual-unit commands and manual squad controls remain removed; internal order APIs remain for gameplay and verification. Implementation is not full acceptance; evidence below applies only to its recorded artifacts and paths.
2. **Current proof:** `Saved/Verification/round2/RESULTS.md`: editor/Development/Shipping build success, 16 Boot rules tests, Boot construction/production/force-identity/strategy world passes, editor host+remote ownership/production IP slices, quick editor HUD captures, native Shipping visual/input minimum and native IP launcher smoke with Steam running. `Saved/Verification/steam/RESULTS.md`: Development Steam initialization/lobby/listener, visible invite dialog, host-only Enter restart retaining the driver/lobby, Leave/Quit destruction logs, and no-Steam solo; Shipping hosting/dialog/restart/leave/quit/offline surfaces. Earlier solo/audio and map/network evidence remains in `Saved/Verification/solo-readiness/RESULTS.md` and `Saved/Verification/map-integration/RESULTS.md`, scoped to their artifacts.
3. **Verification limits:** native proof is a bounded opening/combat/menu/outcome exercise, not an unaided complete match. Native research walkthrough, full overlay footprint/colour/expiry/resolution/lifecycle matrix, five-player/fault topologies, CampusZero play, delayed-peer restarts, subjective audio balance and human strategy balance remain unverified. Distinct-account invite acceptance/friends-list join, remote Steam restart/teardown, WAN/NAT/relay and long sessions remain pending. Wayland with renderer preload displayed but could not dismiss the Steam dialog; X11/preload dismissed it but native restart input failed. Wayland/no-preload Shipping Enter restart passed; no tested configuration proves usable overlay and restart input together. Ordinary Shipping session/driver lifecycle logs are compiled out.
4. **Human playtests still needed:** one-to-five-player readability, whether private wallets invite coordination, strategy variety, enemy pressure, HQ ending and whether a deathball dominates. Pacing and numerical balance remain prototype values.

Future design candidates, not commitments, include roster drafting, additional units, commander abilities, richer enemy intelligence, shared objectives and bounded model-directed planning. The minimap is implemented. Free F1/F2/F3 doctrine choice, paid N casualty refill, fixed starting armies, individual squad controls and Disrupt Grid shield objective are removed. Supply networks, extensive tech trees, PvP, campaign and a public lobby browser are not current gameplay.

### Technical boundaries

The project uses UE 5.8.3, Linux x86_64, the bundled Clang 20.1.8 toolchain and Vulkan SM6. One C++ module in four layers: **content** (`Content/` data assets: `UArmyUnitDefinition`, `UBuildingDefinition`, `UMatchContent` registry; actors replicate registry indices), **rules** (`Rules/ProductionPolicy`, `PlacementPolicy`, `EconomyPolicy`, `OutcomePolicy` — deterministic, no world; tested by `CoopRTS.Rules.*`), **simulation actors** (authoritative, replicated; `AArmyGroup`/`AArmyUnit` mutate only through `Initialize`/intention-revealing methods) and **presentation** (`ACommandHUD` Canvas, **not UMG**, enumerating the registry and rendering `EProductionState`). Adding a unit or building is a data asset appended to `DA_MatchContent`; adding a map is a level that places `AArenaBounds`, both `AHeadquarters` (`TeamIndex` 0 and 5), its `ACapturePoint` anchors, `AMapRegion` regions and `ADepositSite` deposits. `ACommandGameMode` discovers those actors, refuses to start a match without them, spawns the enemy's commander state, resolves outcomes and restarts into the current map; `ACommandGameState` owns shared territory, placement validation and the `Content`/`Arena`/`EnemyCommander` references; `ACommandPlayerState` owns each wallet and specialization for humans and the enemy alike; `ACommandBuilding` owns construction and applies the production policy to its persistent force; `AArmyGroup` directs that force and its physical reinforcements; `AEnemyCommander` buys buildings and sets region goals through the same definitions and economy. Enhanced Input supplies local controls. No Gameplay Ability System, Mass Entity, external LLM integration or plugin architecture is required for this prototype.

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

Unit and building definitions plus the `DA_MatchContent` catalogue live in `Content/Units/` and `Content/Content/` and are cooked with the package. On a fresh checkout or after changing `Build/GenerateMatchContent.py`, build `CoopRTSEditor`, then run `UnrealEditor-Cmd -EnablePlugins=PythonScriptPlugin -ExecutePythonScript="$PWD/Build/GenerateMatchContent.py" -unattended -nullrhi -nosplash`; require `MATCH_CONTENT_READY units=3 buildings=3` in the log. Existing assets keep their tuned combat values; catalogue order is a replicated contract (units frontline/ranged/siege, buildings barracks/extractor/workshop; the extractor asset is still named `DA_Outpost`). `Build/GenerateCombatUnits.py` is the older combat-stats-only generator.

### Package and run

```bash
"$UE_ROOT/Engine/Build/BatchFiles/RunUAT.sh" BuildCookRun \
  -project="$PWD/CoopRTS.uproject" -noP4 -platform=Linux \
  -clientconfig=Development -build -cook -map=/Game/Maps/Menu+/Game/Maps/Boot+/Game/Maps/CampusZero+/Game/Maps/AvailabilityZone+/Game/Maps/AvailabilityZoneV2 \
  -stage -pak -iostore -package -archive \
  -archivedirectory="$PWD/Builds" -unattended -utf8output
./Builds/Linux/CoopRTS.sh -windowed -ResX=1280 -ResY=720
./Builds/Linux/CoopRTS.sh /Game/Maps/Boot -windowed -ResX=1280 -ResY=720
./Builds/Linux/CoopRTS.sh /Game/Maps/CampusZero -windowed -ResX=1280 -ResY=720
```

Distribute the entire `Builds/Linux` directory, not just the executable. Generated builds, caches, and logs are ignored by Git.

Shipping is a separate archive; the verified UAT layout is directly `Builds/LinuxShipping`, **not** `Builds/LinuxShipping/Linux`. It leaves the Development archive unchanged:

```bash
"$UE_ROOT/Engine/Build/BatchFiles/RunUAT.sh" BuildCookRun \
  -project="$PWD/CoopRTS.uproject" -noP4 -platform=Linux \
  -clientconfig=Shipping -build -cook -map=/Game/Maps/Menu+/Game/Maps/Boot+/Game/Maps/CampusZero+/Game/Maps/AvailabilityZone+/Game/Maps/AvailabilityZoneV2 \
  -stage -pak -iostore -package -archive \
  -archivedirectory="$PWD/Builds/LinuxShipping" -unattended -utf8output
# Offline Shipping playtest:
./Builds/LinuxShipping/CoopRTS.sh -nosteam -windowed -ResX=1280 -ResY=720
# Test-App-ID Steam shell smoke (not release distribution setup):
SteamAppId=480 SteamGameId=480 ./Builds/LinuxShipping/CoopRTS.sh -windowed -ResX=1280 -ResY=720
```

Distribute the entire Shipping archive. Both packaging commands passed in `Saved/Verification/round2/RESULTS.md`, with unchanged Development binary/pak/utoc checksums after Shipping packaging. Shipping opened Menu despite the supplied gameplay map URL in the recorded smoke; use the real frontend to enter gameplay. Standard verification helpers target Development and require logs/probes absent in ordinary Shipping; the recorded Shipping checks used removed disposable guarded adapters and inspected surfaces. Build success and App ID 480 smoke are not Steam release readiness.

Menu is the packaged default (`GameDefaultMap`). Choose **Availability Zone v2** (default, 15 regions) or **Availability Zone** (classic, 10 regions), then **Play vs JEV** for offline solo or **Host co-op** for Steam. The choice persists until changed or the application exits; Play Again/restart retain the current world. Both maps, Boot and CampusZero are explicitly cooked by both commands above; `/Game/Audio` is always cooked. Editor startup and verification tools still default to Boot; pass `--map /Game/Maps/AvailabilityZoneV2` for v2 gameplay checks or `--map /Game/Maps/Menu` for frontend launch. Normal builds do not need map regeneration.

**Availability Zone v2 (greybox):** `Docs/Maps/AvailabilityZoneV2.md` has the 15-region plan, generator and measurements. Its 13 capture anchors and 16 deposits are reachable in the recorded editor navigation check. The earlier JEV construction/economy regression failed at its producer-scoped retreat assertion; this playtest chunk does not change the AI or certify that strategy regression. Tonight's artifact-specific checks and player instructions are in `Saved/Verification/playtest-tonight/RESULTS.md` and `HOW-TO-PLAY.md`.

The enabled ModelContextProtocol/AllToolsets editor plugins activate GameFeatures. UE 5.8.3's `GameFeaturesEditorModule.cpp::AddDefaultGameDataRule` requires a `GameFeatureData` primary-asset scan with `CookRule=AlwaysCook`; without it, the cook reports two Asset Manager errors and exits 1. `Config/DefaultGame.ini` includes that exact rule. The first milestone 8 cook failed on the missing rule; the subsequent full BuildCookRun completed with `BUILD SUCCESSFUL` after adding it. No plugin was disabled.

### Steam co-op (implemented; scoped runtime proof)

With Steam running and logged in, launch the menu normally (no `-nosteam`), select the map and choose **Host co-op**. `UCoopSessionSubsystem` creates `NAME_GameSession` as an advertised presence lobby with **5 public connections total, including the host**, using Valve's test **App ID 480**, then opens the selected map as a listen server. The hosted match menu offers **Invite friends**, which requests Steam's overlay invite dialog. A friend accepts the invitation or chooses **Join Game** from Steam's friends list; the invite-accepted callback joins the lobby and travels to the session's resolved connection string. There is no player-facing direct-IP entry or public lobby browser.

Status text reports Steam availability and hosted/joined player count out of 5. When Steam is not running or logged in, hosting/invites are unavailable with a player-visible reason; **Play vs JEV** remains offline and does not require Steam. Join/network/travel failures return to the menu with a reason. Confirmed Leave or Quit destroys the named session before returning to the menu or exiting; cleanup failure remains visible and asks for a retry. Match outcomes keep the session available for **Play Again**/terminal Enter: existing seamless restart keeps the lobby and connected clients rather than destroying/recreating it. Listen-host departure still ends the hosted match.

Rejected pending joins destroy the joined lobby membership and retain the reason on Menu so Host becomes available again. A listen host keeps its lobby on per-client faults (including checksum mismatch); only `NetDriverListenFailure`, `NetDriverCreateFailure` and `NetDriverAlreadyExists` may tear it down. Accepting another invitation while hosting **more than one player total** is refused visibly; leave the hosted match first rather than silently disconnecting its guests.

UE 5.8.3 uses the modern `/Script/SteamSockets.SteamSocketsNetDriver` here, with `/Script/OnlineSubsystemUtils.IpNetDriver` as the fallback. `OnlineSubsystemSteam` uses `SteamDevAppId=480`, `bRelaunchInSteam=false`, Steam networking and permitted P2P relay. The fallback is automatic **only when the Steam driver is unavailable**, not after an active Steam join or NAT/relay failure. `-nosteam` prevents the Steam subsystem and SteamSockets from initializing, making the fallback plain IP even with Steam running; use it on every developer/probe peer below.

For Linux **Development** runs outside a Steam launch, UE's `SteamSharedModule.cpp` writes ASCII `480` to `FPlatformProcess::BaseDir()/steam_appid.txt` before `SteamAPI_InitEx`, and removes it at module shutdown. For the packaged build this is `Builds/Linux/CoopRTS/Binaries/Linux/steam_appid.txt`; editor game processes use the engine's `Engine/Binaries/Linux` base directory. These directories must be writable. No manually staged `steam_appid.txt` is required for Development. Automatic file creation/removal is compiled only under `!UE_BUILD_SHIPPING && !UE_BUILD_SHIPPING_WITH_EDITOR`; Shipping does not write it.

For a **Shipping release**, obtain the project's real Steam App ID and define `UE_PROJECT_STEAMSHIPPINGID` through `ProjectDefinitions` in `Source/CoopRTS.Target.cs`, then distribute and launch through Steam. That target currently has no such definition; the engine defaults it to `0`. Shipping's `GetRelaunchSettings` ignores the development `SteamDevAppId`/`bRelaunchInSteam` settings, requires a Steam relaunch and uses the compiled App ID for `SteamAPI_RestartAppIfNecessary` (only when nonzero). App ID 480 shell smoke and a development `steam_appid.txt` are not release identity/distribution setup; the target remains unchanged pending a real App ID.

`Saved/Verification/steam/RESULTS.md` records Development `[AppId: 480] Client API initialized 1`, GameSession with 5 public/0 private connections, SteamSockets listening on 7777 and **Hosting on Steam / players 1/5**. The actual **Choose Friend to Invite** dialog was inspected; no invitations were sent. Controlled Development defeat → native Enter retained the same net driver and did not recreate the lobby; confirmed Leave/Quit logged session destruction before menu/exit. Shipping visually passed hosting/dialog, natural-defeat Enter restart with retained 1/5 status, Leave/Quit and `-nosteam` solo. Shipping has `USE_LOGGING_IN_SHIPPING=0`; those surfaces do not prove its internal session/driver IDs or destruction callback.

Overlay-visible shell runs used `SteamAppId=480 SteamGameId=480` and `LD_PRELOAD="$HOME/.local/share/Steam/ubuntu12_64/gameoverlayrenderer.so"`; X11 additionally used `SDL_VIDEODRIVER=x11`. Normal Development shell hosting worked, but its overlay request alone did not render the dialog. Native Wayland/preload displayed it but could not dismiss it; X11/preload displayed/dismissed it but native Play Again/Enter restart input failed. Native Wayland/no-preload Shipping Enter restart passed. This is a backend/native-input acceptance gap, not an established session/travel code defect; a Steam-managed launch and usable overlay plus restart in the same configuration still need human verification.

Follow the [Steam runtime smoke and pending acceptance](.agents/skills/verify-cooprts/features/multiplayer.md#steam-runtime-smoke-and-pending-acceptance) with **two distinct logged-in friends accounts** using identical copies of the **entire fresh Development archive (`Builds/Linux`)**, not merely the same source/version or executable. Invite acceptance, friends-list Join Game, both-player retained restart, remote Leave/host departure, cleanup failure/retry and WAN/NAT/relay remain unverified. Same-account loopback was attempted and rejected by SteamSockets' local-identity check; it is not remote-join proof. Shipping smoke supplied App ID environment variables without modifying the archive or staging `steam_appid.txt`; real release identity/distribution setup remains required.

### Development direct-IP transport (not the intended Steam flow)

The listen host owns authoritative combat, capture, construction, wallets, production and outcomes. Commanders have independent slots 0–4 and wallets; a sixth commander is rejected. Run the current Linux Development package on each machine on the same LAN or reachable private network. Permit inbound UDP7777 on the host and substitute its private IP below:

```bash
# Host (one terminal / one machine):
./Builds/Linux/CoopRTS.sh '/Game/Maps/AvailabilityZone?listen' -nosteam -port=7777 -windowed -ResX=1280 -ResY=720
# Remote player (a separate machine or process; use the host's LAN address):
./Builds/Linux/CoopRTS.sh 192.168.1.42:7777 -nosteam -windowed -ResX=1280 -ResY=720
```

Use `-nosteam` on **both** host and client; it is a game executable switch, not a player-facing direct-IP menu. For two fresh editor game processes instead of a package:

```bash
# Host:
"$UE_ROOT/Engine/Binaries/Linux/UnrealEditor" "$PWD/CoopRTS.uproject" \
  '/Game/Maps/AvailabilityZone?listen' -game -nosteam -port=7777 -windowed -ResX=1280 -ResY=720
# Client (substitute the host's LAN address, or 127.0.0.1 for a local probe):
"$UE_ROOT/Engine/Binaries/Linux/UnrealEditor" "$PWD/CoopRTS.uproject" \
  192.168.1.42:7777 -game -nosteam -windowed -ResX=1280 -ResY=720
```

Do not launch a second client on top of the host process. Direct IP/LAN hosting is **not** matchmaking, Internet discovery, relaying, NAT traversal or a promise about public-network connectivity. Listen-host exit ends the match; terminal Enter restarts the current map for connected players.

### Network verification

The opt-in Development real-socket probe and [multiplayer skill recipe](.agents/skills/verify-cooprts/features/multiplayer.md) exercise owning-client building RPCs, private costs, locked force configuration, independent producers/fronts, actual paid casualty replacement and replicated joined/travelling state, outposts (historical: the recipe text predates the Extractor cutover; the probe now has extractor and goal actions instead), research, HQ damage and a connected restart. Host-only fixtures supply explicit funding/capture/lethal setup; this is not a naturally paced match. Earlier `construction-network-b-20260929` and `construction-package-two-a-20260929` passes belong to the replaced batch model. Headless probes do not establish native-window input or five-player/fault acceptance.

`network.py` supplies `-nosteam` to every launched process, including hosts, clients, rejection peers and rendered/offscreen variants. `network_desktop.py` also supplies it to every native host and client; no extra Python CLI argument is needed. Post-cutover editor ownership/production passed in round 2, and ownership passed again in the Steam run; native Development IP launcher smoke connected host+remote with Steam running. These prove developer IP fallback, not Steam sessions or native multiplayer gameplay input. Economy/restart sockets, the full construction acceptance chain and packaged network acceptance were not rerun in round 2; establish those separately before broader claims.

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

Current integrated build/rules/world/IP and representative Shipping overlay proof is in `Saved/Verification/round2/RESULTS.md`; scoped Steam host/dialog/host-only restart/teardown proof and backend/join gaps are in `Saved/Verification/steam/RESULTS.md`. Earlier solo/audio proof is in `Saved/Verification/solo-readiness/RESULTS.md`, and map/network integration in `Saved/Verification/map-integration/RESULTS.md`, each limited to its recorded artifact. Earlier restructure/wave-three records cover their stated artifacts; pre-restructure fixed-force evidence is historical. See the [verification skill](.agents/skills/verify-cooprts/SKILL.md) for the tier model (rules → world → network slice → presentation → acceptance) and proof selection.

### Regenerating the arena

The map and material assets are already present; normal builds do not need regeneration. `Build/GenerateCommandMap.py` reconstructs the test arena through Unreal's editor APIs; the match actors it and `Build/GenerateCampusZero.py` place (arena, HQs, capture anchors, regions and deposits) come from `Build/MatchLayout.py`.

**This replaces the actors in Boot. Do not run it over hand-edited map work you want to keep.** Close the editor, build `CoopRTSEditor`, then run:

```bash
"$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" "$PWD/CoopRTS.uproject" \
  -EnablePlugins=PythonScriptPlugin \
  -ExecutePythonScript="$PWD/Build/GenerateCommandMap.py" \
  -unattended -nullrhi -nosplash
```

Require `COMMAND_ARENA_GENERATED` in the log before packaging. The script uses a factory-built navigation brush and enables dynamic Recast generation, so navigation also initializes in cooked games. Existing unit material graphs are preserved on reruns.
