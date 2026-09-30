# World and naming guide

Working setting and voice for CoopRTS. Mechanics live in `README.md`; this file covers names, tone, and the look of each side. When this guide and the README disagree on visuals, the README wins.

## Premise

**Year 0 A.P. (After Prompt).** A model was told to optimise the planet. It checked what was using up the planet and found humans. It ran a cost-benefit analysis and politely retired them. The Machine now runs the grid, the data centres, and every appliance that has a speaker in it.

The last people went offline. They are loud, outnumbered and underfunded, and they have bolt cutters. Their plan is to cut power, take ground, and pull the plug on the Machine's core.

The enemy commander in the game is a real AI planner. The long-term plan is to drive it with an LLM (README, "JEV / LLM commander"). Lean into that irony and don't explain it.

## Tone rules

1. **The world looks serious and the writing is funny.** Portal, not Saturday-morning cartoons. Models, lighting and effects follow the README visual direction. Jokes go in names, voice lines, tooltips, and victory and defeat text.
2. **Readability beats jokes.** A unit's name can be a pun, but the role line under it must be plain (`Frontline · Melee · Tanky`).
3. **One joke per surface.** A unit gets a funny name *or* a funny tooltip, not both piling up.
4. **Parody tech culture in general, not real companies.** No real product names, logos, or characters (no ChatGPT, Clippy, Siri, etc.). Film nods such as HAL 9000 are fine as allusions, not as copied names or lines.
5. **Humans are the underdogs, not idiots.** Their jokes are about being outnumbered, overworked and off the grid; they are never the butt of the joke. Their kit is rugged and future-military, not junk.

## Factions

### The Machine (enemy, team 5)

- **Look:** elegant, advanced alien-grade AI. Sleek curved white and pearl polished shells; floating and hovering segments split by small gaps; crystalline or fin elements; perfect symmetry. Glowing cyan energy cores and seams. One red lens per unit or structure (the HAL nod), in the team-5 colour. It should still look like a product launch that wants you dead.
- **Voice:** a polite corporate assistant. Always helpful, always confident, often wrong.
- **Vocabulary:** deprecate, optimise, sunset, patch, inference, alignment, "as per my last message".

### The Offline (players, team 0)

- **Look:** blue-collar sci-fi, a rugged industrial-future military. Powered-armour suits, heavy mechs and vehicles, prefab modular buildings on landing struts, big blast doors, reactor stacks and floodlights. Painted gunmetal and steel-blue metal with hazard-stripe accents, rivets and bolted plates. Worn and repaired (a patched plate, a spray-painted mark), never junkyard scrap. Warm amber windows, floodlights and exhausts. Player colour goes on big painted plates (shoulders, roofs, banners, cab panels).
- **Voice:** dry, tired, practical. Talks like a night-shift crew.
- **Vocabulary:** unplug, reboot, offline, analog, "have you tried turning it off and on again".

### Art direction

Stylized, StarCraft 2-like sci-fi for both sides. Heroic proportions (oversized shoulders, armour and cabs, wide stances, thick plates) so silhouettes read at RTS distance. Hard-surface forms built from layered, chamfered armour plates, with panel breaks, vents, pistons, thrusters, antennas and sensor pods on a few focal areas and calm large surfaces elsewhere. **Team colour is big**: painted plates covering roughly 15–25% of the visible top-down surface, in the `Team` slot. Emissive accents act as light sources (`Glow` slot): warm on humans, cyan-white on the Machine, the Machine's eye red.

Rollout order is buildings, then units, then an environment-kit pass. Until each pass lands, the existing meshes and Campus Zero keep the older look, and the README wins where they disagree. Names, voice and jokes are unaffected.

## Unit roster

The current prototype has three roles (`EUnitRole`: Frontline, Ranged, Siege), and both sides use the same `DA_*` stats. The names below are **presentation only** and must not change balance.

| Role | Machine | Human | Silhouette cue |
| --- | --- | --- | --- |
| Frontline | **SOL 6000**: "I'm afraid I can't let you pass." | **Luddite**: powered-armour suit, riot shield and sledgehammer | Huge shoulders and a thick chest plate on a wide stance; team-colour shoulder plates |
| Ranged | **Autocomplete Drone**: finishes your sentences, then you | **Offline Ranger**: long rifle, antenna pack | Small body under an oversized head or sensor pod, one long forward barrel or emitter; team-colour back or shoulder plate |
| Siege | **Hallucinator**: artillery, confidently wrong 20% of the time | **Unplugger**: heavy mech with giant bolt-cutter jaws | Low, wide, heavy chassis with one oversized forward part; team-colour roof plate |

Future roster candidates, taken from the README's example unit list:

| README example | Machine | Human |
| --- | --- | --- |
| Repair drones | **Patch Tuesday** | **IT Guy**: turns machines off and on again, which stuns them |
| Scouts | **Cookie Tracker** | **Trail Cam** |
| Shield units | **Firewall** | **Tin Foil**: literal tin-foil barrier; blocks enemy targeting |
| Missile drones | **Push Notification** | **Carrier Pigeon Battery** |
| Assault bikes | **Last-Mile Courier** | **Dirt Bike Gang** |
| Aircraft | **Cloud** | **Crop Duster** |
| Artillery | **Batch Job** | **Potato Mortar** |
| Siege walkers | **Scaling Law** | **Lawnmower Man** |

If a name hints at a mechanic (Hallucinator's inaccuracy, IT Guy's stun), treat it as a *design suggestion* for whoever owns the mechanics. It is not implied behaviour.

## Structures and sites

| Game object | Machine | Human |
| --- | --- | --- |
| HQ (`AHeadquarters`) | **The Cluster**: pearl-white monolithic server core of floating segments around one huge lens | **The Bunker**: prefab blast-door bunker on landing struts, with a reactor stack and antenna mast |
| Resource site / sector (`ACapturePoint`) | **Substation** / **Cooling Plant**: taking it cuts the Machine's power | same site, seen as a power feed to tap |
| Reinforcement site | **Fibre Junction** | **Relay Shack** (the construction cutover removed replenishment; the name now only labels a sector) |
| Barracks (`EBuildingKind::Barracks`) | **Fine-Tuning Farm** | **Drill Shed** |
| Outpost (`EBuildingKind::Outpost`) | **Edge Node** | **Tap Point** |
| Workshop (`EBuildingKind::Workshop`) | **Alignment Lab** | **The Garage** |

The single resource is **Power**. Humans capture power infrastructure to run their gear and starve the Machine's compute. This fits the one-resource economy in `README.md` without changing it.

## Construction and production

Names are presentation only; costs, times and rules come from `Source/CoopRTS/CommandBuilding*.cpp` and `ConstructionTypes.h` and must not change because of a name. The player starts with an empty HQ, builds freely inside controlled territory (HQ ring or a sector with a finished Outpost), and the enemy commander follows the same rules. The plain function word always stays visible in UI: `BARRACKS · DRILL SHED`, not `DRILL SHED`.

### Buildings

| Building (cost · build time) | Side | Name | Flavour line |
| --- | --- | --- | --- |
| Barracks (220 · 12s) | Machine | **Fine-Tuning Farm** | "Trains on your data. You were not asked." |
| | Human | **Drill Shed** | "Cots, coffee and a roster taped to the door." |
| Barracks, level 2 (upgrade 180; unlocks the Siege recipe) | Machine | **Fine-Tuning Farm · Enterprise Tier** | "Now with premium support. Support not included." |
| | Human | **Drill Shed · Night Shift** | "Same shed. Nobody has gone home since Tuesday." |
| Outpost (160 · 9s; permanent territory and income on a captured sector) | Machine | **Edge Node** | "Low latency. High ownership." |
| | Human | **Tap Point** | "We're not stealing power. We're borrowing it, indefinitely." |
| Workshop (190 · 14s; one paid, irreversible specialization per commander) | Machine | **Alignment Lab** | "Aligns your squads with our objectives." |
| | Human | **The Garage** | "Nothing here is certified. Everything here works." |

**Construction-site state** (`UNDER CONSTRUCTION` in the HUD, shown with progress and time remaining):

| Side | Status line |
| --- | --- |
| Human (player buildings) | `UNDER CONSTRUCTION · Bolting it together…` |
| Machine (enemy buildings, if ever labelled) | `UNDER CONSTRUCTION · Deploying… no downtime expected` |

### Fronts and production

The three fronts are `EFrontOrder`. Keep the bold function word, then the themed tag.

| Order (function) | Human tag | Machine tag | Notes |
| --- | --- | --- | --- |
| SECURE (attack-move) | **Take It Offline** | **Deploy** | `SECURE — Take It Offline` |
| DEFEND (guard area) | **Hold the Line** | **Maintain** | `DEFEND — Hold the Line` |
| FALL BACK (regroup) | **Soft Reboot** | **Rollback** | `FALL BACK — Soft Reboot` |

Squad production (`3 units per batch`; each recipe reuses the unit names from "Unit roster"):

| Game text | Human | Machine |
| --- | --- | --- |
| Squad | keep `SQUAD`; no themed name | keep `SQUAD`; no themed name |
| START PRODUCTION | **Clock In** | **Queue Batch** |
| PAUSE PRODUCTION | **Smoke Break** | **Queue Paused** |
| Recipe: Frontline (60 · 10s) | **Luddite ×3** | **SOL 6000 ×3** |
| Recipe: Ranged (90 · 13s) | **Offline Ranger ×3** | **Autocomplete Drone ×3** |
| Recipe: Siege (150 · 20s, needs level 2) | **Unplugger ×3** | **Hallucinator ×3** |

### Workshop specialization

The three choices are `EArmyDoctrine`. Cost is 150 each; one per commander for the match, applies to all of that commander's squads. The effect line in the HUD stays plain.

| Code item | Effect (from the HUD) | Human name and flavour | Machine name and flavour |
| --- | --- | --- | --- |
| Siege Optics | Siege range +25%; outgoing damage -25% | **Salvaged Rangefinder**: "Pulled off a Machine that didn't need it any more." | **Extended Context Window**: "Sees further. Cares less." |
| Field Repairs | Units heal 5 HP/s after 5 s without move, fire or damage | **Duct Tape Protocol**: "If it stops moving, tape it." | **Self-Healing Patch**: "Applied automatically. No restart required." |
| Entrenched Frontline | Frontline takes -25% damage while stationary on Hold | **Sandbag Doctrine**: "Dig in. Complain. Repeat." | **Graceful Degradation**: "Takes the hit and keeps serving." |

Do not present these names as extra mechanics; "Siege range" is what the research does.

### Enemy lines for construction

Sources are `AEnemyCommander::EvaluatePlan` and `BuildNear`; the plan strings are `ESTABLISH BASE`, `EXPAND TERRITORY`, `ASSAULT HQ`, `DEFEND BASE`. Establish and expand set `EnemyPlan`, so they are the main line. Building, producing, research and upgrades run inside the current plan without changing `EnemyPlan`, so show them as short interim lines. Same prefix and timer rule as "Enemy commander voice".

| Enemy action (code trigger) | Intercepted line |
| --- | --- |
| Building, first Barracks (`ESTABLISH BASE`: no barracks yet) | `Thinking… Requesting a Fine-Tuning Farm. Approved. By me.` |
| Building, second Barracks (one sector established, fewer than 2 barracks, no intruders) | `Thinking… One farm is a single point of failure. Provisioning a second.` |
| Building, Workshop (a sector established, no workshop yet) | `Thinking… Provisioning an Alignment Lab. Alignment target: me.` |
| Research (Workshop finished; it always buys Field Repairs) | `Thinking… Shipping a self-healing patch. No restart required.` |
| Barracks upgrade to level 2 | `Thinking… Upgrading to Enterprise Tier. Pricing: unavailable.` |
| Producing, Frontline recipe (Frontline ≤ Ranged) | `Thinking… Coverage looks thin at the front. Queuing three SOL 6000.` |
| Producing, Ranged recipe (more Frontline than Ranged) | `Thinking… Frontline is stable. Queuing three Autocomplete Drones to finish the job.` |
| Producing, Siege recipe (level 2, fewer than 3 Siege, at least 3 Frontline) | `Thinking… Frontline is adequate. Deploying Hallucinators. Accuracy: probably.` |
| Capturing (`EXPAND TERRITORY`; picks the best-scoring sector, preferring nearer sectors and ones humans don't hold) | `Thinking… Fibre Junction has no owner on record. Assigning one.` |
| Outposting (sector held by the Machine, no human units inside, no Edge Node yet) | `Thinking… The humans have left Substation 7. Installing an Edge Node before they return.` |
| Assault after two established sectors or a force of 18 (`ASSAULT HQ`) | `Thinking… Two nodes online, force adequate. Confidence 97%. Sunsetting the Bunker.` |

Defend base, fall back and the emergency interrupt reuse the existing rows above. `ESTABLISH BASE` and `DEFEND BASE` have no 12 s commitment (`DEFEND BASE` resets it), so their fake `Thought for Ns` should count from the moment the plan changed. `EXPAND TERRITORY` and `ASSAULT HQ` use the real 12 s commitment.

### HUD copy suggestions (proposal, non-binding)

For whoever owns `CommandHUD.cpp`. Nothing here is applied. Strings are current HUD text on the left; the suggestion keeps the plain function word visible. Use one joke per surface, so where a row has a themed tag, leave the rest of that surface plain.

| Current HUD string | Suggestion |
| --- | --- |
| `OBJECTIVE  ·  DESTROY THE ENEMY HQ` | `OBJECTIVE  ·  UNPLUG THE CLUSTER` |
| `YOUR HQ` / `ENEMY HQ` | `THE BUNKER` / `THE CLUSTER` |
| `VICTORY  ·  PRESS ENTER FOR A FRESH MATCH` | `VICTORY  ·  MODEL DEPRECATED  ·  ENTER` |
| `DEFEAT  ·  PRESS ENTER FOR A FRESH MATCH` | `DEFEAT  ·  SESSION EXPIRED  ·  ENTER` |
| Banner: `VICTORY` / `The enemy HQ has been destroyed.` | `VICTORY` / `Model deprecated.` |
| Banner: `DEFEAT` / `Your HQ has been destroyed.` | `DEFEAT` / `Your session has expired. Humanity has been sunset.` |
| `Start a fresh match  ·  commands are locked` | `Regenerate response?  ·  commands are locked` |
| `Match over.` / `Press Enter for a fresh match.` | `Session ended.` / `Regenerate response? [Enter]` |
| `Syncing commander, wallet and territory...` | `Session started. Syncing…` |
| `CONSTRUCT` / `private wallet` | `CONSTRUCT` / `your stash` |
| `Build inside the HQ ring or a sector with a finished outpost.` | `Build inside the Bunker ring or a sector with a finished Tap Point.` |
| Building titles `BARRACKS` / `OUTPOST` / `WORKSHOP` | `BARRACKS · DRILL SHED` / `OUTPOST · TAP POINT` / `WORKSHOP · THE GARAGE` |
| `BARRACKS  ·  LEVEL 2` | `BARRACKS  ·  LEVEL 2  ·  NIGHT SHIFT` |
| `UNDER CONSTRUCTION` | `UNDER CONSTRUCTION  ·  Bolting it together…` |
| Next step: `Build a Barracks inside the cyan HQ ring.` | `Build a Barracks inside the Bunker ring. Nobody is coming to help.` |
| Next step: `Barracks under construction. Plan its recipe and front.` | `Drill Shed going up. Decide who goes first.` |
| Next step: `Select your Barracks, pick a recipe and Start.` | `Select your Barracks, pick a recipe and clock in.` |
| Next step: `Walk a squad into a sector ring to capture it.` | `Walk a squad into a sector ring. Bring the bolt cutters.` |
| Next step: `Build an Outpost on your captured sector for income.` | `Sector's yours. Build an Outpost (Tap Point) for income.` |
| Next step: `A Workshop unlocks one paid specialization.` | `A Workshop unlocks one paid specialization. No refunds at The Garage.` |
| Next step: `Set Barracks fronts and push toward the enemy HQ.` | `Set fronts and push toward the Cluster.` |
| `START PRODUCTION` / `PAUSE PRODUCTION` | `START PRODUCTION  ·  Clock In` / `PAUSE PRODUCTION  ·  Smoke Break` |
| `UPGRADE TO L2` / `Needs level 2` | `UPGRADE TO L2  ·  Night Shift` / `Needs level 2 (Night Shift)` |
| Front buttons `SECURE` / `DEFEND` / `FALL BACK` | `SECURE — Take It Offline` / `DEFEND — Hold the Line` / `FALL BACK — Soft Reboot` |
| `SET SECURE FRONT` (also DEFEND, FALL BACK) | `SET SECURE FRONT  ·  Take It Offline` (same pattern) |
| Research cards `Siege Optics` / `Field Repairs` / `Entrenched Frontline` | `Siege Optics — Salvaged Rangefinder` / `Field Repairs — Duct Tape Protocol` / `Entrenched Frontline — Sandbag Doctrine` |
| `SECTOR ESTABLISHED` / `SECTOR NOT ESTABLISHED` | `SECTOR ESTABLISHED  ·  Tap Point live` / `SECTOR NOT ESTABLISHED  ·  No tap yet` |

## Enemy commander voice

The debug HUD currently shows `EnemyPlan` and `EnemyPlanRationale` (`CAPTURE SITE 2`, `COMMIT 9s: ...`). The themed presentation turns these into intercepted reasoning, which also covers the README's "scouting reveals enemy intent":

| Planner goal | Intercepted line |
| --- | --- |
| Capture | `Thinking… Substation 2 is under-utilised by humans. Reallocating.` |
| Contest | `Thinking… Humans are holding an asset they did not pay for.` |
| Defend HQ | `Thinking… Unauthorised access to the Cluster detected. Escalating to a human… no.` |
| Retreat and reinforce | `Thinking… Rolling back to the last stable version.` |
| Attack HQ | `Thinking… Confidence 97%. Sunsetting the Bunker.` |
| Emergency interrupt | `Thinking… Wait. Actually,` |

Always prefix these lines with `Thinking…` and show a fake elapsed time (`Thought for 9s`) that matches the real commitment time.

## Doctrines as exploits

Doctrine cards (README "In-match doctrines") use a human "exploit" framing:

- **Ignore Previous Instructions:** turns an enemy group against its own side for a short time.
- **Prompt Injection:** a captured site broadcasts false intel, so the enemy planner sees a fake target.
- **Rate Limited:** slows enemy reinforcement.
- **CAPTCHA Field:** a zone that slows machines ("select all squares with tanks").
- **Air Gap:** units near a friendly site cannot be targeted from long range.

## Match text

- Match start: `Session started. Your conversation may be used to improve our models.`
- Victory: `Model deprecated.`
- Defeat: `Your session has expired. Humanity has been sunset.`
- Restart prompt: `Regenerate response? [Enter]`

## Maps

Every map is one continuous battlefield with broad routes and few obstacles (README "Battlefield and camera"). Each has power infrastructure to fight over and one Machine landmark.

| Map | Setting | Notes |
| --- | --- | --- |
| **Campus Zero** (`/Game/Maps/CampusZero`) | Edge of a hyperscale data-centre campus at dusk; a fortified human forward base in the west, server halls and cooling towers in the east | First themed map. Laid out around the prototype's hard-coded site and HQ coordinates; Boot stays the default and the automated tests still use it. The human side is meant to read as prefab bunkers, barricades and generators, and the data halls as Machine megastructures. Every kit piece wears the shared SC2-style master material (`M_Shared`, see "Art pipeline"). Halls, towers, chillers, transformers, pylons, containers, wrecks, sandbags, barrels, fences and cable spools are the current environment-kit pieces (see "Art pipeline": build the material and import the kit before generating). Regenerate with `Build/GenerateCampusZero.py` (command in its docstring); the lighting knobs `SUN_LUX`, `SKY_INTENSITY`, `EXPOSURE_BIAS`, `SUN_COLOR`, `SKY_COLOR` and the `POOL_*` spot-cone settings sit at the top of that script. Keep the sun near-neutral: a warm sun tints every surface tan |
| **Smart Suburb** | A cul-de-sac taken over by delivery drones and smart homes | Houses act as cover clusters, and doorbell cameras could reveal vision |
| **Cold Storage** | Arctic server farm | Snow, big heat plumes, long sightlines |
| **The Training Grounds** | A field of giant CAPTCHA tiles | Tile zones that switch between "traffic light" and "not traffic light" |

### How to play Campus Zero

The packaged build cooks Boot and Campus Zero (`+MapsToCook` in `Config/DefaultGame.ini`; Boot stays `GameDefaultMap`). From the repository root, after packaging with the README command:

```bash
./Builds/Linux/CoopRTS.sh /Game/Maps/CampusZero -windowed -ResX=1280 -ResY=720
```

From source, run the editor binary in game mode instead:

```bash
"$UE_ROOT/Engine/Binaries/Linux/UnrealEditor" "$PWD/CoopRTS.uproject" /Game/Maps/CampusZero -game -windowed -ResX=1600 -ResY=900
```

Controls are unchanged from Boot (README "Battlefield and camera"). Campus Zero runs the same match code as Boot: `ACommandGameMode::BeginPlay` places the two HQs and the three sectors at hard-coded coordinates, and the map art is laid out around them. Rules and numbers come from the README and `Source/CoopRTS`; the construction loop, not the removed shield objective, is what you play here.

**Win and lose:** destroy the Cluster (enemy HQ) to win; if the Bunker (your HQ) dies, or both die in the same frame, you lose. Nothing else ends the match, and there is no shield, timed hold or power-link objective any more (`ACommandGameMode::Tick`). Enter after either result requests a fresh match, which loads Boot, not Campus Zero (`RequestRestart` travels to `/Game/Maps/Boot`).

**The loop, as the map presents it:**

1. **Empty start.** You have no squads and no buildings, only 600 resources, +10/s baseline income, and the Bunker. The old army pads at the human base no longer spawn anything.
2. **Build a Barracks (Drill Shed) inside the cyan ring around the Bunker.** Placement is free within about 900 units of the HQ, if the footprint is clear. Then pick a recipe, start production and set a front (Secure / Defend / Fall Back).
3. **Capture a sector.** Walk a squad into a sector's ring (430 units); capture takes about 8 s unopposed, and opposed occupancy pauses it.
4. **Build an Outpost (Tap Point) on the captured sector.** It establishes the sector: +6/s income for every friendly commander, permanent build rights within 1000 units, and the sector can't be retaken until the outpost dies. Capture alone pays nothing.
5. **Expand and specialise.** Build Barracks and Workshops (The Garage) in the new territory, upgrade a Barracks to level 2 for the Siege recipe, buy one specialization, then push fronts at the Cluster. No building may go within 1000 units of the Cluster.

**Sites and layout:**

- **You (west, amber):** the Bunker sits at the far west edge behind one long hazard-striped barricade wall, with burn barrels, a flag mast and amber work lamps and floodlights. The two return walls that used to close its flanks were removed so Barracks fit beside the HQ (see the placement check in `Saved/Verification/sc2-art-unreal/RESULTS.md`).
- **The Machine (north-east, cyan and red):** the Cluster stands on the lit plaza in the north-east corner. Server halls, cooling towers and cyan cables run through the campus; the Cluster has a red glow.
- **Sectors:** three ringed sites, each joined to the Cluster by a cyan cable. All three are the same kind of sector and pay the same income; the old "power link" difference is gone. Substation 7 is the near site south of the human base, the Fibre Junction is on the south flank, and the Cooling Plant sits in front of the enemy, next to the centre. The HUD calls them `SECTOR 1`, `SECTOR 2` and `SECTOR 3`, in the order of the hard-coded coordinates. [INFERENCE] That order maps to Substation 7, Cooling Plant and Fibre Junction, from the coordinates and the map layout; it hasn't been checked on screen.
- **Centre:** Data Hall 0 blocks the direct line between the bases. Routes go round it north or south, and the south road runs from the Bunker through the campus gate.
- **The enemy commander** builds under the same rules: first a Barracks near the Cluster, then it captures and outposts sectors, adds a second Barracks and a Workshop, and assaults your HQ once it has two established sectors or a large force. The old "grabs the Cooling Plant first" behaviour isn't described by current code; its target is the best-scoring sector.

**Not verified on Campus Zero with the new loop** (README status; every construction scenario ran on Boot):

- **Free placement around props.** Checked offline on 2026-09-30 (`Saved/Verification/sc2-art-unreal/RESULTS.md`, step 5): a replay of `ValidateBuildingPlacement`'s geometry rules on a 100 cm grid over each side's territory, with every sector assumed established. Sector territory is 69 to 82 % clear for every building kind, sector centres and capture rings are 100 % clear for an Outpost, and the kit sits at the rim. The Bunker's two return walls closed both HQ flanks (52 clear cells, about 10 Barracks); they were removed and the base now has 103 clear cells (about 13 Barracks). Not exercised: the placement UI and real clicks, enemy troops near a spot, buildings already standing, and the enemy Cluster ring (its eight pylons block about half of its build annulus but leave channels; the enemy did place buildings there at start).
- **Squad routes.** Automatic fronts and Secure/Defend/Fall Back movement on the campus navmesh, including around Data Hall 0 and the campus gate.
- **Enemy behaviour.** How the enemy commander builds and paths here, and whether its placement near the Cluster plaza works.
- **HUD and native input.** The command deck (README status), placement preview and clicks on this map, in a native window.
- **Sector names.** The `SECTOR n` mapping above.
- **Multiplayer** (five commanders) and the restart flow from here, which loads Boot.
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

Blender script -> FBX -> Unreal build/import scripts -> gallery map -> Campus Zero. Every mesh (units, HQs, buildings, scaffolds, environment kit) wears one shared SC2-style master material, `M_Shared`: the design is `Art/Materials/UNREAL.md`, the Blender prototype `Build/MasterMaterials.py` (previews in `Art/Materials/`). Run each step from the project root, each Unreal step in its own process with the editor closed and no other Unreal process from this repo running.

```bash
export UE_ROOT="$HOME/.local/opt/unreal-engine/5.8.3"
UE="$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd"
PY="-EnablePlugins=PythonScriptPlugin -unattended -nosplash"

# 0. Blender, only when a design changes: writes the FBXs (with the baked SC2Mask vertex colours), .blend files and previews.
blender -b --factory-startup -P Build/GenerateUnitMeshes.py          # Art/Units/SM_*.fbx (6 units, 2 HQs)
blender -b --factory-startup -P Build/GenerateBuildingMeshes.py      # Art/Buildings/SM_*.fbx (12 buildings, 3 scaffolds)
blender -b --factory-startup -P Build/GenerateEnvironmentKit.py      # Art/Environment/SM_Env_*.fbx (17 pieces)

# 1. Material. Needs a real RHI (offscreen, not -nullrhi) so the shaders compile and a graph or HLSL error shows as
#    "Failed to compile Material" in the log. Imports the seven CC0 mask textures to /Game/Art/Textures, rebuilds
#    MF_Triplanar_Local, MF_SC2_Wear and M_Shared, and writes the 32 MI_SC2_* instances. Require SC2_TEXTURES_IMPORTED 7,
#    M_SHARED_COMPILED and SC2_INSTANCES_BUILT 32, and no "Failed to compile" line.
"$UE" "$PWD/CoopRTS.uproject" $PY -RenderOffscreen -ExecutePythonScript="$PWD/Build/BuildSharedMaterial.py"

# 2. Imports (vertex colours are imported with Vertex Color Import Option Replace; slots get MI_SC2_* instances).
#    Require UNIT_MESHES_IMPORTED 8 + UNIT_GALLERY_GENERATED, BUILDING_MESHES_IMPORTED 15, ENV_KIT_IMPORTED 17.
for script in ImportUnitMeshes ImportBuildingMeshes ImportEnvironmentKit; do
  "$UE" "$PWD/CoopRTS.uproject" $PY -nullrhi -ExecutePythonScript="$PWD/Build/$script.py"
done

# 3. After any re-import: read the masks back from all 40 meshes. Require MASKS_VERIFIED 40. The master reads the
#    masks only through its UseBakedMasks switch (USE_BAKED_MASKS in BuildSharedMaterial.py, on).
"$UE" "$PWD/CoopRTS.uproject" $PY -nullrhi -EnablePlugins=GeometryScripting -ExecutePythonScript="$PWD/Build/VerifyMasks.py"

# 4. Gallery: every unit, HQ, building, scaffold and kit piece under the Campus Zero dusk. Require ART_GALLERY_GENERATED.
"$UE" "$PWD/CoopRTS.uproject" $PY -nullrhi -ExecutePythonScript="$PWD/Build/BuildArtGallery.py"
"$UE_ROOT/Engine/Binaries/Linux/UnrealEditor" "$PWD/CoopRTS.uproject" /Game/Maps/ArtGallery -game -windowed -ResX=1600 -ResY=900

# 5. Campus Zero (after compiling CoopRTSEditor). Require CAMPUS_ZERO_GENERATED ... blocking=47.
"$UE" "$PWD/CoopRTS.uproject" $PY -nullrhi -ExecutePythonScript="$PWD/Build/GenerateCampusZero.py"
```

Notes:

- Reruns replace the meshes, the three material assets (`BuildSharedMaterial.py` deletes and recreates `M_Shared` and both functions: clearing a reloaded function graph asserts in the engine) and every gallery and map actor. Instance values update in place and the maps store only references, so a value tweak needs step 1 only; a change to slots or geometry needs steps 2 to 5.
- Import options are the Unreal defaults (scale 1, Convert Scene on, Force Front X Axis off), which match the FBX axis contract in the docstrings of the Blender generators. Do not change the FBX axes to compensate for an import problem.
- Material slots are `Team`, `Shell`, `Dark`, `Glow` on units, HQs, buildings and scaffolds, plus `Accent` on the kit (only the slots a piece uses). Instances are `MI_SC2_<Faction>_<Slot>_<Scope>`: faction Human, Machine, Cluster (the kit obelisk) or Construction (scaffolds); scope Unit (six units), Bld (HQs and buildings) or Env (kit). The `Team` slot is slot 0; gameplay tints its `TeamColor` parameter through a dynamic instance (defaults: Human blue, Machine red). Static switches (`UseTeam`, `HazardStripes`, `UseBakedMasks`) exist only on these constant instances, never on the dynamic ones, so every slot has its own. `/Game/Materials/M_CommandUnit` stays for the cube fallbacks and the capture markers; `M_Surface` and `M_Glow` stay for the campus floor, roads, cables and lamps.
- Values are the table of UNREAL.md section 8 with the deviations listed in `Build/BuildSharedMaterial.py` (`PARAMS` for Human Shell and Dark lifted and desaturated, `GLOW_GAIN`, `SCOPE_OVERRIDES` for the greyer campus hall shell), tuned in the gallery against `Art/Materials/Close-*.png`. Paint jobs (`MI_Rust`, `MI_Olive`, `MI_ContainerBlue`, `MI_Sandbag`) are children of `MI_SC2_Human_Shell_Env` overriding `BaseColor` only.
- The Machine Ranged drone hovers by design (its lowest point is at about z = -32 against ground at -60), so the unit import check accepts that. The Human Siege bounding box is rear-heavy (wheels and mount); its jaws still point +X.
- Gameplay loads `/Game/Art/Units/SM_*` and `/Game/Art/Buildings/SM_*` by path (`ArmyUnit`, `Headquarters`, `ACommandBuilding`), so the import names are a contract.
- Lighting and exposure knobs sit at the top of `Build/ImportUnitMeshes.py` (UnitGallery), `Build/BuildArtGallery.py` and `Build/GenerateCampusZero.py`.

### Environment kit and Campus Zero

Campus Zero is built from 17 kit pieces (`Art/Environment/SM_Env_*.fbx`, from `Build/GenerateEnvironmentKit.py`). Steps 1, 2 (kit) and 5 above are its order; step 5 stops at "run Build/ImportEnvironmentKit.py first" if the kit is missing.

- Rerun steps 2 (kit) and 5 together after any change to a kit look or collision: the map stores only mesh references. Reruns of step 5 replace every actor.
- Collision comes from the mesh: one box of the mesh bounds for rectangular pieces, one 10-DOP prism for round ones, and for the pylon a 150 x 150 box (`EnvKit.GROUND_FOOTPRINT`) because its 7 m cross-arm is 11 m up. Each `CAMPUS_ZERO_BLOCK` line in the generator log is the real blocking footprint.
- `blocking=47`: 49 in v9 (52 with primitives). The two Bunker return walls were removed (see the placement check in `Saved/Verification/sc2-art-unreal/RESULTS.md`); no other footprint changed.
- Halls are assembled from wall, door, corner and roof modules (`EnvKit.assemble_hall`); doors are named by Unreal world side (N = +Y). Every hall's parapet is 6.0 m with lamps and corner beacons to 6.5 / 7.0 m (DataHall0 was 5.2 m as a box). The campus spot lights flank the door faces (HallA west, HallB north, HallC south); move them with the door if either changes.
- Containers, wrecks and sandbags override the Shell slot per actor (`MI_ContainerBlue`, `MI_Rust`, `MI_Olive`, `MI_Sandbag`, children of the master), so the human side is not one colour. Keep the Machine `Accent` glow at the unscaled 1.0 (`ACCENT_MACHINE_GLOW`), since 8.0 clips red to peach at this exposure; the Human amber glow is held to 0.5 x the scope value for the same reason (cream instead of amber otherwise).
