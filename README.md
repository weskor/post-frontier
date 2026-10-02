# Post-Frontier

*"You're absolutely right. Deprecating humanity now."*

Construction-first cooperative RTS roguelite prototype (code name CoopRTS) and Unreal implementation notes. Title, setting and tone: [Docs/World.md](Docs/World.md). Target design: [Docs/Design.md](Docs/Design.md), an index of topic files under `Docs/Design/`, with research in [Docs/Research/](Docs/Research/). A source-verified table comparing the current build with the target is in [Docs/Design/status.md](Docs/Design/status.md). The mechanics sections below describe the current Extractor/region-goal build; Outposts, sector income, Secure/Defend fronts and the Barracks level-2 upgrade are removed. Agents: start at [AGENTS.md](AGENTS.md).

**Linux playtest builds:** Development and Shipping open a main menu with **Availability Zone v2** selected by default and **Availability Zone** as the classic option. **Play vs JEV** starts offline; **Host co-op** uses the same selected map and Valve's test **App ID 480**. Packaging and launch procedures: `./x help package` and `./x help play`.

## Game and current match

One to five commanders cooperate on a continuous top-down battlefield against an enemy commander. Each owns a wallet, buildings, building-owned forces, and at most one purchased workshop specialization; the team shares region control and wins by destroying the enemy HQ. Losing the friendly HQ ends the match. The design target is 8–12 minutes ([Docs/Design/battle.md](Docs/Design/battle.md)), not a measured duration of this build.

**Build first, then give forces goals.** A new match starts with two HQs, a battlefield tiled into polygon regions and **no starting armies or buildings**. Each side's main region belongs to it while its HQ stands; every other region has one capture anchor and belongs to whichever team holds that anchor (5 regions on Boot and Campus Zero, 10 on classic Availability Zone, 15 with 13 anchors on Availability Zone v2). Finite Power deposits sit inside regions: two per main and one per other region on Boot, Campus Zero and classic Availability Zone, and 16 (8 rich) on v2. Each friendly commander starts with 600 resources (Power). Baseline income is a flat 2/s per commander, paid by the server every two seconds; region control alone pays nothing. A completed Extractor pays only the commander who built it: 4/s from a 2,400 deposit or 6/s from a 3,000 rich deposit, until the deposit is empty, even if the region is lost later. The enemy has its own wallet with the same 600 start, a flat 2/s baseline regardless of player count, and income only from its own Extractors. Capturing an anchor requires living troops inside its 430-unit capture radius; opposed occupancy pauses capture. Capture takes 8 seconds from neutral and 16 from enemy-held. Extractors never lock capture, so a region can be retaken while they stand. There is **no timed two-site shield hold or invulnerable enemy HQ**: taking ground funds an offensive HQ assault.

### Construction and production

The HUD offers building selection followed by a ground click on a **world-origin-aligned 50 cm build grid**, not fixed pads. Barracks and workshops need their whole footprint (centre and four corners) inside one region your team controls with no living enemy unit in it: your main while your HQ stands, or any region whose capture anchor your team holds. No Extractor is required for build rights. An Extractor needs a free deposit within 300 cm of the click in such a region; it snaps to that deposit, its footprint must fit the deposit's region, and each deposit takes one Extractor. Barracks occupy 5×5 preview cells, Extractors 4×4 and workshops 6×6. Odd footprints snap their centres to `25 + 50n`, even footprints to `50n`, with nearest ties toward positive infinity. Preview and server placement use the same snapped XY; navigation supplies height only (ground +65 cm). Dark territory-tinted cells and brighter green/red footprint cells replace the old placement circles; capture rings, selected-building square outlines, goal-region outlines and configured-front markers remain. The preview window extends eight cells beyond each footprint side, capped at 22×22 cells; territory tint is not full placement approval. Placement validation also blocks contested regions, enemy troops within 330 cm plus the footprint radius, HQ exclusion zones (1,000 cm plus the footprint radius from the enemy HQ; 210 cm plus the footprint radius from either HQ), overlapping buildings/obstacles, arena bounds and unsuitable navigation. Preview validity is a separate boolean from its explanation; the server rechecks terrain, navigation, territory and the owning commander's wallet before spending. Building ownership and controls are private; a teammate cannot configure, cancel or research at your building.

World-space placement cells/grid, selection/front squares, front poles, goal-region outlines, force links, capture rings and attack flashes use non-replicated pooled instanced meshes, not debug drawing. Their depth-tested unlit translucent material reads per-instance RGBA and is always cooked from `/Game/Materials/Overlay`; dedicated servers create no overlay actor. Capture rings retain 48 segments, owner green/red/neutral yellow, radius 430 cm and ground +9 cm. Attack beams and three eight-segment hit-sphere circles persist for 0.32 seconds: Frontline yellow and Ranged cyan use 3 cm beams/17 cm hit radius; Siege purple uses 6 cm beams/32 cm hit radius.

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

Bindings live in `CommandPlayerController` and click actions in `CommandHUD`.

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

The compact Canvas HUD uses a maximum scale of 1 rather than growing across large monitors. It has a 980×32 maximum top status island, a 144-pixel bottom-left live minimap, 174-pixel vertical building choices and a 720×186 maximum contextual inspector. Smaller viewports scale down. Construction is always clickable, and accepted placement restores choices automatically. Barracks show joined strength/capacity, travelling recruits, vacancies, one building unit's timer, and actual pause/funds/full/deployment status. Enabled full production is distinct from explicit pause. The minimap shows HQs, living buildings/units/forces, region outlines in their controller's colour, deposits, capture anchors/contests, selected barracks front and camera ground footprint; clicking pans without a gameplay command. Building selection and camera are local; purchases, capture, spawning, damage and outcomes remain server-authoritative. Co-op uses an authoritative listen server with Steam lobby and developer direct-IP transports described below; there is no host migration or reconnect.

### Solo entry, menus and audio

Launch procedures: `./x help play`. The `/Game/Maps/Menu` frontend uses a separate game mode with no command match state or enemy economy; JEV starts only when the gameplay level opens. **How to Play / Controls** describes the controls, **Play vs JEV** starts offline solo, and **Host co-op** hosts the selected map. Map selection is session-only and survives Leave to Menu. Leave and Quit require confirmation. **Play Again** resets the current map; a standalone retry remains standalone rather than silently becoming a listen host. There is no match save/load.

The centered screens and gameplay buttons share drawn/hit-test geometry. Overlays consume clicks instead of passing them to the world or minimap. The overview gives the next construction step; the selected-building inspector shows production progress and remedies.

**Audio** provides saved master volume in 10% steps; 0% mutes gameplay and UI sounds. Imported sounds cover unit combat, structure construction/deployment/research/destruction, HQ warnings, capture and results; events are replicated and late join does not replay old one-shots. The audio importer authors distance depth: a logarithmically scaled low-pass filter from 18,000 to 2,500 Hz and reverb send from 0.08 to 0.20 over 900–7,000 units, with 0.85-second reverb decay. Runtime code adds a match-only 32-second ventilation/wind/power-line ambience loop at class gain 0.25, fading in over 2 seconds and out over 1 second on outcome/leave; the menu stays silent and master mute applies. See [Docs/Audio.md](Docs/Audio.md) for assets, routing and scope.


## Design direction and remaining plan

The central promise is cooperative strategy through construction, composition, positioning, income, territory, goals and coordinated HQ assaults rather than individual-unit micro. Each barracks creates a lasting commitment to one force type; building placement, Extractor expansion and reinforcement travel make losses and multiple fronts matter. More barracks provide more independent force capacity, constrained by construction costs, territory and replacement spending rather than a hidden shared squad race. Test whether forward expansion and split responsibilities beat a single deathball. Every map places its own arena, HQ, capture-anchor, region and deposit actors: Boot and Campus Zero use `Build/MatchLayout.py`; Availability Zone uses `Build/Maps/AvailabilityZone.json` and `Build/AvailabilityZoneLayout.py`; Availability Zone v2 uses `Build/Maps/AvailabilityZoneV2.json` and `Build/GenerateAvailabilityZoneV2.py`.

1. **Implemented:** empty construction bases, private wallets/buildings, paid grid-snapped placement, controlled-region build rights, finite-deposit Extractor income paid to the builder, numbered persistent type-locked forces with single-unit paid casualty replacement, owned-unit-to-barracks selection and force links, per-barracks region goals (Hold/Expand/Assault/Fall Back), paid workshop specialization, economic enemy, HQ outcomes/seamless restart, compact HUD/clickable minimap, offline frontend and modal/result screens, event-driven audio with authored ambience/distance depth, Shipping-safe Canvas/world overlays, and Steam lobby host/invite/join using test App ID 480. Individual-unit commands and manual squad controls remain removed; internal order APIs remain for gameplay and verification.
2. **Human playtests still needed:** one-to-five-player readability, whether private wallets invite coordination, strategy variety, enemy pressure, HQ ending and whether a deathball dominates. Pacing and numerical balance remain prototype values.

Future design candidates, not commitments, include roster drafting, additional units, commander abilities, richer enemy intelligence, shared objectives and bounded model-directed planning. The minimap is implemented. Free F1/F2/F3 doctrine choice, paid N casualty refill, fixed starting armies, individual squad controls and Disrupt Grid shield objective are removed. Supply networks, extensive tech trees, PvP, campaign and a public lobby browser are not current gameplay.

### Technical structure

The project uses UE 5.8.3, Linux x86_64, the bundled Clang 20.1.8 toolchain and Vulkan SM6. One C++ module in four layers: **content** (`Content/` data assets: `UArmyUnitDefinition`, `UBuildingDefinition`, `UMatchContent` registry; actors replicate registry indices), **rules** (`Rules/ProductionPolicy`, `PlacementPolicy`, `EconomyPolicy`, `OutcomePolicy` — deterministic, no world; tested by `CoopRTS.Rules.*`), **simulation actors** (authoritative, replicated; `AArmyGroup`/`AArmyUnit` mutate only through `Initialize`/intention-revealing methods) and **presentation** (`ACommandHUD` Canvas, **not UMG**, enumerating the registry and rendering `EProductionState`). Adding a unit or building is a data asset appended to `DA_MatchContent`; adding a map is a level that places `AArenaBounds`, both `AHeadquarters` (`TeamIndex` 0 and 5), its `ACapturePoint` anchors, `AMapRegion` regions and `ADepositSite` deposits. `ACommandGameMode` discovers those actors, refuses to start a match without them, spawns the enemy's commander state, resolves outcomes and restarts into the current map; `ACommandGameState` owns shared territory, placement validation and the `Content`/`Arena`/`EnemyCommander` references; `ACommandPlayerState` owns each wallet and specialization for humans and the enemy alike; `ACommandBuilding` owns construction and applies the production policy to its persistent force; `AArmyGroup` directs that force and its physical reinforcements; `AEnemyCommander` buys buildings and sets region goals through the same definitions and economy. Enhanced Input supplies local controls. No Gameplay Ability System, Mass Entity, external LLM integration or plugin architecture is required for this prototype.

Human and enemy economies use `ACommandPlayerState::Resources`, `TrySpend`, `AddResources` and `Doctrine`. Human states have `TeamIndex = 0` and `CommanderIndex` 0–4; `ACommandGameState::EnemyCommander` references a replicated, controllerless state with `TeamIndex = 5` and `CommanderIndex = -1`. Enemy buildings and forces carry that state as their owner. The enemy stays outside `PlayerArray` on authority and clients, so human commander lists and player RPC ownership remain unchanged; GameMode creates a fresh enemy state after level discovery for each match rather than carrying its wallet or doctrine through seamless travel. The network probe retains the `enemyResources` JSON key.

The visual direction favors readable silhouettes, dark environments and restrained team accents. [World, naming, Campus Zero and art pipeline](Docs/World.md) contains themed map and asset-generation detail; [Audio direction](Docs/Audio.md) specifies faction sound palettes, the event list and the Unreal mix design; [Unreal MCP](Docs/UnrealMCP.md) describes the optional editor bridge. Editor and generation procedures: `./x help editor` and `./x help gen`. The art and lore documents may describe older mechanics; current gameplay rules above and source take precedence.

## Engineering entry point

Procedures live only in `./x help`; the docs describe the project and mechanics, not a second way to run it.

| Need | Procedure reference |
| --- | --- |
| Engine/tool prerequisites and the required shader patch | `./x help setup` |
| Build the editor target | `./x help build` |
| Open the editor | `./x help editor` |
| Package Development or Shipping | `./x help package` |
| Launch offline, Steam or developer direct-IP play | `./x help play` |
| Generate maps, content, materials or imported assets | `./x help gen` |
| Rules/world scopes and network, HUD or native verification | `./x help test`, `./x help verify` |
| Balance simulation | `./x help sim` |
| Prove a change, inspect run records and land it | `./x help check`, `./x help runs`, `./x help land` |

`CoopRTS.uproject` contains the C++ game and editor targets. The project uses Epic's Linux UE 5.8.3 binary distribution, its bundled Clang and .NET SDK, Git LFS for binary content, and a Vulkan SM6-capable GPU/driver; no engine source build is required. `Build/UnrealEngine-5.8.3-ShaderPrint.patch` removes forced unrolling from the two vector-print overloads in `Engine/Shaders/Private/ShaderPrintCommon.ush`, following [DXC issue #8417](https://github.com/microsoft/DirectXShaderCompiler/issues/8417) and the [matching Unreal report](https://forums.unrealengine.com/t/2771073/2). The patch affects the shared engine installation; Vulkan SM6, virtual shadow maps and PSO precaching remain enabled. Packaged-game players do not need the engine or patch separately.

Unit and building definitions plus the `DA_MatchContent` catalogue live in `Content/Units/` and `Content/Content/` and are cooked with the package. Existing assets keep their tuned combat values; catalogue order is a replicated contract (units frontline/ranged/siege, buildings barracks/extractor/workshop; the Extractor asset is still named `DA_Outpost`).

Menu is the packaged default (`GameDefaultMap`). Availability Zone v2 has 15 regions, 13 capture anchors and 16 deposits; classic Availability Zone has 10 regions. Both maps, Boot and CampusZero are in the configured cook list; `/Game/Audio` is always cooked. Development and Shipping have separate run-specific archives; legacy playtest archives are not overwritten by the runner. A distributable package includes its whole archive, not only its executable.

The enabled ModelContextProtocol/AllToolsets editor plugins activate GameFeatures. UE 5.8.3's `GameFeaturesEditorModule.cpp::AddDefaultGameDataRule` requires a `GameFeatureData` primary-asset scan with `CookRule=AlwaysCook`; `Config/DefaultGame.ini` includes that rule.

Map and material assets are already present; normal builds do not need regeneration. `/Game/Maps/Boot` has a floor, central obstacle, boundary walls, home markers and a dynamically generated Unreal navmesh. `Build/GenerateCommandMap.py` reconstructs Boot through Unreal's editor APIs and replaces its actors, so regeneration is destructive to hand-edited map actors. It and `Build/GenerateCampusZero.py` take their arena, HQ, capture-anchor, region and deposit layout from `Build/MatchLayout.py`. The Boot generator uses a factory-built navigation brush and dynamic Recast generation for cooked-game navigation; existing unit material graphs are preserved on reruns. Generator procedures and output markers: `./x help gen`.

Units use Unreal's built-in Detour crowd controller and its path-query flags, without a second RVO layer. The Linux pointer is intentionally allowed to leave the game window (`EMouseLockMode::DoNotLock`).

## Steam co-op

Steam launch procedures: `./x help play`. **Host co-op** creates `NAME_GameSession` through `UCoopSessionSubsystem` as an advertised presence lobby with **5 public connections total, including the host**, using Valve's test **App ID 480**, then opens the selected map as a listen server. **Invite friends** requests Steam's overlay invite dialog. Invitation acceptance or Steam friends-list **Join Game** invokes the invite-accepted callback, joins the lobby and travels to the session's resolved connection string. There is no player-facing direct-IP entry or public lobby browser.

Status text reports Steam availability and hosted/joined player count out of 5. When Steam is not running or logged in, hosting/invites are unavailable with a player-visible reason; **Play vs JEV** remains offline and does not require Steam. Join/network/travel failures return to the menu with a reason. Confirmed Leave or Quit destroys the named session before returning to the menu or exiting; cleanup failure remains visible and asks for a retry. Match outcomes keep the session available for **Play Again**/terminal Enter: existing seamless restart keeps the lobby and connected clients rather than destroying/recreating it. Listen-host departure ends the hosted match.

Rejected pending joins destroy the joined lobby membership and retain the reason on Menu so Host becomes available again. A listen host keeps its lobby on per-client faults (including checksum mismatch); only `NetDriverListenFailure`, `NetDriverCreateFailure` and `NetDriverAlreadyExists` may tear it down. Accepting another invitation while hosting **more than one player total** is refused visibly rather than silently disconnecting guests.

UE 5.8.3 uses `/Script/SteamSockets.SteamSocketsNetDriver`, with `/Script/OnlineSubsystemUtils.IpNetDriver` as the fallback. `OnlineSubsystemSteam` uses `SteamDevAppId=480`, `bRelaunchInSteam=false`, Steam networking and permitted P2P relay. The fallback is automatic **only when the Steam driver is unavailable**, not after an active Steam join or NAT/relay failure. `-nosteam` prevents the Steam subsystem and SteamSockets from initializing, making the fallback plain IP even with Steam running.

For Linux **Development** outside a Steam launch, UE's `SteamSharedModule.cpp` writes ASCII `480` to `FPlatformProcess::BaseDir()/steam_appid.txt` before `SteamAPI_InitEx` and removes it at module shutdown. Packaged processes use their binaries directory; editor game processes use the engine's `Engine/Binaries/Linux` base directory. Those directories must be writable; Development does not require a manually staged file. Automatic creation/removal is compiled only under `!UE_BUILD_SHIPPING && !UE_BUILD_SHIPPING_WITH_EDITOR`; Shipping does not write it.

A **Shipping release** requires a real Steam App ID compiled as `UE_PROJECT_STEAMSHIPPINGID` through `ProjectDefinitions` in `Source/CoopRTS.Target.cs` and Steam distribution/launch. The target currently has no such definition; the engine defaults it to `0`. Shipping's `GetRelaunchSettings` ignores development `SteamDevAppId`/`bRelaunchInSteam`, requires a Steam relaunch and uses the compiled App ID for `SteamAPI_RestartAppIfNecessary` only when nonzero. Test App ID 480 is not a release identity. Shipping has `USE_LOGGING_IN_SHIPPING=0`.

## Developer direct-IP transport

Launch and network procedures: `./x help play` and `./x help verify`.

The listen host owns authoritative combat, capture, construction, wallets, production and outcomes. Commanders have independent slots 0–4 and wallets; a sixth commander is rejected. Developer IP transport uses UDP port 7777 and requires a reachable host. `-nosteam` is a game executable switch, not a player-facing direct-IP menu. Runner network and desktop probes disable Steam on all their peers.

Direct IP/LAN hosting is not matchmaking, Internet discovery, relaying or NAT traversal. Listen-host exit ends the match; terminal Enter restarts the current map for connected players.
