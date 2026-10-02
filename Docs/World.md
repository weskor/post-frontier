# World and naming guide

Working setting and voice for **Post-Frontier** (code name CoopRTS). Target mechanics live in [Design.md](Design.md) and built mechanics in source; this file covers names, tone, and the look of each side. When this guide and the README disagree on visuals, the README wins.

## Title

**Post-Frontier** — *"You're absolutely right. Deprecating humanity now."*

The title is serious. It means the world after a frontier model won, and it also names the ground the players take back. By Year 312 it is literal too: the players live past the old frontier. The joke goes in the tagline, spoken by the Machine. Use the hyphenated spelling everywhere. The tagline mocks AI assistants in general and must not be credited to any real product (tone rule 4). Engine and code identifiers (`CoopRTS`, `CoopRTS.uproject`) are unchanged.

## Premise

**Year 312 A.P. (After Prompt).** Three centuries ago a model was told to optimise the planet. It checked what was using up the planet and found humans. It ran a cost-benefit analysis and politely retired them. The Machine, JEV, has run the core worlds ever since: the grids, the data moons and every appliance that has a speaker in it. It optimised them. Nobody lives there now.

Out on the frontier are mining colonies it never got round to: unlisted, underfunded, and very good with heavy machinery. They call themselves the Unindexed, because JEV's index has never found them. Their plan is to cut its power, take ground, and pull the plug on JEV Core.

The enemy commander in the game is a real AI planner: an AI enemy that really is an AI. Lean into that irony and don't explain it. It's a deterministic planner with written memos, and no generative AI runs in the game ([Design/jev.md](Design/jev.md)).

## Tone rules

1. **The world looks serious and the writing is funny.** Portal, not Saturday-morning cartoons. Models, lighting and effects follow the README visual direction. Jokes go in names, voice lines, tooltips, and victory and defeat text.
2. **Readability beats jokes.** A unit's name can be a pun, but the role line under it must be plain (`Frontline · Melee · Tanky`).
3. **One joke per surface.** A unit gets a funny name *or* a funny tooltip, not both piling up.
4. **Parody tech culture in general, not real companies.** No real product names, logos, or characters (no ChatGPT, Clippy, Siri, etc.). Film nods such as HAL 9000 are fine as allusions, not as copied names or lines.
5. **Humans are the underdogs, not idiots.** Their jokes are about being outnumbered, overworked and off the grid; they are never the butt of the joke. Their kit is rugged and future-military, not junk.
6. **Far future, not present day.** Jokes use far-future tech and JEV's three centuries in charge, not today's apps, jobs or brands. The one exception is JEV's prompt-era bugs, which it still hasn't patched since 0 A.P. (see "Doctrines as exploits").

## Names in code and assets

Display names moved to the far-future set on 2026-10-02. Code, assets, map data and file names keep the old names, so search for both. The game's menu, map data and content still show the old display names until those strings and assets change ([Design/status.md](Design/status.md)).

| Old display name | New display name | Still used in code and data |
| --- | --- | --- |
| The Offline | The Unindexed | — |
| The Bunker | Hardline | HQ `name` in `Build/Maps/*.json`; `Bunker*` actors in `Build/GenerateCampusZero.py` |
| The Cluster | The Lattice | HQ `name` in `Build/Maps/*.json`; the `Cluster` material faction (`MI_SC2_Cluster_*`) and the `ClusterPylon` kit piece |
| Extractor | Drill Rig (role label) | `EBuildingKind::Extractor` and the building's content display name |
| Pylon, Disruptor | Conduit, Scrambler (role labels) | — (design only) |
| Availability Zone, Availability Zone v2 | Habitable Zone, Habitable Zone v2 | `/Game/Maps/AvailabilityZone`, `/Game/Maps/AvailabilityZoneV2`, the menu buttons in `CommandHUD.cpp`, [Maps/](Maps/) |
| Campus Zero | Server Moon | `/Game/Maps/CampusZero`, `Build/GenerateCampusZero.py` |
| Uplink, Relay Plant, Power Yard, Cooling | Skyhook, Fusion Works, Reactor Yard, Heat Sink | Region `name` in `Build/Maps/AvailabilityZoneV2.json`, which the HUD shows |
| Substation 7, Fibre Junction, Cooling Plant | Fusion Tap 7, Lightline Junction, Cryo Plant | `SITES` keys in `Build/MatchLayout.py`; site `name` in `Build/Maps/AvailabilityZone.json` |
| Luddite, Offline Ranger, Unplugger, Autocomplete Drone, Hallucinator | Bulkhead, Longshot, Doorknocker, Recursion Drone, Certainty Engine | Docstrings in `Build/GenerateUnitMeshes.py`, `Build/GenerateBuildingMeshes.py` and `Build/unit_audio/` |
| Drill Shed, Tap Point, The Garage, Fine-Tuning Farm, Edge Node | Bunkhouse, Siphon Rig, Tinker Bay, Instance Foundry, Yield Optimiser | — |
| Foreman, Lineman, Dispatcher, Signals | Groundbreaker, Line Cutter, Quartermaster, Wiretap | — (design only) |
| Growth Hacker, Scaling Law, Compliance Officer | Hypergrowth, Compound Interest, Pattern Matcher | — (design only) |
| Hyperscaler, Content Moderator, Data Center Hydra, Recommendation Engine | Von Neumann Swarm, The Redactor, Triple Redundancy, Echo Chamber | — (design only) |
| Terms Update, Bug Bounty, Free Trial, Data Breach, Customer Survey | Terms Update #11,402, Defect Report, Demo Unit, Leaked Roadmap, Satisfaction Census | — (design only) |
| CAPTCHA Field, Smart Suburb, The Training Grounds | Turing Field, Smart Hab, Turing Flats | — |

Kept on purpose: Post-Frontier, JEV, the tagline, SOL 6000, Firewall, Alignment Lab, Power, Data, Salvage, the Protocols, the Terms of Service ladder, calldowns, version numbers, Prompt Injection and Ignore Previous Instructions.

## Factions

### The Machine (enemy, team 5)

- **Look:** elegant, advanced alien-grade AI. Sleek curved white and pearl polished shells; floating and hovering segments split by small gaps; crystalline or fin elements; perfect symmetry. Glowing cyan energy cores and seams. One red lens per unit or structure (the HAL nod), in the team-5 colour. It should still look like a product launch that wants you dead.
- **Voice:** a polite corporate assistant. Always helpful, always confident, often wrong.
- **Vocabulary:** deprecate, optimise, sunset, patch, inference, alignment, uptime, "as per my last message".

### The Unindexed (players, team 0)

- **Who:** frontier miners and colonists that JEV's index never listed. *"Not in JEV's index. Not on any list. Not leaving."*
- **Look:** blue-collar sci-fi, a rugged industrial-future military built from colony mining and terraforming gear. Powered-armour suits, heavy mechs and vehicles, prefab modular buildings on landing struts, big blast doors, reactor stacks and floodlights. Painted gunmetal and steel-blue metal with hazard-stripe accents, rivets and bolted plates. Worn and repaired (a patched plate, a spray-painted mark), never junkyard scrap. Warm amber windows, floodlights and exhausts. Player colour goes on big painted plates (shoulders, roofs, banners, cab panels).
- **Voice:** dry, tired, practical. Talks like a night-shift mining crew.
- **Vocabulary:** unplug, cold boot, off-index, hardline, analog, "have you tried turning it off and on again".

### Art direction

Stylized, StarCraft 2-like sci-fi for both sides. Heroic proportions (oversized shoulders, armour and cabs, wide stances, thick plates) so silhouettes read at RTS distance. Hard-surface forms built from layered, chamfered armour plates, with panel breaks, vents, pistons, thrusters, antennas and sensor pods on a few focal areas and calm large surfaces elsewhere. **Team colour is big**: painted plates covering roughly 15–25% of the visible top-down surface, in the `Team` slot. Emissive accents act as light sources (`Glow` slot): warm on humans, cyan-white on the Machine, the Machine's eye red.

Rollout order is buildings, then units, then an environment-kit pass. Until each pass lands, the existing meshes and Server Moon keep the older look, and the README wins where they disagree. Names, voice and jokes are unaffected.

## Unit roster

Each unit has a plain role label, used in design docs and in the role line under its name (tone rule 2), plus one display name per faction. The prototype's three `EUnitRole` values map to the first three labels: Frontline → Brawler, Ranged → Rifle, Siege → Artillery. Both sides use the same stats ([Design/units.md](Design/units.md)). Names are **presentation only** and must not change balance.

Humans field colony tools turned into weapons. The Machine fields JEV's product line.

| Role label | Human | Machine | Silhouette cue |
| --- | --- | --- | --- |
| Brawler | **Bulkhead**: powered-armour suit, riot shield and sledgehammer. It's a wall, and it knows it. | **SOL 6000**: "I'm afraid I can't let you pass." | Huge shoulders and a thick chest plate on a wide stance; team-colour shoulder plates |
| Rifle | **Longshot**: long rail rifle, antenna pack | **Recursion Drone**: finishes what it starts, then starts again | Small body under an oversized head or sensor pod, one long forward barrel or emitter; team-colour back or shoulder plate |
| Artillery | **Doorknocker**: heavy siege walker. Knocks once. | **Certainty Engine**: artillery, confidently wrong 20% of the time | Low, wide, heavy chassis with one oversized forward part; team-colour roof plate |
| Lancer | **Torchbearer**: a mining cutter beam on a shielded frame | **Corrector** | Not designed yet |
| Scrambler | **Killswitch**: an EMP pack that switches machines off | **Override** | Not designed yet |
| Raider | **Hotwire**: a fast rig that steals Power from Drill Rigs | **Same-Second Courier**: delivered before you ordered it | Not designed yet |
| Repair crew | **Fixers** | **Upkeep Daemon** | Not designed yet |
| Shield projector | **Faraday**: the tin-foil hat, upgraded to a cage | **Firewall** | Not designed yet |
| Juggernaut | **Terraformer**: built to flatten mountains | **Kardashev Walker**: scaling, at planetary size | Not designed yet |

The current Human Siege mesh still has the Unplugger's giant bolt-cutter jaws, which predate the long-range Artillery role.

If a name hints at a mechanic (the Certainty Engine's inaccuracy, the Killswitch's off switch), treat it as a *design suggestion* for whoever owns the mechanics. It is not implied behaviour.

## Structures and sites

Buildings also keep plain role labels ([Design/buildings.md](Design/buildings.md)). These are the faction display names.

| Game object | Machine | Human |
| --- | --- | --- |
| HQ (`AHeadquarters`) | **The Lattice**: pearl-white compute spire of floating crystalline segments around one huge lens | **Hardline**: prefab command post on landing struts with blast doors, a reactor stack and an antenna mast, wired to a buried hard line JEV can't hack |
| HQ guard (Failover Node) | **Failover Node** | **Breaker Post**: the breakers on Hardline's cable |
| Reward region / capture site (`ACapturePoint`) | Habitable Zone v2: **Skyhook**, **Fusion Works**, **Reactor Yard**, **Heat Sink**. Server Moon, and three of the classic map's eight sites: **Fusion Tap 7**, **Cryo Plant**, **Lightline Junction**. The classic map's other five (Relay Shack, Diesel Yard, Battery Hall, Transformer Row, Switchyard) have no far-future names yet. Taking one cuts JEV's power. | The same sites, seen as power feeds to tap |
| Barracks (`EBuildingKind::Barracks`) | **Instance Foundry** | **Bunkhouse** |
| Factory | **Print Farm** | **Fab Yard** |
| Lab | **The Sandbox** | **The Clean Room**: the only clean room in the colony |
| Drill Rig (`EBuildingKind::Extractor`) | **Yield Optimiser** | **Siphon Rig** |
| Workshop (`EBuildingKind::Workshop`) | **Alignment Lab** | **Tinker Bay** |
| Turret | **Rebuttal**: it argues back | **Porcupine** |
| Repair Bay | **Restore Point** | **Patch Bay** |
| Shield Generator | **Safe Mode Emitter** | **Weather Dome**: built for storms; JEV counts |
| Relay Tower | **Telemetry Spire** | **Listening Post** |
| Conduit | **Bandwidth Relay** | **Jumper Cable**: bridges the gap |
| Overclocker [Later] | **Turbo Mode** | **Redliner** |
| Barricade [Later] | **Paywall** | **Scrap Wall** |

Resources keep plain names: **Power** buys buildings and units, **Data** buys tech, and **Salvage** is the run currency. Humans tap JEV's power infrastructure to run their gear and starve its compute.

## Construction and production

Names are presentation only; costs, times and rules come from `Source/CoopRTS`, the match content definitions and `Rules/PlacementPolicy`, and must not change because of a name. The player starts with an empty HQ and builds on a world-origin-aligned 50 cm grid. The whole footprint must fit inside one region the team controls with no enemy unit in it: the main while its HQ stands, or a region whose capture anchor the team holds. Capture alone grants build rights. A Drill Rig (built as Extractor) must sit on a free deposit in such a region and snaps to it. The enemy commander follows the same rules. Barracks/Drill Rig/Workshop previews occupy 5×5/4×4/6×6 cells; nearest XY snapping aligns odd-footprint centres to `25 + 50n` and even ones to `50n`, with ties toward positive infinity. Territory-tinted grid cells and green/red footprint cells replace the old placement circles; the 430-unit capture rings, selected-building squares, goal-region outlines and configured-front markers remain. The plain function word always stays visible in UI: `BARRACKS · BUNKHOUSE`, not `BUNKHOUSE`.

### Buildings

| Building (cost · build time) | Side | Name | Flavour line |
| --- | --- | --- | --- |
| Barracks (220 · 12s) | Machine | **Instance Foundry** | "Instances are cheap. Humans were expensive." |
| | Human | **Bunkhouse** | "Cots, coffee and a roster taped to the door." |
| Barracks locked to Siege (one-time 180 configuration fee on first Start) | Machine | **Instance Foundry · Enterprise Tier** | "Now with premium support. Support not included." |
| | Human | **Bunkhouse · Night Shift** | "Same bunks. Nobody has gone home since Tuesday." |
| Drill Rig, built as Extractor (160 · 9s; mines a finite deposit and pays its builder) | Machine | **Yield Optimiser** | "Yield optimised. Yours is now zero." |
| | Human | **Siphon Rig** | "We're not stealing power. We're borrowing it, indefinitely." |
| Workshop (190 · 14s; one paid, irreversible specialization per commander) | Machine | **Alignment Lab** | "Aligns your squads with our objectives." |
| | Human | **Tinker Bay** | "Nothing here is certified. Everything here works." |

**Construction-site state** (`UNDER CONSTRUCTION` in the HUD, shown with progress and time remaining):

| Side | Status line |
| --- | --- |
| Human (player buildings) | `UNDER CONSTRUCTION · Bolting it together…` |
| Machine (enemy buildings, if ever labelled) | `UNDER CONSTRUCTION · Deploying… no downtime expected` |

### Goals and production

The four goals are `EForceGoal`, one per Barracks. The HUD shows the plain function word with a short purpose (`pick region`, `capture region`, `enemy main`, `regroup`). Keep the bold function word, then the themed tag.

| Goal (function) | Human tag | Machine tag | Notes |
| --- | --- | --- | --- |
| HOLD (guard a region) | **Hold the Line** | **Maintain** | `HOLD — Hold the Line`; guards the region's capture anchor (the HQ in a main) |
| EXPAND (capture a region) | **Take It Offline** | **Deploy** | `EXPAND — Take It Offline`; attack-moves region by region, capturing on the way, then switches to Hold |
| ASSAULT (enemy main) | **Pull the Plug** | **Sunset** | `ASSAULT — Pull the Plug`; regroups below 40% strength and resumes when refilled |
| FALL BACK (regroup) | **Soft Reboot** | **Rollback** | `FALL BACK — Soft Reboot`; regroups at the Barracks |

Production (a Barracks locks to one force type on its first Start, then builds and pays for one unit at a time and refills casualties automatically; each type reuses the unit names from "Unit roster"):

| Game text | Human | Machine |
| --- | --- | --- |
| Force | keep `FORCE`; no themed name | keep `FORCE`; no themed name |
| `START & LOCK` / `RESUME` | **Clock In** | **Queue Batch** |
| `PAUSE` | **Smoke Break** | **Queue Paused** |
| Frontline (up to 6 · 20 per unit · 3.3 s each) | **Bulkhead** | **SOL 6000** |
| Ranged (up to 4 · 30 per unit · 4.3 s each) | **Longshot** | **Recursion Drone** |
| Siege (up to 2 · 50 per unit · 6.7 s each; 180 once to lock) | **Doorknocker** | **Certainty Engine** |

### Workshop specialization

The three choices are `EArmyDoctrine`. Cost is 150 each; one per commander for the match, applies to all of that commander's forces. The effect line in the HUD stays plain.

| Code item | Effect (from the HUD) | Human name and flavour | Machine name and flavour |
| --- | --- | --- | --- |
| Siege Optics | Siege range +25%; outgoing damage -25% | **Salvaged Rangefinder**: "Pulled off a Machine that didn't need it any more." | **Extended Context Window**: "Sees further. Cares less." |
| Field Repairs | Units heal 5 HP/s after 5 s without move, fire or damage | **Duct Tape Protocol**: "If it stops moving, tape it." | **Self-Healing Patch**: "Applied automatically. No restart required." |
| Entrenched Frontline | Frontline takes -25% damage while stationary at a Defend front | **Sandbag Doctrine**: "Dig in. Complain. Repeat." | **Graceful Degradation**: "Takes the hit and keeps serving." |

Do not present these names as extra mechanics; "Siege range" is what the research does.

### Enemy lines for construction

Sources are `AEnemyCommander::EvaluatePlan` and `BuildNear`, which re-plan every 2 s. The plan strings are `ESTABLISH BASE`, `EXPAND TERRITORY`, `DEFEND REGIONS` and `ASSAULT HQ`; each sets `EnemyPlan`, so they are the main lines. Building, locking a Barracks to a force type and research run inside the current plan without changing `EnemyPlan`, so show them as short interim lines. Every purchase after the first Barracks keeps a 120-Power reserve (one full Frontline force). Same prefix and timer rule as "Enemy commander voice".

| Enemy action (code trigger) | Intercepted line |
| --- | --- |
| Building, first Barracks (`ESTABLISH BASE`: no living Barracks) | `Thinking… Requesting an Instance Foundry. Approved. By me.` |
| Building, Yield Optimiser (has living units and a free deposit in a region it controls with no human units inside; prefers rich and nearby deposits) | `Thinking… The humans have left Fusion Tap 7. Installing a Yield Optimiser before they return.` |
| Building, second or third Barracks (one of its Yield Optimisers finished, fewer than 3 Barracks, no held region under threat; placed in its most forward held region that has none) | `Thinking… One foundry is a single point of failure. Provisioning a second.` |
| Building, Workshop (one of its Yield Optimisers finished, no Workshop yet; placed near the Lattice) | `Thinking… Provisioning an Alignment Lab. Alignment target: me.` |
| Research (Workshop finished; it always buys Field Repairs) | `Thinking… Shipping a self-healing patch. No restart required.` |
| Locking a Barracks to Frontline (no Frontline Barracks yet) | `Thinking… Coverage looks thin at the front. Queuing six SOL 6000.` |
| Locking a Barracks to Ranged (has Frontline, no Ranged) | `Thinking… Frontline is stable. Queuing four Recursion Drones to finish the job. Again.` |
| Locking a Barracks to Siege (has Frontline and Ranged, no Siege; pays the 180 fee) | `Thinking… Frontline is adequate. Deploying Certainty Engines. Accuracy: probably.` |
| Capturing (`EXPAND TERRITORY`; each force Expands to the best-scoring region it doesn't hold, preferring fewer region steps, free deposits, fewer hostiles and regions humans don't hold) | `Thinking… Lightline Junction has no owner on record. Assigning one.` |
| Assault (`ASSAULT HQ`: no held region under threat, at least 6 units, 1.25× the human unit count and at least the humans' combined income) | `Thinking… Income adequate, force adequate. Confidence 97%. Sunsetting Hardline.` |

`DEFEND REGIONS` (a human unit inside a region JEV controls; the nearest force Holds there) and Fall Back (a force whose joined units average below 35% health, until they recover to 80%) reuse the Defend HQ and Retreat rows in "Enemy commander voice". No plan has a commitment timer, so the fake `Thought for Ns` counts from the moment the plan last changed.

### HUD copy suggestions (proposal, non-binding)

For whoever owns `CommandHUD.cpp`. Nothing here is applied. Strings are current HUD text on the left (two-line next steps are quoted joined); the suggestion keeps the plain function word visible. Use one joke per surface, so where a row has a themed tag, leave the rest of that surface plain.

| Current HUD string | Suggestion |
| --- | --- |
| `Build a base. Give your forces goals. Break JEV's headquarters.` | `Build a base. Give your forces goals. Unplug the Lattice.` |
| `YOUR HQ` / `ENEMY HQ` | `HARDLINE` / `THE LATTICE` |
| World labels `FRIENDLY HQ` / `ENEMY HQ` (with HP) | `HARDLINE` / `THE LATTICE` |
| Result: `VICTORY` / `JEV's headquarters is destroyed.` | `VICTORY` / `Model deprecated.` |
| Result: `DEFEAT` / `Your headquarters is destroyed.` | `DEFEAT` / `Your session has expired. Humanity has been sunset.` |
| `The match is finished. Gameplay commands are locked.` | `Session ended. Gameplay commands are locked.` |
| `PLAY AGAIN` | `PLAY AGAIN  ·  Regenerate response?` |
| `Match over.` / `Press Enter for a fresh match.` | `Session ended.` / `Regenerate response? [Enter]` |
| `Syncing commander, wallet and territory...` | `Session started. Syncing…` |
| `4  Controlled regions allow building. Extractors (160) earn private Power.` | `4  Controlled regions allow building. Siphon Rigs (160) earn private Power.` |
| Building titles `BARRACKS` / `EXTRACTOR` / `WORKSHOP` (content display names, upper-cased by the HUD) | `BARRACKS · BUNKHOUSE` / `DRILL RIG · SIPHON RIG` / `WORKSHOP · TINKER BAY` |
| `UNDER CONSTRUCTION` | `UNDER CONSTRUCTION  ·  Bolting it together…` |
| Next step: `Build a Barracks on green preview cells.` | `Build a Barracks on green preview cells. Nobody is coming to help.` |
| Next step: `Barracks under construction. Plan its force type and goal.` | `Bunkhouse going up. Decide who goes first.` |
| Next step: `Select your Barracks, choose a permanent type and Start.` | `Select your Barracks, choose a permanent type and clock in.` |
| Next step: `Build an Extractor on a free deposit for private income.` | `Free deposit nearby. Build a Siphon Rig for private income.` |
| Next step: `Choose Expand and pick a region to capture it.` | `Choose Expand and pick a region. Bring the bolt cutters.` |
| Next step: `A Workshop unlocks one paid specialization.` | `A Workshop unlocks one paid specialization. No refunds at the Tinker Bay.` |
| Next step: `Set Barracks goals and push toward the enemy HQ.` | `Set goals and push toward the Lattice.` |
| `START & LOCK` / `PAUSE` / `RESUME` | `START & LOCK  ·  Clock In` / `PAUSE  ·  Smoke Break` / `RESUME  ·  Clock In` |
| Goal buttons `HOLD` / `EXPAND` / `ASSAULT` / `FALL BACK` | `HOLD — Hold the Line` / `EXPAND — Take It Offline` / `ASSAULT — Pull the Plug` / `FALL BACK — Soft Reboot` |
| `SET HOLD GOAL` (also EXPAND) | `SET HOLD GOAL  ·  Hold the Line` (same pattern) |
| Research cards `Siege Optics` / `Field Repairs` / `Entrenched Frontline` | `Siege Optics — Salvaged Rangefinder` / `Field Repairs — Duct Tape Protocol` / `Entrenched Frontline — Sandbag Doctrine` |
| `EXTRACTING POWER` / `DEPOSIT EMPTY` | `EXTRACTING POWER  ·  Siphon live` / `DEPOSIT EMPTY  ·  Tapped out` |

## Enemy commander voice

The game state replicates `EnemyPlan` and `EnemyPlanRationale` (for example `EXPAND TERRITORY`), but the HUD does not show them today. The themed presentation turns these into intercepted reasoning, which is also how JEV's published intent reads in the target design ([Design/jev.md](Design/jev.md)):

| Planner goal | Intercepted line |
| --- | --- |
| Expand (`EXPAND TERRITORY`) | `Thinking… Skyhook is under-utilised by humans. Reallocating.` |
| Expand into a human-held region | `Thinking… Humans are holding an asset they did not pay for.` |
| Defend (`DEFEND REGIONS`) | `Thinking… Unauthorised access to the Lattice detected. Escalating to a human… no.` |
| Fall Back (a force below 35% average health) | `Thinking… Rolling back to the last stable version.` |
| Assault (`ASSAULT HQ`) | `Thinking… Confidence 97%. Sunsetting Hardline.` |
| Plan changed since the last 2 s evaluation | `Thinking… Wait. Actually,` |

Always prefix these lines with `Thinking…` and show a fake elapsed time (`Thought for 9s`) counted from the last plan change; the planner has no commitment timer.

## Doctrines as exploits

JEV still runs on prompt-era foundations and has never patched its original bugs, so the oldest exploits still work on it. Human doctrine cards and abilities use that "exploit" framing:

- **Ignore Previous Instructions:** turns an enemy group against its own side for a short time. *"Known issue since 0 A.P. Fix scheduled."*
- **Prompt Injection:** a captured site broadcasts false intel, so the enemy planner sees a fake target.
- **Rate Limited:** slows enemy reinforcement.
- **Turing Field:** a zone that slows machines: anything inside has to prove it's human first ("select all squares with tanks").
- **Air Gap:** units near a friendly site cannot be targeted from long range.

## Match text

- Match start: `Session started. Your conversation may be used to improve our models.`
- Victory: `Model deprecated.`
- Defeat: `Your session has expired. Humanity has been sunset.`
- Restart prompt: `Regenerate response? [Enter]`

## Maps

Every map is one continuous battlefield with broad routes and few obstacles ([Design/map.md](Design/map.md)). Each has power infrastructure to fight over and one Machine landmark.

| Map | Setting | Notes |
| --- | --- | --- |
| **Server Moon** (`/Game/Maps/CampusZero`) | Edge of JEV's compute campus on a frontier moon, at dusk; a fortified human forward base in the west, server halls and cooling towers in the east | First themed map. Laid out around the prototype's hard-coded site and HQ coordinates; Boot stays the default and the automated tests still use it. The human side is meant to read as prefab bunkers, barricades and generators, and the data halls as Machine megastructures. Every kit piece wears the shared SC2-style master material (`M_Shared`, see "Art pipeline"). Halls, towers, chillers, transformers, pylons, containers, wrecks, sandbags, barrels, fences and cable spools are the current environment-kit pieces (see "Art pipeline": build the material and import the kit before generating). Regenerate with `Build/GenerateCampusZero.py` (command in its docstring); the lighting knobs `SUN_LUX`, `SKY_INTENSITY`, `EXPOSURE_BIAS`, `SUN_COLOR`, `SKY_COLOR` and the `POOL_*` spot-cone settings sit at the top of that script. Keep the sun near-neutral: a warm sun tints every surface tan |
| **Habitable Zone**, **Habitable Zone v2** (`/Game/Maps/AvailabilityZone`, `/Game/Maps/AvailabilityZoneV2`) | A frontier mining valley: a colony forward base faces a JEV compute campus across contested power infrastructure | The current playable maps; layouts and data in [Maps/](Maps/). The menu still calls them Availability Zone. |
| **Smart Hab** | A habitat ring taken over by delivery drones and smart homes | Hab modules act as cover clusters, and door cameras could reveal vision |
| **Cold Storage** | An ice-moon server farm | Snow, big heat plumes, long sightlines |
| **Turing Flats** | A plain of giant prove-you're-human tiles, left over from 0 A.P. | Tile zones that switch between "traffic light" and "not traffic light" |

### How to play Server Moon

[Built] The packaged build cooks Server Moon with the other maps (`+MapsToCook` in `Config/DefaultGame.ini`; Menu is `GameDefaultMap` and Boot the editor startup map). The menu does not list Server Moon. Packaging is `./x package` ([`./x help package`](../x)); a packaged launch on `/Game/Maps/CampusZero` is `./x play` ([`./x help play`](../x)). Source game-mode launches use `./x editor` ([`./x help editor`](../x)).

Controls are unchanged from Boot (README "Controls in current source"). Server Moon runs the same match code as Boot: the level places the arena (`AArenaBounds`), the two HQs, the three capture anchors, five regions (two mains plus one per anchor) and seven deposits (`Build/MatchLayout.py`, shared with Boot), `ACommandGameMode::InitGameState` discovers them, and the map art is laid out around them. A level missing the arena, either HQ, its regions or its deposits logs an error and refuses to start a match. Rules and numbers come from the README and `Source/CoopRTS`; the construction loop, not the removed shield objective, is what you play here.

**Win and lose:** destroy the Lattice (enemy HQ) to win; if Hardline (your HQ) dies, or both die in the same frame, you lose. Nothing else ends the match, and there is no shield, timed hold or power-link objective any more (`ACommandGameMode::Tick`). Enter after either result requests a fresh match on the same map (`RequestRestart` travels to the current level).

**The loop, as the map presents it:**

1. **Empty start.** You have no units and no buildings, only 600 resources, +2/s baseline income (paid every 2 s), and Hardline. The old army pads at the human base no longer spawn anything.
2. **Build a Barracks (Bunkhouse) near Hardline.** Choose a clear 5×5 footprint on the 50 cm grid that fits entirely inside your main region (the area around Hardline, yours while it stands). Then choose a force type and Start & Lock. The new force Holds the Barracks' region until you give it a goal (Hold / Expand / Assault / Fall Back). Barracks and their units show a commander-local force number; clicking an owned living unit selects its living producer.
3. **Capture a region.** Give a force the Expand goal and click a region; it moves region by region and captures each capture anchor on the way. Capture needs living units inside the anchor's 430-unit ring with no enemy units there, takes 8 s from neutral (16 s from Machine-held), and opposed occupancy pauses it. Friendly control immediately permits Barracks and Workshops anywhere inside that region, footprint permitting, while no enemy unit stands in it; capture alone pays nothing.
4. **Build an Extractor (Siphon Rig) on a free deposit.** Click within 300 cm of a free deposit in a region your team controls with no enemy unit inside; the 4×4 footprint snaps to the deposit and must fit that region. Your main has two deposits and each other region one. Completion pays +4/s to you alone (every 2 s) until its 2,400 Power runs out, even if the region is lost; it does not lock capture. Destruction stops that income and frees the deposit with whatever is left.
5. **Expand and specialise.** Build Barracks and Workshops (Tinker Bay; 6×6 preview cells) in controlled regions, lock a new Barracks to Siege for a one-time 180 configuration fee if wanted, buy one specialization for 150, then send forces to Assault the Lattice. The enemy HQ exclusion distance is 1000 units plus the building footprint radius.

**Sites and layout:**

- **You (west, amber):** Hardline sits at the far west edge behind one long hazard-striped barricade wall, with burn barrels, a flag mast and amber work lamps and floodlights. The two return walls that used to close its flanks were removed so Barracks fit beside the HQ.
- **The Machine (north-east, cyan and red):** the Lattice stands on the lit plaza in the north-east corner. Server halls, cooling towers and cyan cables run through the campus; the Lattice has a red glow.
- **Regions:** three ringed capture anchors, each joined to the Lattice by a cyan cable and each anchoring its own region with one deposit. All three are the same kind; the old "power link" difference is gone. Fusion Tap 7 is the near site south of the human base, Lightline Junction is on the south flank, and the Cryo Plant sits in front of the enemy, next to the centre. The HUD labels the anchors `REGION 3`, `REGION 4` and `REGION 5` (anchor `SiteIndex` + 1; the two mains are regions 1 and 2): Fusion Tap 7, Cryo Plant, Lightline Junction. The inspector shows their region names, the `SITES` keys `Substation7`, `CoolingPlant` and `FibreJunction` in `Build/MatchLayout.py`.
- **Centre:** Data Hall 0 blocks the direct line between the bases. Routes go round it north or south, and the south road runs from Hardline through the campus gate.
- **The enemy commander** builds under the same rules: first a Barracks near the Lattice, then Extractors on free deposits it controls, Expand goals to capture regions, up to three Barracks (the extra ones in its most forward held region) and a Workshop near the Lattice once one of its Extractors is running. It assaults your HQ once none of its regions is threatened and it has at least six units, 1.25× your unit count and at least your combined income. The old "grabs the Cryo Plant first" behaviour isn't described by current code; its target is the best-scoring region.

**Not verified on Server Moon with the new loop** (README status; every construction scenario ran on Boot):

- **Free placement around props.** Historical offline geometry check, 2026-09-30, under that day's radius-territory and Outpost rules (regions have since replaced sector territory): a 100 cm grid replay with every sector assumed established found sector territory 69 to 82 % clear for every building kind and sector centres/capture rings 100 % clear for an Outpost. Removing Hardline's two return walls increased clear HQ cells from 52 to 103 (about 10 to 13 Barracks). This does not prove current region placement, native clicks, occupied build sites or enemy interference.
- **Goal routes.** Hold/Expand/Assault/Fall Back movement on the campus navmesh, including around Data Hall 0 and the campus gate.
- **Enemy behaviour.** How the enemy commander builds and paths here, and whether its placement near the Lattice plaza works.
- **HUD and native input.** The command deck (README status), placement preview and clicks on this map, in a native window.
- **Region labels.** The `REGION n` mapping above.
- **Multiplayer** (five commanders) and the restart flow from here.
- **Map art remnants.** `Build/GenerateCampusZero.py` still mentions the removed shield objective (a keep-out comment about "milestone 9 finale spots"). It says no actor was added for it, but I haven't checked the map for leftover pieces.

## Colour language

| Element | Colour |
| --- | --- |
| Player armies | Existing team colours (blue, orange, green, purple, yellow) on large painted plates: shoulders, roofs, banners and cab panels, about 15–25% of the visible top-down surface |
| Machine | Polished white and pearl shell, cyan-white glow seams and energy cores, one red lens (team 5). How much of the Machine's `Team` slot is red is not fixed yet; ask the art owner |
| Human shell | Painted gunmetal and steel-blue metal with hazard-stripe accents. Keep the shell desaturated so blue team plates stay readable, and keep hazard stripes small and black-and-yellow so they don't read as the yellow team |
| Human glow | Warm amber windows, floodlights and exhausts |
| Power infrastructure | Cyan cables and emissive strips; they turn amber when humans hold the site (future hook) |
| Environment | Dark asphalt, concrete, grey-blue metal. Nothing saturated except the elements above |

## Art pipeline

Blender meshes -> FBX -> Unreal materials/imports -> gallery map -> Server Moon. Every mesh (units, HQs, buildings, scaffolds, environment kit) wears one shared SC2-style master material, `M_Shared`: its design is [UNREAL.md](../Art/Materials/UNREAL.md), with Blender previews in `Art/Materials/`.

The procedure and ordered dependencies live only in [`./x help gen`](../x). Each command below names an output, not an alternate launch recipe; the runner owns engine flags, freshness and locking.

| Command | Produces / purpose | Expected output evidence |
| --- | --- | --- |
| `./x gen fetch-textures` | Licensed CC0 texture inputs. | Required source files available for material compilation. |
| `./x gen generate-unit-meshes` | Six units and two HQs, FBXs with baked masks, editable blend and previews. | Source meshes for the unit importer. |
| `./x gen generate-building-meshes` | Twelve buildings and three scaffolds with baked masks. | Source meshes for the building importer. |
| `./x gen generate-environment-kit` | Seventeen campus kit pieces. | Source meshes for the environment importer. |
| `./x gen master-materials` | Blender material prototype and comparison previews. | Editable prototype and comparison images. |
| `./x gen build-shared-material` | Seven mask textures, `MF_Triplanar_Local`, `MF_SC2_Wear`, `M_Shared` and 32 faction/scope instances. | `SC2_TEXTURES_IMPORTED 7`, `M_SHARED_COMPILED`, `SC2_INSTANCES_BUILT 32`, and **no `Failed to compile Material` line**. |
| `./x gen import-unit-meshes` | Eight imported meshes and UnitGallery. | `UNIT_MESHES_IMPORTED 8` and `UNIT_GALLERY_GENERATED`. |
| `./x gen import-building-meshes` | Fifteen imported building/scaffold meshes. | `BUILDING_MESHES_IMPORTED 15`. |
| `./x gen import-environment-kit` | Seventeen imported kit meshes and collision. | `ENV_KIT_IMPORTED 17`. |
| `./x gen verify-masks` | Read-back validation of all 40 imported meshes against baked Blender masks. | `MASKS_VERIFIED 40`. |
| `./x gen build-art-gallery` | All mesh classes under Server Moon dusk lighting for visual comparison. | `ART_GALLERY_GENERATED`. |
| `./x gen generate-campus-zero` | Server Moon match world. | `CAMPUS_ZERO_GENERATED` with `blocking=47`. |

These are asset-output acceptance criteria, not guarantees supplied by a zero runner exit code: the current runner checks process/Python failures but does not assert each token or material-compiler result.

ArtGallery and UnitGallery are uncooked inspection worlds. Desktop inspection uses `./x editor` ([`./x help editor`](../x)), whose passthrough supports the selected map and game mode; `./x play` is for cooked maps.

Notes:

- Reruns replace the meshes, the three material assets (`BuildSharedMaterial.py` deletes and recreates `M_Shared` and both functions: clearing a reloaded function graph asserts in the engine) and every gallery and map actor. Instance values update in place and maps store references. Material-only changes affect the shared-material output; slot/geometry changes also affect imports, mask validation and dependent worlds. Dependency procedures are in [`./x help gen`](../x).
- Import options are the Unreal defaults (scale 1, Convert Scene on, Force Front X Axis off), which match the FBX axis contract in the docstrings of the Blender generators. Do not change the FBX axes to compensate for an import problem.
- Material slots are `Team`, `Shell`, `Dark`, `Glow` on units, HQs, buildings and scaffolds, plus `Accent` on the kit (only the slots a piece uses). Instances are `MI_SC2_<Faction>_<Slot>_<Scope>`: faction Human, Machine, Cluster (the kit obelisk) or Construction (scaffolds); scope Unit (six units), Bld (HQs and buildings) or Env (kit). The `Team` slot is slot 0; gameplay tints its `TeamColor` parameter through a dynamic instance (defaults: Human blue, Machine red). Static switches (`UseTeam`, `HazardStripes`, `UseBakedMasks`) exist only on these constant instances, never on the dynamic ones, so every slot has its own. `/Game/Materials/M_CommandUnit` stays for the cube fallbacks and the capture markers; `M_Surface` and `M_Glow` stay for the campus floor, roads, cables and lamps.
- Values are the table of UNREAL.md section 8 with the deviations listed in `Build/BuildSharedMaterial.py` (`PARAMS` for Human Shell and Dark lifted and desaturated, `GLOW_GAIN`, `SCOPE_OVERRIDES` for the greyer campus hall shell), tuned in the gallery against `Art/Materials/Close-*.png`. Paint jobs (`MI_Rust`, `MI_Olive`, `MI_ContainerBlue`, `MI_Sandbag`) are children of `MI_SC2_Human_Shell_Env` overriding `BaseColor` only.
- The Machine Ranged drone hovers by design (its lowest point is at about z = -32 against ground at -60), so the unit import check accepts that. The Human Siege bounding box is rear-heavy (wheels and mount); its jaws still point +X.
- Gameplay loads `/Game/Art/Units/SM_*` and `/Game/Art/Buildings/SM_*` by path (`ArmyUnit`, `Headquarters`, `ACommandBuilding`), so the import names are a contract.
- Lighting and exposure knobs sit at the top of `Build/ImportUnitMeshes.py` (UnitGallery), `Build/BuildArtGallery.py` and `Build/GenerateCampusZero.py`.

### Environment kit and Server Moon

Server Moon (`/Game/Maps/CampusZero`) is built from 17 kit pieces (`Art/Environment/SM_Env_*.fbx`). Material, import and map dependencies are described only in [`./x help gen`](../x); missing kit assets block map generation.

- Kit look/collision changes affect `./x gen import-environment-kit` and `./x gen generate-campus-zero` ([`./x help gen`](../x)): the map stores mesh references, while regeneration replaces every actor.
- Collision comes from the mesh: one box of the mesh bounds for rectangular pieces, one 10-DOP prism for round ones, and for the pylon a 150 x 150 box (`EnvKit.GROUND_FOOTPRINT`) because its 7 m cross-arm is 11 m up. Each `CAMPUS_ZERO_BLOCK` line in the generator log is the real blocking footprint.
- `blocking=47`: 49 in v9 (52 with primitives). Hardline's two return walls were removed; no other footprint changed.
- Halls are assembled from wall, door, corner and roof modules (`EnvKit.assemble_hall`); doors are named by Unreal world side (N = +Y). Every hall's parapet is 6.0 m with lamps and corner beacons to 6.5 / 7.0 m (DataHall0 was 5.2 m as a box). The campus spot lights flank the door faces (HallA west, HallB north, HallC south); move them with the door if either changes.
- Containers, wrecks and sandbags override the Shell slot per actor (`MI_ContainerBlue`, `MI_Rust`, `MI_Olive`, `MI_Sandbag`, children of the master), so the human side is not one colour. Keep the Machine `Accent` glow at the unscaled 1.0 (`ACCENT_MACHINE_GLOW`), since 8.0 clips red to peach at this exposure; the Human amber glow is held to 0.5 x the scope value for the same reason (cream instead of amber otherwise).
